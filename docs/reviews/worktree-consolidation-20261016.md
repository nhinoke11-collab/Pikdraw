# Di trú worktree và build hợp nhất — 16/10/2026

## Phạm vi và kết quả

- Giữ nguyên source gốc `C:/Users/CK ART STAGE/Downloads/PikDraw`, branch `main` và mọi local edits; không commit.
- Kiểm kê ban đầu có 68 worktree cũ. Đã rà từng cây, đồng bộ thay đổi còn giá trị hoặc ghi rõ phần đã bị source hiện hành thay thế; tất cả được gỡ bằng `git worktree remove` không dùng force. Git hiện chỉ còn worktree source gốc và thư mục `.worktrees` đã được xóa.
- Không tạo worktree, clone, task branch mới hoặc dùng lane nhà cung cấp khác.
- Task records có đường dẫn checkout cũ đã được đổi `worktree: null`; đường dẫn lịch sử, bằng chứng và trạng thái di trú được giữ trong metadata.

## Rà soát phần còn lại trước khi gỡ cây

| Nhóm worktree | Cách xử lý |
| --- | --- |
| Candidate/review M1, document safety, storage, vector/editor slice | Các thay đổi đã có trong source gốc; bản gốc còn có hardening và cập nhật sau đó. Cây duplicate candidate/integrated-review cùng HEAD được gỡ riêng. |
| M1 build/gate integrity | Chính sách `-Werror`, helper assertion và suite audit được tích hợp/rà soát trên source gốc; suite độc lập M1 trước đó đạt 13/13. |
| M1 interop consistency | Không lấy parser SVG cũ ghi đè importer mới. Sửa capability/docs để công bố đúng SVG subset import/export và DXF subset import; PNG/JPEG/PDF/AI/CDR từ chối rõ ràng. Capability CTest đạt 1/1. |
| Viewport/document/interop spikes | Giữ trạng thái là prototype nghiên cứu chưa được chọn làm runtime; source và tài liệu gốc hiện hành có mô hình được chọn sau đó. Nhánh Git cũ còn để khôi phục. |
| UI integration cuối | Đã kiểm archive/hash của 478,728 byte tracked diff và đủ 30 file untracked. Đưa thông báo bitmap-style không thể sửa như vector fill/stroke vào source gốc; bỏ ảnh BMP không được xác minh và các bản code cũ hơn. Stash phục hồi: `ffedcbc8151ddc3848ef5da6d622c5171c6cf874`; archive và proof ở `.agent/migrations/20261016/`. |

Các checkout sạch được gỡ ngay sau đối chiếu và tạo proof. Worktree UI dirty được giữ trong archive/stash, làm sạch bằng stash, rồi gỡ bình thường. `removal-events.jsonl` và mỗi file `*-removal-proof.json` là bản ghi cụ thể.

## Kiểm chứng

- Strict Debug build hoàn tất đủ 248 target; sửa lỗi biến test chưa dùng và assertion capability sai thay vì hạ mức cảnh báo.
- CTest trên source đã hợp nhất đạt **63/63**. Log: `.agent/migrations/20261016/ctest-final-63-of-63.log`; SHA-256 `E235E309AEBB9A2B4A98CB337A7536773D9E9CD9E1B64E74615D61090C9BB6AC`.
- App được clean build riêng sau đó tại `build/win-dev/apps/desktop/pikdraw.exe`; thư mục output chỉ còn một `.exe` canonical.
- `tooling/pikdraw.ps1 doctor` đạt `ok=true`, 15 module, không issue. Không có commit mới.
- Các giới hạn M1, renderer/GPU, fidelity định dạng và hiệu năng sản xuất vẫn theo `docs/project-state.json`; việc gỡ worktree không được xem là nghiệm thu các tính năng đó.

## Dọn test và output

Theo lựa chọn của người dùng, sau lượt CTest cuối đã xóa **72 file mã nguồn kiểm thử** native, architecture và harness, gồm helper assertion trong `tooling/native/quality`; dọn 5 cache `.pyc`, giữ log/evidence cùng dữ liệu fixture. CMake mặc định tắt test. Runner từ chối yêu cầu test khi nguồn đã xóa và yêu cầu khôi phục nguồn trước. Clean build cuối chỉ target `pikdraw`; giữ source gốc và app canonical. Không xác nhận full Release, M1, renderer/GPU, fidelity định dạng hoặc hiệu năng sản xuất.
