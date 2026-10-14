# workers scope

Treat worker output as untrusted data. Validate before atomic document application. Crashes/timeouts terminate task scope; do not apply partial results.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
