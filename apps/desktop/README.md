# PikDraw desktop

Status: planned application module; no graphics implementation yet.

Responsibility: Desktop shell, dock layout, module workspaces, action routing and accessibility.
Dependency authority: `registry/modules.json` (document, rendering, vector, raster, production, scene3d, interop, plugin-sdk, workers).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
