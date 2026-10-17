---
name: pikdraw-review
description: Review and integrate a PikDraw task handoff for scope, contract compatibility, demonstrated checks and accurate project state.
---

# PikDraw review

Start from task/handoff, primary HEAD, source_baseline and source_snapshot. Inspect the actual snapshot diff and relevant module instructions before reading implementation commentary. Check changed paths against owned paths and distinguish planned behavior from implemented code. Review in the same primary source; no new checkout/worktree and no automatic commit.

Re-run checks appropriate to the risk and verify their evidence. For graphics, require separate visual, editable-structure and dimension/color evidence. For harness changes, exercise filesystem/Git coordination and failure paths using isolated temporary repositories.

For UI, review the feature's completed [UI template](../../docs/design/feature-ui-template.md) against the [UI/UX standard](../../docs/design/ui-ux-standard.md): menu classification, placement on property bar/Properties, shared commands and values, concise labels, SVG icons with accessible names, hover/focus help, status hints and actual usage instructions. Check empty/single/multi/mixed/locked states, numeric/IME focus, cancellation/Undo, keyboard, narrow windows and scaling. Reject ad hoc panels or long instructional buttons without a placement rationale. Historical mockups do not override the standard; a completed spec is not runtime UX evidence. This is a manual review gate, not a new harness checker.
The implementing agent authors and decides the spec autonomously within scope. Review its rationale and result; do not invent a requirement that the user fill the form or approve routine decisions before coding. Existing scope/contract and delivery review requirements still apply.

Return concrete findings with paths and consequences, or state the checks and unresolved limits. A passed harness test is not evidence of graphics-engine performance or plugin isolation.

The coordinator verifies the reviewed source snapshot still matches current files, then updates docs/project-state.json and records architectural decisions. Follow [agent protocol](../../docs/agents/PROTOCOL.md); never turn a reviewer report into automatic external publishing permission. Legacy worktree integration follows the migration checklist before removal.
