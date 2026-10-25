"""Behavioral queue, failure and input-version regressions using isolated fake tools."""
import json
import os
from pathlib import Path
import time
import unittest
from unittest.mock import patch

from native_build_fixture import NativeBuildFixture
from build_broker import auto_jobs, log_progress
from build_state import BuildLock, is_build_process_name, safe_path, source_digest


class BuildBrokerTests(NativeBuildFixture, unittest.TestCase):
    def test_orphan_detection_recognizes_real_llvm_mingw_process_names(self):
        for name in ("clang-23.exe", "C__~1.EXE", "LLVM-R~1.EXE", "llvm-ar.exe", "ninja.exe"):
            self.assertTrue(is_build_process_name(name), name)
        for name in ("pikdraw.exe", "python.exe", "notclang-23.exe"):
            self.assertFalse(is_build_process_name(name), name)

    def test_auto_parallelism_uses_available_machine_without_forcing_one_job(self):
        self.assertEqual(auto_jobs(4, 2150), 2)
        self.assertEqual(auto_jobs(4, 3650), 3)
        self.assertEqual(auto_jobs(4, 768), 1)
        self.assertEqual(auto_jobs(1, 8192), 1)
        self.assertEqual(auto_jobs(2, 8192), 2)

    def test_status_reports_steps_without_inventing_time_remaining(self):
        run = self.repo / "build/win-dev/test-output" / ("1-" + "0" * 32)
        run.mkdir(parents=True)
        (run / "build.log").write_text("[117/148 | active=2 | elapsed=900.5s] compile file.cpp\n")
        progress = log_progress(self.repo, str(run))
        self.assertEqual(progress["completed_edges"], 117)
        self.assertEqual(progress["active_edges"], 2)
        self.assertEqual(progress["elapsed_seconds"], 900.5)
        self.assertNotIn("remaining_seconds", progress)
        (run / "build.log").write_text("[118/148] Linking CXX executable pikdraw.exe\n")
        self.assertEqual(log_progress(self.repo, str(run))["completed_edges"], 118)
    def enqueue(self, **changes):
        return self.coordinator.enqueue(self.request(**changes), self.tools)

    def run_entry(self, **changes):
        entry = self.enqueue(**changes)
        self.coordinator.worker(entry["id"])
        return self.coordinator.load(entry["id"])

    def test_exact_target_selects_primary_without_repeating_alias(self):
        result = self.run_entry(test=True, test_targets=["test_alpha"])
        self.assertEqual(result["state"], "succeeded", result)
        self.assertEqual(result["tests"], ["test_alpha"])
        self.assertEqual(list((self.repo / "build/win-dev/test-output").iterdir()), [])

    def test_alias_mapping_regex_and_missing_mapping_fail_closed(self):
        result = self.run_entry(test=True, test_targets=["test_beta"])
        self.assertEqual(result["tests"], ["registered_beta"])
        result = self.run_entry(test=True, test_targets=["test_alpha"], test_regex="^alias_alpha$")
        self.assertEqual(result["tests"], ["alias_alpha"])
        result = self.run_entry(test=True, test_targets=["benchmark_only"])
        self.assertEqual(result["state"], "failed")
        self.assertIn("No registered CTest", result["error"])
        self.assertEqual(sum(c[0] == "ctest" and "-R" in c for c in self.commands()), 2)

    def test_source_edit_during_build_cannot_publish_success(self):
        with patch.dict(os.environ, PIKDRAW_NATIVE_MUTATE="1"):
            result = self.run_entry()
        self.assertEqual(result["state"], "stale", result)
        self.assertNotEqual(result["source"], result["source_after"])
        self.assertTrue(Path(result["output"]).is_dir())

    def test_retry_build_failure_preserves_objects_and_does_not_clean(self):
        build = self.repo / "build/win-dev"
        build.mkdir(parents=True)
        sentinel = build / "keep.obj"
        sentinel.write_bytes(b"incremental object")
        with patch.dict(os.environ, PIKDRAW_NATIVE_BUILD_FAIL="1"):
            failed = self.run_entry()
        self.assertEqual(failed["state"], "failed")
        request, tools = self.coordinator.retry(failed["id"])
        retry = self.coordinator.enqueue(request, tools)
        self.coordinator.worker(retry["id"])
        self.assertEqual(self.coordinator.load(retry["id"])["state"], "succeeded")
        self.assertEqual(sentinel.read_bytes(), b"incremental object")
        self.assertFalse(any(c[-1] == "clean" for c in self.commands()))

    def test_retry_uses_own_failed_tests_even_after_another_test_run(self):
        with patch.dict(os.environ, PIKDRAW_NATIVE_FAIL_TEST="registered_beta"):
            failed = self.run_entry(test=True, all_tests=True)
        self.assertEqual(failed["failed_tests"], ["registered_beta"])
        self.run_entry(test=True, test_targets=["test_alpha"])
        request, tools = self.coordinator.retry(failed["id"])
        retry = self.coordinator.enqueue(request, tools)
        self.coordinator.worker(retry["id"])
        result = self.coordinator.load(retry["id"])
        self.assertEqual(result["state"], "succeeded", result)
        self.assertEqual(result["tests"], ["registered_beta"])
        self.assertTrue(any("--build" in c for c in self.commands()))

    def test_identical_overlapping_requests_share_one_build_and_test(self):
        # Both requests exist before the first completes; a later historical request reruns.
        first = self.enqueue(test=True, test_targets=["test_alpha"])
        second = self.enqueue(test=True, test_targets=["test_alpha"])
        self.coordinator.worker(first["id"])
        self.coordinator.worker(second["id"])
        result = self.coordinator.load(second["id"])
        self.assertEqual(result["state"], "succeeded", result)
        self.assertEqual(result.get("shared_from"), first["id"])
        self.assertEqual(sum("--build" in c for c in self.commands()), 1)
        self.assertEqual(sum(c[0] == "ctest" and "-R" in c for c in self.commands()), 1)
        self.run_entry(test=True, test_targets=["test_alpha"])
        self.assertEqual(sum("--build" in c for c in self.commands()), 2)

    def test_environment_difference_and_modified_binary_prevent_sharing(self):
        first = self.enqueue(test=True, test_targets=["test_alpha"])
        with patch.dict(os.environ, PIKDRAW_FIXTURE_MODE="different"):
            second = self.enqueue(test=True, test_targets=["test_alpha"])
        third = self.enqueue(test=True, test_targets=["test_alpha"])
        self.coordinator.worker(first["id"])
        self.coordinator.worker(second["id"])
        binary = self.repo / "build/win-dev/tests/test_alpha.exe"
        binary.write_text("externally changed output")
        self.coordinator.worker(third["id"])
        self.assertNotIn("shared_from", self.coordinator.load(second["id"]))
        self.assertNotIn("shared_from", self.coordinator.load(third["id"]))
        self.assertEqual(sum("--build" in c for c in self.commands()), 3)

    def test_two_real_worker_processes_serialize_through_fifo_lock(self):
        with patch.dict(os.environ, PIKDRAW_NATIVE_DELAY="0.5"):
            first = self.enqueue(targets=["first"])
            second = self.enqueue(targets=["second"])
            self.coordinator.start(first)
            self.coordinator.start(second)
        a, b = self.finish(first), self.finish(second)
        self.assertEqual(a["state"], "succeeded", a)
        self.assertEqual(b["state"], "succeeded", b)
        self.assertGreaterEqual(b["started"], a["finished"])
        self.assertEqual([c[-1] for c in self.commands() if "--build" in c], ["first", "second"])

    def test_lock_preserves_live_owner_even_when_file_is_deletable(self):
        lock = self.repo / ".agent/native-build.lock"
        lock.parent.mkdir(parents=True)
        lock.write_text(f"pid={os.getpid()} source={self.repo}")
        contender = BuildLock(self.repo, "0" * 32)
        self.assertFalse(contender.acquire())
        self.assertTrue(lock.is_file())

    def test_dead_parent_does_not_allow_stealing_from_orphaned_compiler(self):
        lock = self.repo / ".agent/native-build.lock"
        lock.parent.mkdir(parents=True)
        lock.write_text("pid=999999999 source=fixture")
        contender = BuildLock(self.repo, "0" * 32)
        with patch("build_state.active_build_processes", return_value=["ninja.exe:123"]):
            self.assertFalse(contender.acquire())
        self.assertTrue(lock.exists())
        with patch("build_state.active_build_processes", return_value=[]):
            self.assertFalse(contender.acquire())
            self.assertFalse(lock.exists())
            self.assertTrue(contender.acquire())
            contender.release()

    def test_status_is_read_only_and_reports_interrupted_worker(self):
        entry = self.enqueue()
        entry.update(worker_pid=999999999, worker_token="old")
        self.coordinator.save(entry)
        path = self.coordinator.path(entry["id"])
        before = path.read_bytes()
        self.assertEqual(self.coordinator.status(entry["id"])["requests"][0]["state"], "interrupted")
        self.assertEqual(path.read_bytes(), before)
        request, _ = self.coordinator.retry(entry["id"])
        self.assertTrue(request["no_reuse"])
        entry["state"] = "interrupted"
        self.coordinator.save(entry)

    def test_queued_cancellation_preserves_another_tasks_lock(self):
        entry = self.enqueue()
        lock = self.repo / ".agent/native-build.lock"
        lock.write_text("another-session")
        self.coordinator.cancel(entry["id"])
        self.coordinator.worker(entry["id"])
        self.assertEqual(self.coordinator.load(entry["id"])["state"], "cancelled")
        self.assertEqual(lock.read_text(), "another-session")
        self.assertFalse(self.commands())

    def test_dirty_ninja_graph_and_external_corpus_prevent_coalescing(self):
        first = self.enqueue(test=True, test_targets=["test_alpha"])
        second = self.enqueue(test=True, test_targets=["test_alpha"])
        self.coordinator.worker(first["id"])
        with patch.dict(os.environ, PIKDRAW_NATIVE_NINJA_DIRTY="1"):
            self.coordinator.worker(second["id"])
        self.assertNotIn("shared_from", self.coordinator.load(second["id"]))
        with patch.dict(os.environ, PIKDRAW_CORPUS_DIR=str(self.repo)):
            third = self.run_entry(test=True, test_targets=["test_alpha"])
        self.assertFalse(third["shareable"])

    def test_source_digest_tracks_local_new_and_deleted_files(self):
        first = source_digest(self.repo)
        source = self.repo / "apps/desktop/tests/new.cpp"
        source.write_text("// new")
        self.assertNotEqual(first, source_digest(self.repo))
        source.unlink()
        self.assertEqual(first, source_digest(self.repo))
        with self.assertRaises(ValueError):
            self.coordinator.path("../../outside")
        with self.assertRaises(ValueError):
            safe_path(self.repo, self.repo.parent / "outside")


if __name__ == "__main__":
    unittest.main()
