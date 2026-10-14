# geometry scope

Keep visual curves separate from manufacturing approximations. Robustness/tolerance decisions need fixtures; never silently simplify source paths.
Source coordinates and transforms propose mm/binary64; no renderer scalar types in public storage contracts. Double alone does not prove topology correctness. Polygon clipping candidates require checked quantization/range and an error budget covering approximation, algorithm and export.
Use independent precision/topology oracles for near-contact, holes, degenerate paths, large origins and reflections. Do not use viewport tolerance for manufacturing or silently remove slivers. See ../../docs/architecture/document-runtime.md.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
