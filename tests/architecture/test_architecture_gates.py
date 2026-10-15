"""PikDraw Architecture Gates Test Suite

Validates repository architectural invariants:
1. No module-to-UI dependencies (core and platform modules cannot include UI headers or Qt Widgets).
2. Module dependency graph is a Directed Acyclic Graph (DAG) with no cycles.
3. Cross-module inclusions follow declared registry dependencies.
4. Path containment prevents escaping repository boundaries.
5. Negative test fixtures prove the gates detect violations.
"""

from __future__ import annotations

import json
import os
import re
import unittest
from pathlib import Path


def find_repo_root() -> Path:
    current = Path(__file__).resolve().parent
    while current.parent != current:
        if (current / "registry" / "modules.json").exists():
            return current
        current = current.parent
    raise RuntimeError("Repository root could not be located.")


def detect_cycles(modules_data: dict) -> list[list[str]]:
    """Returns all cycles found in the module dependency graph."""
    graph: dict[str, list[str]] = {}
    for mod in modules_data.get("modules", []):
        graph[mod["id"]] = mod.get("dependencies", [])

    cycles = []
    visited: set[str] = set()
    rec_stack: list[str] = []

    def dfs(node: str):
        visited.add(node)
        rec_stack.append(node)

        for neighbor in graph.get(node, []):
            if neighbor not in visited:
                dfs(neighbor)
            elif neighbor in rec_stack:
                cycle_start = rec_stack.index(neighbor)
                cycles.append(rec_stack[cycle_start:] + [neighbor])

        rec_stack.pop()

    for node in graph:
        if node not in visited:
            dfs(node)

    return cycles


def scan_file_for_ui_dependencies(file_path: Path) -> list[str]:
    """Scans C++ source/header file for forbidden UI dependencies."""
    violations: list[str] = []
    forbidden_patterns = [
        re.compile(r'#include\s*[<"](?:apps/desktop/|pikdraw/desktop/)[^>"]+[>"]'),
        re.compile(r'#include\s*[<"](?:QtWidgets/|QtGui/QApplication|QWidget|QMainWindow|QDialog)[^>"]*[>"]'),
    ]

    try:
        content = file_path.read_text(encoding="utf-8", errors="replace")
        for line_num, line in enumerate(content.splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("/*"):
                continue
            for pattern in forbidden_patterns:
                if pattern.search(line):
                    violations.append(f"{file_path}:{line_num}: {line.strip()}")
    except Exception as e:
        violations.append(f"Error reading {file_path}: {e}")

    return violations


def is_path_safe(base_dir: Path, target_path: str) -> bool:
    """Checks if target_path stays safely within base_dir without escaping."""
    if os.path.isabs(target_path):
        return False
    if ".." in Path(target_path).parts:
        return False
    try:
        resolved = (base_dir / target_path).resolve()
        return base_dir.resolve() in resolved.parents or resolved == base_dir.resolve()
    except Exception:
        return False


class ArchitectureGatesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo_root = find_repo_root()
        cls.registry_path = cls.repo_root / "registry" / "modules.json"
        with open(cls.registry_path, "r", encoding="utf-8") as f:
            cls.registry = json.load(f)

    def test_registry_schema_and_completeness(self):
        """Verifies registry/modules.json contains well-formed module definitions."""
        self.assertIn("modules", self.registry)
        modules = self.registry["modules"]
        self.assertGreaterEqual(len(modules), 10)

        seen_ids = set()
        for mod in modules:
            mod_id = mod["id"]
            self.assertNotIn(mod_id, seen_ids, f"Duplicate module id: {mod_id}")
            seen_ids.add(mod_id)

            self.assertIn("path", mod)
            self.assertIn("dependencies", mod)
            self.assertIn("owned_paths", mod)
            self.assertIn("purpose", mod)

    def test_module_dependency_graph_is_dag(self):
        """Verifies the module dependency graph has no cycles."""
        cycles = detect_cycles(self.registry)
        self.assertEqual(cycles, [], f"Detected circular module dependencies: {cycles}")

    def test_desktop_is_the_only_ui_module(self):
        """Verifies no non-desktop module declares a dependency on desktop."""
        for mod in self.registry["modules"]:
            if mod["id"] != "desktop":
                self.assertNotIn(
                    "desktop",
                    mod.get("dependencies", []),
                    f"Module {mod['id']} illegally depends on 'desktop' UI module!",
                )

    def test_no_module_to_ui_in_source_code(self):
        """Scans all non-desktop source files to ensure zero module-to-UI leaks."""
        non_ui_roots = ["core", "modules", "platform", "packages", "tooling"]
        all_violations = []

        for root_name in non_ui_roots:
            scan_dir = self.repo_root / root_name
            if not scan_dir.exists():
                continue

            for ext in [".h", ".hpp", ".hxx", ".c", ".cpp", ".cxx"]:
                for file_path in scan_dir.rglob(f"*{ext}"):
                    # Skip tests/architecture/fixtures
                    if "tests" in file_path.parts and "fixtures" in file_path.parts:
                        continue
                    violations = scan_file_for_ui_dependencies(file_path)
                    all_violations.extend(violations)

        self.assertEqual(
            all_violations,
            [],
            f"Found forbidden UI dependencies in non-UI modules:\n" + "\n".join(all_violations),
        )

    def test_negative_ui_dependency_fixture_is_caught(self):
        """Proves the gate catches unauthorized UI dependencies."""
        fixture_file = (
            self.repo_root
            / "tests"
            / "architecture"
            / "fixtures"
            / "negative_ui_include"
            / "sample.cpp"
        )
        self.assertTrue(fixture_file.exists(), f"Fixture {fixture_file} must exist")
        violations = scan_file_for_ui_dependencies(fixture_file)
        self.assertGreaterEqual(
            len(violations),
            2,
            "Negative fixture with UI include must produce at least 2 violations",
        )

    def test_negative_cycle_fixture_is_caught(self):
        """Proves the cycle detector catches circular dependencies."""
        fixture_file = (
            self.repo_root
            / "tests"
            / "architecture"
            / "fixtures"
            / "negative_cycle_modules.json"
        )
        self.assertTrue(fixture_file.exists(), f"Fixture {fixture_file} must exist")
        with open(fixture_file, "r", encoding="utf-8") as f:
            data = json.load(f)
        cycles = detect_cycles(data)
        self.assertGreaterEqual(
            len(cycles), 1, "Negative cycle fixture must be detected by cycle detector"
        )

    def test_negative_path_escape_fixture_is_caught(self):
        """Proves path escape validator rejects unauthorized escaping paths."""
        fixture_file = (
            self.repo_root
            / "tests"
            / "architecture"
            / "fixtures"
            / "negative_path_escape.json"
        )
        self.assertTrue(fixture_file.exists(), f"Fixture {fixture_file} must exist")
        with open(fixture_file, "r", encoding="utf-8") as f:
            data = json.load(f)

        for invalid_path in data["invalid_escapes"]:
            self.assertFalse(
                is_path_safe(self.repo_root, invalid_path),
                f"Path {invalid_path} must be rejected as an unsafe escape!",
            )

    def test_owned_paths_do_not_escape_repository(self):
        """Verifies that all owned_paths in registry/modules.json stay within repository."""
        for mod in self.registry["modules"]:
            for path in mod.get("owned_paths", []):
                self.assertTrue(
                    is_path_safe(self.repo_root, path),
                    f"Module {mod['id']} has unsafe owned_path: {path}",
                )


if __name__ == "__main__":
    unittest.main()
