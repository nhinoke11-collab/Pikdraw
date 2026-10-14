# PikDraw rendering

Status: planned application module; no graphics implementation yet.

Responsibility: Viewport rendering, retained cache, incremental invalidation and output fidelity.
Dependency authority: `registry/modules.json` (document, geometry, color, typography).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
