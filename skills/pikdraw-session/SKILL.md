---
name: pikdraw-session
description: Bootstrap or resume a Codex task in the single primary PikDraw source with bounded context and owned paths.
---

# PikDraw session

Read root AGENTS.md, docs/START.md and docs/project-state.json. Run the current harness from the primary source, using its shared task board. Legacy linked worktrees are migration inputs only.

Find the assigned task with `task list` or `task show ID`. Claim with a unique provider/session owner before editing. If there is no authorized task, obtain scope; do not infer a new product objective.

Generate `context --task-id ID`. Open only its module context and relevant contracts. The packet limit is a character bound, not a token guarantee. Use smaller packets when a provider has a tight input limit.

Claim a Codex task in the primary source; the claim records the source baseline before editing. Do not create a worktree or switch branches. Use [single-source workflow and legacy migration](../../docs/agents/WORKTREES.md). All communication uses the same coordinator/task board; other providers are inactive historical metadata.

Build with `tooling/native/build.ps1` under the shared build lock. Every session overwrites `build/win-dev/apps/desktop/pikdraw.exe`; do not create app aliases or alternate build folders.

Before ending a session, save a short task update or handoff: summary, evidence, limitations and next action. Review [handoff protocol](../../docs/agents/PROTOCOL.md) only when needed.
Handoff includes the task-owned source snapshot even when changes are uncommitted; never commit just to satisfy the old workflow.
