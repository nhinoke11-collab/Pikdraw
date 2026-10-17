"""Meaningful coordinator regressions; all Git mutations stay in temporary repos."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "tooling/agent_harness.py"
sys.path.insert(0, str(SOURCE.parent))
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
                                        "Make undo predictable", "codex", "main", paths)

    def claim(self, identifier="doc-task", owner="codex:session-1"):
        return self.harness.claim(identifier, owner, "codex")

    def commit_change(self, path="modules/document/command.txt"):
        self.write(path, "A new tracked command\n")
        self.git("add", "--", path)
        self.git("commit", "-m", "Task implementation")
        return self.git("rev-parse", "HEAD")

    def handoff(self, head="main", **overrides):
        arguments = dict(task_id="doc-task", owner="codex:session-1", head=head,
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
            self.claim(owner="codex:session-2")
        with self.assertRaisesRegex(h.HarnessError, "Single-source|Provider differs"):
            self.harness.claim("doc-task", "provider-b:session-1", "provider-b")

    def test_claim_rejects_overlapping_paths_across_different_tasks(self):
        self.create()
        self.create("second", paths=["modules/document/nested"])
        self.claim()
        with self.assertRaisesRegex(h.HarnessError, "Ownership conflict"):
            self.claim("second", "codex:session-2")
        self.assertIsNone(self.harness.task("second")["lease"])

    def test_overlap_comparison_is_conservative_for_windows(self):
        self.assertTrue(h.paths_overlap("modules/Document", "modules/document/nodes"))
        self.assertFalse(h.paths_overlap("modules/document", "modules/documentation"))

    def test_disjoint_claims_can_run_in_parallel(self):
        self.create()
        self.create("render-task", "rendering")
        self.claim()
        self.claim("render-task", "codex:session-2")
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
            self.harness.update("doc-task", "codex:session-1", "done", "Done", [], [])
        with self.assertRaisesRegex(h.HarnessError, "at most 320 characters"):
            self.harness.update("doc-task", "codex:session-1", "in_progress", "x" * 321, [], [])
        with self.assertRaisesRegex(h.HarnessError, "at most 6"):
            self.harness.update("doc-task", "codex:session-1", "in_progress", "Short", [], ["e"] * 7)
        reviewed = self.harness.update("doc-task", "codex:session-1", "review", "Ready for review", [], ["Unit test pass"])
        self.assertIsNone(reviewed["lease"])
        self.claim()
        completed = self.harness.update("doc-task", "codex:session-1", "done", "Done", [], ["Regression passed"])
        self.assertIsNone(completed["lease"])
        with self.assertRaisesRegex(h.HarnessError, "cannot be claimed"):
            self.claim()

    def test_doctor_rejects_bloated_project_state(self):
        self.write_json("docs/project-state.json", {"schema_version": 1, "history": "x" * 8100})
        result = self.harness.doctor()
        self.assertFalse(result["ok"])
        self.assertIn("exceeds 8000 bytes", result["issues"][0])

    def test_doctor_rejects_non_markdown_files_in_docs(self):
        self.write("docs/quality/fixture.json", "{}")
        result = self.harness.doctor()
        self.assertFalse(result["ok"])
        self.assertIn("docs/ must only contain .md files", result["issues"][0])

    def test_explicit_release_allows_next_task_to_claim(self):
        self.create()
        self.create("next-task")
        self.claim()
        self.harness.release("doc-task", "codex:session-1", "Handing off to another session")
        self.assertIsNotNone(self.claim("next-task", "codex:session-2")["lease"])

    def test_context_is_bounded_and_selectively_routes_references(self):
        self.create()
        context = self.harness.context("doc-task", 1000)
        self.assertLessEqual(len(context) + 1, 1000)
        full = self.harness.context("doc-task")
        self.assertNotIn("NEVER_LOAD_IRRELEVANT_SENTINEL", full)
        self.assertNotIn("docs/irrelevant.md", full)
        self.assertIn("modules/document/README.md", full)
        self.assertIn("excerpt", full)

    def legacy_checkout(self):
        target = self.repo / '.worktrees/legacy'
        self.git('worktree', 'add', '-b', 'legacy', str(target), 'main')
        return target

    def test_new_worktree_rejected_without_mutation(self):
        self.create()
        self.claim()
        before = self.git('show-ref')
        with self.assertRaisesRegex(h.HarnessError, 'worktree create is disabled'):
            self.harness.create_worktree('doc-task', 'codex', 'main', 'codex:session-1')
        self.assertEqual(before, self.git('show-ref'))
        self.assertFalse((self.repo / '.worktrees').exists())
        self.assertIsNone(self.harness.task('doc-task')['worktree'])

    def test_legacy_checkout_can_read_board_but_cannot_write(self):
        self.create()
        linked = h.Harness(self.legacy_checkout())
        self.assertEqual(linked.task('doc-task')['id'], 'doc-task')
        with self.assertRaisesRegex(h.HarnessError, 'primary source'):
            linked.claim('doc-task', 'codex:session-1', 'codex')
        self.assertIsNone(self.harness.task('doc-task')['lease'])

    def test_other_agent_system_cannot_create_tasks(self):
        with self.assertRaisesRegex(h.HarnessError, 'Single agent system'):
            self.harness.create_task('foreign', 'Foreign task', 'document', 'Goal', 'claude', 'HEAD')
        self.assertFalse(self.harness.task_path('foreign').exists())

    def test_handoff_preserves_inherited_dirty_and_reports_owned_source_changes(self):
        self.write('modules/document/README.md', 'Inherited user edit\n')
        self.write('modules/rendering/README.md', 'Unrelated user edit\n')
        self.write('modules/document/inherited.txt', 'Inherited untracked\n')
        self.create()
        self.claim()
        self.write('modules/document/new.txt', 'Task addition\n')
        self.write('modules/document/README.md', 'Inherited user edit plus fix\n')
        packet = self.handoff(head='HEAD')
        self.assertEqual(packet['changed_paths'], ['modules/document/README.md', 'modules/document/new.txt'])
        self.assertTrue(packet['uncommitted'])
        baseline = h.read_json(self.repo / packet['source_baseline'])
        self.assertIn('modules/document/inherited.txt', baseline['files'])
        self.assertNotIn('modules/rendering/README.md', baseline['files'])
        self.assertEqual(self.git('diff', '--cached'), '')
        self.assertEqual((self.repo / 'modules/rendering/README.md').read_text(), 'Unrelated user edit\n')
        before = (self.repo / packet['source_baseline']).parent / baseline['files_directory'] / 'modules/document/README.md'
        self.assertEqual(before.read_text(), 'Inherited user edit\n')
        self.assertEqual(h.read_json(self.repo / packet['source_snapshot'])['head_oid'], self.git('rev-parse','HEAD'))

    def test_source_handoff_detects_deletions_and_staged_renames(self):
        self.create()
        self.claim()
        self.git('mv', 'modules/document/README.md', 'modules/document/renamed.md')
        packet = self.handoff(head='HEAD')
        self.assertEqual(packet['changed_paths'], ['modules/document/README.md', 'modules/document/renamed.md'])
        self.assertFalse((self.repo / 'modules/document/README.md').exists())
        self.assertIn('modules/document/renamed.md', self.git('diff','--cached','--name-only'))

    def test_stale_task_base_cannot_claim(self):
        self.create()
        self.commit_change()
        with self.assertRaisesRegex(h.HarnessError, 'base is stale'):
            self.claim()
        self.assertIsNone(self.harness.task('doc-task')['lease'])

    def test_root_manifest_source_copy_cannot_collide_with_snapshot_metadata(self):
        self.write('manifest.json', 'Original source bytes')
        self.create(paths=['manifest.json'])
        self.claim()
        baseline_path = self.repo / self.harness.task('doc-task')['source_baseline']
        baseline = h.read_json(baseline_path)
        preserved = baseline_path.parent / baseline['files_directory'] / 'manifest.json'
        self.assertEqual(preserved.read_text(), 'Original source bytes')
        self.write('manifest.json', 'Task source bytes')
        self.assertEqual(self.handoff(head='HEAD')['changed_paths'], ['manifest.json'])

    def test_context_from_legacy_checkout_uses_current_primary_instructions(self):
        self.create()
        checkout = self.legacy_checkout()
        (checkout / 'docs/START.md').write_text('STALE_WORKTREE_SENTINEL', encoding='utf-8')
        self.write('docs/START.md', 'CURRENT_PRIMARY_SENTINEL')
        context = h.Harness(checkout).context('doc-task')
        self.assertIn('CURRENT_PRIMARY_SENTINEL', context)
        self.assertNotIn('STALE_WORKTREE_SENTINEL', context)

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

    def test_handoff_rejects_non_current_head(self):
        self.create()
        self.claim()
        base = self.git('rev-parse', 'HEAD')
        self.commit_change()
        with self.assertRaisesRegex(h.HarnessError, 'match primary HEAD'):
            self.handoff(head=base)

    def test_cli_rejects_legacy_checkout_writes(self):
        self.create()
        checkout = self.legacy_checkout()
        result = subprocess.run([sys_executable(), str(SOURCE), 'task', 'claim', 'doc-task',
                                 '--owner', 'codex:session-1', '--provider', 'codex'],
                                cwd=checkout, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('primary source', result.stderr)

    def test_doctor_rejects_corrupt_worktree_assignment(self):
        task = self.create()
        task["worktree"] = {"path": "../escaped", "branch": "agent/codex/doc-task",
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
