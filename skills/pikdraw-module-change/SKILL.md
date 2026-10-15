---
name: pikdraw-module-change
description: Implement a scoped PikDraw module change while preserving document invariants, dependency direction and shared-contract compatibility.
---

# PikDraw module change

Use the assigned task packet and module AGENTS.md. `registry/modules.json` defines the allowed dependency direction and default owned paths. Keep feature code in its module; UI routes commands and shows state.

Before editing source, read [code organization](../../docs/development/code-organization.md) and record a bounded file map. Group model/commands/history/storage/UI responsibilities by feature; follow file-size limits without minifying or splitting into artificial parts. New feature code cannot change the shared size checker/policy/exceptions to make itself pass. Handoff includes actual feature/API/test paths, size metrics and reviewed exceptions. A small unrelated legacy fix does not authorize a repository-wide refactor.

For M1, select only the assigned parent and checkpoints from [M1 plan](../../docs/roadmap/m1-implementation.md) and [checkpoint catalog](../../docs/roadmap/m1-task-packages.json). Checkpoints run serially under the parent lease unless the coordinator explicitly creates disjoint replacement tasks.

Document writes use commands/transactions; rendering consumes snapshots. Preserve stable IDs, source data, units, color metadata and non-destructive parameters when the changed feature touches them.

For shared contract changes, agree on a scoped contract task and consumer review. Record an ADR for a new backend, dependency direction, persistence format or breaking API. Do not silently widen task ownership.

Run relevant checks and update task evidence. A core/library prototype is not a finished editor feature. Read [architecture](../../docs/architecture/architecture.md) for boundary changes and [quality gates](../../docs/quality/gates.md) for release-affecting behavior.
