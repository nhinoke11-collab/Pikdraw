# PikDraw workers

Status: planned application module; no graphics implementation yet.

Responsibility: Process lifecycle, timeouts, resource limits, cancellation and validated task results.
Dependency authority: `registry/modules.json` (interop, plugin-sdk).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
