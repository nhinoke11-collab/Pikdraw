"""Meaningful coordinator regressions; all Git mutations stay in temporary repos."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "tooling/agent_harness.py"
SPEC = importlib.util.spec_from_file_location("pikdraw_agent_harness", SOURCE)
h = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(h)


@unittest.skipUnless(shutil.which("git"), "Git is required for harness integration tests")
class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="pikdraw-harness-")
        self.repo = Path(self.temporary.name) / "repo"
        self.repo.mkdir()
        self.write("AGENTS.md", "Canonical agent instructions.\n" * 300)
        self.write("docs/START.md", "Read the task before changing its owned paths.\n")
        self.write_json("docs/project-state.json", {"schema_version": 1, "stage": "scaffold"})
        self.write("modules/document/README.md", "Document module\n")
        self.write("modules/rendering/README.md", "Rendering module\n")
        self.write("docs/irrelevant.md", "NEVER_LOAD_IRRELEVANT_SENTINEL\n")
        self.registry = {"schema_version": 1, "modules": [
            {"id": "document", "path": "modules/document", "status": "planned",
             "dependencies": [], "context": ["modules/document/README.md"],
             "owned_paths": ["modules/document"]},
            {"id": "rendering", "path": "modules/rendering", "status": "planned",
             "dependencies": ["document"], "context": ["modules/rendering/README.md"],
             "owned_paths": ["modules/rendering"]}]}
        self.write_json("registry/modules.json", self.registry)
        self.write_json("registry/boot.json", {"schema_version": 1, "routes": [
            {"id": "document", "module": "document", "paths": ["modules/document/README.md"]},
            {"id": "rendering", "module": "rendering", "paths": ["docs/irrelevant.md"]}]})
        self.write(".gitignore", ".agent/\n.worktrees/\n")
        self.git("init", "--initial-branch=main")
        self.git("config", "user.name", "Harness Test")
        self.git("config", "user.email", "harness-test@example.invalid")
        self.git("add", ".")
        self.git("commit", "-m", "Fixture baseline")
        self.harness = h.Harness(self.repo)

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, path, content):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def write_json(self, path, data):
        self.write(path, json.dumps(data))

    def git(self, *args, root=None):
        result = subprocess.run(["git", "-C", str(root or self.repo), *args], capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def create(self, identifier="doc-task", module="document", paths=None):
        return self.harness.create_task(identifier, "Implement document command", module,
                                        "Make undo predictable", "provider-a", "main", paths)

    def claim(self, identifier="doc-task", owner="provider-a:session-1"):
        return self.harness.claim(identifier, owner, "provider-a")

    def commit_change(self, path="modules/document/command.txt"):
        self.write(path, "A new tracked command\n")
        self.git("add", "--", path)
        self.git("commit", "-m", "Task implementation")
        return self.git("rev-parse", "HEAD")

    def handoff(self, head="main", **overrides):
        arguments = dict(task_id="doc-task", owner="provider-a:session-1", head=head,
                         summary="Implemented document command", changed_paths=[],
                         decisions=["Keep the document API provider-neutral"],
                         tests=["Document command regression passed"], questions=[],
                         next_steps=["Review the document command"])
        arguments.update(overrides)
        return self.harness.handoff(**arguments)

    def test_doctor_validates_fixture(self):
        result = self.harness.doctor()
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["modules"], 2)

    def test_doctor_reports_cycle(self):
        self.registry["modules"][0]["dependencies"] = ["rendering"]
        self.write_json("registry/modules.json", self.registry)
        result = self.harness.doctor()
        self.assertFalse(result["ok"])
        self.assertIn("cycle", result["issues"][0])

    def test_doctor_rejects_unknown_dependency(self):
        self.registry["modules"][0]["dependencies"] = ["unknown"]
        self.write_json("registry/modules.json", self.registry)
        self.assertIn("unknown dependencies", self.harness.doctor()["issues"][0])

    def test_doctor_rejects_broken_boot_reference(self):
        self.write_json("registry/boot.json", {"schema_version": 1,
                                              "routes": [{"id": "general", "paths": ["docs/missing.md"]}]})
        self.assertIn("Missing referenced path", self.harness.doctor()["issues"][0])

    def test_unsafe_paths_and_slug_are_rejected(self):
        for value in ("../escape", "/absolute", "C:/escape", "a\\b", "a/../b", ".git/config",
                      ".GIT/config", ".git./config", ".git /config", "CON.txt", "x/NUL",
                      ".worktrees/x", ".codex/x", "a//b", "a/./b"):
            with self.subTest(path=value), self.assertRaises(h.HarnessError):
                h.relative_path(value)
        with self.assertRaises(h.HarnessError):
            self.create(identifier="../escape")
        with self.assertRaises(h.HarnessError):
            self.create(paths=[".agent/tasks/doc-task.json"])
        with self.assertRaises(h.HarnessError):
            self.create(paths=[".AGENT/tasks/doc-task.json"])

    def test_symlink_context_cannot_escape_repository(self):
        external = Path(self.temporary.name) / "external.md"
        external.write_text("Outside", encoding="utf-8")
        link = self.repo / "docs/link.md"
        try:
            link.symlink_to(external)
        except OSError as exc:
            self.skipTest(f"Host does not permit creating symlinks: {exc}")
        self.registry["modules"][0]["context"].append("docs/link.md")
        self.write_json("registry/modules.json", self.registry)
        self.assertIn("escapes repository", self.harness.doctor()["issues"][0])

    def test_create_pins_base_commit_and_is_non_destructive(self):
        task = self.create()
        self.assertEqual(task["base"]["oid"], self.git("rev-parse", "main"))
        self.assertIsNone(task["lease"])
        with self.assertRaisesRegex(h.HarnessError, "already exists"):
            self.create()
        self.assertEqual(self.harness.task("doc-task")["base"], task["base"])

    def test_claim_rejects_different_owner_and_provider(self):
        self.create()
        first = self.claim()
        again = self.claim()
        self.assertEqual(first["lease"]["acquired_at"], again["lease"]["acquired_at"])
        with self.assertRaisesRegex(h.HarnessError, "already leased"):
            self.claim(owner="provider-a:session-2")
        with self.assertRaisesRegex(h.HarnessError, "Provider differs"):
            self.harness.claim("doc-task", "provider-b:session-1", "provider-b")

    def test_claim_rejects_overlapping_paths_across_different_tasks(self):
        self.create()
        self.create("second", paths=["modules/document/nested"])
        self.claim()
        with self.assertRaisesRegex(h.HarnessError, "Ownership conflict"):
            self.claim("second", "provider-a:session-2")
        self.assertIsNone(self.harness.task("second")["lease"])

    def test_overlap_comparison_is_conservative_for_windows(self):
        self.assertTrue(h.paths_overlap("modules/Document", "modules/document/nodes"))
        self.assertFalse(h.paths_overlap("modules/document", "modules/documentation"))

    def test_disjoint_claims_can_run_in_parallel(self):
        self.create()
        self.create("render-task", "rendering")
        self.claim()
        self.claim("render-task", "provider-a:session-2")
        self.assertTrue(self.harness.doctor()["ok"])

    def test_board_lock_refuses_writer_without_timeout_stealing(self):
        self.create()
        with self.harness.locked():
            with self.assertRaisesRegex(h.HarnessError, "board is locked"):
                self.claim()
        self.assertFalse((self.repo / ".agent/board.lock").exists())
        self.assertIsNotNone(self.claim()["lease"])

    def test_update_requires_lease_and_evidence_to_complete(self):
        self.create()
        self.claim()
        with self.assertRaisesRegex(h.HarnessError, "not leased"):
            self.harness.update("doc-task", "someone-else", "done", "Done", [], ["Passed"])
        with self.assertRaisesRegex(h.HarnessError, "validation evidence"):
            self.harness.update("doc-task", "provider-a:session-1", "done", "Done", [], [])
        completed = self.harness.update("doc-task", "provider-a:session-1", "done", "Done", [], ["Regression passed"])
        self.assertIsNone(completed["lease"])
        with self.assertRaisesRegex(h.HarnessError, "cannot be claimed"):
            self.claim()

    def test_explicit_release_allows_next_task_to_claim(self):
        self.create()
        self.create("next-task")
        self.claim()
        self.harness.release("doc-task", "provider-a:session-1", "Handing off to another session")
        self.assertIsNotNone(self.claim("next-task", "provider-a:session-2")["lease"])

    def test_context_is_bounded_and_selectively_routes_references(self):
        self.create()
        context = self.harness.context("doc-task", 1000)
        self.assertLessEqual(len(context) + 1, 1000)
        full = self.harness.context("doc-task")
        self.assertNotIn("NEVER_LOAD_IRRELEVANT_SENTINEL", full)
        self.assertNotIn("docs/irrelevant.md", full)
        self.assertIn("modules/document/README.md", full)
        self.assertIn("excerpt", full)

    def test_worktree_shares_primary_task_board(self):
        self.create()
        self.claim()
        created = self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        linked = h.Harness(Path(created["path"]))
        self.assertEqual(linked.root, self.repo.resolve())
        self.assertEqual(linked.task("doc-task")["lease"]["owner"], "provider-a:session-1")
        self.assertTrue(any(row["worktree"].replace("\\", "/") == created["path"].replace("\\", "/")
                            for row in linked.worktrees()))

    def test_context_reads_invocation_checkout_documents(self):
        self.create()
        self.claim()
        created = self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        checkout = Path(created["path"])
        (checkout / "docs/START.md").write_text("TASK_CHECKOUT_CONTEXT_SENTINEL\n", encoding="utf-8")
        linked = h.Harness(checkout)
        packet = linked.context("doc-task")
        self.assertIn("TASK_CHECKOUT_CONTEXT_SENTINEL", packet)
        self.assertNotIn("TASK_CHECKOUT_CONTEXT_SENTINEL", self.harness.context("doc-task"))

    def test_worktree_collision_does_not_create_branch(self):
        self.create()
        self.claim()
        target = self.repo / ".worktrees/provider-a-doc-task"
        target.mkdir(parents=True)
        (target / "keep.txt").write_text("Preserve me", encoding="utf-8")
        with self.assertRaisesRegex(h.HarnessError, "existing worktree path"):
            self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        self.assertTrue((target / "keep.txt").is_file())
        result = h.git(self.repo, "show-ref", "--verify", "refs/heads/agent/provider-a/doc-task", check=False)
        self.assertNotEqual(result.returncode, 0)

    def test_worktree_requires_matching_assignment_and_owner(self):
        self.create()
        with self.assertRaisesRegex(h.HarnessError, "not leased"):
            self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        self.claim()
        with self.assertRaisesRegex(h.HarnessError, "provider/base"):
            self.harness.create_worktree("doc-task", "provider-b", "main", "provider-a:session-1")

    def test_handoff_uses_committed_diff_and_releases_for_review(self):
        self.create()
        self.claim()
        head = self.commit_change()
        packet = self.handoff()
        self.assertEqual(packet["head"]["oid"], head)
        self.assertEqual(packet["changed_paths"], ["modules/document/command.txt"])
        task = self.harness.task("doc-task")
        self.assertEqual(task["status"], "review")
        self.assertIsNone(task["lease"])
        self.assertEqual(h.read_json(self.repo / task["handoff"]), packet)

    def test_handoff_rejects_out_of_scope_commit(self):
        self.create()
        self.claim()
        self.commit_change("modules/rendering/foreign.txt")
        with self.assertRaisesRegex(h.HarnessError, "outside task ownership"):
            self.handoff()
        self.assertIsNotNone(self.harness.task("doc-task")["lease"])

    def test_handoff_rejects_false_declared_changes_and_missing_test_evidence(self):
        self.create()
        self.claim()
        self.commit_change()
        with self.assertRaisesRegex(h.HarnessError, "differ from"):
            self.handoff(changed_paths=["modules/document/not-changed.txt"])
        with self.assertRaisesRegex(h.HarnessError, "needs --test"):
            self.handoff(tests=[])

    def test_handoff_requires_clean_registered_worktree_and_matching_head(self):
        self.create()
        self.claim()
        created = self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        checkout = Path(created["path"])
        changed = checkout / "modules/document/command.txt"
        changed.write_text("An owned change\n", encoding="utf-8")
        with self.assertRaisesRegex(h.HarnessError, "worktree is dirty"):
            self.handoff()
        self.git("add", "--", "modules/document/command.txt", root=checkout)
        self.git("commit", "-m", "Implement task in its worktree", root=checkout)
        with self.assertRaisesRegex(h.HarnessError, "match the task worktree HEAD"):
            self.handoff()
        packet = self.handoff(head=created["branch"])
        self.assertEqual(packet["changed_paths"], ["modules/document/command.txt"])

    def test_cli_handoff_head_resolves_in_task_checkout(self):
        self.create()
        self.claim()
        created = self.harness.create_worktree("doc-task", "provider-a", "main", "provider-a:session-1")
        checkout = Path(created["path"])
        (checkout / "modules/document/command.txt").write_text("Task-only commit\n", encoding="utf-8")
        self.git("add", "--", "modules/document/command.txt", root=checkout)
        self.git("commit", "-m", "Implement in linked checkout", root=checkout)
        expected = self.git("rev-parse", "HEAD", root=checkout)
        self.assertNotEqual(expected, self.git("rev-parse", "HEAD"))
        result = subprocess.run([sys_executable(), str(SOURCE), "handoff", "--task-id", "doc-task",
                                 "--owner", "provider-a:session-1", "--head", "HEAD",
                                 "--summary", "Task implemented", "--test", "Regression passed"],
                                cwd=checkout, capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["head"]["oid"], expected)

    def test_doctor_rejects_corrupt_worktree_assignment(self):
        task = self.create()
        task["worktree"] = {"path": "../escaped", "branch": "agent/provider-a/doc-task",
                            "base_oid": task["base"]["oid"]}
        self.write_json(".agent/tasks/doc-task.json", task)
        self.assertIn("worktree path differs", self.harness.doctor()["issues"][0])

    def test_cli_emits_actionable_error_and_doctor_status(self):
        result = subprocess.run([sys_executable(), str(SOURCE), "--repo", str(self.repo), "doctor"],
                                text=True, capture_output=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])
        result = subprocess.run([sys_executable(), str(SOURCE), "--repo", str(self.repo),
                                 "task", "show", "missing"],
                                text=True, capture_output=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 2)
        self.assertIn("PikDraw harness", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_cli_context_emits_utf8_even_with_legacy_windows_pipe_encoding(self):
        self.create()
        self.write("docs/START.md", "Khởi động phiên chỉnh sửa đồ họa tiếng Việt.\n")
        self.write_json("docs/project-state.json", {"schema_version": 1, "stage": "Thiết kế đồ họa"})
        environment = dict(os.environ, PYTHONIOENCODING="cp1252", PYTHONUTF8="0")
        result = subprocess.run([sys_executable(), str(SOURCE), "--repo", str(self.repo),
                                 "context", "--task-id", "doc-task"], env=environment,
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Khởi động phiên chỉnh sửa đồ họa tiếng Việt", result.stdout)
        self.assertNotIn("UnicodeEncodeError", result.stderr)


def sys_executable():
    import sys
    return sys.executable


if __name__ == "__main__":
    unittest.main()
