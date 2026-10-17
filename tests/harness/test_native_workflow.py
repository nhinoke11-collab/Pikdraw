"""Exercise the shared runner without compiling or touching real app output."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

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
class NativeWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pikdraw-native-policy-")
        self.repo = Path(self.temp.name)
        self.native = self.repo / "tooling/native"
        self.native.mkdir(parents=True)
        test_sources = self.repo / "apps/desktop/tests"
        test_sources.mkdir(parents=True)
        (test_sources / "policy_probe.cpp").write_text("int main() { return 0; }\n",
                                                       encoding="utf-8")
        (self.repo / ".git").mkdir()
        shutil.copy2(ROOT / "tooling/native/build.ps1", self.native / "build.ps1")
        self.log = self.repo / "commands.jsonl"
        (self.native / "env.ps1").write_text(
            "function Get-PikDrawToolchain { @{ CMake = (Join-Path $PSScriptRoot 'fake-cmake.ps1'); Clang = $null } }\n"
            "function Assert-PikDrawPrimarySource([string]$SourceRoot) {\n"
            " if (-not (Get-Item -LiteralPath (Join-Path $SourceRoot '.git')).PSIsContainer) { throw 'primary source' }\n}\n",
            encoding="utf-8")
        (self.native / "fake-cmake.ps1").write_text(
            "$entry = ConvertTo-Json -InputObject @($args) -Compress\n"
            "Add-Content -LiteralPath $env:PIKDRAW_NATIVE_TEST_LOG -Value $entry\n"
            "$global:LASTEXITCODE = if ($env:PIKDRAW_NATIVE_TEST_FAIL) { 23 } else { 0 }\n",
            encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def run_build(self, *args, fail=False):
        env = dict(os.environ, PIKDRAW_NATIVE_TEST_LOG=str(self.log))
        if fail:
            env["PIKDRAW_NATIVE_TEST_FAIL"] = "yes"
        return subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(self.native / "build.ps1"), *args], cwd=self.repo, env=env,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)

    def test_two_configurations_share_output_and_release_lock(self):
        for configuration in ("Debug", "Release"):
            result = self.run_build("-Configuration", configuration)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((self.repo / ".agent/native-build.lock").exists())
        commands = [json.loads(line.lstrip("\ufeff")) for line in self.log.read_text(encoding="utf-8-sig").splitlines()]
        builds = [entry for entry in commands if entry[0] == "--build"]
        self.assertEqual(len(builds), 2)
        self.assertEqual({Path(entry[1]).resolve() for entry in builds}, {(self.repo / "build/win-dev").resolve()})
        self.assertTrue(all(entry[-1] == "pikdraw" for entry in builds), builds)

    def test_targeted_test_build_keeps_tests_configured_and_skips_ctest(self):
        result = self.run_build(
            "-Test", "-TestTarget", "benchmark_pikdraw_rendering_effects", "-BuildOnly")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.repo / ".agent/native-build.lock").exists())
        commands = [json.loads(line.lstrip("\ufeff"))
                    for line in self.log.read_text(encoding="utf-8-sig").splitlines()]
        configure = next(entry for entry in commands if entry[0] == "--preset")
        self.assertIn("-DPIKDRAW_BUILD_TESTS=ON", configure)
        builds = [entry for entry in commands if entry[0] == "--build"]
        self.assertEqual(len(builds), 1)
        self.assertEqual(builds[0][-1], "benchmark_pikdraw_rendering_effects")
        self.assertFalse(any("ctest.exe" in entry[0].lower() for entry in commands))

    def test_active_build_lock_is_preserved(self):
        lock = self.repo / ".agent/native-build.lock"
        lock.parent.mkdir()
        lock.write_text("another-session", encoding="utf-8")
        result = self.run_build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Another session", result.stderr)
        self.assertEqual(lock.read_text(), "another-session")
        self.assertFalse(self.log.exists())

    def test_failed_configure_releases_lock_and_never_builds_fallback(self):
        result = self.run_build(fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.repo / ".agent/native-build.lock").exists())
        commands = self.log.read_text(encoding="utf-8-sig").splitlines()
        self.assertEqual(len(commands), 1)
        self.assertEqual(list(self.repo.rglob("*.exe")), [])


if __name__ == "__main__":
    unittest.main()
