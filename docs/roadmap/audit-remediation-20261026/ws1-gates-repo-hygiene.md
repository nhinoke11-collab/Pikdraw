# WS1 — Gates CI và vệ sinh repo

Owned paths: `core/document/src/storage/native_format*.cpp` (chỉ tách file, không đổi wire format),
`core/document/CMakeLists.txt`, `registry/modules.json`, `docs/architecture/ui-reference/`,
`docs/architecture/workspace-blueprint.png`, `.agent/` (phần legacy), `tooling/__pycache__`, `tests/harness/__pycache__`,
`skills/`, `artifacts/`, `out/`, `scratch/`, `docs/agents/`.

Gate chính của gói:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tooling/native/check.ps1 -Suite architecture
~~~

Trạng thái 26/10/2026: **FAILED 4/13** (đã xác minh bằng lệnh trên).

---

## WS1-01 [Critical] [x] Tách `native_format.cpp` dưới 2000 dòng

- Bằng chứng (đã xác minh): `core/document/src/storage/native_format.cpp` 2083 dòng; gate
  `test_source_files_respect_2000_loc_limit` và alias `..._800_loc_limit` fail.
- Cách sửa:
  1. Đọc cấu trúc file; xác định khối độc lập: (a) encode/decode bảng asset raster v24 (`AssetSourceEntry`,
     `AssetPixelEntry`, dedup), (b) decode container legacy/chunk (`decode_payload_bytes`, kiểm chunk/CRC),
     (c) deserialize phần page/layer v22–v25.
  2. Tách tối thiểu một khối sang file mới cùng thư mục, ví dụ `native_format_assets.cpp` hoặc
     `native_container_decode.cpp`; helper dùng chung khai báo trong header nội bộ `native_format_internal.hpp`
     ở `src/storage/` (không public).
  3. Đăng ký file mới trong `core/document/CMakeLists.txt`.
  4. Không đổi hành vi, thứ tự byte, version hay tên symbol public. Mục tiêu mỗi file 300–1800 dòng.
- Test: `.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_document_storage','test_pikdraw_save_recovery')`
  (kiểm tên target trong `cmake/PikDrawModules.cmake` nếu khác; có thể thêm `test_pikdraw_document_storage_fidelity`).
- Nghiệm thu: gate 2000 LOC pass; round-trip/fixture storage pass; `git diff --stat` chỉ là di chuyển code + CMake.
- Phối hợp: WS2-07/WS2-08 chỉnh deserializer sau khi WS1-01 merge.

## WS1-02 [Critical] [x] Sửa cạnh phụ thuộc test `raster -> rendering`

- Bằng chứng (đã xác minh): `modules/raster/tests/test_raster_edit_preview.cpp:7` include `pikdraw/rendering/...`;
  `registry/modules.json` module `raster` không có `test_dependencies`.
- Cách sửa (chọn một, ưu tiên a):
  a. Nếu test thật sự cần renderer để so pixel: thêm `"test_dependencies": ["rendering"]` vào module `raster`
     (theo mẫu `com-server`) và bảo đảm CMake test link `pikdraw_rendering`.
  b. Nếu chỉ cần kiểu dữ liệu nhỏ: bỏ include, dùng API của `raster`/`document`.
- Test: `-TestTarget test_pikdraw_raster_preview_service` (hoặc target chứa file trên) + architecture gate.
- Nghiệm thu: `test_cross_module_includes_match_registry` pass.

## WS1-03 [High] [x] Đồng bộ dependency registry với CMake

- Bằng chứng (cần xác minh lại dòng): `modules/vector/CMakeLists.txt` link `PUBLIC pikdraw_color` nhưng registry
  `vector` không có `color`; `modules/automation/CMakeLists.txt` không link trực tiếp `pikdraw_geometry` dù registry khai báo.
- Cách sửa: thêm `"color"` vào `vector.dependencies`; thêm `pikdraw_geometry` vào `target_link_libraries`
  của automation. Kiểm DAG gate vẫn pass. Đối chiếu tất cả module bằng script nhỏ tạm (đọc `target_link_libraries`
  so với registry) rồi xóa script sau khi đọc kết quả.
- Nghiệm thu: architecture gate pass; build Auto thành công.

## WS1-04 [Critical] [x] Đưa ảnh ra khỏi `docs/`

- Bằng chứng (đã xác minh): 44 file — `docs/architecture/workspace-blueprint.png` và 43 `.bmp` trong
  `docs/architecture/ui-reference/`; gate `test_docs_contains_only_markdown_and_project_state` fail.
- Cách sửa:
  1. `grep` toàn repo các tham chiếu tới `ui-reference/` và `workspace-blueprint.png`.
  2. Với blueprint: thay bằng sơ đồ Mermaid/bảng trong `docs/architecture/workspace-layout.md`.
  3. Với `.bmp` là ảnh chụp test cũ: xóa theo AGENTS (ảnh chụp tạm phải dọn). Nếu người dùng muốn giữ ảnh tham chiếu
     thiết kế, hỏi trước rồi chuyển ra ngoài `docs/` (ví dụ `tests/fixtures/ui-reference/` có README nêu mục đích),
     không đặt lại dưới `docs/`.
  4. Sửa mọi link gãy do thay đổi.
- Nghiệm thu: gate docs pass; không còn link trỏ tới file đã xóa.

## WS1-05 [High] [ ] Dọn harness multi-agent cũ trong `.agent/`

- Bằng chứng (cần xác minh số lượng): `.agent/prepare_lanes.py`, `.agent/verify_scaffold.py` import
  `tooling/agent_harness.py`/`registry/team.json` đã xóa; `.agent/tasks/` (~273 JSON), `.agent/handoffs/` (~226 JSON),
  `.agent/migrations/`, `.agent/evidence/`.
- Cách sửa:
  1. Liệt kê bằng `Get-ChildItem .agent -Force`; xác nhận không file nào được `tooling/native/*` tham chiếu
     (`grep -r "\.agent/" tooling tests`). Chỉ `native-build.lock`, `native-build/` và toolchains đang dùng được giữ.
  2. Xóa: `prepare_lanes.py`, `verify_scaffold.py`, `tasks/`, `handoffs/`, `migrations/`, `evidence/`.
  3. **Không tự xóa** `.agent/backups/`, `.agent/recovery/`, `.agent/toolchains/{Qt,aqtinstall,skia-src,pikdraw-spike-*,spike-a-*}`:
     liệt kê kích thước và hỏi người dùng (đây có thể là backup/toolchain của người dùng theo AGENTS).
  4. Nếu các path này có trong Git, xóa bằng commit riêng; nếu chỉ local, báo đường dẫn đã xóa.
- Nghiệm thu: `tooling/native/build.ps1 -Status` vẫn chạy; không còn script import module không tồn tại.

## WS1-06 [Low] [ ] Dọn bytecode mồ côi và thư mục rỗng

- Bằng chứng (cần xác minh): `tooling/__pycache__/{agent_harness,harness_context,harness_support}.*.pyc`,
  `tests/harness/__pycache__/test_agent_harness.*.pyc`; thư mục rỗng `skills/*`, `artifacts/evidence/*` (đã xác minh rỗng),
  `docs/agents/`, `out/`, `scratch/`.
- Cách sửa: xóa `.pyc` mồ côi (không xóa `.pyc` của module còn tồn tại); xóa thư mục rỗng. Kiểm `.gitignore`
  có `__pycache__/`, `out/`, `scratch/`, `artifacts/` để tránh tái tạo; nếu thiếu thì thêm.
- Lưu ý: kiểm toán ghi nhận `artifacts/ui-audit/*.bmp`, `out/stress_test.pr`, `scratch/*.cpp|py|ps1` tại một thời điểm,
  nhưng lần kiểm 26/10 thấy `out/`, `scratch/` rỗng và `artifacts/` chỉ còn thư mục rỗng. Kiểm lại trước khi xóa.
- Nghiệm thu: không còn file generated ở root ngoài build tree.

## WS1-07 [Medium] [ ] Dọn output test tồn đọng trong build tree

- Bằng chứng (cần xác minh): khoảng 106 mục `pikdraw-*` trong `build/win-dev/Testing/Temporary/`.
- Cách sửa: chạy `tooling/native/clean-test-artifacts.ps1` (dry-run trước nếu script hỗ trợ), đọc danh sách,
  rồi chạy thật. Không quét/xóa theo extension.
- Nghiệm thu: cleaner báo 0 mục còn lại; build cache nguyên vẹn (`-Status` không yêu cầu reconfigure).

## WS1-08 [Low] [ ] `cdr/test-reports/` nằm ngoài phạm vi cleaner

- Bằng chứng (cần xác minh): `cdr/test-reports/{dxf-worker-import,svg-worker-import,worker-source-probe}.json`.
- Cách sửa: xác định test nào sinh ra các file này; sửa test ghi vào `PIKDRAW_TEST_OUTPUT_DIR`. Xóa 3 report sau khi
  hỏi người dùng (thư mục `cdr/` chứa dữ liệu khách hàng, chỉ xóa đúng 3 file generated).
- Nghiệm thu: chạy lại test liên quan không tạo file trong `cdr/`.
