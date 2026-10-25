# PikDraw production

Status: implementation in progress. The print slice has validated RGB output conditions,
deterministic diagnostic/1:1-crop/variation-grid test-sheet planning and bounded ICC-based
sheet rendering. Desktop can export an A4 300 ppi sRGB TIFF test sheet without changing the
document. Raster TAC analysis converts full-resolution RGBA8/U16 through an RGB-to-CMYK ICC
transform in bounded cancellable batches. Measurement services parse a documented CGATS/CSV
subset, calculate CIEDE2000, compare patches by stable ID and explicit mean/p95/maximum limits,
and persist bounded, checksummed .pdmr run artifacts. Desktop can import a measurement file,
save the immutable run, compare two runs by sample ID against their declared conditions,
apply operator-supplied Delta E limits, and save an atomic CSV report. This is a declared-run
comparison, not proof of instrument or contract-proof accuracy. Desktop now has an unverified
direct printer path using Windows WCS with driver ICM disabled; its worker hashes the destination-
profile file returned by the printer DC and blocks submission if it cannot identify that file.
Physical output and the driver's actual profile association and ΔE remain unvalidated. RIP submission,
native CMYK separations, overprint, PDF/X
and physical proof qualification remain unimplemented.

Responsibility: print/output conditions, proof/test jobs, CAD 2D annotations,
cut/engrave/drill classification, nesting and job outputs.
Dependency authority: `registry/modules.json` (document, geometry, color).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
