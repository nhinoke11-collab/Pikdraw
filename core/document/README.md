# PikDraw document

Status: planned application module; no graphics implementation yet.

Responsibility: Stable object IDs, pages/layers, transactions, history, persistence and recovery.
Dependency authority: `registry/modules.json` (geometry, color, typography).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
