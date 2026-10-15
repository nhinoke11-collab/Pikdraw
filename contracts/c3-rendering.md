# Contract C3: Viewport State and Rendering Projection

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `core/rendering`
Consumers: `modules/desktop`, `core/document`

---

## 1. Camera-Relative Coordinate Projection

To eliminate floating-point jitter and precision loss when rendering objects located meters away from the canvas origin on standard GPU hardware:
- **World Geometry**: Maintained strictly in millimeters with binary64 `double` coordinates $(X_w, Y_w)$.
- **Camera Anchor**: Camera center $(C_x, C_y)$ is maintained in binary64 `double` mm.
- **Local Float Conversion**:
  Prior to submitting coordinates to the rasterizer (Skia, Vulkan, Direct3D), coordinates are translated relative to the camera center in `double`, and only the offset is downcast to single-precision IEEE-754 `float`:
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
  Objects or layers whose pre-computed world bounds do not intersect the visible frustum AABB are culled before rasterization or path tessellation.
- **Damage Rectangle (Invalidation)**:
  Document mutations compute a dirty bounding rectangle `dirty_rect_world: Rect2D`. The renderer only repaints or invalidates tile caches intersecting this damage rectangle.

---

## 4. Architectural Isolation

- **Render Independence**:
  - The rendering module consumes immutable `DocumentSnapshot` pointers. Under no circumstance may the rendering pipeline modify document nodes, layers, or styling.
  - Level of Detail (LOD) simplifications, raster caches, or tessellation meshes are purely transient render-engine artifacts and must never overwrite source curve data.
- **No Leaked Graphics Handles**:
  - Native graphics library types (e.g. `SkPath`, `SkCanvas`, `QPainter`, `ID3D11Device`, `VkPipeline`) are strictly internal to `core/rendering` and `platform/` implementations. They are prohibited from appearing in `contracts/`, `core/document/`, or storage schemas.
