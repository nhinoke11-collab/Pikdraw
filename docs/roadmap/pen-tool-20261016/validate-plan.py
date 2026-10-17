"""Validate planning artifacts only; does not run app tests or restore sources."""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path


BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[2]
catalog = json.loads((BASE / "tasks.json").read_text(encoding="utf-8"))
tasks = catalog["tasks"]
ids = [task["id"] for task in tasks]
assert ids == [f"P{i:02d}" for i in range(17)], ids
assert len(ids) == len(set(ids))
assert not catalog["dispatch_authorized"]
assert not catalog["runtime_implemented_by_this_plan"]
ac_text = (BASE / "acceptance.md").read_text(encoding="utf-8")
acceptance_ids = set(re.findall(r"\| (AC\d{2}) \|", ac_text))
assert acceptance_ids == {f"AC{i:02d}" for i in range(1, 19)}
visited: set[str] = set()
for task in tasks:
    assert task["status"] == "planned"
    assert set(task["depends_on"]) <= visited, task["id"]
    assert set(task["acceptance_ids"]) <= acceptance_ids
    assert task["outputs"] and task["proposed_owned_paths"]
    for raw in task["proposed_owned_paths"]:
        candidate = (ROOT / raw).resolve()
        assert candidate.is_relative_to(ROOT) and ".." not in Path(raw).parts
        assert ".worktrees" not in raw
    assert f"| {task['id']} " in (BASE / "implementation.md").read_text(encoding="utf-8")
    visited.add(task["id"])
effort = [sum(t["effort_person_days"][i] for t in tasks) for i in (0, 1)]
assert effort == catalog["effort_total_person_days"] == [25.0, 43.5]

# validation.json is generated below, so count it as a planned link target on first run.
local_links = 0
for path in BASE.glob("*.md"):
    content = path.read_text(encoding="utf-8")
    assert content.count("```") % 2 == 0, path.name
    for target in re.findall(r"(?<!!)\[[^\]]+\]\(([^)]+)\)", content):
        if target.startswith(("https://", "http://", "#")):
            continue
        resolved = (path.parent / target.split("#", 1)[0]).resolve()
        assert resolved.is_relative_to(ROOT)
        assert resolved.exists() or resolved == BASE / "validation.json", (path.name, target)
        local_links += 1

inputs = [
    "docs/project-state.json",
    "docs/roadmap/drawing-tools-plan.md",
    "docs/roadmap/drawing-tools-tasks.json",
    "contracts/drawing/semantics.json",
    "apps/desktop/include/pikdraw/desktop/actions/action_id.hpp",
    "apps/desktop/include/pikdraw/desktop/shell/native_window.hpp",
    "apps/desktop/src/shell/native_window.cpp",
    "apps/desktop/src/shell/native_window_menus.hpp",
    "apps/desktop/src/shell/native_window_paint.cpp",
    "modules/vector/include/pikdraw/vector/tools/line/path_drawing_tool.hpp",
    "modules/vector/include/pikdraw/vector/precision/numeric/numeric_input.hpp",
    "modules/vector/src/precision/numeric/numeric_input.cpp",
    "core/document/include/pikdraw/document/model/editable_path.hpp",
    "core/document/include/pikdraw/document/commands/path_commands.hpp",
    "core/document/src/commands/command.cpp",
    "core/document/include/pikdraw/document/storage/native_format.hpp",
    "core/document/src/storage/native_format.cpp",
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
assert head == catalog["source_head"], "Refresh audit baseline if primary HEAD changed."
artifacts = sorted(p for p in BASE.iterdir() if p.is_file() and p.name != "validation.json")
result = {
    "status": "pass",
    "scope": "planning_artifact_validation_only",
    "checked_at_utc": datetime.now(timezone.utc).isoformat(),
    "primary_head": head,
    "source_identity_note": "Dirty primary checkout. Hashes reflect inputs at validation time; HEAD alone is not source identity.",
    "task_count": len(tasks),
    "acyclic_dependency_order": ids,
    "acceptance_count": len(acceptance_ids),
    "effort_person_days": effort,
    "local_links_checked": local_links,
    "proposed_paths_contained_in_primary_source": True,
    "task_statuses": "all_planned",
    "markdown_fences": "balanced",
    "doctor": "Run tooling/pikdraw.ps1 doctor separately; result recorded in task handoff.",
    "native_build": "not_run_documentation_only",
    "native_tests": "not_run_test_sources_deleted_prerequisite_P00_P02",
    "harness_tests": "not_run_test_sources_deleted",
    "native_ui": "not_exercised_source_audit_only",
    "runtime_edits": False,
    "artifacts_sha256": {str(p.relative_to(ROOT)).replace("\\", "/"): digest(p) for p in artifacts},
    "audit_inputs_sha256": {raw: digest(ROOT / raw) for raw in inputs},
}
(BASE / "validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({key: result[key] for key in ("status", "task_count", "acceptance_count", "local_links_checked", "effort_person_days")}, ensure_ascii=False))
