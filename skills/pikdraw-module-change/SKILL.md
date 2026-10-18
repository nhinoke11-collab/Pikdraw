---
name: pikdraw-module-change
description: Make a scoped PikDraw change while preserving document data and module boundaries.
---

# PikDraw module change

registry/modules.json defines module dependencies. Keep feature code with the module that owns its behavior; UI routes commands rather than owning core logic.

Work directly in the primary source. Preserve existing local edits. Use the shared build runner (tooling/native/build.ps1) when a build is relevant, and verify changes with targeted test suites.

Group code by feature cohesion. Source/header files should stay within 300–1800 lines and strictly <= 2000 lines. Never append new features to files approaching or exceeding 2000 lines; split by feature domain instead. See docs/development/code-organization.md.

For UI changes, read docs/design/ui-ux-standard.md and review docs/architecture/workspace-layout.md. Record the menu path, shared action IDs, toolbox/toolbar, contextual property bar/Properties, field scopes/states, SVG icons, short labels, tooltip/focus help, status hints and usage help in the feature spec or equivalent. Mark irrelevant items as not applicable. The implementing agent makes routine design decisions and continues within scope.

Document changes use commands/transactions and preserve IDs, units, source curves, color metadata and undo/redo. Async proposals must reject stale revisions/generations. Changes to shared schemas, persistence or public APIs need consumer review and migration notes.

For a behavior change or feature, add or update acceptance tests for the real user or consumer workflow. Use the shared Given/When/Then case structure and select only the relevant test layers for the change's risk; do not run a large default suite for every task. Reuse suitable tests instead of duplicating them. See [feature acceptance](../../docs/quality/feature-acceptance.md).

After verification, delete one-off test sources and temporary fixtures, outputs and logs. Keep useful regression tests and reusable fixtures in the owning module's suite.

Run checks that cover the changed behavior. A core prototype is not a complete editor feature. Record actual evidence and unresolved limits; do not claim planned capabilities as implemented.
