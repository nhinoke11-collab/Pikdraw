"""Shared state primitives for the canonical native build queue (stdlib only)."""
from __future__ import annotations

import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time
import uuid

TERMINAL = {"succeeded", "failed", "stale", "interrupted", "cancelled"}
ID = re.compile(r"^[a-f0-9]{32}$")
BUILD_PROCESSES = {"cmake.exe", "ninja.exe", "ctest.exe", "clang.exe", "clang++.exe",
                   "c++.exe", "g++.exe", "cl.exe", "link.exe", "lld-link.exe", "ld.lld.exe",
                   "llvm-ar.exe", "llvm-ranlib.exe", "llvm-strip.exe", "ar.exe", "ranlib.exe"}


def is_build_process_name(name: str) -> bool:
    name = name.lower()
    # LLVM-MinGW launches clang-23.exe through the Windows short name C__~1.EXE.
    return name in BUILD_PROCESSES or bool(re.fullmatch(
        r"(?:clang(?:\+\+)?(?:-\d+(?:\.\d+)*)?|c__~\d+|llvm-r~\d+)\.exe", name))


def safe_path(root: Path, path: Path) -> Path:
    """Reject traversal and all symlink/junction ancestors before writing or deleting."""
    root, path = Path(os.path.abspath(root)), Path(os.path.abspath(path))
    if path != root and root not in path.parents:
        raise ValueError(f"Output outside workspace: {path}")
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            try:
                info = item.lstat()
            except FileNotFoundError:
                continue  # A successful run may have just removed its owned output.
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError(f"Refusing output through a reparse point: {item}")
        if item == root:
            break
    return path


def open_shared(path: Path):
    """Readers allow atomic replacement/deletion while retaining their own handle."""
    if os.name != "nt":
        return path.open("rb")
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        error = ctypes.get_last_error()
        if error in (2, 3):
            raise FileNotFoundError(str(path))
        if error in (5, 32):
            raise PermissionError(str(path))
        raise OSError(error, str(path))
    return os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDONLY), "rb")


def read_shared(path: Path) -> bytes:
    for attempt in range(100):
        try:
            with open_shared(path) as stream:
                return stream.read()
        except (FileNotFoundError, PermissionError):
            if attempt == 99:
                raise
            time.sleep(.01)
    raise AssertionError("unreachable")


def read_json(path: Path) -> dict:
    return json.loads(read_shared(path).decode("utf-8-sig"))


def atomic_json(root: Path, path: Path, value: dict) -> None:
    safe_path(root, path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
        # Windows readers/antivirus may briefly deny delete-sharing on the old JSON.
        for attempt in range(100):
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.02)
    finally:
        temporary.unlink(missing_ok=True)


def process_token(pid: int) -> str:
    if not pid:
        return ""
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.c_void_p] * 4
        kernel.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return "unknown" if ctypes.get_last_error() == 5 else ""
        try:
            exit_code = ctypes.c_uint32()
            if not kernel.GetExitCodeProcess(handle, ctypes.byref(exit_code)) or exit_code.value != 259:
                return ""
            times = [ctypes.c_uint64() for _ in range(4)]
            if not kernel.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)):
                return "unknown"
            return str(times[0].value)
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return Path(f"/proc/{pid}/stat").read_text().split()[21] if Path("/proc").exists() else "alive"
    except (ProcessLookupError, FileNotFoundError):
        return ""
    except PermissionError:
        return "unknown"


def alive(pid: int, token: str = "") -> bool:
    current = process_token(pid)
    return bool(current) and (not token or current == "unknown" or current == token)


def active_build_processes() -> list[str]:
    if os.name != "nt":
        return []
    from ctypes import wintypes

    class Entry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG),
                    ("flags", wintypes.DWORD), ("name", wintypes.WCHAR * 260)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype = ctypes.c_void_p
    kernel.Process32FirstW.argtypes = [ctypes.c_void_p, ctypes.POINTER(Entry)]
    kernel.Process32NextW.argtypes = [ctypes.c_void_p, ctypes.POINTER(Entry)]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise OSError("Cannot verify that orphaned build processes have stopped.")
    found = []
    try:
        entry = Entry()
        entry.dwSize = ctypes.sizeof(entry)
        valid = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while valid:
            if is_build_process_name(entry.name):
                found.append(f"{entry.name}:{entry.pid}")
            valid = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    return found


class BuildLock:
    """CreateNew and deny delete/write sharing, compatible with the old PS runner."""
    def __init__(self, root: Path, request_id: str):
        self.root, self.path = root, root / ".agent/native-build.lock"
        self.request_id, self.handle = request_id, None

    def acquire(self) -> bool:
        safe_path(self.root, self.path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if os.name == "nt":
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            handle = kernel.CreateFileW(str(self.path), 0x40000000, 1, None, 1, 0x80, None)
            if handle == ctypes.c_void_p(-1).value:
                error = ctypes.get_last_error()
                if error not in (32, 80, 183, 5):
                    raise OSError(error, "Cannot acquire build lock")
                return self._recover_dead_owner()
            import msvcrt
            self.handle = os.fdopen(msvcrt.open_osfhandle(handle, os.O_WRONLY), "wb")
        else:
            try:
                self.handle = self.path.open("xb")
            except FileExistsError:
                return self._recover_dead_owner()
        content = f"pid={os.getpid()} source={self.root} request={self.request_id} token={process_token(os.getpid())}"
        self.handle.write(content.encode("utf-8"))
        self.handle.flush()
        return True

    def _recover_dead_owner(self) -> bool:
        try:
            content = self.path.read_text(encoding="utf-8-sig")
        except (FileNotFoundError, PermissionError):
            return False
        match = re.match(r"pid=(\d+)\b", content)
        token = re.search(r"\btoken=(\S+)", content)
        if not match or alive(int(match[1]), token[1] if token else ""):
            return False
        if active_build_processes():
            return False
        try:
            if self.path.read_text(encoding="utf-8-sig") == content:
                self.path.unlink()
        except (FileNotFoundError, PermissionError):
            pass
        return False

    def release(self) -> None:
        if self.handle is not None:
            self.handle.close()
            self.handle = None
            for attempt in range(100):
                try:
                    self.path.unlink(missing_ok=True)
                    break
                except PermissionError:
                    if attempt == 99:
                        raise
                    time.sleep(.02)


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_digest(root: Path) -> str:
    """Actual tracked/untracked inputs, not HEAD; prose and generated output excluded."""
    result = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
                             "--exclude-standard"], capture_output=True, check=True)
    names = sorted(set(os.fsdecode(p) for p in result.stdout.split(b"\0") if p))
    digest = hashlib.sha256()
    for name in names:
        path = root / name
        if name.startswith(("docs/", ".agent/", "build/", ".git/")) or path.suffix.lower() in {".md", ".log", ".tmp"}:
            continue
        digest.update(name.encode("utf-8", errors="surrogateescape") + b"\0")
        if path.is_symlink():
            digest.update(os.readlink(path).encode("utf-8"))
        elif path.is_file():
            before = path.stat()
            digest.update(digest_file(path).encode("ascii"))
            after = path.stat()
            if (before.st_mtime_ns, before.st_size) != (after.st_mtime_ns, after.st_size):
                raise RuntimeError(f"Source changed while hashing: {path}")
            # A modify/restore edit during the run also invalidates the result.
            digest.update(str(after.st_mtime_ns).encode("ascii"))
        else:
            digest.update(b"deleted")
    return digest.hexdigest()


def environment_digest() -> str:
    # Environment values never go to the queue, logs or command lines.
    values = {k: v for k, v in os.environ.items() if k not in {"PIKDRAW_TEST_OUTPUT_DIR", "_", "SHLVL"}}
    return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()


def remove_run(root: Path, run: Path) -> None:
    safe_path(root, run)
    if run.parent != root / "build/win-dev/test-output" or not re.fullmatch(r"\d+-[a-f0-9]{32}", run.name):
        raise ValueError(f"Not an owned run: {run}")
    if not (run / ".pikdraw-test-run").is_file():
        raise ValueError(f"Missing run ownership marker: {run}")
    for directory, children, files in os.walk(run, followlinks=False):
        for name in children + files:
            safe_path(root, Path(directory) / name)
    shutil.rmtree(run)
