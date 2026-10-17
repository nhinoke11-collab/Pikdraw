"""Module/context routing and read-only health reporting for the primary source."""
from __future__ import annotations
import json
from harness_support import (SCHEMA_VERSION, STATUSES, HarnessError, now, checked_slug,
    checked_owner, checked_text, relative_path, contained, read_json, atomic_json,
    git, coordinator_root, commit_oid, paths_overlap, path_owned)


class ContextRoutes:
    def modules(self) -> dict[str, dict]:
        registry = read_json(contained(self.root, "registry/modules.json", must_exist=True))
        if not isinstance(registry, dict) or registry.get("schema_version") != SCHEMA_VERSION:
            raise HarnessError("registry/modules.json requires schema_version: 1.")
        raw_modules = registry.get("modules")
        if not isinstance(raw_modules, list) or not raw_modules:
            raise HarnessError("Module registry must contain a nonempty modules array.")
        modules = {}
        for module in raw_modules:
            if not isinstance(module, dict):
                raise HarnessError("Each module must be an object.")
            identifier = checked_slug(module.get("id"), "module id")
            if identifier in modules:
                raise HarnessError(f"Duplicate module id: {identifier}")
            contained(self.root, module.get("path"), must_exist=True)
            checked_text(module.get("status"), f"{identifier} status", 80)
            for field in ("dependencies", "context", "owned_paths"):
                if not isinstance(module.get(field), list) or not all(isinstance(v, str) for v in module[field]):
                    raise HarnessError(f"{identifier}.{field} must be an array of strings.")
                if len(module[field]) != len(set(module[field])):
                    raise HarnessError(f"Duplicate {identifier}.{field} value.")
            if not module["owned_paths"]:
                raise HarnessError(f"{identifier} requires owned_paths.")
            for path in module["context"]:
                contained(self.root, path, must_exist=True)
            for path in module["owned_paths"]:
                contained(self.root, path, allow_runtime=False)
            modules[identifier] = module
        for identifier, module in modules.items():
            unknown = set(module["dependencies"]) - modules.keys()
            if unknown:
                raise HarnessError(f"{identifier}: unknown dependencies {sorted(unknown)}")
        visited, visiting = set(), []

        def visit(identifier):
            if identifier in visiting:
                raise HarnessError("Module dependency cycle: " + " -> ".join(visiting + [identifier]))
            if identifier in visited:
                return
            visiting.append(identifier)
            for dependency in modules[identifier]["dependencies"]:
                visit(dependency)
            visiting.pop()
            visited.add(identifier)

        for identifier in modules:
            visit(identifier)
        return modules

    def boot(self, modules: dict[str, dict]) -> dict:
        path = contained(self.root, "registry/boot.json")
        if not path.exists():
            return {"schema_version": 1, "routes": []}
        boot = read_json(path)
        if not isinstance(boot, dict) or boot.get("schema_version") != 1 or not isinstance(boot.get("routes"), list):
            raise HarnessError("registry/boot.json requires schema_version: 1 and routes array.")
        seen = set()
        for route in boot["routes"]:
            if not isinstance(route, dict):
                raise HarnessError("Boot route must be an object.")
            identifier = checked_slug(route.get("id"), "route id")
            if identifier in seen:
                raise HarnessError(f"Duplicate boot route: {identifier}")
            seen.add(identifier)
            if not isinstance(route.get("paths"), list) or not route["paths"]:
                raise HarnessError(f"Boot route {identifier} needs paths.")
            for reference in route["paths"]:
                contained(self.root, reference, must_exist=True)
            if route.get("module") is not None and route["module"] not in modules:
                raise HarnessError(f"Boot route {identifier}: unknown module.")
        return boot

    def doctor(self):
        issues = []
        modules = {}
        try:
            modules = self.modules()
            self.boot(modules)
            for reference in ("AGENTS.md", "docs/START.md", "docs/project-state.json"):
                contained(self.root, reference, must_exist=True)
            state = read_json(contained(self.root, "docs/project-state.json", must_exist=True))
            if not isinstance(state, dict) or state.get("schema_version") != 1:
                raise HarnessError("docs/project-state.json requires schema_version: 1.")
            tasks = self.all_tasks()
            for task in tasks:
                self.validate_task(task, modules)
                if task.get("lease"):
                    self.assert_no_overlap(task, tasks)
                if task.get("handoff"):
                    packet = read_json(contained(self.root, task["handoff"], must_exist=True))
                    if packet.get("schema_version") != 1 or packet.get("task_id") != task["id"]:
                        raise HarnessError(f"Invalid handoff reference for {task['id']}.")
        except HarnessError as exc:
            issues.append(str(exc))
        return {"ok": not issues, "product": "PikDraw", "schema_version": 1,
                "modules": len(modules), "issues": issues,
                "workflow": "single-source", "agent_system": "codex",
                "source_root": str(self.root),
                "app_executable": str(self.root / "build/win-dev/apps/desktop/pikdraw.exe")}

    def context(self, task_id, max_chars=12000):
        if max_chars < 1000 or max_chars > 50000:
            raise HarnessError("--max-chars must be between 1000 and 50000.")
        modules = self.modules()
        task = self.task(task_id)
        self.validate_task(task, modules)
        module = modules[task["module"]]
        boot = self.boot(modules)
        references = ["AGENTS.md", "docs/START.md", "docs/project-state.json", "registry/modules.json"]
        references += module["context"]
        route_ids = {module["id"], "session", "session-start", "general", "module-change"}
        if module["id"] == "plugin-sdk":
            route_ids.add("plugin")
        if task["status"] == "review":
            route_ids.add("review")
        for route in boot["routes"]:
            if route.get("module") == module["id"] or route["id"] in route_ids:
                references += route["paths"]
        references = list(dict.fromkeys(references))
        sections = [f"PikDraw task context v1 | task={task_id} | module={module['id']}",
                    "POLICY: one primary source; no new worktrees; Codex only; claim owned paths before editing.",
                    "BUILD: build/win-dev/apps/desktop/pikdraw.exe; use tooling/native/build.ps1 (shared lock).",
                    "Shared coordinator: " + str(self.root),
                    "Source checkout: " + str(self.checkout),
                    "Read only the references relevant to this task; expand context on demand.",
                    "TASK\n" + json.dumps(task, ensure_ascii=False, separators=(",", ":")),
                    "MODULE\n" + json.dumps(module, ensure_ascii=False, separators=(",", ":")),
                    "DEPENDENCY POINTERS\n" + json.dumps({dep: modules[dep]["context"] for dep in module["dependencies"]}, ensure_ascii=False),
                    "REFERENCES\n" + "\n".join(references)]
        # Only three boot documents are opened, each with a strict byte/read bound.
        for reference, limit in (("AGENTS.md", 2600), ("docs/START.md", 1500), ("docs/project-state.json", 1600)):
            path = contained(self.root, reference, must_exist=True)
            try:
                with path.open(encoding="utf-8-sig") as stream:
                    snippet = stream.read(limit + 1)
            except OSError as exc:
                raise HarnessError(f"Cannot read context document {reference}: {exc}") from exc
            if len(snippet) > limit:
                snippet = snippet[:limit] + "\n[excerpt; open reference only if needed]"
            sections.append(reference + "\n" + snippet)
        content = "\n\n".join(sections)
        # Reserve print's newline; no unbounded logs or automatic directory scan.
        if len(content) + 1 > max_chars:
            suffix = "\n[context capped; use task show and listed references as needed]"
            content = content[:max_chars - len(suffix) - 1] + suffix
        return content
