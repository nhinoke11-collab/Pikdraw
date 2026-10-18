# Open, Import, Export corpus baseline

Baseline date: 18 October 2026. Source commit: `6147837efe505d95b6e8eae90d156d7bb0650356`. This document records what the local corpus can currently prove; it does not enable or advertise a format profile.

The local source directory is `cdr/`, which `.gitignore` excludes because it may contain private customer artwork. Its machine-local manifest is `cdr/test-reports/open-import-export-baseline-20261018.json`; it stores relative file names, byte sizes, SHA-256 hashes and probe states. Do not commit or upload that manifest or its source files. The tracked manifest structure is defined by [file-interop-corpus-manifest.schema.json](../../contracts/file-interop-corpus-manifest.schema.json).

The current corpus contains 16 source files totaling 116,989,010 bytes: four CDR, three PDF, one AI, two EPS, one DXF, one SVG and four JPEG. It contains no PNG or TIFF. The largest sample is about 38.6 MB. The earlier worker transport probe verified read-only delivery for these 16 samples; that probe did not execute format parsers and explicitly did not complete the large-source parser spike.

Two parser outcomes are already recorded locally. The 1,410,334-byte DXF sample was rejected by the current `dxf-entities-v1` semantic-record profile. The 891-byte SVG smoke sample was rejected because it contains an element outside the current importer subset. Neither outcome is a successful open or round trip. The other PDF, CDR, AI, EPS and JPEG files have not been parsed by PikDraw in this baseline.

The current capability code enables SVG and DXF import, and SVG export. PDF, CDR, AI, EPS, PNG and JPEG are not enabled. TIFF has no `FormatType` entry yet. The local corpus does not establish appearance fidelity, editability, PDF page handling, embedded assets, color management or CNC readiness. The Adobe Illustrator and CorelDRAW package directories in Downloads are installer archives, not evidence that a particular installed application build, settings profile or sample license was used as an oracle. Oracle runs and source redistribution rights remain unverified.

For each later adapter, update only its sample probe state after recording the exact reader/writer build, options schema, validation result and independent application evidence. Preserve the initial hashes; if a source changes, create a new manifest snapshot rather than rewriting this baseline in place. Add redistributable fixtures to `tests/fixtures` only after rights are verified.
