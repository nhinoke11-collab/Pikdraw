# Contract C3: Viewport State and Rendering Projection

Version: 1.3.3 (opt-in NativeWindow swap-chain integration)
Audited: 2026-10-24 against the working source.
Status: NativeWindow defaults to Direct2D/D3D11 full-frame readback + GDI presentation
and software fallback. `PIKDRAW_DIRECT_PRESENT=1` opts into an HWND/IDXGISwapChain1
path that composes GDI chrome, a D2D canvas viewport, GDI canvas overlays/handles, then
GDI modals, menus, popups and tooltips. A visible smoke screenshot showed a modal card
hidden behind the D2D canvas; those GDI layers were moved after D2D. The updated source
compiled and the canonical Release executable was relinked; app launch and graceful
close succeeded. Computer Use could not capture a fresh screenshot because its trusted
RPC service was unavailable, so the modal fix is not visually verified yet. Release
hidden-window swap-chain tests pass on default and WARP. Visible-present performance,
complete overlay parity, DPI migration and input-to-present acceptance remain open.
Producers: `core/rendering`
Consumers: `apps/desktop`; document snapshots are rendering inputs.

---

## 1. Camera-Relative Coordinate Projection

The model and camera retain binary64 precision. Current backend conversion is:

- **World Geometry**: Maintained strictly in millimeters with binary64 `double` coordinates $(X_w, Y_w)$.
- **Camera Anchor**: Camera center $(C_x, C_y)$ is maintained in binary64 `double` mm.
- **Camera2D projection** computes `(world.x-center.x)*zoom + width/2` and `(center.y-world.y)*zoom + height/2` in double, then returns float screen coordinates.
- **Direct2D object projection** subtracts camera center from the object's translation in double before narrowing the matrix to float. The geometry cache separately casts path-local coordinates/control points to float. It does **not** subtract the camera center from every geometry-sink point.
- Consequently, a path containing very large local coordinates can still lose GPU precision. Object-local rebasing and measured large-coordinate parity remain follow-up work; this contract does not claim jitter has been eliminated.

---

## 2. Viewport State Parameters

Current `Camera2D` provides:

- `center: Point2D`: binary64 world mm.
- `zoom: double`: pixels per mm, clamped to [0.01,1000]; `1.0` is not intrinsically the UI's 100% scale.
- `width`, `height`: signed 32-bit viewport dimensions, minimum 1.

There is no camera rotation or device-pixel-ratio field in this API. Native shell DPI/DIP conversion and the UI's percentage convention belong to the shell. A future rotated viewport needs its own API and culling/inverse-projection changes.

---

## 3. Spatial Culling and Invalidation Bounds

- **Visible World Bounds (AABB)**:
  The viewport calculates an axis-aligned bounding box in world space enclosing the visible view frustum.
- **Hierarchical Culling**:
  The rendering module has a `SpatialIndex` BVH and culling paths. Actual traversal, effect bounds, clipping and index rebuild costs depend on the backend/request; the presence of the index is not an O(log n) or FPS guarantee for every edit.
- **Damage Rectangle (Invalidation)**:
  `DamageTracker` unions old/new visual bounds between the cached and current immutable snapshots. NativeWindow clips that rectangle to the viewport and converts/copies only a small dirty pixel region into its GDI bitmap cache when camera, size, theme and profile are stable. Direct2D still renders and reads back the full frame; GPU partial-render and tile caches are not implemented.

### 3.1 Opt-in DXGI swap-chain prototype

- `D2DRenderBackend::create_swap_chain_target` creates an HWND swap chain from
  the backend's own D3D11 device. For `DxgiSwapChain`, the borrowed
  `native_resource` must be an `IDXGISwapChain1` created on that same device.
  `GpuTexture` continues to mean a borrowed `IDXGISurface`.
- The caller renders through `render_gpu`, then calls
  `present_swap_chain_target`. Resize and recovery invalidate borrowed targets;
  callers reacquire one through `swap_chain_target()`. The capture API is
  synchronous readback for explicit oracle/screenshot use.
- Release tests `test_pikdraw_swap_chain` and `test_pikdraw_swap_chain_warp`
  rendered a red sample, captured pixels, presented, resized, and repeated the
  pixel check. The paired 10k-object Release benchmark (21 samples, hardware,
  hidden HWND) measured PixelBuffer+readback p50/p95 9.639/15.623 ms, swap-chain
  `render_gpu` 5.821/6.959 ms, and `Present(0)` 0.219/0.917 ms. These backend
  numbers do not measure visible FPS, multi-monitor migration, overlay
  clipping/order, or input-to-present latency.
- NativeWindow defaults to full-frame readback plus GDI; the opt-in integration is
  `PIKDRAW_DIRECT_PRESENT=1`. It draws the canvas after the base GDI frame, then draws
  canvas handles and all topmost GDI UI layers. The source compiled and linked in the
  canonical Release app, but its visual behavior has not been re-captured because
  Computer Use was unavailable. Other
  overlay parity, visible W1/W2/W3, DPI/multi-monitor migration and input-to-present
  measurements remain unverified.

---

## 4. Architectural Isolation

- **Render Independence**:
  - The rendering module consumes immutable `DocumentSnapshot` pointers. Under no circumstance may the rendering pipeline modify document nodes, layers, or styling.
  - Level of Detail (LOD) simplifications, raster caches, or tessellation meshes are purely transient render-engine artifacts and must never overwrite source curve data.
- **No Leaked Graphics Handles**:
  - Native graphics library types (e.g. `SkPath`, `SkCanvas`, `QPainter`, `ID3D11Device`, `VkPipeline`) are strictly internal to `core/rendering` and `platform/` implementations. They are prohibited from appearing in `contracts/`, `core/document/`, or storage schemas.

## 5. Tool previews and snap hints (extension 1.1)

These are requirements for each preview producer/consumer. Current native overlays are not one fully asynchronous render service; do not infer every tool already stamps/checks every generation listed here.

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

## 7. Source authority and performance work

- [Camera API](../core/rendering/include/pikdraw/rendering/camera.hpp), [D2D object projection](../core/rendering/src/gpu/d2d/d2d_render_backend.cpp), [geometry cache](../core/rendering/src/gpu/d2d/d2d_geometry_cache.cpp).
- [Spatial index](../core/rendering/include/pikdraw/rendering/spatial_index.hpp), [native canvas presentation](../apps/desktop/src/shell/native_window_canvas.cpp).
- [GPU effect limits](../core/rendering/src/gpu/d2d/d2d_effects_pipeline.cpp), [CPU blur limits](../core/rendering/src/effects/gaussian_blur.hpp).
- Regression locations: [camera](../core/rendering/tests/camera/test_camera.cpp), [compositor](../core/rendering/tests/effects/test_object_compositor.cpp). These links do not claim a fresh test run.

Full-frame readback in NativeWindow, synchronous work, index/cache invalidation and retained raster memory must be measured separately. The Release swap-chain tests establish pixel correctness for the hidden HWND prototype only; they supply no visible FPS, startup-latency or peak-RAM result. Priorities and measurement gates are in the [performance plan](../docs/roadmap/performance-storage-automation.md).
