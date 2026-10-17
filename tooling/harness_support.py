"""Validated paths, Git and atomic board storage shared by the local harness."""
from __future__ import annotations
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
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


