"""Exercise the shared runner without compiling or touching real app output."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from native_build_fixture import NativeBuildFixture

ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake") or str(ROOT / ".agent/toolchains/python-tools/cmake/data/bin/cmake.exe")


@unittest.skipUnless(Path(CMAKE).is_file(), "CMake is required for configure rejection tests")
class CMakePolicyTests(unittest.TestCase):
    def test_alternate_output_and_linked_source_are_rejected_before_compilation(self):
        with tempfile.TemporaryDirectory(prefix="pikdraw-cmake-policy-") as directory:
            source = Path(directory) / "source"
            source.mkdir()
            shutil.copy2(ROOT / "CMakeLists.txt", source / "CMakeLists.txt")
            (source / ".git").mkdir()
            result = subprocess.run([CMAKE, "-S", str(source), "-B", str(Path(directory) / "other")],
                                    capture_output=True, text=True, timeout=90)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Single build policy", result.stderr)
            (source / ".git").rmdir()
            (source / ".git").write_text("gitdir: legacy\n", encoding="utf-8")
            result = subprocess.run([CMAKE, "-S", str(source), "-B", str(source / "build/win-dev")],
                                    capture_output=True, text=True, timeout=90)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("primary PikDraw source", result.stderr)
            self.assertEqual(list(source.rglob("*.exe")), [])


@unittest.skipUnless(os.name == "nt" and shutil.which("powershell"), "Windows runner tests")
class NativeWorkflowTests(NativeBuildFixture, unittest.TestCase):
    def run_build(self, *args, fail=False):
        env = dict(os.environ)
        if fail:
            env["PIKDRAW_NATIVE_TEST_FAIL"] = "yes"
        return subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(self.native / "build.ps1"), *args], cwd=self.repo, env=env,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)

    def test_two_configurations_share_output_and_release_lock(self):
        for configuration in ("Debug", "Release"):
            result = self.run_build("-Configuration", configuration)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((self.repo / ".agent/native-build.lock").exists())
        builds = [entry for entry in self.commands() if "--build" in entry]
        self.assertEqual(len(builds), 2)
        self.assertEqual({Path(entry[2]).resolve() for entry in builds}, {(self.repo / "build/win-dev").resolve()})
        self.assertTrue(all(entry[-1] == "pikdraw" for entry in builds), builds)

    def test_targeted_test_build_keeps_tests_configured_and_skips_ctest(self):
        result = self.run_build("-Test", "-TestTarget", "benchmark_pikdraw_rendering_effects", "-BuildOnly")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = self.commands()
        configure = next(entry for entry in commands if "--preset" in entry)
        self.assertIn("-DPIKDRAW_BUILD_TESTS=ON", configure)
        builds = [entry for entry in commands if "--build" in entry]
        self.assertEqual(len(builds), 1)
        self.assertEqual(builds[0][-1], "benchmark_pikdraw_rendering_effects")
        self.assertFalse(any(entry[0] == "ctest" for entry in commands))

    def test_active_build_lock_is_preserved(self):
        lock = self.repo / ".agent/native-build.lock"
        lock.parent.mkdir()
        lock.write_text("another-session", encoding="utf-8")
        result = self.run_build("-LockWaitSeconds", "0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("still queued", result.stderr)
        self.assertEqual(lock.read_text(), "another-session")
        self.assertFalse(self.log.exists())

    def test_failed_configure_releases_lock_and_never_builds_fallback(self):
        result = self.run_build(fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.repo / ".agent/native-build.lock").exists())
        self.assertEqual(len(self.commands()), 1)
        self.assertEqual(list(self.repo.rglob("*.exe")), [])

    def test_default_preserves_release_configuration(self):
        self.assertEqual(self.run_build("-Configuration", "Release").returncode, 0)
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(sum("--preset" in c for c in self.commands()), 1)

    def test_unsafe_bypass_and_implicit_full_suite_are_rejected(self):
        for args in (("-Force",), ("-NoLock",), ("-Test",), ("-TestsOnly",)):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_build(*args).returncode, 0)
        self.assertFalse(self.log.exists())

    def test_target_automatically_limits_registered_tests(self):
        result = self.run_build("-Test", "-TestTarget", "test_beta")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        tests = [c for c in self.commands() if c[0] == "ctest" and "-R" in c]
        self.assertEqual(len(tests), 1)
        self.assertIn("^(registered_beta)$", tests[0])

    def test_wrapper_forwards_named_array_and_job_options(self):
        result = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(self.repo / "tooling/pikdraw.ps1"), "test", "-TestTarget", "test_alpha,test_beta", "-Jobs", "1"],
            cwd=self.repo, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        builds = [c for c in self.commands() if "--build" in c]
        self.assertEqual(builds[0][-2:], ["test_alpha", "test_beta"])
        self.assertEqual(builds[0][builds[0].index("--parallel") + 1], "1")

    def test_submit_and_status_do_not_steal_legacy_lock(self):
        lock = self.repo / ".agent/native-build.lock"
        lock.parent.mkdir()
        lock.write_text("another-session")
        result = self.run_build("-Submit")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        entry = self.coordinator.entries()[0]
        status = self.run_build("-Status", "-RequestId", entry["id"])
        self.assertEqual(status.returncode, 0, status.stderr)
        self.assertEqual(json.loads(status.stdout)["requests"][0]["state"], "queued")
        self.assertEqual(lock.read_text(), "another-session")
        lock.unlink()
        self.assertEqual(self.finish(entry)["state"], "succeeded")

    def test_retry_restores_own_scope_and_accepts_job_override(self):
        with patch.dict(os.environ, PIKDRAW_NATIVE_FAIL_TEST="test_alpha"):
            failed = self.run_build("-Test", "-TestTarget", "test_alpha")
        self.assertNotEqual(failed.returncode, 0)
        entry = self.coordinator.entries()[-1]
        result = self.run_build("-RetryFailed", entry["id"], "-Jobs", "1")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        builds = [c for c in self.commands() if "--build" in c]
        self.assertEqual(builds[-1][builds[-1].index("--parallel") + 1], "1")
        self.assertEqual(self.coordinator.entries()[-1]["tests"], ["test_alpha"])


if __name__ == "__main__":
    unittest.main()
