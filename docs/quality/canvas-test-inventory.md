# Kiểm kê source kiểm thử trước nâng cấp hiệu năng canvas

Ngày kiểm kê: 2026-10-16. Đây là inventory trước khi khôi phục test, không phải kết quả kiểm thử mới.

## Baseline tại thời điểm kiểm kê

- `HEAD`: `11512046d23fac08059ecd37382179283b4530a0`.
- Working tree có 183 mục thay đổi/untracked; 65 source test tracked đang bị xóa. Không reset, checkout hoặc ghi đè các thay đổi đó.
- Có 72 file test/support cần khôi phục để bật lại bộ kiểm thử: 65 file tracked bị xóa, 5 file test/support không tracked trong baseline status và 2 source audit tích hợp.
- Binary canonical `build/win-dev/apps/desktop/pikdraw.exe`: 46,116,352 bytes, SHA-256 `dea40828baea04f587e190e705edf2d6482aab4ad8b129bdea05de8a3216d416`.
- `.agent/native-build.lock` ghi PID 11756; PID đó không tồn tại và file khóa mở độc quyền thành công khi kiểm tra. Xem như khóa cũ; để build runner xử lý, không xóa thủ công.
- Kết quả CTest 63/63 tại `.agent/migrations/20261016/ctest-final-63-of-63.log` là lịch sử trước khi xóa source, không thay thế lần chạy mới sau khi khôi phục.
- Đánh giá chạy app trước đó nằm ở `artifacts/evidence/canvas-performance-runtime-20261016/runtime-evaluation-vi.md`. Các lần chạy đo khởi động/paint/chụp ảnh không đo FPS đầu vào/present thật.

## Nguồn phục hồi đã xác định

- Archive nền `.agent/migrations/20261016/root-source-before.zip`, SHA-256 `58620351ffa5dd41ab698e36da27742c6fb3a057c7d19178df5286e8caecf1d5`.
- Desktop: `.agent/snapshots/migration-consumer-checks-20261016/source-afeghctl/files/`.
- Color, phần lớn document, rendering, interop và vector: archive nền phía trên.
- Geometry: `.agent/snapshots/migrate-geometry-20261016/source-bpctl_7h/files/`.
- Typography: `.agent/snapshots/migrate-typography-20261016/source-mbzcd1sh/files/`.
- Workers: `.agent/snapshots/migrate-worker-policy-20261016/source-5lxqlk0n/files/`.
- Architecture/harness: `.agent/snapshots/migration-build-cleanup-20261016/source-5wud9kxq/files/`.
- `tooling/native/quality/test_assertions.hpp`: `.agent/snapshots/test-source-cleanup-20261016/source-3xoy9fg_/files/`.
- Hai audit source: `.agent/migrations/20261016/previews/codex-m1-integrated-review-20261015/tests/integration/m1-audit/`; các preview đã so khớp có cùng hash.
- Ngoại lệ giữ nguyên staged intent: `core/document/tests/storage/test_storage_fidelity.cpp` lấy từ Git index, SHA-256 `d97905170bd3635151c671ba082e4e2c7f2ab8b9568dbc287e7d8b47c94b64c8` (22,748 bytes). Archive có bản cũ khác hash; không dùng bản archive để thay thế index.
- `modules/vector/tests/precision/snap/test_snap.cpp` có cùng SHA-256 `0ae0c0b8fa6371692d13bd4c4f40c7381d23bf994258f6b1f8ecb45c1ae7741d` ở archive và snapshot snap-stability.

## Danh sách 72 file

### Desktop (10)

- `apps/desktop/tests/benchmark_editor_tools.cpp`
- `apps/desktop/tests/test_canvas_view.cpp`
- `apps/desktop/tests/test_desktop_actions.cpp`
- `apps/desktop/tests/test_desktop_composition.cpp`
- `apps/desktop/tests/test_desktop_reload.cpp`
- `apps/desktop/tests/test_desktop_shell.cpp`
- `apps/desktop/tests/test_drawing_tool_controller.cpp`
- `apps/desktop/tests/test_drawing_tool_ui.cpp`
- `apps/desktop/tests/test_editor_tools.cpp`
- `apps/desktop/tests/test_vertical_slice_e2e.cpp`

### Color (6)

- `core/color/tests/preview/test_icc_preview.cpp`
- `core/color/tests/preview/test_preview.cpp`
- `core/color/tests/profiles/test_profiles.cpp`
- `core/color/tests/test_harness.hpp`
- `core/color/tests/test_main.cpp`
- `core/color/tests/values/test_color_values.cpp`

### Document (7)

- `core/document/tests/async/test_stale_proposals.cpp`
- `core/document/tests/async/test_writer_safety.cpp`
- `core/document/tests/commands/test_commands.cpp`
- `core/document/tests/snapshots/test_persistent_map.cpp`
- `core/document/tests/snapshots/test_snapshots.cpp`
- `core/document/tests/storage/test_storage.cpp`
- `core/document/tests/storage/test_storage_fidelity.cpp`

### Geometry (8)

- `core/geometry/tests/acceptance/test_20m_billboard.cpp`
- `core/geometry/tests/curves/test_curve_analytic.cpp`
- `core/geometry/tests/curves/test_curves.cpp`
- `core/geometry/tests/operations/test_operations.cpp`
- `core/geometry/tests/operations/test_subset_safety.cpp`
- `core/geometry/tests/primitives/test_primitives.cpp`
- `core/geometry/tests/test_harness.hpp`
- `core/geometry/tests/test_main.cpp`

### Rendering (5)

- `core/rendering/tests/camera/test_camera.cpp`
- `core/rendering/tests/culling/test_culling.cpp`
- `core/rendering/tests/rasterizer/test_authoring_path_adapter.cpp`
- `core/rendering/tests/rasterizer/test_pixel_semantics.cpp`
- `core/rendering/tests/rasterizer/test_rasterizer.cpp`

### Typography (4)

- `core/typography/tests/layout/test_layout.cpp`
- `core/typography/tests/shaping/test_shaping.cpp`
- `core/typography/tests/test_harness.hpp`
- `core/typography/tests/test_main.cpp`

### Interop (6)

- `modules/interop/tests/adapters/test_dxf_adapter.cpp`
- `modules/interop/tests/adapters/test_dxf_worker_adapter.cpp`
- `modules/interop/tests/adapters/test_svg_adapter.cpp`
- `modules/interop/tests/adapters/test_svg_worker_adapter.cpp`
- `modules/interop/tests/capabilities/test_capabilities.cpp`
- `modules/interop/tests/writers/test_svg_exporter.cpp`

### Vector (11)

- `modules/vector/tests/gestures/test_gestures.cpp`
- `modules/vector/tests/interaction/test_input_router.cpp`
- `modules/vector/tests/interaction/test_tool_session.cpp`
- `modules/vector/tests/precision/numeric/test_numeric_input.cpp`
- `modules/vector/tests/precision/snap/test_direction_snap.cpp`
- `modules/vector/tests/precision/snap/test_snap.cpp`
- `modules/vector/tests/selection/test_editor_selection.cpp`
- `modules/vector/tests/selection/test_selection.cpp`
- `modules/vector/tests/test_support.hpp`
- `modules/vector/tests/tools/line/test_path_drawing_tool.cpp`
- `modules/vector/tests/tools/test_shape_tools.cpp`

### Workers (7)

- `platform/workers/tests/dxf_worker_import_tests.cpp`
- `platform/workers/tests/live_containment.cpp`
- `platform/workers/tests/production_policy_tests.cpp`
- `platform/workers/tests/spike_c_tests_main.cpp`
- `platform/workers/tests/svg_worker_import_tests.cpp`
- `platform/workers/tests/v2_integrity_tests.cpp`
- `platform/workers/tests/worker_location.hpp`

### Architecture và harness (5)

- `tests/architecture/fixtures/negative_ui_include/sample.cpp`
- `tests/architecture/test_architecture_gates.py`
- `tests/architecture/verify_contracts.py`
- `tests/harness/test_agent_harness.py`
- `tests/harness/test_native_workflow.py`

### Assertion helper (1)

- `tooling/native/quality/test_assertions.hpp`

### Independent M1 audit (2)

- `tests/integration/m1-audit/document_regressions.cpp`
- `tests/integration/m1-audit/fidelity_regressions.cpp`

## Kết quả P00 sau khi chạy lại

- P00b khôi phục đủ 72/72 đường dẫn từ các nguồn đã ghi; hash sau chép khớp manifest và trạng thái staged của `test_storage_fidelity.cpp` được giữ nguyên.
- `tooling/pikdraw.ps1 doctor`: `ok=true`, 15 module, không có issue.
- Python harness: 35/35 test đạt.
- Debug: `tooling/native/build.ps1 -Configuration Debug -Test` hoàn tất build incremental; CTest đạt 63/63 trong 46,20 giây.
- Release: `tooling/native/build.ps1 -Configuration Release -Test` dựng 250/250 mục trong build tree chuẩn; CTest đạt 63/63 trong 39,49 giây.
- Nhóm regression P00d chạy riêng trong Debug đạt 6/6, gồm capability, SVG adapter, worker, live containment, desktop shell và vertical-slice E2E.
- Các test oracle lỗi thời đã được cập nhật theo capability và menu contract hiện hành; không đổi logic sản phẩm và không tắt assertion.
- Kết quả P00 chỉ xác nhận khôi phục source, build và test; chưa đo FPS hoặc độ trễ thao tác canvas.

P00 đã hoàn tất. Tiếp theo là P01: thêm trace và benchmark đo input-to-present, thời gian render/cull, bộ nhớ và workload lớn theo kế hoạch `docs/roadmap/canvas-performance-upgrade-20261016/`.
