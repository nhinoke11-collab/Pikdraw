# PikDraw raster

Status: implementation in progress. The module has an immutable disk-backed
source store with bounded streaming copy, incremental SHA-256 fingerprints for
embedded assets, cancellable bounded verification, and 64-bit random reads, a shared
document asset descriptor, an LRU tile cache with RAII memory reservations,
and native persistence for source descriptors and the crop/flip/rotate/exposure/
Light/Color Balance/Channel Mixer/Levels/master-HSL/selective-HSL/five-anchor Curves/relative White Balance/Vibrance/Clone Stamp edit graph. Color Balance shifts RGB axes in linear-light sRGB over selected shadow/midtone/highlight luminance bands, optionally rescales luminosity, supports node masks, and preserves alpha. Channel Mixer applies a validated 3×3 linear-light sRGB matrix with mask support and preserves alpha. Clone Stamp stores one normalized soft dab, snapshots its bounded source patch before writing, composites straight alpha correctly, and supports RGBA8/RGBA16LE. Light combines linear-sRGB exposure and contrast with separate tonal-range controls; its PikDraw curve is versioned and does not claim compatibility with other editors. HSL rotates hue and scales saturation/lightness on
encoded sRGB samples, preserves alpha, and leaves neutral gray unchanged for
hue-only edits; White Balance applies relative temperature/tint gains in linear-light sRGB,
not camera Kelvin metadata. Vibrance increases muted encoded-sRGB colors more strongly than vivid
colors, leaves neutral grays unchanged, and does not perform skin-tone detection. Selective HSL
targets six feathered encoded-sRGB hue bands, preserves neutral grays and alpha, and stores a
distinct node per band. The document can
also retain an optional straight-alpha, encoded sRGB RGBA16LE working buffer
beside its RGBA8 renderer preview. Graph schema v17
persists that buffer and migrates older graph schemas; asynchronous preview evaluates
the 16-bit buffer before reducing to RGBA8 at the renderer boundary. This is
covered with pixel and native save/reopen tests using directly populated model
buffers. The interop decoder can stream WIC rows from a 64-bit byte source
into worker chunks. Product import has no fixed source, decoded-image, worker,
preflight or aggregate staging byte quota. Raster record fields retain their
32-bit representation ceiling. Worker decode/chunk reservations and host
full-frame pixel/source staging pause at 95% physical-RAM use, resume when
system headroom returns, and share a cumulative five-minute wait allowance per
import. A CPU evaluator applies the saved graph to straight-alpha sRGB RGBA8 or
RGBA16LE, with cancellation polling at most every 256 pixels in transform loops and
every 64 KiB during row copies. This bounds work between polls but does not
guarantee wall-clock latency; intermediate buffers remain memory-accounted.
When the graph starts with an enabled crop node, the evaluator copies only the crop rows
directly from the immutable source view and reserves the cropped output, avoiding
a source-sized working copy. Pointwise tone/color nodes update the evaluator-owned
frame in place, avoiding another full-frame allocation for each adjustment;
geometric transforms still allocate their output frame. Other graph orders still use
full-frame staging.
Native Windows PNG16 import can now preserve RGBA16LE through WIC decoding, worker/IPC,
desktop commit, and native `.pr` save/reopen; the preview remains RGBA8. A
lossless RGBA16LE TIFF writer API is implemented and round-trip tested directly.
The raster analysis API computes exact-code histograms for all four U8/U16
channels, supports per-pixel mask coverage, clipping/mean statistics, a result
memory limit and cancellation. It retains exact U16 bins and provides a 256-bin
display projection; packed RGBA8 `ImageObject` pixels are analyzed without a
full-frame byte copy. `RasterHistogramService` supports both source and edited
RGB histograms asynchronously, keyed to immutable document/source/mode state.
Edited mode evaluates RGBA8 packed samples directly into one budgeted frame or
preserves RGBA16LE samples, with a 256 MiB working-memory limit. Properties shows
the edited RGB chart, dotted alpha coverage, and labels that ICC/proof are not
applied. Masks and beyond-quota tile analysis remain open. Pixel and worker-service
tests cover source-versus-edited values, channel order, U16 precision and stale-job
cancellation.
The Release `test_pikdraw_raster_assets` regression also uses a sparse linked TIFF
larger than 4 GiB and verifies random reads at a 64-bit offset without allocating
the logical file size. This validates only the asset-store API; renderer/export
integration and linked-source lifecycle remain open.
Desktop page export still composites and encodes RGBA8, so 16-bit export is not
yet connected to the export workflow. The raster export worker evaluates supported 8-bit edits
before page composition. An asynchronous, revision-keyed preview cache supplies
evaluated frames to the software and Direct2D renderers; it is not
full-resolution source/tile evaluation. Desktop source-store integration is
partial: import stages encoded bytes in a per-import session, and `.pr` save encodes
the payload through a bounded 1 MiB buffer, writes compressed chunk records directly
to the exclusive temp file, and patches container size/count/CRC after completion.
Atomic save no longer buffers a second full serialized payload; load still buffers the
whole `.pr` and decoded payload. Interop exposes region-row decoding for WIC-supported frames
and the supported uncompressed BigTIFF profile. Tile request scheduling, edit-graph evaluation
on tiles, cache/render integration and non-sRGB native samples remain unimplemented.
Adjustment masks can be immutable grayscale bitmap coverage or compact normalized geometric
regions: feathered rectangles, left-to-right linear gradients, and radial gradients. Pointwise
U8/U16 adjustments blend through mask coverage, density and inversion; graph validation checks
mask dimensions at the node and preview keys include mask data. The UI exposes local Exposure
with these three mask shapes and supports masked Light and Channel Mixer nodes in the edit graph.
Linear gradient masks store an angle (0° left-to-right, positive clockwise) and project coverage
across the mask bounds; older graph schemas read the angle as 0°. Paint/refine strokes, mask
overlays, vector masks, and attaching a mask to every adjustment from the UI are not implemented.

Exposure EV is also available as a shared numeric field in the selected-bitmap property bar and
Properties inspector. Both read the same latest Exposure graph node and update it through the same
undoable document command; an existing node keeps its ID, enabled state, and mask. Editing requires
one unlocked bitmap with an sRGB preview basis. Light, Color Balance, HSL/Vibrance, Levels, Curves,
and relative White Balance expose supported fields in Properties. Selective HSL and Channel Mixer
are reachable from the More tab through their existing dialogs; they do not yet have inline shared
fields there. Native visual/focus/DPI behavior remains unverified.
Selected bitmap-master export can write CMYK16 TIFF with the exact embedded
output profile; Release writer and desktop-export tests verify the ICC metadata
and white-composited CMYK samples. This path currently uses classic TIFF offsets
and rejects output at the 4 GiB boundary; page export remains RGBA8 and is not
streamed or tile-based.

Desktop import stages encoded raster sources by bounded writes into a per-import
`TemporaryRasterAssetSession` and attaches a disk-backed reader provider plus SHA-256
descriptor to the image snapshot. After staging, the import assembly releases its
encoded vector and RAM reservation; snapshots and Undo retain the provider and its
session. Native `.pr` serialization can read provider-only sources in 64 KiB blocks,
and reopen restores bytes from the validated container into a temporary provider,
then releases the decoded source-table vector. Native save emits the payload
incrementally to the exclusive temp file and patches container metadata/CRC after
encoding; `NativeFormat::serialize()` still returns an in-memory vector for callers
that request it. Open still buffers the complete `.pr` and decoded payload; this does
not provide bounded whole-document memory. Renderer, tile
evaluation, full-resolution export and linked-source lifecycle do not use the
provider yet.
The worker reports aggregate copied/total source bytes through the coalesced import
progress channel and the native status message; that does not reduce memory use.

Kế hoạch triển khai: [import ảnh lớn, asset trên đĩa và nền tảng sửa ảnh](../../docs/roadmap/raster-import-large-assets-20261021.md). P0–P7 bao gồm nguồn stream, tile/LOD, graph không phá hủy, native streaming và nghiệm thu GiB; tài liệu này chưa bật capability runtime của module.

Responsibility: Basic non-destructive image edits, masks and image asset management.
Dependency authority: `registry/modules.json` (document, color).

Public interfaces must be versioned where consumed across processes/plugins.
Do not depend on desktop UI. See the scoped AGENTS.md before a change.
# Profile conversion

`convert_image_profile_to_rgb` converts decoded RGBA8 or RGBA16LE pixels from an unresolved embedded
RGB ICC profile into a validated RGB destination. It preserves straight alpha, supports batch
cancellation/progress, and returns new immutable buffers; document publication belongs to a command.
The current desktop consumer exposes only conversion to system sRGB with Relative Colorimetric intent
and BPC off. It preserves source bytes/profile and the edit graph. This does not implement profile
assignment, CMYK image samples, monitor/proof conversion, channel separations, or physical print proof.
