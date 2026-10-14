---
name: pikdraw-session
description: Bootstrap or resume an assigned PikDraw agent task across providers using bounded context, owned paths and Git worktrees.
---

# PikDraw session

Read root AGENTS.md, docs/START.md and docs/project-state.json. Use the harness from the checked-out repository; it resolves the shared board for linked worktrees.

Find the assigned task with `task list` or `task show ID`. Claim with a unique provider/session owner before editing. If there is no authorized task, obtain scope; do not infer a new product objective.

Generate `context --task-id ID`. Open only its module context and relevant contracts. The packet limit is a character bound, not a token guarantee. Use smaller packets when a provider has a tight input limit.

Create/use the task worktree according to [worktree lifecycle](../../docs/agents/WORKTREES.md). Provider names are routing labels; this skill never establishes provider connections.

Before ending a session, save a short task update or handoff: summary, evidence, limitations and next action. Review [handoff protocol](../../docs/agents/PROTOCOL.md) only when needed.
