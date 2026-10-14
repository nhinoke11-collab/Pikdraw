---
name: pikdraw-review
description: Review and integrate a PikDraw task handoff for scope, contract compatibility, demonstrated checks and accurate project state.
---

# PikDraw review

Start from task/handoff, base and head revisions. Inspect the actual diff and relevant module instructions before reading implementation commentary. Check changed paths against owned paths and distinguish planned behavior from implemented code.

Re-run checks appropriate to the risk and verify their evidence. For graphics, require separate visual, editable-structure and dimension/color evidence. For harness changes, exercise filesystem/Git coordination and failure paths using isolated temporary repositories.

Return concrete findings with paths and consequences, or state the checks and unresolved limits. A passed harness test is not evidence of graphics-engine performance or plugin isolation.

Integrator applies reviewed commits, then updates docs/project-state.json and records architectural decisions. Follow [agent protocol](../../docs/agents/PROTOCOL.md); never turn a reviewer report into automatic external publishing permission.
