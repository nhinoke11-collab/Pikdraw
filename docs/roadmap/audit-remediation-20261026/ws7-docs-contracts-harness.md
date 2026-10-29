# WS7 — Docs, contracts và harness

Owned paths: `docs/**` (trừ `docs/architecture/ui-reference/` thuộc WS1), `contracts/**`, `README.md`,
module `README.md`/`AGENTS.md`, `tests/architecture/verify_contracts.py`, `tooling/pikdraw.ps1`.

Nguyên tắc: tài liệu người dùng viết tiếng Việt; contract kỹ thuật có thể tiếng Anh. Mỗi khẳng định version/capability
phải đối chiếu source trước khi ghi. Không tạo handoff/log dưới `docs/`. `docs/project-state.json` ≤ 8000 byte.

Nguồn sự thật version `.pr` (đã xác minh): `core/document/include/pikdraw/document/storage/native_format.hpp` —
payload schema hiện tại v25 (`kLayerHierarchyFormatVersion`), schema v23 vẫn ghi cho tài liệu không có raster source,
v24 cho raster tables; container v23 (<1 MiB), v24 (chunk, schema v23), v25 (schema v24), v26 (schema v25); reader v2–v26.

Gate: `tooling/native/check.ps1 -Suite architecture` (docs gate, project-state size), và `verify_contracts.py` sau WS7-04.

---

## WS7-01 [High] [x] Đồng bộ version `.pr` trên toàn bộ tài liệu

- Bằng chứng (cần xác minh từng dòng): đang ghi v23/v24/v25 cũ ở
  `README.md:8-10`, `docs/project-state.json:46`, `contracts/c5-storage.md` (nhiều chỗ, kể cả `kCurrentFormatVersion = 24`),
  `contracts/README.md:13`, `core/document/README.md:20`, `apps/desktop/README.md:85`,
  `contracts/c8-import-fidelity.md:39`, `contracts/c9-appearance-effects.md:68`,
  `docs/roadmap/performance-storage-automation.md:27,293-295,307-309`.
- Cách sửa: `grep -rn "v2–v25\|v2-v25\|v23–v25\|v23-v25\|writer hiện là v23\|emits PKDR v23"` rồi sửa theo nguồn sự thật;
  bổ sung phần schema v25 (LayerId/group_id/LayerRole, raster-table flag) và container v26 vào C5 (wire layout giống
  `native-pr-format.md` §2). Đoạn lịch sử có ngày giữ nguyên nhưng đánh dấu "lịch sử".
- Nghiệm thu: grep không còn khẳng định hiện trạng sai; C5 khớp header.

## WS7-02 [High] [x] Trạng thái `production` mâu thuẫn

- Bằng chứng (cần xác minh): `README.md:31`, `docs/architecture/architecture.md:39`,
  `docs/development/code-organization.md:9` ghi Planned; registry và project-state ghi `in_progress`.
- Cách sửa: ghi `in_progress` với phạm vi đã có (test sheet, TAC, CGATS/CSV ΔE00, .pdmr, WCS print) và phần còn kế hoạch
  (nesting/CNC/toolpath).

## WS7-03 [High] [x] Khẳng định kỹ thuật cũ trong README module

- `modules/automation/README.md:16-17` "chưa có stable layer IDs" → sai từ schema v25; sửa và kiểm automation có dùng
  `LayerId` chưa (nếu chưa, ghi "model có LayerId; automation vẫn target theo index" — và tạo hạng mục WS5).
- `core/color/README.md:61-62` "page/paint bindings pending" → đã có từ v15/v17.
- `docs/README.md:17` Quick Shapes CAD "chưa triển khai" → "đang triển khai theo giai đoạn" theo spec feature.
- `docs/roadmap/performance-storage-automation.md:44` bảng nghẽn vẫn ghi `query_objects` materialize toàn bộ → mâu thuẫn §P6.
- `contracts/c9-appearance-effects.md:58-60`, `docs/design/features/effects-system-overhaul.md:29` mô tả 2 effect →
  schema có 7 (`effect_stack.hpp`); ghi rõ GPU hiện chỉ hỗ trợ 2 (liên kết WS3-07).
- Mâu thuẫn `modules/interop/README.md:8-12` và `modules/raster/README.md:89-97` về giữ source bytes trong RAM/stream save:
  đọc `raster_source_restore.cpp`, `asset_store.cpp`, đường import assembly rồi ghi một mô tả đúng ở raster README;
  interop README chỉ link sang.
- Dependency trong README lệch registry: `modules/production/README.md:21` thiếu raster; `modules/interop/README.md:28`
  thiếu raster; `platform/workers/README.md:5` thêm plugin-sdk sai. Thay danh sách bằng câu "xem registry" để không lệch lại.

## WS7-04 [High] [x] Trùng số contract C10, index và gate thiếu

- Bằng chứng (đã xác minh): `contracts/c10-agent-automation.md` và `contracts/c10-file-export-production.md`;
  `contracts/README.md` chỉ liệt kê C1–C9; `tests/architecture/verify_contracts.py` chỉ kiểm c1–c7 (cần xác minh).
- Cách sửa:
  1. Đổi `c10-file-export-production.md` → `c11-file-export-production.md`, sửa tiêu đề "Contract C11"; cập nhật mọi link
     (`grep -rn c10-file-export`).
  2. Chuẩn tiêu đề C10 thành `# Contract C10: Agent automation service` (theo quy ước `verify_contracts.py`).
  3. Thêm C10, C11 và `contracts/drawing/README.md` vào `contracts/README.md`, `docs/README.md`.
  4. Mở rộng `verify_contracts.py` sang C1–C11 (đọc danh sách từ glob `c*-*.md` thay vì hardcode) và kiểm số không trùng.
- Test: chạy `verify_contracts.py` (qua runner kiến trúc nếu có) pass; architecture gate pass.

## WS7-05 [Medium] [x] Link gãy và feature spec chưa được index

- `docs/design/features/drawing-annotations-and-detail-views.md:5,127` link `annotation-tools-interaction-properties.md`
  không tồn tại → tạo spec ngắn (menu/action ID, toolbox, property bar, Properties, tooltip, status hint theo AGENTS)
  hoặc bỏ link và gộp nội dung vào file hiện có.
- `docs/README.md` thêm: `drawing-annotations-and-detail-views.md`, `effects-system-overhaul.md`,
  `cut-erase-smart-tools.md`, và bộ kế hoạch `roadmap/audit-remediation-20261026/README.md`.
- Thêm kiểm link tương đối Markdown vào architecture gate (script nhỏ trong `tests/architecture/`, giữ trong suite).

## WS7-06 [Medium] [ ] README module hướng dẫn build vượt runner

- Bằng chứng (cần xác minh): `core/color/README.md:65-69` (`cmake --preset`, `ctest --preset`),
  `core/typography/README.md:21` (`ctest --test-dir BUILD`); `core/geometry/README.md:40`, `apps/desktop/README.md:67`,
  `modules/vector/README.md:72` lặp `-TestRegex` cùng `-TestTarget`.
- Cách sửa: thay bằng `.\tooling\native\build.ps1 -Test -TestTarget <target>`; link tới native-build.md.
- Thêm gate grep: README module không chứa `cmake --build`/`ctest ` trực tiếp (ngoại lệ trong `docs/development/native-build.md`).

## WS7-07 [Medium] [x] `project-state.json` sát trần 8000 byte và thiếu capability mới

- Bằng chứng (đã xác minh): 7963 byte.
- Cách sửa: viết lại gọn — mỗi capability một câu, bỏ chi tiết đã có ở roadmap/contract (link thay vì mô tả);
  thêm: schema v25/container v26, annotations/detail views, CAD circle/arc construction (một phần), knife/eraser, 7 effect
  (GPU 2), trạng thái gates. Mục tiêu ≤ 6500 byte để còn chỗ.
- Nghiệm thu: gate size pass; không có câu nào mâu thuẫn source.

## WS7-08 [Low] [x] Thiếu `AGENTS.md` cấp module

- Bằng chứng (cần xác minh): `modules/automation/`, `apps/mcp-server/`, `apps/com-server/` chỉ có README;
  registry `context` của automation chỉ trỏ README.
- Cách sửa: tạo `AGENTS.md` ngắn (ranh giới, không phụ thuộc desktop, guard ghi, test target) theo mẫu module khác,
  **không lặp workflow** của AGENTS gốc; thêm vào `context` trong registry.

## WS7-09 [Low] [x] `tooling/pikdraw.ps1 doctor` in `agent_system = "codex"`

- Bằng chứng (cần xác minh): `tooling/pikdraw.ps1:52`.
- Cách sửa: đọc giá trị từ `docs/project-state.json` (`workflow.agent_system`) hoặc in "shared AGENTS workflow".

## WS7-10 [Low] [ ] Alias test gây hiểu nhầm

- Bằng chứng (cần xác minh): `tests/architecture/test_architecture_gates.py:136`
  `test_source_files_respect_800_loc_limit = test_source_files_respect_2000_loc_limit`.
- Cách sửa: xóa alias (chạy trùng, tên sai ngưỡng) sau khi kiểm không CI/script nào gọi theo tên cũ.

## WS7-11 [Low] [ ] Cập nhật tài liệu sau mỗi gói

- Khi WS2–WS6 đổi hành vi: cập nhật C2/C3/C6/C7/C9/C10 và roadmap hiệu năng (trạng thái ngắn, không nối nhật ký).
- Khi toàn bộ bộ kế hoạch xong: thêm một dòng trạng thái vào README của thư mục này và giữ file WS như spec lịch sử,
  hoặc xóa nếu người dùng muốn gọn (không giữ checklist lỗi thời làm nguồn sự thật).
