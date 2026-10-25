"""Isolated fake CMake/CTest fixture; never compiles or touches canonical output."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tooling/native"))
from build_broker import Coordinator
from build_state import alive

FAKE_TOOL = r'''
import json, os, pathlib, re, sys, time
import xml.etree.ElementTree as ET
kind, args = sys.argv[1], sys.argv[2:]
root = pathlib.Path.cwd()
build = root / 'build/win-dev'
build.mkdir(parents=True, exist_ok=True)
with (root / 'build/commands.jsonl').open('a', encoding='utf-8') as stream:
    stream.write(json.dumps([kind, *args]) + '\n')
if kind == 'cmake':
    if '--preset' in args:
        if os.environ.get('PIKDRAW_NATIVE_TEST_FAIL'):
            print('fixture configure failure', flush=True)
            sys.exit(23)
        config = next(a.split('=', 1)[1] for a in args if a.startswith('-DCMAKE_BUILD_TYPE='))
        (build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=' + config + '\nPIKDRAW_BUILD_TESTS:BOOL=ON\n')
        (build / 'build.ninja').write_text('# fake build graph\n')
    elif '--build' in args:
        time.sleep(float(os.environ.get('PIKDRAW_NATIVE_DELAY', '0')))
        if os.environ.get('PIKDRAW_NATIVE_BUILD_FAIL'):
            sys.exit(24)
        if os.environ.get('PIKDRAW_NATIVE_MUTATE'):
            with (root / 'apps/desktop/tests/policy_probe.cpp').open('a') as stream:
                stream.write('// changed during build\n')
        for target in args[args.index('--target') + 1:]:
            targets = ['pikdraw', 'test_alpha', 'test_beta'] if target == 'all' else [target]
            for name in targets:
                output = build / ('apps/desktop' if name == 'pikdraw' else 'tests') / (name + '.exe')
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text('stable fake binary ' + name)
    print('fake cmake completed', flush=True)
elif kind == 'ninja':
    print('pending compile' if os.environ.get('PIKDRAW_NATIVE_NINJA_DIRTY') else 'ninja: no work to do.')
else:
    tests = [
        {'name': 'test_alpha', 'command': [str(build / 'tests/test_alpha.exe')]},
        {'name': 'alias_alpha', 'command': [str(build / 'tests/test_alpha.exe'), '--focused']},
        {'name': 'registered_beta', 'command': [str(build / 'tests/test_beta.exe')]},
    ]
    if '--show-only=json-v1' in args:
        print(json.dumps({'tests': tests}))
    else:
        selected = [t for t in tests if re.search(args[args.index('-R') + 1], t['name'])]
        suite = ET.Element('testsuite')
        failed = False
        for test in selected:
            case = ET.SubElement(suite, 'testcase', name=test['name'])
            if os.environ.get('PIKDRAW_NATIVE_FAIL_TEST') == test['name']:
                ET.SubElement(case, 'failure', message='fixture assertion failure')
                failed = True
        ET.ElementTree(suite).write(args[args.index('--output-junit') + 1], encoding='utf-8')
        print('fake CTest: ' + ','.join(t['name'] for t in selected), flush=True)
        sys.exit(8 if failed else 0)
'''


class NativeBuildFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pikdraw-native-policy-")
        self.repo = Path(self.temp.name)
        self.native = self.repo / "tooling/native"
        self.native.mkdir(parents=True)
        source = self.repo / "apps/desktop/tests/policy_probe.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("int main() { return 0; }\n", encoding="utf-8")
        (self.repo / ".gitignore").write_text("build/\n.agent/\n", encoding="utf-8")
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True, capture_output=True)
        for name in ("build.ps1", "build_broker.py", "build_state.py"):
            shutil.copy2(ROOT / "tooling/native" / name, self.native / name)
        shutil.copy2(ROOT / "tooling/native/forward.ps1", self.native / "forward.ps1")
        shutil.copy2(ROOT / "tooling/pikdraw.ps1", self.repo / "tooling/pikdraw.ps1")
        fake = self.native / "fake_tool.py"
        fake.write_text(FAKE_TOOL, encoding="utf-8")
        python = sys.executable.replace("'", "''")
        (self.native / "env.ps1").write_text(
            "function Get-PikDrawToolchain { param([switch]$PythonOnly)\n"
            f" $py = '{python}'\n"
            " @{ Python = $py; CMake = @($py, (Join-Path $PSScriptRoot 'fake_tool.py'), 'cmake');\n"
            " CTest = @($py, (Join-Path $PSScriptRoot 'fake_tool.py'), 'ctest'); Clang = $null }\n}\n"
            "function Assert-PikDrawPrimarySource([string]$SourceRoot) {\n"
            " if (-not (Get-Item -Force -LiteralPath (Join-Path $SourceRoot '.git')).PSIsContainer) { throw 'primary source' }\n}\n",
            encoding="utf-8")
        self.log = self.repo / "build/commands.jsonl"
        self.coordinator = Coordinator(self.repo)
        self.tools = {"CMake": [sys.executable, str(fake), "cmake"],
                      "CTest": [sys.executable, str(fake), "ctest"],
                      "Ninja": [sys.executable, str(fake), "ninja"], "Clang": None}

    def tearDown(self):
        # Release only our synthetic unknown lock, allowing detached fixture workers to finish.
        lock = self.repo / ".agent/native-build.lock"
        if lock.is_file() and lock.read_text(encoding="utf-8") == "another-session":
            lock.unlink()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            pending = [e for e in self.coordinator.entries() if
                       e.get("worker_pid") != os.getpid() and
                       (e["state"] not in {"succeeded", "failed", "stale", "interrupted", "cancelled"} or
                        alive(e.get("worker_pid", 0), e.get("worker_token", "")))]
            if not pending:
                break
            time.sleep(.1)
        for child in self.coordinator.children:
            child.wait(timeout=10)
        for attempt in range(100):
            try:
                self.temp.cleanup()
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.05)

    def commands(self):
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()] if self.log.exists() else []

    def request(self, **changes):
        result = dict(configuration="Auto", targets=["pikdraw"], clean=False, test=False,
                      tests_only=False, test_targets=[], build_only=False, test_regex="", all_tests=False,
                      verbose_tests=False, reconfigure=False, jobs=2, test_jobs=1, no_reuse=False)
        result.update(changes)
        return result

    def finish(self, entry):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            result = self.coordinator.load(entry["id"])
            if result["state"] in {"succeeded", "failed", "stale", "interrupted", "cancelled"}:
                return result
            time.sleep(.05)
        raise AssertionError(f"Fixture worker did not finish: {entry['id']}")
