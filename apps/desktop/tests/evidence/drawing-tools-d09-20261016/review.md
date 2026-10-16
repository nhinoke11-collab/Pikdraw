# D09 — Native Line/Polyline evidence

Revision under test: task branch `agent/codex/drawing-native-tools-d09-20261016`.
The feature is built and verified in this worktree; it has not been integrated into `main`.

## Behavior checked

- Toolbar Line/Polyline actions, icon tooltips, `L`, `Shift+L` and `F3`; existing `F8`,
  `F10` and `Shift+F2` remain unclaimed.
- Native pointer routing places Line endpoints and Polyline nodes. `Enter` commits,
  `Backspace` removes the last point, and `Escape` cancels a fresh empty draft.
- The UI test enters an absolute first point through `WM_CHAR` and D06 syntax. The
  controller test checks a relative distance/angle point, asynchronous index readiness,
  and an actual snap back to an existing Endpoint.
- A translated `WM_CHAR` after the `L` tool binding is ignored; save/reopen checks the
  Polyline's first source node is still exactly `(15, 15) mm`.
- Snap settings persist outside document data. The source/grid candidates are limited
  to Endpoint, Source node, Line midpoint and Grid; unsupported relation/curve options
  are disclosed in the panel.
- Native save/reopen keeps the committed Line and editable three-node Polyline.

## Build and checks

Toolchain: MSVC 19.44.35229.0, Windows SDK 10.0.19041.0, CMake 3.31.8, C++20, Debug and
Release with `/utf-8`.

| Check | Result |
|---|---|
| `pikdraw.ps1 doctor` | `ok=true`, 15 modules |
| Harness unit tests | 27/27 passed |
| Full Debug CTest | 42/42 passed |
| Final Debug D09 UI test | passed |
| Final Release D09 controller and UI CTest | 2/2 passed |
| Native Release UI run | passed; screenshots and native save/reopen verified |
| `git diff --check` | pass on final source/docs/evidence diff |

## Screenshots

- [Line preview and Snap panel](line-preview-snap-panel.bmp): Line candidate, mm/length/angle
  HUD, Grid and direction controls, plus disclosed unsupported snaps.
- [Native save/reopen](drawing-tools-save-reopen.bmp): two editable objects after reopen,
  including the three-node Polyline.

## Release CPU benchmark

Workload: 1,000 paths / 101,000 path elements, 800×600 software raster; three sequential
runs, each with three warmups. Machine: Intel Core i7-3537U, 4 logical processors,
8,452,857,856 bytes installed RAM. Values below are p95 CPU wall time in milliseconds.

| Metric | Run 1 | Run 2 | Run 3 |
|---|---:|---:|---:|
| Source redraw | 35.425 | 52.354 | 57.046 |
| Point pick | 15.759 | 14.306 | 15.547 |
| Window selection | 17.355 | 15.569 | 15.755 |
| Crossing selection | 19.571 | 18.639 | 21.273 |
| Cached native CPU paint | 12.429 | 14.088 | 10.459 |

Raw results: [run 1](benchmark-release-run-1.json), [run 2](benchmark-release-run-2.json),
[run 3](benchmark-release-run-3.json).

This existing editor benchmark is not a Line/Polyline microbenchmark. It measures CPU
wall time and hidden native paint; it does not measure present intervals, input-to-visible
latency, GPU rendering, 1080p/60 Hz, or a 100,000-object document. Source redraw exceeded
both the 16.7 ms and 33.3 ms p95 budgets in all three runs. This delivery does not establish
the product's 60 FPS target.

## Code-size check and limits

Manual nonblank-line counts are below the configured caps. The largest changed desktop
implementation is `native_window.cpp` at 491 lines (500 cap); the event adapter is 481.
The drawing input adapter is 66 lines, controller 349, preferences 189, and each new test
is under 150. No LOC checker is claimed as an automated gate.

Win32/GDI is the current native adapter. D09 does not add Pen/Node editing, curve/relation
snaps, full CAD constraints, ToolSession crash-recovery serialization, or GPU qualification.
