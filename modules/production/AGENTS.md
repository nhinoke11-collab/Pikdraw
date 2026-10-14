# production scope

Real-world units and manufacturing tolerances are explicit. Open engraving paths and closed cutting profiles have different validation. CAM/G-code is not implemented.
Derived output tracks source IDs/revisions, algorithm/tool/material parameters and full approximation/quantization/export error budget. Never manufacture from render LOD/raster cache. Offset contours alone do not prove valid CAM/toolpaths or a CAD constraint solver; separate milestones and machine/output fixtures are required.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
