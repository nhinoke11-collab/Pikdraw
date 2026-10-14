#!/usr/bin/env python3
"""PikDraw's local, provider-neutral task/worktree/context coordinator.

Python stdlib only. Task leases are explicit: a clock never transfers ownership.
The board is shared by all Git worktrees and all writers take one O_EXCL lock.
This is coordination, not a security sandbox for agents or third-party plugins.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SCHEMA_VERSION = 1
STATUSES = {"todo", "in_progress", "review", "done", "blocked"}
SLUG = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
OWNER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:@-]{0,127}\Z")
PROTECTED = {".git", ".worktrees", ".codex", ".agents"}
WINDOWS_DEVICES = {"con", "prn", "aux", "nul"} | {
    f"{prefix}{digit}" for prefix in ("com", "lpt") for digit in range(1, 10)
}
MAX_JSON_BYTES = 2 * 1024 * 1024


class HarnessError(Exception):
    """An actionable CLI error, without an implementation traceback."""


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def checked_slug(value: str, label: str = "id") -> str:
    if not isinstance(value, str) or not SLUG.fullmatch(value):
        raise HarnessError(f"Invalid {label}: use lowercase letters/digits/hyphen (max 64).")
    if value in WINDOWS_DEVICES:
        raise HarnessError(f"Invalid {label}: Windows reserves {value!r}.")
    return value


def checked_owner(value: str) -> str:
    if not isinstance(value, str) or not OWNER.fullmatch(value):
        raise HarnessError("Invalid owner: use a stable provider/session identifier.")
    return value


def checked_text(value: str, label: str, limit: int = 2000) -> str:
    if not isinstance(value, str) or not value.strip() or len(value) > limit:
        raise HarnessError(f"{label} must be nonempty and at most {limit} characters.")
    return value.strip()


def relative_path(value: str, *, allow_runtime: bool = True) -> str:
    if not isinstance(value, str) or not value or "\\" in value or ":" in value:
        raise HarnessError(f"Unsafe relative POSIX path: {value!r}")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise HarnessError(f"Unsafe relative POSIX path: {value!r}")
    if any(part.endswith((".", " ")) or any(ord(char) < 32 for char in part)
           or part.split(".", 1)[0].casefold() in WINDOWS_DEVICES for part in parts):
        raise HarnessError(f"Unsafe Windows-compatible path: {value!r}")
    if parts[0].casefold() in PROTECTED or (not allow_runtime and parts[0].casefold() == ".agent"):
        raise HarnessError(f"Protected path: {value!r}")
    return value


def contained(root: Path, relative: str, *, must_exist: bool = False,
              allow_runtime: bool = True) -> Path:
    relative_path(relative, allow_runtime=allow_runtime)
    path = root / relative
    try:
        path.resolve().relative_to(root.resolve())
    except (ValueError, OSError, RuntimeError) as exc:
        raise HarnessError(f"Path escapes repository: {relative}") from exc
    if must_exist and not path.exists():
        raise HarnessError(f"Missing referenced path: {relative}")
    return path


def read_json(path: Path):
    try:
        if path.stat().st_size > MAX_JSON_BYTES:
            raise HarnessError(f"JSON exceeds {MAX_JSON_BYTES} bytes: {path}")
        with path.open(encoding="utf-8-sig") as stream:
            return json.load(stream)
    except (OSError, ValueError) as exc:
        raise HarnessError(f"Cannot read JSON {path}: {exc}") from exc


def atomic_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def git(root: Path, *arguments: str, check: bool = True) -> subprocess.CompletedProcess:
    try:
        result = subprocess.run(["git", "-C", str(root), *arguments],
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=60)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HarnessError(f"Git unavailable or timed out: {exc}") from exc
    if check and result.returncode:
        raise HarnessError(f"Git {' '.join(arguments[:2])} failed: {result.stderr.strip()[:1000]}")
    return result


def coordinator_root(start: Path) -> Path:
    """A linked worktree's common Git dir points back to its primary board."""
    start = start.resolve()
    top = git(start, "rev-parse", "--show-toplevel", check=False)
    if top.returncode:
        # doctor/context also work before the initial commit/repository exists.
        return start
    checkout = Path(top.stdout.strip()).resolve()
    common_raw = git(checkout, "rev-parse", "--git-common-dir").stdout.strip()
    common = Path(common_raw)
    if not common.is_absolute():
        common = checkout / common
    common = common.resolve()
    if common.name != ".git":
        raise HarnessError("Unsupported Git layout: common Git directory must be <repo>/.git.")
    root = common.parent
    if not (root / "registry/modules.json").is_file():
        raise HarnessError("Shared coordinator lacks registry/modules.json; use --repo explicitly.")
    return root


def commit_oid(root: Path, ref: str) -> str:
    if not isinstance(ref, str) or not ref or ref.startswith("-") or any(c.isspace() for c in ref):
        raise HarnessError("Invalid Git ref.")
    result = git(root, "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}")
    return result.stdout.strip()


def paths_overlap(left: str, right: str) -> bool:
    # Windows is the primary host; be conservative on other systems too.
    left, right = left.casefold(), right.casefold()
    return left == right or left.startswith(right + "/") or right.startswith(left + "/")


def path_owned(path: str, owned_paths: list[str]) -> bool:
    return any(path.casefold() == own.casefold() or path.casefold().startswith(own.casefold() + "/")
               for own in owned_paths)


class Harness:
    def __init__(self, repo: Path):
        top = git(repo.resolve(), "rev-parse", "--show-toplevel", check=False)
        self.checkout = Path(top.stdout.strip()).resolve() if not top.returncode else repo.resolve()
        self.root = coordinator_root(repo)
        self.board = contained(self.root, ".agent")

    @contextmanager
    def locked(self):
        # Reject a board redirected by a symlink/junction outside this checkout.
        contained(self.root, ".agent")
        self.board.mkdir(parents=True, exist_ok=True)
        lock_path = contained(self.root, ".agent/board.lock")
        try:
            descriptor = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError as exc:
            raise HarnessError("Task board is locked. Retry after the writer finishes; if it crashed, "
                               "verify no writer is active before manually removing .agent/board.lock.") from exc
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump({"pid": os.getpid(), "started_at": now()}, stream)
            yield
        finally:
            lock_path.unlink()

    def modules(self) -> dict[str, dict]:
        registry = read_json(contained(self.checkout, "registry/modules.json", must_exist=True))
        if not isinstance(registry, dict) or registry.get("schema_version") != SCHEMA_VERSION:
            raise HarnessError("registry/modules.json requires schema_version: 1.")
        raw_modules = registry.get("modules")
        if not isinstance(raw_modules, list) or not raw_modules:
            raise HarnessError("Module registry must contain a nonempty modules array.")
        modules = {}
        for module in raw_modules:
            if not isinstance(module, dict):
                raise HarnessError("Each module must be an object.")
            identifier = checked_slug(module.get("id"), "module id")
            if identifier in modules:
                raise HarnessError(f"Duplicate module id: {identifier}")
            contained(self.checkout, module.get("path"), must_exist=True)
            checked_text(module.get("status"), f"{identifier} status", 80)
            for field in ("dependencies", "context", "owned_paths"):
                if not isinstance(module.get(field), list) or not all(isinstance(v, str) for v in module[field]):
                    raise HarnessError(f"{identifier}.{field} must be an array of strings.")
                if len(module[field]) != len(set(module[field])):
                    raise HarnessError(f"Duplicate {identifier}.{field} value.")
            if not module["owned_paths"]:
                raise HarnessError(f"{identifier} requires owned_paths.")
            for path in module["context"]:
                contained(self.checkout, path, must_exist=True)
            for path in module["owned_paths"]:
                contained(self.checkout, path, allow_runtime=False)
            modules[identifier] = module
        for identifier, module in modules.items():
            unknown = set(module["dependencies"]) - modules.keys()
            if unknown:
                raise HarnessError(f"{identifier}: unknown dependencies {sorted(unknown)}")
        visited, visiting = set(), []

        def visit(identifier):
            if identifier in visiting:
                raise HarnessError("Module dependency cycle: " + " -> ".join(visiting + [identifier]))
            if identifier in visited:
                return
            visiting.append(identifier)
            for dependency in modules[identifier]["dependencies"]:
                visit(dependency)
            visiting.pop()
            visited.add(identifier)

        for identifier in modules:
            visit(identifier)
        return modules

    def boot(self, modules: dict[str, dict]) -> dict:
        path = contained(self.checkout, "registry/boot.json")
        if not path.exists():
            return {"schema_version": 1, "routes": []}
        boot = read_json(path)
        if not isinstance(boot, dict) or boot.get("schema_version") != 1 or not isinstance(boot.get("routes"), list):
            raise HarnessError("registry/boot.json requires schema_version: 1 and routes array.")
        seen = set()
        for route in boot["routes"]:
            if not isinstance(route, dict):
                raise HarnessError("Boot route must be an object.")
            identifier = checked_slug(route.get("id"), "route id")
            if identifier in seen:
                raise HarnessError(f"Duplicate boot route: {identifier}")
            seen.add(identifier)
            if not isinstance(route.get("paths"), list) or not route["paths"]:
                raise HarnessError(f"Boot route {identifier} needs paths.")
            for reference in route["paths"]:
                contained(self.checkout, reference, must_exist=True)
            if route.get("module") is not None and route["module"] not in modules:
                raise HarnessError(f"Boot route {identifier}: unknown module.")
        return boot

    def task_path(self, task_id: str) -> Path:
        return contained(self.root, f".agent/tasks/{checked_slug(task_id, 'task id')}.json")

    def validate_task(self, task: dict, modules: dict[str, dict] | None = None) -> dict:
        if not isinstance(task, dict) or task.get("schema_version") != SCHEMA_VERSION:
            raise HarnessError("Task record requires schema_version: 1.")
        checked_slug(task.get("id"), "task id")
        checked_slug(task.get("provider"), "provider")
        checked_text(task.get("title"), "Task title", 160)
        checked_text(task.get("goal"), "Task goal", 2000)
        if task.get("status") not in STATUSES:
            raise HarnessError("Invalid task status.")
        if modules is not None and task.get("module") not in modules:
            raise HarnessError(f"Unknown task module: {task.get('module')}")
        if not isinstance(task.get("owned_paths"), list) or not task["owned_paths"]:
            raise HarnessError("Task needs owned_paths.")
        for path in task["owned_paths"]:
            contained(self.root, path, allow_runtime=False)
        if not isinstance(task.get("base"), dict) or not isinstance(task["base"].get("ref"), str):
            raise HarnessError("Task needs base.ref.")
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", str(task["base"].get("oid", ""))):
            raise HarnessError("Task needs a resolved base.oid.")
        lease = task.get("lease")
        if lease is not None:
            if not isinstance(lease, dict):
                raise HarnessError("Task lease must be an object or null.")
            checked_owner(lease.get("owner"))
            if lease.get("provider") != task["provider"]:
                raise HarnessError("Task lease provider does not match task provider.")
            if task["status"] == "done":
                raise HarnessError("Completed task cannot retain a lease.")
        worktree = task.get("worktree")
        if worktree is not None:
            if not isinstance(worktree, dict):
                raise HarnessError("Task worktree must be an object or null.")
            expected = f".worktrees/{task['provider']}-{task['id']}"
            if worktree.get("path") != expected:
                raise HarnessError("Task worktree path differs from its provider/task assignment.")
            if worktree.get("branch") != f"agent/{task['provider']}/{task['id']}":
                raise HarnessError("Task worktree branch differs from its provider/task assignment.")
            if worktree.get("base_oid") != task["base"]["oid"]:
                raise HarnessError("Task worktree base differs from its assigned base.")
        return task

    def task(self, task_id: str) -> dict:
        task = self.validate_task(read_json(self.task_path(task_id)))
        if task["id"] != task_id:
            raise HarnessError("Task id does not match record filename.")
        return task

    def all_tasks(self) -> list[dict]:
        directory = contained(self.root, ".agent/tasks")
        if not directory.exists():
            return []
        tasks = []
        for path in sorted(directory.glob("*.json")):
            contained(self.root, path.relative_to(self.root).as_posix(), must_exist=True)
            tasks.append(self.task(path.stem))
        return tasks

    def save_task(self, task: dict) -> None:
        task["updated_at"] = now()
        self.validate_task(task)
        atomic_json(self.task_path(task["id"]), task)

    def require_owner(self, task: dict, owner: str) -> None:
        checked_owner(owner)
        if not task.get("lease") or task["lease"]["owner"] != owner:
            raise HarnessError(f"Task {task['id']} is not leased to {owner}; claim it first.")

    def assert_no_overlap(self, task: dict, tasks: list[dict]) -> None:
        for other in tasks:
            if other["id"] == task["id"] or not other.get("lease"):
                continue
            for own in task["owned_paths"]:
                for foreign in other["owned_paths"]:
                    if paths_overlap(own, foreign):
                        raise HarnessError(f"Ownership conflict: {own} overlaps {foreign} in "
                                           f"task {other['id']} ({other['lease']['owner']}).")

    def create_task(self, task_id, title, module_id, goal, provider, base, paths=None):
        checked_slug(task_id, "task id")
        checked_slug(provider, "provider")
        checked_text(title, "Task title", 160)
        checked_text(goal, "Task goal")
        modules = self.modules()
        if module_id not in modules:
            raise HarnessError(f"Unknown module: {module_id}")
        owned = list(dict.fromkeys(paths or modules[module_id]["owned_paths"]))
        for path in owned:
            contained(self.root, path, allow_runtime=False)
        oid = commit_oid(self.checkout, base)
        with self.locked():
            if self.task_path(task_id).exists():
                raise HarnessError(f"Task already exists: {task_id}")
            task = {"schema_version": 1, "id": task_id, "title": title, "module": module_id,
                    "goal": goal, "provider": provider, "status": "todo", "owned_paths": owned,
                    "base": {"ref": base, "oid": oid}, "lease": None, "worktree": None,
                    "summary": "", "next_steps": [], "evidence": [], "created_at": now()}
            self.save_task(task)
        return task

    def claim(self, task_id, owner, provider):
        checked_owner(owner)
        checked_slug(provider, "provider")
        with self.locked():
            task = self.task(task_id)
            if provider != task["provider"]:
                raise HarnessError("Provider differs from task assignment.")
            if task["status"] == "done":
                raise HarnessError("Completed task cannot be claimed.")
            if task.get("lease") and task["lease"]["owner"] != owner:
                raise HarnessError(f"Task already leased to {task['lease']['owner']}.")
            self.assert_no_overlap(task, self.all_tasks())
            if not task.get("lease"):
                task["lease"] = {"owner": owner, "provider": provider, "acquired_at": now(),
                                 "release_policy": "explicit"}
            task["status"] = "in_progress"
            self.save_task(task)
        return task

    def update(self, task_id, owner, status, summary, next_steps, evidence):
        checked_text(summary, "Summary")
        for item in next_steps + evidence:
            checked_text(item, "Next step/evidence")
        if status not in STATUSES:
            raise HarnessError("Invalid task status.")
        with self.locked():
            task = self.task(task_id)
            self.require_owner(task, owner)
            if status == "done" and not evidence and not task.get("evidence"):
                raise HarnessError("Done requires concrete validation evidence.")
            task.update(status=status, summary=summary, next_steps=next_steps,
                        evidence=list(dict.fromkeys(task.get("evidence", []) + evidence))[-32:])
            if status == "done":
                task["lease"] = None
            self.save_task(task)
        return task

    def release(self, task_id, owner, summary):
        checked_text(summary, "Summary")
        with self.locked():
            task = self.task(task_id)
            self.require_owner(task, owner)
            task["lease"] = None
            task["summary"] = summary
            if task["status"] == "in_progress":
                task["status"] = "todo"
            self.save_task(task)
        return task

    def worktrees(self):
        output = git(self.root, "worktree", "list", "--porcelain").stdout
        result, record = [], {}
        for line in output.splitlines() + [""]:
            if not line:
                if record:
                    result.append(record)
                    record = {}
            else:
                key, _, value = line.partition(" ")
                record[key] = value or True
        return result

    def create_worktree(self, task_id, provider, base, owner):
        checked_slug(provider, "provider")
        checked_slug(task_id, "task id")
        branch = f"agent/{provider}/{task_id}"
        relative = f".worktrees/{provider}-{task_id}"
        # .worktrees is reserved from agent-owned sources, but created only here.
        parent = self.root / ".worktrees"
        target = parent / f"{provider}-{task_id}"
        try:
            target.resolve().relative_to(self.root.resolve())
        except (ValueError, OSError) as exc:
            raise HarnessError("Worktree target escapes repository.") from exc
        oid = commit_oid(self.checkout, base)
        with self.locked():
            task = self.task(task_id)
            self.require_owner(task, owner)
            if task["provider"] != provider or task["base"]["oid"] != oid:
                raise HarnessError("Worktree provider/base must match task assignment.")
            self.assert_no_overlap(task, self.all_tasks())
            if task.get("worktree"):
                raise HarnessError("Task already has a registered worktree.")
            if target.exists() or target.is_symlink():
                raise HarnessError(f"Refusing existing worktree path: {target}")
            if git(self.root, "show-ref", "--verify", f"refs/heads/{branch}", check=False).returncode == 0:
                raise HarnessError(f"Refusing existing branch: {branch}")
            for reference in ("AGENTS.md", "docs/START.md", "docs/project-state.json", "registry/modules.json"):
                git(self.root, "ls-files", "--error-unmatch", "--", reference)
                git(self.root, "cat-file", "-e", f"{oid}:{reference}")
            parent.mkdir(exist_ok=True)
            git(self.root, "worktree", "add", "-b", branch, str(target), oid)
            # Never remove a checkout on registration failure: preserve it for recovery.
            task["worktree"] = {"path": relative, "branch": branch, "base_oid": oid}
            try:
                self.save_task(task)
            except Exception as exc:
                raise HarnessError(f"Worktree created at {target}, but registration failed. "
                                   "Preserve it; repair the task record before retrying.") from exc
        return {"task_id": task_id, "branch": branch, "path": str(target), "base_oid": oid}

    def handoff(self, task_id, owner, head, summary, changed_paths, decisions,
                tests, questions, next_steps):
        checked_text(summary, "Handoff summary")
        for field in (decisions, tests, questions, next_steps):
            if len(field) > 32:
                raise HarnessError("Handoff arrays are limited to 32 concise entries.")
            for item in field:
                checked_text(item, "Handoff entry")
        with self.locked():
            task = self.task(task_id)
            self.require_owner(task, owner)
            base_oid = task["base"]["oid"]
            checkout = self.checkout
            if task.get("worktree"):
                checkout = self.root / task["worktree"]["path"]
                try:
                    checkout.resolve().relative_to(self.root.resolve())
                except (ValueError, OSError, RuntimeError) as exc:
                    raise HarnessError("Registered task worktree escapes repository.") from exc
            # HEAD and other checkout-local refs must resolve in the assigned checkout,
            # not the primary checkout where the shared board lives.
            head_oid = commit_oid(checkout, head)
            if task.get("worktree"):
                if commit_oid(checkout, "HEAD") != head_oid:
                    raise HarnessError("Handoff head must match the task worktree HEAD.")
                if git(checkout, "status", "--porcelain", "--untracked-files=normal").stdout.strip():
                    raise HarnessError("Task worktree is dirty; commit owned changes before handoff.")
            if git(self.root, "merge-base", "--is-ancestor", base_oid, head_oid, check=False).returncode:
                raise HarnessError("Handoff head must descend from the assigned base commit.")
            actual = sorted(filter(None, git(self.root, "diff", "--name-only", "-z", base_oid, head_oid).stdout.split("\0")))
            for path in actual:
                contained(checkout, path, allow_runtime=False)
                if not path_owned(path, task["owned_paths"]):
                    raise HarnessError(f"Changed file is outside task ownership: {path}")
            if changed_paths and sorted(set(changed_paths)) != actual:
                raise HarnessError("Declared changed paths differ from the committed Git diff.")
            if not tests:
                raise HarnessError("Handoff needs --test evidence (or an explicit reason a check was not run).")
            packet = {"schema_version": 1, "type": "handoff", "task_id": task_id,
                      "provider": task["provider"], "owner": owner, "module": task["module"],
                      "base": task["base"], "head": {"ref": head, "oid": head_oid},
                      "summary": summary, "changed_paths": actual, "decisions": decisions,
                      "tests": tests, "open_questions": questions, "next_steps": next_steps,
                      "created_at": now()}
            packet_path = f".agent/handoffs/{task_id}-{head_oid[:12]}.json"
            atomic_json(contained(self.root, packet_path), packet)
            task.update(status="review", summary=summary, next_steps=next_steps,
                        handoff=packet_path, lease=None,
                        evidence=list(dict.fromkeys(task.get("evidence", []) + tests))[-32:])
            self.save_task(task)
        return packet

    def doctor(self):
        issues = []
        modules = {}
        try:
            modules = self.modules()
            self.boot(modules)
            for reference in ("AGENTS.md", "docs/START.md", "docs/project-state.json"):
                contained(self.checkout, reference, must_exist=True)
            state = read_json(contained(self.checkout, "docs/project-state.json", must_exist=True))
            if not isinstance(state, dict) or state.get("schema_version") != 1:
                raise HarnessError("docs/project-state.json requires schema_version: 1.")
            tasks = self.all_tasks()
            for task in tasks:
                self.validate_task(task, modules)
                if task.get("lease"):
                    self.assert_no_overlap(task, tasks)
                if task.get("handoff"):
                    packet = read_json(contained(self.root, task["handoff"], must_exist=True))
                    if packet.get("schema_version") != 1 or packet.get("task_id") != task["id"]:
                        raise HarnessError(f"Invalid handoff reference for {task['id']}.")
        except HarnessError as exc:
            issues.append(str(exc))
        return {"ok": not issues, "product": "PikDraw", "schema_version": 1,
                "modules": len(modules), "issues": issues}

    def context(self, task_id, max_chars=12000):
        if max_chars < 1000 or max_chars > 50000:
            raise HarnessError("--max-chars must be between 1000 and 50000.")
        modules = self.modules()
        task = self.task(task_id)
        self.validate_task(task, modules)
        module = modules[task["module"]]
        boot = self.boot(modules)
        references = ["AGENTS.md", "docs/START.md", "docs/project-state.json", "registry/modules.json"]
        references += module["context"]
        route_ids = {module["id"], "session", "session-start", "general", "module-change"}
        if module["id"] == "plugin-sdk":
            route_ids.add("plugin")
        if task["status"] == "review":
            route_ids.add("review")
        for route in boot["routes"]:
            if route.get("module") == module["id"] or route["id"] in route_ids:
                references += route["paths"]
        references = list(dict.fromkeys(references))
        sections = [f"PikDraw task context v1 | task={task_id} | module={module['id']}",
                    "Shared coordinator: " + str(self.root),
                    "Source checkout: " + str(self.checkout),
                    "Read only the references relevant to this task; expand context on demand.",
                    "TASK\n" + json.dumps(task, ensure_ascii=False, separators=(",", ":")),
                    "MODULE\n" + json.dumps(module, ensure_ascii=False, separators=(",", ":")),
                    "DEPENDENCY POINTERS\n" + json.dumps({dep: modules[dep]["context"] for dep in module["dependencies"]}, ensure_ascii=False),
                    "REFERENCES\n" + "\n".join(references)]
        # Only three boot documents are opened, each with a strict byte/read bound.
        for reference, limit in (("AGENTS.md", 2600), ("docs/START.md", 1500), ("docs/project-state.json", 1600)):
            path = contained(self.checkout, reference, must_exist=True)
            try:
                with path.open(encoding="utf-8-sig") as stream:
                    snippet = stream.read(limit + 1)
            except OSError as exc:
                raise HarnessError(f"Cannot read context document {reference}: {exc}") from exc
            if len(snippet) > limit:
                snippet = snippet[:limit] + "\n[excerpt; open reference only if needed]"
            sections.append(reference + "\n" + snippet)
        content = "\n\n".join(sections)
        # Reserve print's newline; no unbounded logs or automatic directory scan.
        if len(content) + 1 > max_chars:
            suffix = "\n[context capped; use task show and listed references as needed]"
            content = content[:max_chars - len(suffix) - 1] + suffix
        return content


def parser():
    result = argparse.ArgumentParser(description="PikDraw provider-neutral agent harness")
    result.add_argument("--repo", type=Path, default=Path.cwd(), help="Checkout or primary repository path")
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor", help="Validate module graph, routes and task coordination")
    context = commands.add_parser("context", help="Bounded task boot context")
    context.add_argument("--task-id", required=True)
    context.add_argument("--max-chars", type=int, default=12000)
    task = commands.add_parser("task").add_subparsers(dest="action", required=True)
    create = task.add_parser("create")
    create.add_argument("id")
    for name in ("title", "module", "goal", "provider", "base"):
        create.add_argument("--" + name, required=True)
    create.add_argument("--path", dest="paths", action="append", help="Explicit owned path; repeat to override module defaults")
    claim = task.add_parser("claim")
    claim.add_argument("id")
    claim.add_argument("--owner", required=True)
    claim.add_argument("--provider", required=True)
    update = task.add_parser("update")
    update.add_argument("id")
    update.add_argument("--owner", required=True)
    update.add_argument("--status", choices=sorted(STATUSES), required=True)
    update.add_argument("--summary", required=True)
    update.add_argument("--next", dest="next_steps", action="append", default=[])
    update.add_argument("--evidence", action="append", default=[])
    release = task.add_parser("release")
    release.add_argument("id")
    release.add_argument("--owner", required=True)
    release.add_argument("--summary", required=True)
    show = task.add_parser("show")
    show.add_argument("id")
    task.add_parser("list")
    worktree = commands.add_parser("worktree").add_subparsers(dest="action", required=True)
    worktree.add_parser("list")
    worktree_create = worktree.add_parser("create")
    for name in ("task-id", "provider", "base", "owner"):
        worktree_create.add_argument("--" + name, required=True)
    handoff = commands.add_parser("handoff", help="Publish a bounded committed handoff; release task into review")
    for name in ("task-id", "owner", "head", "summary"):
        handoff.add_argument("--" + name, required=True)
    for name, destination in (("changed-path", "changed_paths"), ("decision", "decisions"),
                              ("test", "tests"), ("open-question", "questions"), ("next-step", "next_steps")):
        handoff.add_argument("--" + name, dest=destination, action="append", default=[])
    return result


def main(argv=None):
    # Windows pipes otherwise inherit a legacy code page and can fail on Vietnamese.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    args = parser().parse_args(argv)
    try:
        harness = Harness(args.repo)
        if args.command == "doctor":
            output = harness.doctor()
            print(json.dumps(output, ensure_ascii=False, indent=2))
            return 0 if output["ok"] else 1
        if args.command == "context":
            print(harness.context(args.task_id, args.max_chars))
            return 0
        if args.command == "task":
            if args.action == "create":
                output = harness.create_task(args.id, args.title, args.module, args.goal, args.provider, args.base, args.paths)
            elif args.action == "claim":
                output = harness.claim(args.id, args.owner, args.provider)
            elif args.action == "update":
                output = harness.update(args.id, args.owner, args.status, args.summary, args.next_steps, args.evidence)
            elif args.action == "release":
                output = harness.release(args.id, args.owner, args.summary)
            elif args.action == "show":
                output = harness.task(args.id)
            else:
                output = harness.all_tasks()
        elif args.command == "worktree":
            output = harness.worktrees() if args.action == "list" else harness.create_worktree(args.task_id, args.provider, args.base, args.owner)
        else:
            output = harness.handoff(args.task_id, args.owner, args.head, args.summary, args.changed_paths,
                                     args.decisions, args.tests, args.questions, args.next_steps)
        print(json.dumps(output, ensure_ascii=False, indent=2))
        return 0
    except (HarnessError, OSError) as exc:
        print(f"PikDraw harness: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
