# Contract C9: Appearance graph, effects and source fidelity

- Version: 1.1.1 (documentation correction; no effect schema change).
- Audited: 2026-10-24 against the working source.
- Status: native Path/Text basic appearance exists; SVG presentation opacity has a bounded import/export mapping. PDF/AI/CDR/EPS adapters exist, with effect mapping and vendor fidelity limited as listed below.
- Producers: document commands, format adapters in `modules/interop`, and native authoring tools.
- Consumers: `core/document`, `core/rendering`, native storage, interop adapters, and desktop fidelity reporting.
- Depends on: [C2 Document](c2-document.md), [C3 Rendering](c3-rendering.md), [C4 Color and typography](c4-color-typography.md), [C5 Storage](c5-storage.md), and [C8 Import fidelity](c8-import-fidelity.md).
- Related implementation roadmap: [Basic effects plan](../docs/roadmap/basic-effects-20261017.md).

## 1. Purpose and compatibility claim

This contract defines the current native opacity, compositing, blur and shadow semantics and requirements for extending appearance effects. Illustrator, Photoshop, and CorelDRAW expose overlapping names but differ in scope, hierarchy, parameter meaning, compositing, and file-version behavior. A matching label such as “opacity”, “blur”, or “blend” is not proof that two features are equivalent.

Compatibility is reported per **format family + source application/version + representation + feature + scope + parameter set + tested operation**. “AI supported” or “CDR supported” is too broad to be a valid capability claim. AI opened through its PDF-compatible representation is a distinct path and does not represent all Illustrator authoring data. CDR support cannot be inferred from one parser run, one major release, or a different effect family.

The goals are editable native effects where semantics are verified, predictable rendering, safe native save/reopen, and explicit outcomes when an effect cannot be kept. The goal is not to claim pixel-identical results across every release, plugin, color profile, and renderer.

## 2. Ownership and boundaries

| Concern | Owner | Contract boundary |
| --- | --- | --- |
| Canonical appearance data, stable IDs, validation, undo and revisions | `core/document` | Owns source values and ordered graph; no renderer- or importer-specific types. Writes go through atomic commands under C2. |
| Pixels, alpha compositing, bounds, halos, clipping and effect evaluation | `core/rendering` | Reads an immutable document snapshot under C3; no mutation of document source values. CPU reference vectors are the semantic oracle for accelerated backends. |
| Per-format parsing and source mapping | `modules/interop` | Converts untrusted parser data into a bounded proposal with source locator, coverage and fidelity report under C6/C8. It does not assign itself capability. |
| Native persistence and migrations | storage | Preserves graph order, parameters, provenance and unknown-node policy under C5. Backend filter objects are never the document representation. |
| User choices and diagnostics | desktop workflow/UI | Shows per-element support, asks before lossy conversion, and commits one validated proposal/command. It does not translate unsupported to Normal or silently flatten. |

No core module depends on desktop or interop. Rendering libraries, vendor parser objects, UI controls, and GPU resource handles must not leak into the canonical model or persisted data.

## 3. Canonical graph semantics

The native Path/Text profile has a versioned, ordered appearance stack, first persisted in PKDR v4 and retained by the current v23 writer. Broader graph scopes, per-node provenance and arbitrary graph-input references in this section remain design requirements, not implemented runtime APIs.

Each node needs at least:

- stable instance ID; stable type ID; node schema version; enabled state;
- explicit attachment scope: object, group, layer, fill, stroke, or another catalogued source component;
- explicit order/stack position and any graph input references; references must be acyclic and depth bounded;
- typed parameter values with declared unit, coordinate basis, range, default, and migration rule;
- source provenance when imported: source object locator, original feature name/parameters, format/version/representation and mapping status;
- a renderer-independent semantic definition and reference vectors for each implemented type.

The graph must preserve the distinction among color alpha, fill opacity, stroke opacity, object opacity, and group/layer compositing. Applying opacity at a parent group once is not equivalent to applying it independently to overlapping children. Selecting several objects does not implicitly make them a group. Isolated, pass-through, knockout, mask, and clip behavior must be explicit states; omission must never imply a vendor default.

The order of fill, stroke, and effect nodes is source data. Reordering is a document edit and can change output. A renderer may optimize evaluation only when it proves the same pixels, alpha, bounds, and source semantics. Unknown required nodes must not be skipped to make the rest of an effect stack render.

Opacity parameters are normalized to a finite value in `[0, 1]` in the canonical model; import/export adapters translate source units and record the mapping. Blend modes use stable PikDraw IDs, not localized labels or vendor enum ordinals. A mode is enabled only after its blend space, alpha behavior, group scope, and edge cases have a defined reference.

Blur and shadow parameters require explicit document/object coordinate basis and units. Blur records independent axes or an equivalent transform-aware representation so non-uniform object scale is not guessed. Shadow records offset, blur support, color/alpha, spread if the source defines one, compositing mode and transform basis. Unsupported source parameters remain in source provenance only if the native file can safely retain them; they cannot be represented as a fully editable native node by name alone.

Rendering evaluates declared nodes in stack order with explicit clipping, group isolation, color space, alpha convention and bounds propagation. The graph records semantic parameters, not a GDI bitmap, Skia filter, GPU handle, or preview cache. Export output must be produced from source graph values, never from a low-quality preview cache.

### 3.1 Implemented basic authored profile v1

The implemented native profile applies to PathObject and TextObject, with real glyph outlines for Text. It does not attach effects to Group, Image, Layer, masks, or a separately editable fill/stroke stack.

Each supported Path/Text object owns one Appearance value with finite object, fill, and stroke opacity in [0,1] (all default 1), one object-level blend ID (default normal), and an ordered single-input effect list. The six v1 blend IDs are normal, multiply, screen, overlay, darken, and lighten. These are compositing modes; they do not name Illustrator's geometry Blend tool. Color alpha remains in the source ColorValue and is never rewritten by these properties. A Text object with no stroke keeps the default stroke value but does not expose a stroke control for nonexistent source content.

The initial node types are pikdraw.effect.gaussian-blur and pikdraw.effect.drop-shadow, each with major schema version 1. Every node has a stable nonzero instance ID, stable type ID, enabled state, typed finite parameters, and source order. GaussianBlur schema 1.0 stores sigma_x_mm and sigma_y_mm. DropShadow schema 1.0 stores local-mm dx/dy, both sigmas, a source ColorValue, and separate shadow opacity in [0,1]; its shadow compositing is Normal and spread is zero. DropShadow schema 1.1 appends a stable `blend_mode` ID using the six v1 modes: normal, multiply, screen, overlay, darken, and lighten. Schema 1.0 loads as Normal. An importer must not coerce unsupported source values into these nodes.

Authored v1 effects use object-local millimeters, transformed with the owning object's affine transform. The source graphic is fill/stroke output on transparent pixels; fill/stroke opacity is applied while producing that graphic. Normal-mode effects run in node order, then the object transform is applied, then object opacity is applied once to the complete premultiplied result (including a shadow), then object blend/compositing is evaluated against the painter-order backdrop. For a non-Normal Drop Shadow, that node must be the last enabled node because it reads the actual backdrop: blend the shadow branch against the target below the object, composite the source graphic above it using the object blend mode, then apply object opacity once to the complete result. A later enabled node is invalid; a later disabled node is permitted. The renderer preserves node order and rejects invalid stacks. Viewport clipping occurs at the target boundary. Arbitrary effect masks and group isolation/pass-through/knockout are outside this Appearance profile. Native Group transforms and geometric PowerClip/clipping exist separately; their presence does not supply a general group Appearance stack or verify effect/clip ordering for every imported source.

V1 blend calculations use encoded sRGB (srgb_encoded_v1); blur/filter calculations use linear sRGB (linear_srgb_v1). Source RGB conversion for preview must not change saved RGB/CMYK/spot/gray channels, ICC identity, or overprint flags, and screen rendering does not certify print separations. Gaussian blur uses a normalized separable reference kernel with finite radius ceil(4*sigma_px) on each local filter axis; sigma zero is identity. Samples outside the source surface are transparent zero and the kernel is not renormalized at an edge. This finite support is a PikDraw semantic version, not a claim of pixel identity with every vendor kernel. Drop Shadow derives its mask from the alpha entering that node, blurs then offsets the mask, multiplies by shadow opacity and shadow ColorValue alpha, tints it, and places it below the input. Its own schema 1.1 blend mode is evaluated against the actual backdrop beneath the object; object blend and object opacity then apply to the resulting source/shadow group exactly once.

The basic profile permits at most 256 effect nodes on one object and 100,000 effect nodes in one document, matching appearance codec limits introduced in v4 and retained by current storage. Counts, finite values, and derived support/bounds calculations are checked before allocation or render admission. C3 owns render budgets; a valid source graph remains intact when a render request exceeds them and receives an explicit incomplete/budget diagnostic. Current per-request kernel and compositor work/cache limits are enforced; aggregate multi-job reservation and measured RSS remain unverified.

Appearance schema 1.0 is stored in native container v4 with minimum-compatible version 4. Effect node major schema 1 supports Gaussian Blur minor 0 and Drop Shadow minor 0/1; Drop Shadow minor 0 defaults to Normal and minor 1 stores its shadow blend ID. The current writer emits PKDR v23 and reader accepts v2–v23. V2/v3 receive default appearance after any path migration; explicit save writes v23. V4 was the first appearance container, not the current writer choice. No downgrade writer exists. C5 retains the historical v4 framing for migration fixtures. Independent storage fixtures cover representative appearance records, blend IDs, ordering, corruption, checksums, and v2/v3 migration. Native rendering/storage exist for the bounded profile. SVG maps a subset of presentation opacity attributes on Path/Rect/Text into native Appearance; group opacity, effects and non-Normal blend mappings remain unsupported by that subset. Other format adapters exist but do not establish general per-effect or vendor equivalence. Unknown required effect type/schema is rejected; it is never silently removed or changed to Normal.

### 3.2 Alpha, blend, blur, and shadow reference semantics

The following equations define the native v1 compositor reference. Let Cs and Cb be unassociated source and backdrop color components in encoded sRGB, and as and ab their alpha values after the applicable fill/stroke or object-opacity stage. Let B(Cb,Cs) be the selected blend function. Composite alpha is:

    ao = as + ab * (1 - as)

The premultiplied output color is:

    co = as * (1 - ab) * Cs + as * ab * B(Cb,Cs) + (1 - as) * ab * Cb

For Normal, B(Cb,Cs)=Cs. The other v1 functions operate component-wise: Multiply is Cb*Cs; Screen is Cb+Cs-Cb*Cs; Overlay branches on backdrop (2*Cb*Cs when Cb<=0.5, otherwise 1-2*(1-Cb)*(1-Cs)); Darken is min(Cb,Cs); Lighten is max(Cb,Cs). Apply object opacity to both premultiplied source color and source alpha before this equation. Do not compute object opacity by separately multiplying fill and stroke alpha after they have already overlapped.

Gaussian blur uses sigma in local millimeters converted for the render transform. For each axis with sigma_px greater than zero, support radius is ceil(4*sigma_px) and the normalized discrete kernel is proportional to exp(-i*i/(2*sigma_px*sigma_px)); zero sigma is an identity on that axis. Convolve premultiplied linear-sRGB RGB and alpha in separable X/Y passes. Do not blur hidden RGB at alpha zero, clip the input to source geometry, or renormalize the kernel at transparent boundaries.

Drop Shadow for the input alpha A is defined as offset(blur(A, sigma_x, sigma_y), dx, dy) multiplied by shadow opacity and the shadow color alpha. Convert the shadow color through its source profile for rendering, form premultiplied tinted pixels, and place the result behind the node input using Normal for schema 1.0/default mode; schema 1.1 non-Normal shadow follows the final-enabled-node/backdrop rule in section 3.1. Its dx/dy use object-local axes under C1 (+X right, +Y up). Fractional local-pixel offsets use deterministic bilinear sampling; samples outside the padded filter-local surface are transparent zero and are not renormalized. This resampling rule is a PikDraw v1 semantic, not a claim of Illustrator or CorelDRAW equivalence. Subsequent nodes receive the preceding node's full output, so reordering can change both pixels and bounds. Finally apply object opacity once to the complete stack output.

The target boundary is the only implicit viewport clip in this profile. Filter output bounds, damage, reverse ROI, and conservative resampling support follow C3. Source geometry bounds remain available separately for selection and production. The contract requires finite scratch/work quotas, checked arithmetic and reservation before allocation, with tiling only after a complete-halo seam oracle. Current source enforces per-request kernel scratch and cumulative filter-work limits; aggregate scratch reservation across concurrent jobs and tiled seam qualification remain open. A budget refusal or unsupported transform is an explicit incomplete result; it must not be presented as a successful frame with a missing effect.

### 3.3 Gradient fill paint authority and compositing

Gradient is a paint server for path fills, not an effect node in the effect stack.
1. **Authority hierarchy**:
   - `has_fill == false` -> None (no fill is painted).
   - `has_fill == true && fill_gradient != nullptr` -> Gradient paint is evaluated as the authoritative fill paint.
   - `has_fill == true && fill_gradient == nullptr` -> Solid `fill` ColorValue is painted.
   Solid `fill` is retained in memory as the remembered solid color when switching paint modes, but must never be used as a visual fallback when gradient rendering is expected.
2. **Evaluation in compositor**:
   The gradient sampler produces straight color and effective alpha `(source_alpha * stop_opacity)` across covered pixels.
   Fill opacity is applied once to the gradient rasterization before stroke drawing.
   Object opacity and blend modes apply to the resulting combined graphic exactly as with solid fills.
3. **Invariants**:
   - Types: Linear, Radial (centered ellipse).
   - Spreads: Pad (clamp), Repeat, Reflect.
   - Interpolation: EncodedSRGBStraightAlpha.
   - Relative invertibility guardrail: `n = max(|a|,|b|,|c|,|d|) > 0` with `|det(A/n)| > 1e-12`.

## 4. Proposed feature families and separation rules


| Family | Candidate semantics | Distinctions that must stay visible |
| --- | --- | --- |
| Opacity/transparency | Object, fill, stroke, group/layer; gradient, pattern or texture transparency; masks | Overall object opacity is separate from fill/stroke and source-color alpha. Transparency percentage orientation is mapped explicitly. |
| Compositing | Normal/source-over first; then individually named blend modes | “Blend mode” means compositing. Illustrator’s Blend tool/object interpolation is a different geometry feature and must not map to a compositing mode. Photoshop Fill Opacity, layer effects, Blend If/channel ranges and group pass-through are separate capabilities. |
| Blur/feather | Gaussian blur; source-specific feather where its kernel/bounds are known | Sigma/radius labels are not interchangeable without a measured mapping. Keep edge mode, finite support/tail tolerance, transform basis and clip order explicit. |
| Shadow/glow | Drop shadow first; inner shadow/glow only as separately gated nodes | Keep source alpha, tint, offset, blur, spread, blend mode, scope, transform and clipping distinct. Do not approximate inner shadow with drop shadow. |
| Advanced appearance | Multiple fills/strokes, masks, knockout, isolation/pass-through, bevel/emboss, lens, mesh, plugin/live effects | Separate each feature and source-version mapping. Unknown plugin payload is not an editable or visually verified node. |

The native Path/Text slice already includes object/fill/stroke opacity, six blend modes, Gaussian blur and drop shadow. Future group/mask/advanced families need separate model/render/storage/UI vertical slices and reference vectors; this roadmap does not enable them.

## 5. Import mapping, fidelity states, and no-silent-loss policy

Every mapped feature follows C8's independent appearance/editability/report axes. For each source element and effect, the adapter reports one of these outcomes:

1. `native_editable`: source parameters and scope map to a canonical node with verified semantics and editable round-trip in PikDraw.
2. `reference_only`: source remains available as a reference representation and the reference rendering passes the appearance gate; it is not editable geometry.
3. `explicit_conversion`: the user chooses a documented conversion such as outline/expand or rasterization, sees the affected feature and limits before commit, and the native document retains recoverable source provenance where policy allows.
4. `unsupported`: the user can cancel or defer the import; the feature is not silently omitted.

These states are per element/effect and aggregate conservatively at document level. “Parsed without warning” is never equivalent to `native_editable` or `verified`.

When an effect is outside the exact catalog, do not silently set its blend to Normal, remove its mask, discard its opacity, substitute blur for feather, rasterize it, or alter its layer scope. An opaque vendor payload may be carried as preservation data only if storage can round-trip its bytes safely; this alone does not prove the visible output. If a valid reference renderer cannot reproduce the source appearance, report `unsupported` rather than `reference_only`.

Before commit, the user sees which effects remain editable, which remain reference-only, which require conversion and what the conversion changes. A cancellation, failed parse, stale revision, or failed conversion leaves the open document and history unchanged. Conversion is one explicit, undoable transaction under C2/C8.

Native PikDraw save/reopen must preserve the canonical graph, order, unknown-data status, provenance and diagnostics. Source-format save-back is a separate capability. Do not overwrite AI/CDR source or claim a round trip until a writer for the exact target version and feature set passes the full source-app reopen gate.

## 6. Compatibility matrix requirement

Before enabling an effect for an importer, add a capability record with at least:

| Field | Meaning |
| --- | --- |
| Format family and source version range | Exact AI or CDR major/minor/build scope, not a filename extension alone |
| Representation | Illustrator authoring data, PDF-compatible representation, converted PDF/EPS, or native CDR source |
| Source application/build | The actual application/version that produced the reference fixture |
| Feature and source scope | Effect type plus object/fill/stroke/group/layer attachment |
| Parameter coverage | Tested values/ranges, units, color space/profile, linked assets and font conditions |
| Import result | Editable, reference-only, explicit-conversion, or unsupported; each status has evidence |
| Fidelity evidence | Reference render ID/hash held in approved test storage, comparison method/tolerance and environment |
| Edit/save evidence | Import-edit-undo/redo-save/reopen and, if claimed, export-back/reopen in the named source application |
| Known losses | Per-feature diagnostics, not only a document-level boolean |

Do not store user-corpus file names, bytes, paths, or private content in ordinary docs, logs, or Git. Use neutral fixture IDs and governed local/approved fixture storage. AI PDF-compatible fixtures prove only the PDF path. CDR fixtures prove only their named version and feature rows; they do not unlock other CDR releases by inheritance.

The registry must allow different outcomes for the same file family: for example, basic opacity may be editable while an unsupported mask makes one element reference-only, or a shadow parameter mismatch may require explicit conversion. A feature-level capability can be enabled only after its own adapter, renderer, storage and UI/report consumers are reviewed.

## 7. Fidelity and regression gates

An effect-format vertical slice is not ready for user files until all applicable gates pass:

- **Semantic vectors:** boundary values, overlapping shapes, transparent source colors, clipping, nested groups and each attachment scope; alpha and color are checked separately.
- **Appearance oracle:** render the same fixture in the exact source app/build and PikDraw's declared path. Record document color profile, working space, background, raster scale, antialiasing environment and comparison metric. Establish tolerance from measured baselines; do not invent one universal threshold.
- **Structure:** compare object/layer hierarchy, stable IDs, text/font information, effect stack order, scope and parameter values. Pixel match alone does not prove editability.
- **Lifecycle:** import staging, user decision, commit, edit, undo/redo, save/reopen PikDraw, and failure/cancel/stale-revision paths. Confirm one atomic commit and no mutation on reject.
- **Source round-trip:** only when export is claimed: save to the named target version, reopen in the named source app, then compare rendered appearance and editable structure again. Preview/PDF export alone is not a native AI/CDR round trip.
- **Render correctness/performance:** bounds including shadow outside source geometry, finite halo/ROI, tile seams, deletion/edit invalidation, stale preview rejection, scratch/cache budget, cancellation and memory pressure. Preview quality must be labeled separately and must not contaminate export.
- **Negative cases:** unknown effects, missing assets/fonts/profiles, corrupt data, out-of-range parameters, unsupported source version and resource limits produce precise diagnostics without silent normalization or partial document commit.

Keep privacy-sensitive customer corpus local unless the owner has approved a sanitized fixture for repository use. Corpus inventory or parser success is not an appearance, editability, production, or round-trip gate.

## 8. Remaining implementation and acceptance sequence

1. The native Path/Text schema, commands, renderer, storage, Properties UI, and undo/redo vertical slice is implemented. Maintain its contracts and regression vectors as the runtime evolves.
2. Complete remaining native acceptance for direct OS interaction, async-preview cancellation, memory pressure, and any unsupported transform or budget edge cases; do not infer those from source harnesses.
3. Add fill/stroke-stack, group/layer, mask, isolation, clipping, or additional effect scopes only as separate model/render/storage/UI vertical slices with independent reference vectors.
4. Add one source adapter/version/feature row at a time after format worker, version, security, license, and fixture review. Keep AI PDF-representation path distinct from Illustrator authoring data.
5. Publish import, editability, rendering, production, and export capability only for rows that pass their own gates; native PikDraw persistence does not imply source-format round-trip.

## 9. References and scope

These vendor help pages inform the semantics questions; they are not a PikDraw support promise or a substitute for testing files from exact app versions:

- Adobe [Illustrator Appearance attributes](https://helpx.adobe.com/illustrator/using/appearance-attributes.html) describes appearance attributes on objects, groups, layers, fills and strokes, with an ordered stack.
- Adobe [Illustrator transparency and blending](https://helpx.adobe.com/illustrator/using/transparency-blending-modes-alt.html) and [Photoshop layer opacity and blending](https://helpx.adobe.com/photoshop/using/layer-opacity-blending.html) distinguish transparency scopes and group/blend behavior.
- Corel [Object transparency](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Object-transparency.html) documents uniform/fountain/pattern/texture transparency and merge modes; [Merge modes](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Merge-modes.html) documents source/base compositing modes.
- PikDraw's current [import capability statement](../modules/interop/README.md) and [C8 fidelity contract](c8-import-fidelity.md) remain the authority for the present product state.

## 10. Acceptance cases and current status

- **AC-C9-01 — Scope:** Open; group opacity and isolated group compositing are outside the implemented profile.
- **AC-C9-02 — Effect order:** Implemented for the single-input object-level stack; regression coverage includes ordering and command undo. Per-fill/stroke effect stacks remain open.
- **AC-C9-03 — Alpha/color:** Implemented for the native Path/Text preview profile and covered by compositor and color tests; print separations, ICC proofing, and vendor oracle remain open.
- **AC-C9-04 — Unknown effect:** Native storage rejects unknown required nodes and SVG export rejects effect-bearing documents it cannot represent. PDF/AI/CDR/EPS adapters and native import review/page reports exist. Complete per-element effect provenance and choices remain open; current reports/capability notes are not a full vendor-effect graph.
- **AC-C9-05 — Version row:** Format/representation rows exist for PDF-compatible AI and libcdr CDR subsets. Exact source-version + effect + scope + parameter + source-app oracle qualification remains open.
- **AC-C9-06 — Save/reopen:** Appearance persistence introduced in v4 remains in current v23 storage. Durable page import reports exist from v20; per-node source provenance and external-format round-trip are not established.
- **AC-C9-07 — Stale result:** Synchronous native rendering avoids an asynchronous stale render result in the current path; asynchronous preview cancellation and generation races remain unverified.
- **AC-C9-08 — No export overclaim:** Implemented for the current SVG exporter guard; no AI/CDR writer is advertised or implemented.

## 11. Current source and audit boundary

- [Appearance model/catalog](../core/document/include/pikdraw/document/effects/effect_stack.hpp), [Path/Text attachment](../core/document/include/pikdraw/document/model/object.hpp).
- [D2D shadow rejection/budgets](../core/rendering/src/gpu/d2d/d2d_effects_pipeline.cpp), [format capability rows](../modules/interop/src/format_capability_matrix.cpp).
- [C5 current persistence](c5-storage.md), [C8 report scope](c8-import-fidelity.md).
- Regression locations: [compositor](../core/rendering/tests/effects/test_object_compositor.cpp), [appearance migration](../core/document/tests/storage/test_storage_appearance_migration.cpp), [native appearance UI](../apps/desktop/tests/test_desktop_shell_appearance.cpp).

This update records source scope and preserves the native reference equations. It runs no new native tests, pixel oracle or vendor fixtures. The GPU backend still explicitly rejects non-Normal Drop Shadow blend without scene-backdrop support; a CPU semantic implementation does not imply GPU parity.
