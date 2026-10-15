# PikDraw rendering

Status: Implemented in Milestone M1 (ps09-renderer).

Responsibility:
- Viewport Camera (`camera.hpp`): double-precision mm world coordinates, origin subtraction in double before float casting, zoom-to-fit, 20m billboard precision preservation without cancellation.
- Spatial Culler (`culling.hpp`): frustum bounding box query against camera view, filtering out-of-view and hidden objects.
- Frame Target (`frame_target.hpp`): 32-bit RGBA pixel buffer management.
- Backend Interface (`backend.hpp`): IRenderBackend with device loss classification (Healthy, RecoverableLoss, FatalDriverCrash) and frame metrics.
- CPU Software Rasterizer (`software_rasterizer.hpp`): ADR 0002 software fallback baseline rendering Cyber Dark canvas (`#12141a`), white page background, flattened path outlines, text bounding blocks, and Bresenham rasterizer.
- Viewport Renderer Facade (`renderer.hpp`): coordinates camera, culling, and automatic recovery on recoverable device loss.

Dependencies: `core/document`, `core/geometry`, `core/color`, `core/typography`. Zero UI dependencies.
Tests: 3 test suites passing (`test_pikdraw_rendering_camera`, `test_pikdraw_rendering_culling`, `test_pikdraw_rendering_rasterizer`).
