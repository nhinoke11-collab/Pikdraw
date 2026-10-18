# Contract C10: File export and production geometry

- Version: 0.1.0.
- Status: Proposed; specification only, not implemented by export or CNC runtime.
- Producers: export planners and format-specific writers in `modules/interop`; validated geometry operations owned by `core/geometry` and `modules/production`.
- Consumers: desktop Export workflow, document snapshots/storage, raster/rendering, interop format readers, and independent output applications/CAM.
- Depends on: [C1 geometry and units](c1-geometry.md), [C2 document snapshots and transactions](c2-document.md), [C3 rendering](c3-rendering.md), [C4 color and typography](c4-color-typography.md), [C5 storage](c5-storage.md), [C6 workers](c6-workers.md), [C8 import fidelity](c8-import-fidelity.md), and [C9 appearance/effects](c9-appearance-effects.md).

## 1. Purpose and claims

C10 specifies how an immutable document snapshot becomes one or more external files, how the UI describes scope and options, and what additional evidence is required for files intended for cutting or machining.

Export, source-format round-trip, rendering fidelity, and manufacturing readiness are separate capabilities. Exporting a valid file does not prove that another editor can reopen every attribute, that its rendered pixels match PikDraw, or that a CAM system will cut the intended geometry at the intended size. A successful parser/importer does not grant writer capability.

Every advertised capability is scoped to a format, version/profile, output representation, object/page feature set, options, and tested consumer. Capability reports must not collapse these results into one `supported` boolean.

## 2. Export job snapshot

An export job is an immutable request produced by the host from one document snapshot. The versioned request identifies:

- document ID, snapshot revision, and the exact ordered page IDs or single-page identity;
- the selected object IDs and group boundaries captured when the user prepares the job;
- export scope: selection, active page, ordered page list/ranges, all pages, or all artwork;
- bounds policy: page, visible artwork, selection, or explicit rectangle; page clipping is a separate choice;
- format adapter ID, adapter build, output version/profile, options schema version, and the validated typed options;
- color/font/image dependencies, conversion choices, and the preflight report accepted for this exact job;
- deterministic output names, target paths, and whether this is a single file, per-page batch, or per-asset batch;
- cancellation generation and job identity, used to discard stale progress/output.

The worker never chooses the snapshot, target path, page, output profile, filename, or option values. The host captures and validates those values before the adapter runs. Selection/page changes after the review screen do not silently mutate an active job.

If the user keeps editing while output is generated, the job remains bound to its captured immutable snapshot. It may finish from that snapshot; it must not mix revisions. Its receipt names the revision actually written. Export does not mark the current document clean and does not add an undo step.

## 3. Result and loss report

A report records the outcome at job, output-file, page, and object scope when applicable. It separates:

| Axis | Required outcome |
| --- | --- |
| Job | `complete`, `complete_with_diagnostics`, `rejected`, `cancelled`, `timeout`, `worker_failed`, `resource_limit`, or `write_failed` |
| Format/profile | Target format, version, profile, writer build, and validated option-schema version |
| Appearance | `verified_for_catalog`, `partial`, `unknown`, or `not_assessed`, with the checked feature catalog/oracle |
| Structure/editability | `preserved_for_catalog`, `converted`, `flattened`, `partial`, `unknown`, or `not_applicable` |
| Source metadata | Field-level `preserved`, `converted`, `omitted_by_choice`, `unsupported`, or `unknown` |
| Production | `verified_for_profile`, `blocked`, or `not_assessed`, with the exact tolerance/topology profile if verified |
| Files | Each path, byte size, checksum, snapshot revision, page/object scope, and write/verification outcome |

An empty warnings list is not evidence of complete feature coverage. Unknown source data remains `unknown` unless an independent gate establishes the mapping. Rasterization, text outlining, color conversion, image downsampling, path approximation, clipping, font substitution, hidden-object inclusion/exclusion, page-bound changes, and metadata removal are reported at the smallest reliable scope.

Diagnostics have stable codes, severity, affected source locator/object ID when known, selected action, before/after representation, and a user-readable explanation. They do not contain source file contents, private text, passwords, or customer paths.

## 4. Preflight, choice, and loss policy

The host runs format and scope validation before writing final output. Preflight confirms the profile can represent every selected visible object, dependency, page, color mode, and option. If the format does not support an attribute, the user sees a concrete conversion choice and preview before the job begins.

Default behavior:

1. Preserve supported vector and source metadata.
2. Reject unsupported content in strict mode. Do not silently skip an object, substitute Normal blend, turn text into another font, flatten an effect, or rasterize vector geometry.
3. Permit rasterize/outline/flatten/approximate only when the user selects that conversion and the report captures its parameters and loss.
4. Keep raster/reference content distinct from paths that can be used for production.
5. Fail closed on malformed/non-finite options, unknown output profile, missing required assets, output-path collisions, stale job generation, or failure to validate the written file.

The output adapter receives a bounded neutral export view; it cannot change the document snapshot or mutate writer state. Changes to source artwork require a separate document command with Undo.

## 5. Common vector and page rules

Format adapters serialize only what their tested profile represents. They map the canonical model into that format's semantics; they do not force every format through one intermediate writer if that loses source meaning. Native IDs remain stable in PikDraw. Output IDs/layers/names are generated deterministically where possible, with source mapping recorded in the job receipt.

Page scope, output bounds, and clipping are independent values. A page-sized output with clip enabled must not create a new background rectangle. Objects beyond the page remain unmodified in the document. Selecting all artwork may include pasteboard content only after the user chooses that scope.

Multi-page export preserves page order and page-specific physical size. A single-file format may encode pages together only if its version/profile supports that representation. Otherwise, generate one file per page with unique deterministic names. Never place pages at a common origin in one CNC drawing without an explicit placement/nesting operation.

Colors and embedded images follow [C4](c4-color-typography.md). Source profiles, spot names/tints, overprint and transparency are preserved only where the target format and tested profile support them. A screen preview in RGB does not establish print separation correctness. Required linked assets must be resolved through a host-granted read capability; file-declared URLs are never fetched by the writer or worker.

Text is editable in the target only when the target font/text model preserves its layout and metrics. Outlining text is an explicit irreversible output conversion. Export reports font embedding rights, substitution, outlining and shaping differences; it never changes the native text.

## 6. Raster export

Raster output derives from the captured snapshot, not a screenshot or low-resolution viewport cache. A tiled/streamed renderer must carry enough effect halo across tile boundaries so blur, shadow, clipping and transparency match the selected export semantics. Output bounds include stroke/effects when chosen by policy.

The request records pixel width/height, physical dimensions, independent X/Y DPI, color model/profile/intent, alpha convention, background/matte, resampling/antialias policy, compression family and quality. Host validation checks positive finite dimensions and computed pixels, profile support, integer overflow, decoder/renderer budgets and output disk limits before allocation.

`pixels = round(mm × dpi / 25.4)` is the default physical-size conversion. The receipt records the exact integer dimensions and rounding rule. DPI changes output sampling, not document geometry. Pixel width/height edits that alter aspect ratio require an explicit unlocked ratio choice.

Lossy quality applies only to a lossy encoder/profile and must not be presented for a lossless format. A target-size optimizer may reduce quality or sampling only inside user-approved minima; failure to meet the target is reported rather than silently producing lower quality. Existing JPEG input is encoded from the captured source pixels, never recompressed from a previous export cache.

ICC/profile conversion, alpha flattening, indexed-color reduction, bit-depth reduction, metadata stripping and downsampling are explicit options with diagnostics. A codec's ability to read or write a container does not prove the complete PikDraw color/image pipeline preserves every bit depth or metadata field.

## 7. Vector interchange profiles

Format profiles are separately versioned and gated. The initial format families in scope are:

| Family | Candidate output profiles | Important constraints |
| --- | --- | --- |
| SVG | Design; CNC | Physical size/viewBox and coordinate precision; font policy; image embedding/link; minification. Script, animation, external resource references and untested filters are not silently emitted as active content |
| DXF | R12/AC1009 CNC; R2000/AC1015 CNC; other explicit CAD profile | Declared units, target entity version/set, layer mapping, transforms, OCS/WCS, arcs/bulges and polyline closure. Entity support is gated by target CAM |
| EPS | Design/vector subset; named tested CNC consumer profile if applicable | Explicit page bounds, text/font policy, image handling and transparency conversion; preview image cannot stand in for vector content |
| PDF | General design; print profile; named tested vector CAM profile if applicable | Page boxes, page order, font embedding, image sampling and transparency. PDF/X, output intent, spot and overprint require separate print/RIP gates |
| AI / CDR | No native writer capability in this contract | Do not rename PDF/SVG output to `.ai` or `.cdr`; require a versioned writer, license review and independent application fixtures before adding capability |
| JPG/JPEG, PNG, TIFF | Bounded raster profiles | Quality/compression/color/depth options follow the codec and verified application pipeline |

SVG decimal precision is selected from a maximum output-space coordinate error budget, not a UI zoom value. XML output is well-formed; XML escaping and finite numeric formatting are required. Any compression wrapper has a decompressed-size limit on import.

An EPS vector profile reports every required transparency flattening/unsupported PostScript feature. A PDF print profile names the expected output intent, color conversion, fonts and preflight engine. Successful file creation alone does not authorize a PDF/X or print-quality claim.

## 8. CNC geometry profile

CNC export is a geometry-production capability, not a general appearance export mode. Its input set excludes raster/reference-only objects and every object whose geometry, units or source-to-native conversion has unknown production status.

The CNC job records the intended operation class (`cut`, `score`, or `engrave`), but does not encode feed, spindle, laser power, material thickness, fixture origin, depth, kerf compensation, tool choice, toolpath ordering, or G-code unless a separate machine-specific contract defines and gates those fields.

### 8.1 Units, scale and coordinate mapping

- Canonical export calculations use double precision physical coordinates and named units. Preserve the requested scale, transforms, page origin, selected origin policy and mapping from the source unit. A camera zoom or fit-to-page operation is never part of CNC output.
- Unknown, unitless, conflicting, or non-finite source units block CNC export until the user makes an explicit unit/scale choice. Save this choice in the export receipt; do not rewrite source geometry.
- DXF must include the correct unit header when the target version supports it; formats/profiles without reliable unit metadata must report the selected unit and require a dimension check on import to CAM.
- Preserve OCS/WCS and INSERT/block transforms. A transform that turns a circle/arc into an ellipse must retain that geometry if supported or be approximated under the stated curve tolerance. Reflection may reverse path winding and arc direction; validate the result.
- Output extent and physical measurements are verified after independent reopen. Coordinate origin normalization is allowed only as an explicit output mapping recorded in the receipt.

### 8.2 Curves and precision

Preserve source line, circular arc, circle, quadratic/cubic Bezier, spline and closure data exactly when the target profile supports the same semantics. Polygonization/spline approximation requires adaptive subdivision in output millimeters, with bounded maximum deviation after all transforms and serialization. Segment count alone is not an error metric.

The preliminary Fine-profile ceiling is 0.01 mm total maximum curve/coordinate deviation. It is a proposed budget requiring CNC consumer review, not a machine guarantee. A profile may allocate at most 0.002 mm to source import conversion, 0.001 mm to unit/transform normalization, 0.004 mm to approximation, and 0.003 mm to serialization/reopen. The measured combined error must stay within 0.01 mm; if input/API precision cannot support the budget, export is blocked or the user selects a looser named profile.

Measure source-to-reopened output in physical coordinates using independent semantic geometry and deviation checks. Record coordinate/extents error, curve error, endpoints, closure, contour count, length, area, nesting, orientation, and resulting topology. Pixel diff or matching bounding boxes do not prove CNC accuracy.

### 8.3 Topology, strokes and diagnostics

Closed outlines and open paths are distinct. Cut profiles require closed valid contours and preserve intended holes/nesting. Score and engraving profiles may include open paths. Expected operation class is selected before applying those rules; a single blanket “all paths must close” rule would reject valid score paths.

The writer/preflight detects zero-length edges, duplicates, overlaps, self-intersections, unexpectedly open contours, tiny features, degenerate curves, unknown fill rules, clipping dependencies, unsupported entities and objects outside page bounds. Detection is read-only by default. Welding, closing gaps, deleting duplicate paths, simplifying nodes, changing winding, expanding a stroke into its outline, converting text to curves, or offsetting paths are explicit, previewed conversions and never mutate the source document.

Centerline cutting and filled-region silhouettes have different geometry. The default CNC mapping treats a stroke as its source centerline and does not convert it to two outline cuts. Expanding a stroke or converting a filled region to an exterior contour requires an explicit operation choice and a topology report.

Kerf/tool compensation, nesting, order optimization and machine-specific construction geometry are out of scope unless added as separate, versioned and undoable features. The export adapter does not add a hidden compensation offset or infer tool settings from color or layer names.

### 8.4 Independent consumer requirement

A CNC profile cannot reach `verified_for_profile` using its own writer and reader as the only oracle. Test each supported target against a separately implemented DXF/SVG/PDF reader and a named CAM application's version/settings. A dimensioned coupon should include known-length lines, circles/arcs, a Bezier or spline, nested holes, an open score path, and a closed cut contour.

Verify physical dimensions in the CAM after import, curve/topology metrics after independent reopen, layer and operation mapping, warnings, units, and scale. Machine cutting is an additional manual gate owned by the named device/workflow; a successful digital coupon does not certify material/feed/kerf performance.

## 9. Atomic output, batches and cancellation

Each file is written to a unique host-created temporary file in the destination directory/volume, flushed as required by the selected durability policy, reopened or parsed by a validator, and atomically renamed to its final name only after successful validation. Never advertise a partial temporary file as complete.

For a multi-file batch, each successful file is committed independently after its own validation. A failure/cancel leaves completed files intact and reports the failed and unstarted items. A receipt records the complete batch set and outcomes; the host does not claim an all-or-nothing transaction across unrelated target filesystems.

Preflight prevents overwriting the source or an unrelated file without user choice. Name collisions, invalid/reserved names, disk-full, access-denied, locked output, antivirus interference and rename failure produce explicit outcomes. Cleanup only the temporary files owned by the failed job. A stale generation cannot publish a final file after the job has been cancelled or replaced.

If output path is on removable/network storage, atomic rename/durability behavior must be checked for that provider; otherwise capability is limited to the tested local filesystem. A crash/fault-injection test must demonstrate that a partially written file is not reported as valid.

## 10. Capability registry

The trusted host registry records, per adapter build and output profile:

- target format/version and representation;
- object/page/image/text/curve/effect feature catalog and per-feature status;
- supported scope, bounds, selection, page model and asset dependency policy;
- coordinate units, precision/error profile, numeric range and serialization rounding;
- color model/profile/intent, text/font, transparency, image bit-depth and metadata coverage;
- explicit conversions, warnings, rejection rules, and independent validator/consumer evidence;
- writer binary/dependency versions, build hash, notices/license and security update owner;
- output operation status, fixture catalog version, release snapshot and known limitations.

Import capability does not imply export capability. Export capability does not imply source-format round-trip. Design output does not imply print or CNC readiness. The adapter cannot set or expand its own trusted catalog at runtime.

## 11. Acceptance gates

Every writer profile must provide evidence for the gates relevant to its claimed behavior. Unmet gates retain `unknown`, `blocked` or `not_assessed` status.

| Gate | Evidence required |
| --- | --- |
| C10-G1 Snapshot and scope | Correct document revision, selected IDs, page range/order, bounds, clipping and asset dependencies captured in the job; no silent scope changes |
| C10-G2 Output validity | Independently reopen every single/batch output; format/version/profile and every final file validate; malformed options fail before writer execution |
| C10-G3 Geometry and structure | Compare physical units/transforms, curves, closure, topology, layer/order, object counts and names against source semantic oracle |
| C10-G4 Appearance and color | Compare rendered output to independent source oracle at declared DPI/color profile; separately inspect alpha, effects, fonts, color profiles, spot/overprint and print output when claimed |
| C10-G5 Loss disclosure | Unsupported features are rejected in strict mode or listed with an accepted explicit conversion; no silent omission, substitution, flattening, clipping or rasterization |
| C10-G6 CNC | Geometry precision budget, units, centerline/outline policy, closure/holes and topology pass after independent reopen in the named CAM/version/settings; device cutting is reported separately |
| C10-G7 Fault lifecycle | Cancel, stale job, worker crash, resource/disk failure and output lock preserve document state and do not publish incomplete files; cleanup respects ownership |
| C10-G8 Resource profile | Measure source/output bytes, pixels, segments, worker/host peak allocations, scratch, CPU, wall time, UI stall, disk and handles on named machines |
| C10-G9 Security and license | Test parser/writer sandbox and dependency build/license inventory for the exact release artifact; output paths remain host-controlled |
| C10-G10 UI workflow | Menu/action/shortcut, page and selection scope, options, diagnostics, progress/cancel, accessibility, DPI and keyboard behavior work in the native shell |

Shared fixtures should use synthetic or redistributable sources with explicit rights, source app/build/settings and independent expected geometry/render output. Customer files remain local; release receipts use neutral fixture IDs/hashes and never contain customer names, document text, credentials, or full private paths.

## 12. Publication and change control

C10 remains Proposed until export planner/report, format-specific writer, document snapshot consumers, desktop workflow, storage/asset handling and relevant QA owners review the fields they consume. Schema changes require versioned migration/compatibility notes before capability entries point at them.

Updating a format profile to verified requires evidence for the exact integrated source/build/adapter and independent consumer. One passed file, one version, a green preview, or the absence of writer diagnostics cannot broaden another format family/version/profile. Release documentation states the supported subset and every measured limit.
