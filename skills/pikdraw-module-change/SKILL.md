---
name: pikdraw-module-change
description: Implement a scoped PikDraw module change while preserving document invariants, dependency direction and shared-contract compatibility.
---

# PikDraw module change

Use the assigned task packet and module AGENTS.md. `registry/modules.json` defines the allowed dependency direction and default owned paths. Keep feature code in its module; UI routes commands and shows state.

Document writes use commands/transactions; rendering consumes snapshots. Preserve stable IDs, source data, units, color metadata and non-destructive parameters when the changed feature touches them.

For shared contract changes, agree on a scoped contract task and consumer review. Record an ADR for a new backend, dependency direction, persistence format or breaking API. Do not silently widen task ownership.

Run relevant checks and update task evidence. A core/library prototype is not a finished editor feature. Read [architecture](../../docs/architecture/architecture.md) for boundary changes and [quality gates](../../docs/quality/gates.md) for release-affecting behavior.
