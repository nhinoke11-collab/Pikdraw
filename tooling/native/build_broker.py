"""One canonical build writer, FIFO requests, scoped CTest and resumable Ninja work.

Each detached worker retains its caller's environment. Only identical overlapping
requests may share a result; completed historical tests are never silently cached.
"""
from __future__ import annotations

import argparse
import base64
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

from build_state import (BuildLock, ID, TERMINAL, alive, atomic_json, digest_file,
                         environment_digest, open_shared, process_token, read_json, remove_run,
                         safe_path, source_digest)


def command(tool, *args) -> list[str]:
    prefix = list(tool) if isinstance(tool, list) else [str(tool)]
    return prefix + [str(a) for a in args]


def cache_configuration(build: Path) -> str:
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^CMAKE_BUILD_TYPE:STRING=(Debug|Release)$", cache.read_text(encoding="utf-8-sig"), re.M)
        if match:
            return match[1]
    return ""


def tool_digest(tools: dict, build: Path) -> str:
    digest = hashlib.sha256()
    paths = set()
    for name in ("CMake", "Ninja", "Clang", "CTest"):
        value = tools.get(name)
        if value:
            paths.update(value if isinstance(value, list) else [value])
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        digest.update(cache.read_bytes())
        compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]*=(.+)$", cache.read_text(encoding="utf-8-sig"), re.M)
        if compiler:
            paths.add(compiler[1])
    for path in sorted(paths):
        if Path(path).is_file():
            digest.update(str(path).encode() + digest_file(Path(path)).encode())
    return digest.hexdigest()


def has_external_corpus() -> bool:
    return any(key.startswith("PIKDRAW_") and key != "PIKDRAW_TEST_OUTPUT_DIR" and
               key.endswith(("_DIR", "_PATH", "_FILE", "_ROOT")) and Path(value).exists()
               for key, value in os.environ.items() if value)


def machine_budget() -> dict:
    available = 4 * 1024**3
    if os.name == "nt":
        class Memory(ctypes.Structure):
            _fields_ = [("length", ctypes.c_uint32), ("load", ctypes.c_uint32)] + [
                (name, ctypes.c_uint64) for name in ("total", "available", "page_total", "page_available",
                                                    "virtual_total", "virtual_available", "extended")]
        memory = Memory()
        memory.length = ctypes.sizeof(memory)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            available = memory.available
    return {"logical_cpus": os.cpu_count() or 1, "available_mib": int(available / 1024**2)}


def auto_jobs(cpus: int, available_mib: int) -> int:
    # Leave one logical CPU for the desktop on machines with at least four.
    # The previous 1.5 GiB/job estimate serialized this 4-CPU/8-GiB workstation.
    cpu_budget = max(1, cpus - 1) if cpus >= 4 else max(1, cpus)
    memory_jobs = max(1, (available_mib - 256) // 640)
    return max(1, min(cpu_budget, memory_jobs))


def log_progress(root: Path, output: str) -> dict:
    if not output:
        return {}
    try:
        path = safe_path(root, Path(output) / "build.log")
        with open_shared(path) as stream:
            stream.seek(max(0, path.stat().st_size - 65536))
            tail = stream.read().decode("utf-8", errors="replace")
        matches = list(re.finditer(r"\[(\d+)/(\d+)(?: \| active=(\d+) \| elapsed=([\d.]+)s)?\]", tail))
        if not matches:
            return {}
        match = matches[-1]
        progress = {"completed_edges": int(match[1]), "total_edges": int(match[2]),
                    "meaning": "build steps, not percent of time or a finish-time prediction"}
        if match[3]:
            progress.update(active_edges=int(match[3]), elapsed_seconds=float(match[4]))
        return progress
    except (FileNotFoundError, PermissionError):
        return {}


def validate_request(r: dict) -> None:
    if r["configuration"] not in {"Auto", "Debug", "Release"}:
        raise ValueError("Invalid configuration")
    for target in r["targets"] + r["test_targets"]:
        if not re.fullmatch(r"[A-Za-z0-9_+./-]+", target) or target.startswith("-"):
            raise ValueError(f"Invalid target: {target}")
    if r["test_targets"] and not r["test"]:
        raise ValueError("Test targets require -Test")
    if r["test"] and not r["test_targets"] and not r["all_tests"] and not r.get("test_names"):
        raise ValueError("Select test build targets, or explicitly request -AllTests")
    if (r["test"] or r["tests_only"]) and not r["build_only"] and not (
            r["test_targets"] or r["test_regex"] or r["all_tests"] or r.get("test_names")):
        raise ValueError("An explicit test selection is required")
    if not 0 <= r["jobs"] <= 256 or not 1 <= r["test_jobs"] <= 64:
        raise ValueError("Invalid build/test parallelism")


def select_tests(tests: list[dict], r: dict) -> list[dict]:
    if r.get("test_names"):
        names = set(r["test_names"])
        selected = [t for t in tests if t["name"] in names]
        if {t["name"] for t in selected} != names:
            raise ValueError("A test recorded by the failed request is no longer registered")
    elif r["all_tests"]:
        selected = tests
    elif r["test_regex"]:
        regex = re.compile(r["test_regex"])
        selected = [t for t in tests if regex.search(t["name"])]
    else:
        selected = []
        for target in r["test_targets"]:
            # Prefer the primary test to avoid repeating its --focused aliases.
            matches = [t for t in tests if t["name"] == target]
            if not matches:
                matches = [t for t in tests if t.get("command") and
                           Path(t["command"][0]).stem == target]
            if not matches:
                raise ValueError(f"No registered CTest matches {target}; provide -TestRegex explicitly")
            selected.extend(matches)
    selected = list({t["name"]: t for t in selected}.values())
    if not selected:
        raise ValueError("Test selection matched zero registered tests")
    return selected


class Coordinator:
    def __init__(self, root: Path):
        self.root = root.absolute()
        self.state = safe_path(self.root, self.root / ".agent/native-build")
        self.build = safe_path(self.root, self.root / "build/win-dev")
        self.children = []

    def path(self, request_id: str) -> Path:
        if not ID.fullmatch(request_id):
            raise ValueError("Request ID must be 32 lowercase hex characters")
        return safe_path(self.root, self.state / f"{request_id}.json")

    def load(self, request_id: str) -> dict:
        return read_json(self.path(request_id))

    def cancelled(self, request_id: str) -> bool:
        return self.path(request_id).with_suffix(".cancel").is_file()

    def cancel(self, request_id: str) -> dict:
        entry = self.load(request_id)
        if entry["state"] != "queued":
            raise ValueError("Only queued work can be cancelled; an active build is left running")
        marker = safe_path(self.root, self.path(request_id).with_suffix(".cancel"))
        marker.write_text("Queued cancellation requested", encoding="utf-8")
        return {"id": request_id, "cancellation": "requested; confirm state with -Status"}

    def save(self, entry: dict) -> None:
        atomic_json(self.root, self.path(entry["id"]), entry)

    def entries(self) -> list[dict]:
        values = []
        if self.state.exists():
            for path in self.state.glob("*.json"):
                if ID.fullmatch(path.stem):
                    try:
                        values.append(read_json(safe_path(self.root, path)))
                    except (OSError, ValueError):
                        continue
        return sorted(values, key=lambda e: (e["submitted"], e["id"]))

    def status(self, request_id: str = "") -> dict:
        values = [self.load(request_id)] if request_id else self.entries()[-20:]
        summaries = []
        for e in values:
            live = alive(e.get("worker_pid", 0), e.get("worker_token", ""))
            summaries.append({k: e[k] for k in ("id", "state", "phase", "configuration", "submitted",
                              "started", "finished", "targets", "tests", "failed_tests", "shared_from",
                              "output", "error", "timings", "source", "source_after", "worker_pid") if k in e})
            for key in ("jobs", "job_policy", "configured_before", "configuration_changed"):
                if key in e:
                    summaries[-1][key] = e[key]
            progress = log_progress(self.root, e.get("output", ""))
            if progress:
                summaries[-1]["progress"] = progress
            if e["state"] not in TERMINAL and e.get("worker_pid") and not live:
                summaries[-1]["state"] = "interrupted"
                summaries[-1]["error"] = "Worker exited; no success is inferred. Resubmit/retry after inspecting the build lock."
            elif e["state"] not in TERMINAL and not e.get("worker_pid") and time.time() - e["submitted"] > 30 and not alive(
                    e["creator_pid"], e.get("creator_token", "")):
                summaries[-1]["state"] = "interrupted"
        lock = self.root / ".agent/native-build.lock"
        try:
            owner = lock.read_text(encoding="utf-8-sig") if lock.exists() else ""
        except FileNotFoundError:
            owner = ""
        return {"lock": owner, "requests": summaries}

    def enqueue(self, request: dict, tools: dict) -> dict:
        validate_request(request)
        entry = {"id": uuid.uuid4().hex, "submitted": time.time(), "state": "queued", "phase": "queued",
                 "request": request, "tools": tools, "environment": environment_digest(),
                 "source_submitted": source_digest(self.root), "creator_pid": os.getpid(),
                 "creator_token": process_token(os.getpid())}
        self.save(entry)
        return entry

    def start(self, entry: dict) -> None:
        args = [sys.executable, "-B", str(Path(__file__).absolute()), "--worker-root", str(self.root),
                "--worker-id", entry["id"]]
        options = {"cwd": self.root, "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL,
                   "stderr": subprocess.DEVNULL}
        if os.name == "nt":
            options["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS
        else:
            options["start_new_session"] = True
        self.children.append(subprocess.Popen(args, **options))

    def wait(self, request_id: str, lock_wait: int) -> int:
        deadline = time.monotonic() + lock_wait
        offsets = {}
        while True:
            e = self.load(request_id)
            if e.get("output"):
                try:
                    log = safe_path(self.root, Path(e["output"]) / "build.log")
                    if log.is_file():
                        with open_shared(log) as stream:
                            stream.seek(offsets.get(str(log), 0))
                            text = stream.read()
                            offsets[str(log)] = stream.tell()
                            if text:
                                print(text.decode("utf-8", errors="replace"), end="", flush=True)
                except (FileNotFoundError, PermissionError):
                    pass  # The owner can remove successful output while a waiter is polling.
            if e["state"] in TERMINAL:
                print(json.dumps(self.status(request_id), ensure_ascii=False), flush=True)
                return 0 if e["state"] == "succeeded" else 1
            if e.get("worker_pid") and not alive(e["worker_pid"], e.get("worker_token", "")):
                print(json.dumps(self.status(request_id), ensure_ascii=False), flush=True)
                return 1
            if e["state"] == "queued" and time.monotonic() >= deadline:
                print(f"Request {request_id} is queued; it continues in the background. Use -Status/-Wait -RequestId {request_id}.", flush=True)
                return 75
            time.sleep(0.2)

    def _first_pending(self, own: dict) -> bool:
        for e in self.entries():
            if e["id"] == own["id"]:
                return True
            if e["state"] in TERMINAL:
                continue
            pid = e.get("worker_pid") or e["creator_pid"]
            token = e.get("worker_token") or e["creator_token"]
            if alive(pid, token) or time.time() - e["submitted"] < 30:
                return False
        return True

    def _shared_result(self, entry: dict, source: str, configuration: str) -> dict | None:
        r = entry["request"]
        if r["no_reuse"] or r["clean"] or r["reconfigure"] or r["tests_only"]:
            return None
        for other in reversed(self.entries()):
            # Only requests that overlapped in time; no historical CTest cache.
            if other["state"] != "succeeded" or other.get("finished", 0) < entry["submitted"]:
                continue
            if not other.get("shareable", False) or other.get("tool_digest") != tool_digest(entry["tools"], self.build):
                continue
            previous = other["request"]
            current = dict(r, configuration=configuration)
            previous = dict(previous, configuration=other.get("configuration"))
            if current != previous or entry["environment"] != other["environment"] or entry["tools"] != other["tools"]:
                continue
            if source != other.get("source") or source != other.get("source_after"):
                continue
            if any(not Path(path).is_file() or digest_file(Path(path)) != value
                   for path, value in other.get("artifacts", {}).items()):
                continue
            # Non-test custom targets (package, staging, generators) must always execute.
            if not other.get("artifacts"):
                continue
            ninja = entry["tools"].get("Ninja")
            if ninja:
                targets = (r["test_targets"] or ["all"]) if r["test"] else r["targets"]
                check = subprocess.run(command(ninja, "-C", self.build, "-n", *targets),
                                       capture_output=True, text=True, encoding="utf-8", errors="replace")
                # Includes discovered external headers and generated dependencies. Any
                # pending work/configure/staging conservatively runs through Ninja again.
                if check.returncode or "ninja: no work to do." not in check.stdout:
                    continue
            return other
        return None

    def worker(self, request_id: str) -> int:
        entry = self.load(request_id)
        entry.update(worker_pid=os.getpid(), worker_token=process_token(os.getpid()))
        self.save(entry)
        lock = BuildLock(self.root, request_id)
        acquired = False
        try:
            deadline = time.monotonic() + 3600
            while not acquired:
                if self.cancelled(request_id):
                    entry.update(state="cancelled", phase="cancelled", finished=time.time())
                    self.save(entry)
                    return 0
                if time.monotonic() > deadline:
                    raise TimeoutError("Build queue waited one hour. Inspect -Status; no lock was removed.")
                if self._first_pending(entry):
                    acquired = lock.acquire()
                if not acquired:
                    time.sleep(0.5)
            if self.cancelled(request_id):
                entry.update(state="cancelled", phase="cancelled", finished=time.time())
                lock.release()
                self.save(entry)
                return 0
            r = entry["request"]
            entry.update(state="running", phase="input-check", started=time.time(), timings={})
            self.save(entry)
            config = cache_configuration(self.build)
            configuration = config if r["configuration"] == "Auto" and config else r["configuration"]
            if configuration == "Auto":
                configuration = "Debug"
            source = source_digest(self.root)
            entry.update(phase="prepare", configuration=configuration,
                         source=source, targets=r["test_targets"] if r["test"] else r["targets"],
                         configured_before=config,
                         configuration_changed=bool(config and config != configuration))
            shared = self._shared_result(entry, source, configuration)
            if shared:
                entry.update(state="succeeded", phase="complete", shared_from=shared["id"], finished=time.time(),
                             source_after=source, tests=shared.get("tests", []), artifacts=shared["artifacts"])
                lock.release()
                self.save(entry)
                return 0
            self.save(entry)
            self.execute(entry)
            entry["tool_digest"] = tool_digest(entry["tools"], self.build)
            entry["source_after"] = source_digest(self.root)
            if entry["source_after"] != source:
                raise RuntimeError("Source changed during build/test. Outputs are incremental, but this run cannot validate the current source.")
            entry.update(state="succeeded", phase="complete", finished=time.time())
            # No temporary successful build/test logs, JUnit files or probe artifacts remain.
            remove_run(self.root, Path(entry["output"]))
            entry["output"] = ""
            lock.release()
            self.save(entry)
            self.prune()
            return 0
        except BaseException as error:
            entry.update(state="stale" if "Source changed" in str(error) else "failed",
                         error=str(error), finished=time.time())
            try:
                entry["source_after"] = source_digest(self.root)
            except Exception:
                entry["source_after"] = "unavailable"
            lock.release()
            self.save(entry)
            return 1
        finally:
            lock.release()

    def run(self, entry: dict, phase: str, args: list[str], *, capture=False, env=None) -> str:
        entry["phase"] = phase
        self.save(entry)
        log_path = safe_path(self.root, Path(entry["output"]) / "build.log")
        started = time.monotonic()
        # File-backed output avoids pipe deadlocks and retains asynchronous failure diagnostics.
        with log_path.open("a", encoding="utf-8") as log:
            log.write(f"\n[{phase}] {subprocess.list2cmdline(args)}\n")
            log.flush()
            if capture:
                result = subprocess.run(args, cwd=self.root, env=env, stdout=subprocess.PIPE,
                                        stderr=log, text=True, encoding="utf-8", errors="replace")
            else:
                result = subprocess.run(args, cwd=self.root, env=env, stdout=log, stderr=log)
        entry["timings"][phase] = round(time.monotonic() - started, 3)
        self.save(entry)
        if result.returncode:
            raise RuntimeError(f"{phase} failed ({result.returncode}). Inspect {log_path}; retry preserves Ninja objects/cache.")
        return result.stdout if capture else ""

    def execute(self, entry: dict) -> None:
        r, tools = entry["request"], entry["tools"]
        run = safe_path(self.root, self.build / "test-output" / f"{os.getpid()}-{entry['id']}")
        run.mkdir(parents=True)
        (run / ".pikdraw-test-run").write_text("Owned by build.ps1", encoding="utf-8")
        entry["output"] = str(run)
        self.save(entry)
        with (run / "build.log").open("a", encoding="utf-8") as log:
            log.write(f"[plan] configuration={entry['configuration']}; previous={entry['configured_before'] or 'not configured'}\n")
            if entry["configuration_changed"]:
                log.write("[plan] Configuration switch changes compiler flags; dependent objects may all rebuild. Keep Auto for regular iteration.\n")
        if not tools.get("CMake"):
            raise RuntimeError("CMake was not found")
        cache = self.build / "CMakeCache.txt"
        if not r["tests_only"]:
            setup = self.root / "tooling/native/setup-clipper2.ps1"
            if setup.is_file():
                self.run(entry, "dependencies", command(tools["PowerShell"], "-NoProfile", "-NonInteractive",
                         "-ExecutionPolicy", "Bypass", "-File", setup))
            configured = cache_configuration(self.build)
            tests_configured = cache.is_file() and re.search(r"^PIKDRAW_BUILD_TESTS:BOOL=ON$",
                                                             cache.read_text(encoding="utf-8-sig"), re.M)
            if r["reconfigure"] or configured != entry["configuration"] or not tests_configured or not (self.build / "build.ninja").is_file():
                self.run(entry, "configure", command(tools["CMake"], "--preset", "win-dev",
                         f"-DCMAKE_BUILD_TYPE={entry['configuration']}", "-DPIKDRAW_BUILD_TESTS=ON"))
            if r["clean"]:
                self.run(entry, "clean", command(tools["CMake"], "--build", self.build, "--target", "clean"))
            targets = (r["test_targets"] or ["all"]) if r["test"] else r["targets"]
            budget = machine_budget()
            jobs = r["jobs"] or auto_jobs(budget["logical_cpus"], budget["available_mib"])
            entry["jobs"] = jobs
            entry["job_policy"] = dict(budget, mode="explicit" if r["jobs"] else "auto",
                                       reserve_mib=256, estimated_mib_per_compile=640)
            with (run / "build.log").open("a", encoding="utf-8") as log:
                log.write(f"[plan] jobs={jobs}; CPUs={budget['logical_cpus']}; available RAM={budget['available_mib']} MiB; mode={entry['job_policy']['mode']}\n")
            build_env = dict(os.environ)
            build_env.setdefault("NINJA_STATUS", "[%f/%t | active=%r | elapsed=%es] ")
            self.run(entry, "build", command(tools["CMake"], "--build", self.build, "--parallel", jobs,
                                            "--target", *targets), env=build_env)
            # Preserve standalone runtime staging from the previous runner.
            if tools.get("Clang") and isinstance(tools["Clang"], str):
                app_dir = self.build / "apps/desktop"
                if app_dir.is_dir():
                    for dll in Path(tools["Clang"]).parent.glob("*.dll"):
                        destination = safe_path(self.root, app_dir / dll.name)
                        if not destination.exists():
                            shutil.copy2(dll, destination)
        elif cache_configuration(self.build) != entry["configuration"]:
            raise ValueError("-TestsOnly configuration differs from the built tree; use -Test to rebuild")
        if (r["test"] or r["tests_only"]) and not r["build_only"]:
            if not cache.is_file() or not re.search(r"^PIKDRAW_BUILD_TESTS:BOOL=ON$",
                                                    cache.read_text(encoding="utf-8-sig"), re.M):
                raise RuntimeError("CTest is not configured. Use -Test with explicit targets first.")
            ctest = tools.get("CTest") or str(Path(tools["CMake"]).with_name("ctest.exe"))
            info = self.run(entry, "test-discovery", command(ctest, "--test-dir", self.build, "--show-only=json-v1"), capture=True)
            selected = select_tests(json.loads(info)["tests"], r)
            entry["tests"] = [t["name"] for t in selected]
            entry["shareable"] = not has_external_corpus()
            for test in selected:
                properties = {p["name"]: p.get("value") for p in test.get("properties", [])}
                if properties.get("RUN_SERIAL") or any(k.startswith("FIXTURES_") for k in properties) or any(
                        word in test["name"].lower() for word in ("_e2e", "_benchmark", "_ui", "_native_shell")) or set(
                            properties.get("LABELS", [])) & {"windows", "gpu", "benchmark", "isolated-import", "e2e"}:
                    entry["shareable"] = False
            # Shared results must still reference the same executable bytes.
            artifacts = {t["command"][0] for t in selected if t.get("command") and Path(t["command"][0]).is_file()}
            entry["artifacts"] = {str(p): digest_file(Path(p)) for p in artifacts}
            regex = "^(" + "|".join(re.escape(t["name"]) for t in selected) + ")$"
            args = command(ctest, "--test-dir", self.build, "--output-on-failure", "--no-tests=error",
                           "--parallel", r["test_jobs"], "-R", regex, "--output-junit", run / "ctest.xml")
            if r["verbose_tests"]:
                args.append("-V")
            env = dict(os.environ, PIKDRAW_TEST_OUTPUT_DIR=str(run))
            try:
                self.run(entry, "test", args, env=env)
            finally:
                report = run / "ctest.xml"
                if report.is_file():
                    entry["failed_tests"] = [case.attrib["name"] for case in ET.parse(report).iter("testcase")
                                             if case.find("failure") is not None or case.find("error") is not None]
        elif r["targets"] == ["pikdraw"] and not r["test"]:
            app = self.build / "apps/desktop/pikdraw.exe"
            if app.is_file():
                entry["artifacts"] = {str(app): digest_file(app)}
                entry["shareable"] = True

    def retry(self, request_id: str) -> tuple[dict, dict]:
        previous = self.load(request_id)
        if previous["state"] not in TERMINAL and previous.get("worker_pid") and not alive(
                previous["worker_pid"], previous.get("worker_token", "")):
            previous["state"] = "interrupted"
        if previous["state"] not in {"failed", "stale", "interrupted"}:
            raise ValueError("Retry requires a failed/stale/interrupted request")
        request = copy.deepcopy(previous["request"])
        request.update(clean=False, reconfigure=False, no_reuse=True)
        if previous.get("failed_tests"):
            request.update(test_names=previous["failed_tests"], all_tests=False, test_regex="")
        # Target build remains enabled: source fixes must be relinked before retrying tests.
        if request["tests_only"]:
            raise ValueError("Retry of -TestsOnly cannot build fresh binaries. Submit -Test with explicit targets.")
        return request, previous["tools"]

    def prune(self) -> None:
        # Only bounded operational metadata. Never delete failed logs or arbitrary paths.
        finished = [e for e in self.entries() if e["state"] in TERMINAL and
                    (not e.get("output") or not Path(e["output"]).exists())]
        for entry in finished[:-128]:
            try:
                self.path(entry["id"]).unlink(missing_ok=True)
                self.path(entry["id"]).with_suffix(".cancel").unlink(missing_ok=True)
            except PermissionError:
                pass  # Another reader may still hold old metadata; prune on the next run.


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload")
    parser.add_argument("--worker-root")
    parser.add_argument("--worker-id")
    args = parser.parse_args()
    if args.worker_root:
        return Coordinator(Path(args.worker_root)).worker(args.worker_id)
    payload = json.loads(base64.b64decode(args.payload))
    coordinator = Coordinator(Path(payload["root"]))
    if payload.get("status"):
        print(json.dumps(coordinator.status(payload.get("request_id", "")), ensure_ascii=False, indent=2))
        return 0
    if payload.get("wait"):
        return coordinator.wait(payload["request_id"], payload["lock_wait_seconds"])
    if payload.get("cancel"):
        print(json.dumps(coordinator.cancel(payload["request_id"])))
        return 0
    request, tools = payload["request"], payload["tools"]
    if payload.get("retry_failed"):
        request, tools = coordinator.retry(payload["retry_failed"])
        request.update(payload.get("retry_overrides", {}))
    entry = coordinator.enqueue(request, tools)
    print(f"Build request: {entry['id']}", flush=True)
    coordinator.start(entry)
    if payload.get("submit"):
        print(f"Queued. Use -Status/-Wait -RequestId {entry['id']}.", flush=True)
        return 0
    return coordinator.wait(entry["id"], payload["lock_wait_seconds"])


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (Exception, KeyboardInterrupt) as error:
        print(f"[build coordinator] {error}", file=sys.stderr, flush=True)
        raise SystemExit(1)
