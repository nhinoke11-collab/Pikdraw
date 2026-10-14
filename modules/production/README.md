# PikDraw production

Status: planned application module; no graphics implementation yet.

Responsibility: CAD 2D annotations, cut/engrave/drill classification, nesting and job outputs.
Dependency authority: `registry/modules.json` (document, geometry).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
