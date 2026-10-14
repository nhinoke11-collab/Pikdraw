# color scope

Retain source color model/profile and spot identities. A preview conversion must not overwrite document colors.
Preserve source CMYK/spot/ICC/overprint semantics within the declared model; print output does not use RGBA viewport caches. Pixel similarity is insufficient for separations/PDF/X claims; require scoped output/RIP fixtures and explicit loss diagnostics.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
