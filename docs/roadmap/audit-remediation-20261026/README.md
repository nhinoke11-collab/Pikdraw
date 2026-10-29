# Kế hoạch khắc phục sau kiểm toán kiến trúc 25/10/2026

Bộ kế hoạch này chuyển kết quả kiểm toán toàn diện (kiến trúc/hiệu năng, UI/UX và đồng bộ, docs/harness,
nợ kỹ thuật/bảo mật/test) thành các gói việc giao trực tiếp cho agent. Workflow duy nhất vẫn là
[AGENTS](../../../AGENTS.md); tài liệu này **không** tạo task board, lease, claim hay handoff.

## Cách dùng

1. Đọc AGENTS, `git status`, rồi mở đúng file gói (WS) được giao. Không nạp toàn bộ thư mục.
2. Mỗi hạng mục có ID (`WSx-NN`), mức độ, bằng chứng `file:dòng`, bước xác minh, cách sửa, test và tiêu chí nghiệm thu.
3. **Luôn xác minh trước khi sửa.** Số dòng là snapshot ngày 25–26/10/2026, có thể đã lệch do local edits.
   Hạng mục ghi `Cần xác minh` là phát hiện chưa được đối chiếu lần hai; nếu source không khớp, ghi lại
   "không tái hiện" trong báo cáo lượt làm và bỏ qua, không sửa theo suy đoán.
4. Build/test qua `tooling/native/build.ps1` (xem [native build](../../development/native-build.md)); chọn
   `-TestTarget` đúng phạm vi, gom Release vào một đợt. Dọn output tạm bằng `tooling/native/clean-test-artifacts.ps1`.
5. Khi xong một hạng mục: báo lệnh, kết quả, paths đổi và giới hạn trực tiếp; cập nhật tài liệu sở hữu capability
   và `docs/project-state.json` nếu hành vi thực đổi (giữ ≤ 8000 byte). Đánh dấu `[x]` hạng mục trong file WS tương ứng.
6. Commit chỉ phần đã kiểm chứng, tách khỏi local edits của người khác.

## Các gói việc và owned paths

Owned paths tách để nhiều agent chạy song song mà không sửa chồng cùng boundary.

| Gói | File | Owned paths chính | Ưu tiên cao nhất |
| --- | --- | --- | --- |
| WS1 | [Gates CI và vệ sinh repo](ws1-gates-repo-hygiene.md) | `core/document/src/storage/native_format*.cpp`, `registry/`, `docs/architecture/ui-reference/`, `.agent/` (phần legacy), `tooling/__pycache__` | Critical |
| WS2 | [Toàn vẹn document, Undo và validation](ws2-document-undo-integrity.md) | `core/document/src/{commands,snapshots}`, `core/document/include/.../snapshots`, `apps/desktop/src/tools/node_tool_controller.cpp`, `modules/vector/src/tools/node` | Critical |
| WS3 | [Rendering pipeline và GPU](ws3-rendering-pipeline.md) | `core/rendering/**`, `apps/desktop/src/shell/native_window_{canvas,direct_present}.cpp` | Critical |
| WS4 | [Input, phím tắt và UI desktop](ws4-desktop-input-ui.md) | `apps/desktop/src/shell/native_window_{events,shortcuts*,settings,measurement,recovery}.cpp`, `native_style_dialog*` | High |
| WS5 | [Automation MCP/COM và IPC](ws5-automation-ipc.md) | `apps/desktop/src/automation/**`, `apps/mcp-server/**`, `apps/com-server/**` | High |
| WS6 | [Bảo mật worker và importer](ws6-security-workers-interop.md) | `platform/workers/**`, `modules/interop/src/adapters/**`, `modules/production/src/measurement/**` | High |
| WS7 | [Docs, contracts và harness](ws7-docs-contracts-harness.md) | `docs/**` (trừ ui-reference), `contracts/**`, module `README.md`/`AGENTS.md`, `tests/architecture/verify_contracts.py`, `tooling/pikdraw.ps1` | High |
| WS8 | [Bộ nhớ, raster, typography và co giãn phần cứng](ws8-memory-raster-scaling.md) | `modules/raster/src/**`, `core/typography/src/shaping/**`, `core/color/src/preview/**`, `core/document/src/storage/save_recovery.cpp` | Medium |
| WS9 | [Test và khả năng bảo trì](ws9-tests-maintainability.md) | `apps/desktop/tests/**`, `apps/desktop/include/.../native_window.hpp` (tách state), `CMakeLists.txt` gốc, `tooling/native/build_state.py` | Medium |

Xung đột cần phối hợp:
- WS3 và WS4 cùng chạm vùng canvas/input trong `apps/desktop/src/shell`; WS3 sở hữu `native_window_canvas.cpp`
  và `native_window_direct_present.cpp`, WS4 sở hữu `native_window_events.cpp`. Không sửa file của gói kia.
- WS1-01 tách `native_format.cpp`; WS2-07/08 sửa deserializer. Làm WS1-01 trước, sau đó WS2 sửa trên file đã tách.
- WS9-04 (tách state `NativeWindow`) chỉ bắt đầu sau khi WS2/WS3/WS4/WS5 đã merge các sửa Critical/High
  để tránh xung đột header lớn.

## Thứ tự triển khai

~~~mermaid
flowchart LR
  A["WS1 gates xanh"] --> B["WS2 Critical"]
  A --> C["WS3 Critical"]
  A --> D["WS7 docs đồng bộ"]
  B --> E["WS4 High"]
  B --> F["WS5 High"]
  C --> E
  A --> G["WS6 High"]
  C --> H["WS8 Medium"]
  E --> I["WS9 Medium"]
  F --> I
~~~

| Đợt | Nội dung | Gate thoát đợt |
| --- | --- | --- |
| Đợt 1 — Critical | WS1-01..04, WS2-01..04, WS3-01..05, WS6-01..02 | `tooling/native/check.ps1 -Suite architecture` pass 13/13; test mục tiêu của từng hạng mục pass |
| Đợt 2 — High | Phần High còn lại WS2..WS7 | Test mục tiêu pass; một lượt Release `-AllTests` gom chung cuối đợt |
| Đợt 3 — Medium | WS3 phần direct present/effects parity, WS8, WS9, phần Medium còn lại | Benchmark W1/W2 trước/sau ghi trong roadmap hiệu năng; không suy FPS từ offscreen |

## Ma trận mức độ

| Mức | Định nghĩa trong bộ kế hoạch |
| --- | --- |
| Critical | Hỏng dữ liệu/Undo, gãy gate CI, hoặc lỗi làm vô hiệu cache/culling cốt lõi, hoặc lỗ hổng bảo mật có thể khai thác bằng file đầu vào |
| High | Sai hành vi người dùng thấy được, rủi ro đồng bộ/IPC, docs sai khiến agent sinh code lỗi |
| Medium | Hiệu năng/khả năng mở rộng, nợ kỹ thuật, độ phủ test |
| Low | Vệ sinh, nhất quán, cải thiện nhỏ |

## Quy tắc an toàn khi dọn dẹp

- Không xóa `cdr/` (mẫu khách hàng), `.agent/backups/`, `.agent/recovery/` hoặc toolchain đang dùng
  (`.agent/toolchains/{llvm-mingw,msys64,vcpkg}`) khi chưa có xác nhận rõ ràng của người dùng.
- Không xóa build cache canonical (PCH, obj, lib, CMakeCache, .ninja_*). Chỉ dọn output test qua cleaner chuẩn.
- Không giữ dump/screenshot làm bằng chứng trong repo; đọc xong thì dọn.

## Giới hạn

Các phát hiện là kết quả đọc source tĩnh và chạy architecture gate; chưa đo FPS hiển thị, input-to-present,
VRAM hay containment thực. Mọi tuyên bố cải thiện hiệu năng phải kèm benchmark tái lập theo
[kế hoạch hiệu năng](../performance-storage-automation.md).
