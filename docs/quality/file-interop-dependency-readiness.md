# F03: Worker and file-format dependency readiness

Audit date: 2026-10-18. Repository snapshot: `6147837efe505d95b6e8eae90d156d7bb0650356`.

## Gate result

The Windows worker capability and containment spike passed once on this machine. It is
evidence for the tested worker build and AppContainer policy only. It is not parser fidelity,
format support, CPU/memory quota acceptance, or release approval. No external PDF, CDR, EPS,
JPEG, PNG, or TIFF engine is currently pinned or linked by `cmake/PikDrawDependencies.cmake`.

The only integration-ready raster candidate found in this audit is Windows Imaging Component
(WIC), which is supplied by Windows. The PDFium, libcdr, MuPDF, and Ghostscript entries below
remain candidates; none has a PikDraw revision, artifact hash, toolchain lock, transitive notice
bundle, or ABI test. Do not advertise their formats from this audit.

## Worker evidence

The following focused targets passed through `tooling/native/build.ps1`:

| Target | Result | What it establishes |
| --- | --- | --- |
| `test_pikdraw_workers_live_containment` | Pass | Five AppContainer/HANDLE_LIST/Job canaries were denied by Windows after positive controls succeeded. A source handle was read-only. |
| `test_pikdraw_worker_v2_integrity` | Pass | V2 identity, framing, digest, ordering, and malformed-stream checks in the current suite. |
| `test_pikdraw_workers_policy` | Pass | The production worker rejects test-only canary and sandbox-bypass controls. |

The live test generated a synthetic source of 20 MiB + 19 bytes, staged it through
`SourceSnapshot::Create`, passed its read-only handle to the test worker, and checked the source
size and SHA-256. The test worker successfully read it. The same run measured explicit OS denial
for ungranted file read, file write, loopback network, an ungranted handle, and child process
creation; each denial had a successful positive control. Local report:
`build/win-dev/platform/workers/live-containment-report.json` (`status=pass`, five denials,
`source_handle_read_only=true`, `full_spike_accepted=false`).

The source snapshot code opens the selected file without following a reparse point, enforces a
host-provided byte ceiling, copies to a job-specific staging directory, computes SHA-256 while
copying, and reopens the staged file read-only. This proves the tested source-handle path can
exceed v1's 16 MiB inline payload cap. It does not prove arbitrary files can be decoded, that
every production worker uses the same configuration, or that staging, decoded images, native
scene memory and undo storage fit one application-wide quota.

The live report explicitly limits its scope to one local Windows run. It does not measure parser
corpus fidelity, CPU rate limits, working-set/RAM limits, staging cleanup after every crash mode,
full Spike C acceptance, or format-specific resource profiles. C6 correctly continues to
distinguish Job Object commit limits from physical RAM use and requires further release evidence.

## Dependency audit

| Area | Candidate and source evidence | Build, license, and feature decision |
| --- | --- | --- |
| JPEG / PNG / TIFF | Windows Imaging Component exposes native codecs for JPEG, PNG, and TIFF. Microsoft documents these codecs and their decoder/encoder pixel formats. | Prefer WIC for a first Windows-only baseline. Pin the Windows SDK/API floor and call the native codec CLSIDs explicitly so installed third-party codec extensions are not implicitly trusted. The app's current RGBA8 image model and render path still cannot claim CMYK, 16-bit, ICC, orientation, metadata, or multi-frame fidelity. Add per-format fixtures and encoder options before enabling capability. |
| PDF and AI-PDF representation | PDFium is a candidate for isolated PDF rendering and page access. Its upstream README uses Chromium build tooling, GN/Ninja and Clang; its GN options enable V8/JavaScript and XFA by default. | Upstream license begins with BSD terms and includes Apache-2.0 text; a shipping build also needs the exact `DEPS` third-party license inventory. Disable JavaScript and XFA unless a reviewed feature requires them. The public embedder API and render output do not by themselves provide editable vector extraction, print-color fidelity, or Illustrator authoring data. No revision or build artifact is pinned. |
| CDR | LibreOffice's `libcdr` project describes a reader/converter for CorelDRAW binary files and identifies MPL-2.0. Its README lists Boost, ICU, lcms2, librevenge, and zlib build requirements. | Candidate for a versioned import-only worker. The local four-file CDR corpus has not been run through it, and the current upstream target/profile, Windows build recipe, transitive notices, ABI, supported CDR versions, and feature coverage are not locked. Do not use a successful parse as CDR fidelity evidence. |
| PDF alternative | MuPDF provides PDF parsing/rendering. | Its upstream license is AGPL, with a commercial option. It offers no release-ready advantage before license, dependency, and ABI review; not selected. |
| EPS / legacy AI interpreter | Ghostscript is a PostScript/PDF interpreter and offers vector devices. Its release and FAQ pages describe AGPL and commercial licensing. | Do not bundle until the distribution license is explicitly resolved and interpreter execution is covered by the worker sandbox. A rendered preview is not vector/CNC evidence. AI files without a verified PDF-compatible representation remain a separate unsupported profile. |
| Current in-tree formats | SVG and DXF adapters are in-tree C++; worker transport uses Windows APIs and BCrypt. The current dependency CMake file discovers Python and optional Qt; it does not fetch or pin file-format engines. | These adapters are independent of the external engines above. Their import capability is profile-specific and does not authorize general SVG/DXF support or export claims. |

Official references reviewed on 2026-10-18:

- [Microsoft: WIC native codecs](https://learn.microsoft.com/en-us/windows/win32/wic/native-wic-codecs), [WIC overview and built-in formats](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec), [native pixel formats](https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-codec-native-pixel-formats)
- [PDFium upstream license](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE), [PDFium build and feature configuration](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/README.md)
- [LibreOffice/libcdr upstream README](https://github.com/LibreOffice/libcdr/blob/master/README), [libcdr build requirements](https://github.com/LibreOffice/libcdr/blob/master/configure.ac)
- [MuPDF 1.28.5 license](https://mupdf.readthedocs.io/en/1.28.5/license.html)
- [Ghostscript licensing FAQ](https://ghostscript.com/faq/), [Ghostscript releases](https://ghostscript.com/releases)

## Release gates for adapters

Before an engine is added to the package, its task must record an immutable source tag/commit,
source archive SHA-256, toolchain and ABI, build flags, every transitive runtime component,
license/notice obligations, SBOM entry, and update owner. The worker package must contain only
the reviewed engine build. The worker receives the host-created source handle and bounded
capabilities; it must not resolve paths, network resources, installed codecs, scripts, or linked
assets on its own.

The adapter-specific work must add reproducible build and runtime tests, malformed-input and
resource-limit cases, cancel/crash cleanup, and format fixtures with independent expectations.
WIC bit depth, color model, metadata, frame, and compression profiles; PDF vector/reference
coverage; CDR version coverage; and EPS interpreter behavior each remain explicit gates.
