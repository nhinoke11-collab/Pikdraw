# Contract C3: Viewport State and Rendering Projection

Version: 1.3.0
Status: M0 projection invariants remain locked; native Path/Text appearance v1 is implemented with the limits below; broader scopes remain proposed
Producers: `core/rendering`
Consumers: `modules/desktop`, `core/document`

---

## 1. Camera-Relative Coordinate Projection

To eliminate floating-point jitter and precision loss when rendering objects located meters away from the canvas origin on standard GPU hardware:
- **World Geometry**: Maintained strictly in millimeters with binary64 `double` coordinates $(X_w, Y_w)$.
- **Camera Anchor**: Camera center $(C_x, C_y)$ is maintained in binary64 `double` mm.
- **Local Float Conversion**:
  In the current Direct2D path, path/text geometry coordinates are translated relative to the camera center in `double`, and only the offset is downcast to single-precision IEEE-754 `float` before they reach the D2D geometry sink:
  $$x_{\text{local}} = \text{static\_cast<float>}(X_w - C_x)$$
  $$y_{\text{local}} = \text{static\_cast<float>}(Y_w - C_y)$$
- All rendering matrix transformations (zoom, canvas rotation, viewport aspect) operate purely on this camera-relative float coordinate space.

---

## 2. Viewport State Parameters

The viewport snapshot provides:
- `center_x: double`: World horizontal coordinate at viewport center (mm).
- `center_y: double`: World vertical coordinate at viewport center (mm).
- `zoom: float`: Scale multiplier ($1.0 = 100\%$, range: $0.01$ to $1000.0$).
- `rotation: float`: Canvas view rotation in radians counter-clockwise.
- `viewport_width_px: uint32_t`: Physical viewport width in screen pixels.
- `viewport_height_px: uint32_t`: Physical viewport height in screen pixels.
- `device_pixel_ratio: float`: High-DPI scaling factor (e.g. 1.0, 1.25, 1.5, 2.0).

---

## 3. Spatial Culling and Invalidation Bounds

- **Visible World Bounds (AABB)**:
  The viewport calculates an axis-aligned bounding box in world space enclosing the visible view frustum.
- **Hierarchical Culling**:
  Objects or layers whose pre-computed world bounds do not intersect the visible frustum AABB are culled before rasterization or path tessellation. Implemented via `SpatialIndex` (Bounding Volume Hierarchy).
- **Damage Rectangle (Invalidation)**:
  `DamageTracker` unions old/new visual bounds between the cached and current immutable snapshots. NativeWindow clips that rectangle to the viewport and converts/copies only a small dirty pixel region into its GDI bitmap cache when camera, size, theme and profile are stable. Direct2D still renders and reads back the full frame; GPU partial-render and tile caches are not implemented.

---

## 4. Architectural Isolation

- **Render Independence**:
  - The rendering module consumes immutable `DocumentSnapshot` pointers. Under no circumstance may the rendering pipeline modify document nodes, layers, or styling.
  - Level of Detail (LOD) simplifications, raster caches, or tessellation meshes are purely transient render-engine artifacts and must never overwrite source curve data.
- **No Leaked Graphics Handles**:
  - Native graphics library types (e.g. `SkPath`, `SkCanvas`, `QPainter`, `ID3D11Device`, `VkPipeline`) are strictly internal to `core/rendering` and `platform/` implementations. They are prohibited from appearing in `contracts/`, `core/document/`, or storage schemas.

## 5. Tool previews and snap hints (extension 1.1)

- Tool preview inputs are immutable values stamped with document ID/epoch/revision, tool generation, gesture generation, and camera generation. Preview drawing does not mutate a document snapshot, increment revision, or allocate document undo records.
- Snap candidates and anchors remain world/local binary64 values until the renderer subtracts the camera anchor. Screen markers, handles, hit radii, and acquisition/release radii are expressed in DIP and converted using the current device-pixel ratio; they are not model-space tolerances.
- Preview overlays, tracking lines, snap markers, numeric HUDs, and selection handles are transient. They may be cached only with keys that include their source generations and camera/viewport state; they must never replace source curves or persist as document geometry.
- A result stamped with an old document, tool, gesture, camera, configuration, or capability generation is discarded before display. UI must not show it as a confirmed snap.

## 6. Native appearance effects (implementation status, extension 1.3)

This section records the renderer contract and current implementation boundary for C9's native basic appearance profile. Direct2D 1.1 (hardware or WARP) and the software reference renderer evaluate Path/Text appearance effects. Image objects draw their bitmap and Group objects traverse children with inherited transforms; those object types do not carry an `Appearance` payload in the current document model. A non-Normal blend on a Drop Shadow node is rejected by the GPU backend because the effect requires scene-backdrop input not yet supported by that node path.

- Rendering consumes an immutable document snapshot. Filter evaluation creates transient surfaces and never writes pixels, bounds, caches, or normalized parameters back into the document.
- The authored pipeline is source fill/stroke on transparent pixels, with their own opacity factors; ordered effect nodes in object-local millimeters; the object affine transform; object opacity once over the entire premultiplied result; the object blend mode against the painter-order backdrop; then viewport clipping. Effect bounds do not replace source geometry bounds used by selection, hit-testing, or production geometry.
- A Drop Shadow node with a non-Normal shadow blend is backdrop-dependent. It is evaluated as the final enabled effect: blend the tinted shadow branch with the actual painter-order target beneath the object, place the source graphic above that result using the object blend mode, then apply object opacity once to the complete source/shadow composite. Any later enabled node makes that graph invalid; later disabled nodes are ignored. The source order is retained and invalid stacks are rejected rather than silently reordered or flattened.
- Maintain separate source-geometry, effect-output, and target-clipped bounds. Propagate output bounds through each node in order. Gaussian Blur expands by its declared finite support on each local filter axis. Drop Shadow unions the input with the blurred alpha branch translated by the authored local offset. Convert the actual rounded pixel support and resampling footprint back through the view/object transform conservatively; a broad world cull bound must not omit an output pixel.
- Culling tests effect-output bounds against the viewport. Thus an object whose source geometry is outside the viewport must still render when its blur or shadow intersects it. An effect is not clipped to the source geometry bounds; transparent samples outside the source are part of the declared input.
- Damage after an appearance edit is the union of old and new visual-output bounds, clipped only to the render target. Reordering, disabling, deleting, changing a parameter, or changing the object transform invalidates every affected downstream node and target tile. Source-geometry damage remains available separately.
- Reverse ROI evaluation starts from the requested output region and walks the graph backward: apply inverse object transform, inflate by the blur support, and translate the shadow branch by the negative authored offset before inflating its blur support. Checked arithmetic and conservative rounding are required at every step.
- A transient surface or cache key includes the document ID/epoch/revision, object and node IDs, node type/schema, enabled state, parameters, order, object transform, viewport scale/DPI, color profile, sampling quality, and job generation wherever those values affect output. A result with an old revision, selection, camera, profile, or job generation is discarded.
- Compute dimensions, halo, pixel count, byte count, and estimated filter work with checked 64-bit arithmetic before reservation. The software target limits remain 16,384 pixels per side and 8,388,608 pixels total. The Gaussian CPU reference enforces 8,192 pixels per side, 4,194,304 pixels, 256 px maximum sigma, 268,435,456 pixel-taps and 160 MiB scratch; Drop Shadow caps its own peak scratch at 224 MiB. The compositor also preflights cumulative filter work and bounds its reusable output cache to 262,144 pixels. These are per-render request limits, not an aggregate scheduler or a measured RSS guarantee. The current software render path is synchronous; concurrent-job reservation, tiled seam proof, peak process memory and pressure behavior remain unverified.
- Budget refusal, unsupported transforms, allocation failure, or cancellation returns an explicit incomplete/budget diagnostic. The renderer must not report a fully rendered frame after silently dropping an effect. A downsampled preview carries a preview-quality state and is never reused as final output.
