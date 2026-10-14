# rendering scope

Rendering consumes snapshots and cannot mutate documents. Preview levels never change export data. Benchmark chosen renderer before committing to a backend.
Choose GPU/software by tested capabilities/driver/build, not GPU brand. Pin backend implementation and enforce context ownership/lifetime. Subtract camera/tile origin in double before float conversion; clip/chunk huge paths without changing source geometry.
Revisioned index/cache bounds include strokes/effects; test zoom-to-fit, giant paths, cold/warm cache, tile/filter boundaries and invalidation. Caches have memory budgets and preserve compositing order. GPU resources cannot be the only recoverable asset copy.
Software has a separate interaction budget. Recoverable device loss differs from fatal native driver crash/hang; do not claim seamless recovery without evidence. See ../../docs/decisions/0002-runtime-and-hardware.md and ../../docs/roadmap/foundation-spikes.md.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
