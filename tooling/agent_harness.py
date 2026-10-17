#!/usr/bin/env python3
"""PikDraw's local single-source task/context coordinator.

Python stdlib only. Task leases are explicit: a clock never transfers ownership.
The primary source owns the board; legacy worktrees are readable migration inputs.
All board writers take one O_EXCL lock.
This is coordination, not a security sandbox for agents or third-party plugins.
"""
from __future__ import annotations

import argparse
import hashlib
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import sys
import tempfile

from harness_support import (SCHEMA_VERSION, STATUSES, HarnessError, now, checked_slug,
    checked_owner, checked_text, relative_path, contained, read_json, atomic_json,
    git, coordinator_root, commit_oid, paths_overlap, path_owned)
from harness_context import ContextRoutes


class Harness(ContextRoutes):
    def __init__(self, repo: Path):
        top = git(repo.resolve(), "rev-parse", "--show-toplevel", check=False)
        self.checkout = Path(top.stdout.strip()).resolve() if not top.returncode else repo.resolve()
        self.root = coordinator_root(repo)
        self.board = contained(self.root, ".agent")

    def require_primary(self):
        if self.checkout != self.root or Path.cwd().resolve().is_relative_to(self.root / ".worktrees"):
            raise HarnessError(f"Single-source policy: run writes from the primary source: {self.root}")

    def source_files(self, owned):
        output = git(self.root, "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                     "--", *[":(literal)" + path for path in owned]).stdout
        files = {}
        for relative in sorted(set(filter(None, output.split("\0")))):
            path = contained(self.root, relative, allow_runtime=False)
            if path.is_file():
                files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
            elif path.exists():
                raise HarnessError(f"Unsupported source entry: {relative}")
        return files

    def source_snapshot(self, task):
        files = self.source_files(task["owned_paths"])
        directory = contained(self.root, f".agent/snapshots/{task['id']}")
        directory.mkdir(parents=True, exist_ok=True)
        target = Path(tempfile.mkdtemp(prefix="source-", dir=directory))
        for relative, digest in files.items():
            data = contained(self.root, relative, allow_runtime=False).read_bytes()
            if hashlib.sha256(data).hexdigest() != digest:
                raise HarnessError("Source changed while taking snapshot; retry after writers finish.")
            copy = contained(target, "files/" + relative, allow_runtime=False)
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_bytes(data)
        if files != self.source_files(task["owned_paths"]):
            raise HarnessError("Source changed while taking snapshot; retry after writers finish.")
        manifest = {"schema_version": 1, "head_oid": commit_oid(self.root, "HEAD"),
                    "files": files, "files_directory": "files", "created_at": now()}
        atomic_json(target / "manifest.json", manifest)
        return target.relative_to(self.root).as_posix() + "/manifest.json"

    @contextmanager
    def locked(self):
        self.require_primary()
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
        self.require_primary()
        checked_slug(task_id, "task id")
        checked_slug(provider, "provider")
        if provider != "codex":
            raise HarnessError("Single agent system: new tasks use --provider codex; historical records remain readable.")
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
            if provider != "codex" or task.get("worktree"):
                raise HarnessError("Single-source policy: migrate the legacy task; claim a Codex task at the primary source.")
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
            if not task.get("source_baseline"):
                if commit_oid(self.root, "HEAD") != task["base"]["oid"]:
                    raise HarnessError("Task base is stale; create a scoped task at the primary HEAD.")
                task["source_baseline"] = self.source_snapshot(task)
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
        raise HarnessError(f"Single-source policy: worktree create is disabled. Edit owned paths in {self.root}.")

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
            if task.get("worktree"):
                raise HarnessError("Migrate legacy worktree before a single-source handoff.")
            head_oid = commit_oid(self.root, head)
            if commit_oid(self.root, "HEAD") != head_oid:
                raise HarnessError("Handoff head must match primary HEAD.")
            if not task.get("source_baseline"):
                raise HarnessError("Missing source baseline; claim again before editing, never infer a baseline afterwards.")
            if git(self.root, "merge-base", "--is-ancestor", base_oid, head_oid, check=False).returncode:
                raise HarnessError("Handoff head must descend from the assigned base commit.")
            committed = filter(None, git(self.root, "diff", "--name-only", "-z", "--no-renames", base_oid, head_oid).stdout.split("\0"))
            for path in committed:
                contained(self.root, path, allow_runtime=False)
                if not path_owned(path, task["owned_paths"]):
                    raise HarnessError(f"Changed file is outside task ownership: {path}")
            baseline = read_json(contained(self.root, task["source_baseline"], must_exist=True))
            snapshot_path = self.source_snapshot(task)
            snapshot = read_json(contained(self.root, snapshot_path, must_exist=True))
            actual = sorted(path for path in baseline["files"].keys() | snapshot["files"].keys()
                            if baseline["files"].get(path) != snapshot["files"].get(path))
            if changed_paths and sorted(set(changed_paths)) != actual:
                raise HarnessError("Declared changed paths differ from the source snapshot diff.")
            if not tests:
                raise HarnessError("Handoff needs --test evidence (or an explicit reason a check was not run).")
            packet = {"schema_version": 1, "type": "handoff", "task_id": task_id,
                      "provider": task["provider"], "owner": owner, "module": task["module"],
                      "base": task["base"], "head": {"ref": head, "oid": head_oid},
                      "source_baseline": task["source_baseline"], "source_snapshot": snapshot_path,
                      "uncommitted": bool(git(self.root, "status", "--porcelain").stdout.strip()),
                      "summary": summary, "changed_paths": actual, "decisions": decisions,
                      "tests": tests, "open_questions": questions, "next_steps": next_steps,
                      "created_at": now()}
            packet_path = f".agent/handoffs/{task_id}-{Path(snapshot_path).parent.name}.json"
            atomic_json(contained(self.root, packet_path), packet)
            task.update(status="review", summary=summary, next_steps=next_steps,
                        handoff=packet_path, lease=None,
                        evidence=list(dict.fromkeys(task.get("evidence", []) + tests))[-32:])
            self.save_task(task)
        return packet



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
    handoff = commands.add_parser("handoff", help="Snapshot owned source changes without committing; release task into review")
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
