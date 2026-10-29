# WS9 — Test và khả năng bảo trì

Owned paths: `apps/desktop/tests/**`, `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp` (chỉ tách state,
sau khi WS2–WS5 merge phần Critical/High), các file shell > 1800 dòng khi tách, `CMakeLists.txt` gốc,
`cmake/PikDrawModules.cmake`, `tooling/native/build_state.py`.

Đọc trước: [code organization](../../development/code-organization.md), [acceptance](../../quality/feature-acceptance.md).

---

## WS9-01 [High] [ ] `test_pikdraw_effect_draft` đang fail và để rác `%TEMP%`

- Bằng chứng (cần xác minh): `build/win-dev/Testing/Temporary/LastTestsFailed.log` có `test_pikdraw_effect_draft`;
  `apps/desktop/tests/test_effect_draft.cpp:134-144` ghi `.bmp` vào `%TEMP%/pikdraw-effect-reactivation-20261017`
  không dọn; tọa độ click là tổng hằng số pixel (`:383-386`, `:520-541`, `:559-586`).
- Cách sửa:
  1. Chạy `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_effect_draft`, đọc log, xác định nguyên nhân
     (có thể do local edits trong `test_effect_draft.cpp` hiện đang `M`).
  2. Ghi ảnh vào `PIKDRAW_TEST_OUTPUT_DIR` qua `ScopedTestOutputDirectory`; chỉ ghi khi bật biến debug.
  3. Thay tọa độ cứng bằng hit-rect lấy từ API layout (thêm accessor test seam trả `RECT` của control theo ID).
- Nghiệm thu: test pass; chạy xong không còn file trong `%TEMP%` do test tạo.

## WS9-02 [Medium] [ ] `assert()` trong test bỏ qua RAII cleanup

- Bằng chứng (cần xác minh): test desktop dùng `cassert`; abort bỏ qua `~ScopedTestOutputDirectory`.
- Cách sửa: thêm header test chung (ví dụ `tests/support/check.hpp` hoặc trong từng module `tests/`) với macro
  `PIKDRAW_CHECK(expr)` ném `std::runtime_error` kèm file:dòng; `main` bắt exception, trả mã lỗi. Thay `assert` dần theo file
  khi chạm vào; bắt buộc cho test tạo file. Lưu ý `assert` bị tắt ở Release (NDEBUG) → test Release hiện không kiểm gì
  ở các assert đó; xác minh và ưu tiên sửa.
- Nghiệm thu: test cố tình fail vẫn dọn output; Release build test thực sự kiểm điều kiện.

## WS9-03 [Medium] [ ] Test desktop phụ thuộc HWND/GDI và tọa độ màn hình

- Bằng chứng (cần xác minh): khoảng 89 executable test trong `apps/desktop/tests` dựng `NativeWindow(1440, 900)`.
- Cách sửa: với logic mới (shortcut registry WS4-05, `has_active_document_interaction` WS5-01, coalescer, budget policy),
  viết test cấp controller/workflow không cần HWND. Giữ một số E2E shell làm smoke. Không xóa test có giá trị.
- Nghiệm thu: test mới chạy không tạo cửa sổ; thời gian suite desktop không tăng.

## WS9-04 [Medium] [x] `NativeWindow` là god-class

- Bằng chứng (đã xác minh): `native_window.hpp` 1407 dòng, hàng trăm field/method, include nhiều header nghiệp vụ,
  `friend struct DropShadowRibbon;` khai báo hai lần (`:705`, `:1387`).
- Cách sửa (theo pha, mỗi pha một commit có test xanh):
  1. Xóa friend trùng; chuyển include nghiệp vụ không cần sang forward declaration + pImpl.
  2. Tách state nhóm thành struct riêng sở hữu bởi `unique_ptr`: `CanvasCacheState`, `NumericInputState`,
     `RecoveryModalState`, `SettingsModalState`, `GuidelineState`, `RasterDockState`, `TextInlineState`.
  3. Tách presenter/controller có hành vi (guidelines, raster dock, settings modal) thành class riêng nhận interface hẹp,
     không truy cập toàn bộ `NativeWindow`.
- Nghiệm thu: header < 900 dòng; đổi một state struct không rebuild toàn bộ `native_window_*.cpp` (kiểm bằng Ninja `-n`).

## WS9-05 [Medium] [ ] File gần trần 2000 dòng

- Bằng chứng (cần xác minh số dòng): `apps/desktop/tests/test_svg_dxf_import.cpp` (~1981), `tests/fixtures/canvas-corpus-v1.json`
  (~1967), `apps/desktop/src/shell/native_window_raster_edits.cpp` (~1964), `native_window_menu_actions.cpp` (~1918),
  `core/document/src/storage/object_codec.cpp` (~1860).
- Cách sửa: tách theo domain trước khi thêm tính năng: raster edits theo nhóm (crop/flip, adjustments, clone/retouch);
  menu actions theo menu (file, edit, object, view, color); test SVG/DXF tách SVG và DXF; object codec tách theo loại object;
  fixture JSON tách theo nhóm case (kiểm gate có quét `.json` không). Hạng mục do agent chạm file đó tiếp theo thực hiện,
  phối hợp owner WS liên quan.
- Nghiệm thu: không file nào > 1800 dòng trong danh sách.

## WS9-06 [Low] [ ] Root `CMakeLists.txt` tiêm source vào `pikdraw_desktop`

- Bằng chứng (cần xác minh): `CMakeLists.txt:35-42` `target_sources(pikdraw_desktop ... embedded_mcp_http_host.cpp)` và link
  `pikdraw_mcp_server_lib`; `apps/desktop/CMakeLists.txt:127-130` export `${CMAKE_SOURCE_DIR}` PUBLIC include.
- Cách sửa: chuyển khai báo vào `apps/desktop/CMakeLists.txt` (hoặc `PikDrawModules.cmake`) sau khi target mcp-server
  được định nghĩa (dùng thứ tự `add_subdirectory` hoặc `if(TARGET ...)`); thu hẹp include root thành PRIVATE hoặc bỏ nếu
  không cần. Registry `desktop` đã có `mcp-server`.
- Test: configure sạch không cần thiết — chỉ build Auto; architecture gate pass.

## WS9-07 [Low] [ ] Lock build bị coi là bận khi file lock hỏng trong 5 giây

- Bằng chứng (cần xác minh): `tooling/native/build_state.py:242-246`.
- Cách sửa: ghi lock nguyên tử (ghi temp rồi `os.replace`) để không có trạng thái nửa ghi; khi JSON hỏng, kiểm PID holder
  (nếu có trong tên/metadata) thay vì chỉ dựa mtime.
- Test: `tests/harness/test_build_broker.py` thêm case lock hỏng → lấy lock sau khi xác minh holder không còn.

## WS9-08 [Low] [ ] Độ phủ test module `in_progress`/`planned`

- `modules/scene3d`, `packages/plugin-sdk`: chỉ có README/AGENTS — đúng trạng thái planned; không thêm code trong bộ này.
- `modules/raster`, `modules/production`: liệt kê test hiện có (theo CMake, không theo tên đoán), map với capability trong
  project-state; capability nào chưa có test → thêm test hoặc hạ khẳng định trong docs (phối hợp WS7).
