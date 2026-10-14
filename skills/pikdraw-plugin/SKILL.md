---
name: pikdraw-plugin
description: Design or implement PikDraw plugin manifests, host APIs and isolated execution with explicit capabilities and versioned contracts.
---

# PikDraw plugin

Read packages/plugin-sdk/AGENTS.md and [manifest schema](../../contracts/plugin-manifest.schema.json). The host is planned, not implemented; manifest examples do not demonstrate working plugins.

Define the contribution (command/panel/importer), minimum/maximum host API and required capabilities. Use a process boundary for third-party code; host validates messages, enforces capabilities, resource budgets and cancellation. A capability declaration is not permission enforcement.

Plugins receive snapshots/selection and submit validated commands. They cannot mutate raw document state or bypass undo. Normalize entrypoints inside the package and reject traversal/symlink escape.

Compatibility and crash behavior need host tests before claiming support. Breaking plugin contracts require consumer review and version changes. Read [architecture](../../docs/architecture/architecture.md) for process boundaries.
