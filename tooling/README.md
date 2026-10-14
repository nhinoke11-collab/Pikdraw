# PikDraw harness

Status: implemented coordination tooling.

Responsibility: Provider-neutral task board, worktree coordination, context routing and evidence handoffs.
Windows launcher: `./tooling/pikdraw.ps1 doctor` finds Python 3.10+ locally; `PIKDRAW_PYTHON` overrides discovery. No installation or network calls.
Dependency authority: `registry/modules.json` (no module dependencies).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
