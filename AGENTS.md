# PikDraw agent instructions

## Source và task

- Chỉ làm trong source gốc: C:/Users/CK ART STAGE/Downloads/PikDraw. Không tạo worktree, clone hay nhánh task mới.
- Tìm task bằng tooling/pikdraw.ps1 task list. Tạo và claim task với owned paths cụ thể trước khi sửa.
- Giữ nguyên local edits, file chưa commit và thay đổi của task khác. Không reset, stash, xóa hoặc commit trừ khi người dùng yêu cầu rõ.
- Chỉ chạy delegate khi người dùng hoặc chỉ dẫn áp dụng cho phép; các task song song phải có paths không chồng lấn.

## Mã và hành vi

- Runtime desktop hiện tại là C++20/Win32/GDI. Qt/Skia và các workspace chưa làm vẫn là thiết kế, không phải năng lực runtime.
- Đọc registry/modules.json để biết dependency. Core/module không phụ thuộc ngược vào apps/desktop.
- Document writes đi qua command/transaction và một writer logic. Giữ stable IDs, undo/redo, đơn vị thật, Unicode, màu nguồn và metadata. Async result phải bị từ chối khi revision/epoch/job generation đã cũ.
- Importer và plugin ngoài process trả dữ liệu chưa tin cậy; host kiểm tra capability, kích thước, revision và nội dung trước khi áp dụng.
- File code có thể dài khoảng 1.000 dòng hoặc hơn nếu vẫn rõ trách nhiệm. Không chia file chỉ để đạt con số; xem docs/development/code-organization.md.

## Lập kế hoạch tính năng dùng tài nguyên vector

Khi lập kế hoạch hoặc đặc tả cho chức năng đọc/nhập, chỉnh sửa, render, chuyển đổi hay dùng tài nguyên vector tải từ mạng, phải đọc [hướng dẫn tương thích tài nguyên vector](docs/architecture/vector-resource-interoperability.md) và đối chiếu C8/C9 cùng capability hiện tại. Phạm vi thường gồm PDF, AI, CDR, EPS, SVG và DXF; nêu rõ định dạng nào liên quan hoặc không áp dụng.

Plan phải mô tả adapter riêng theo định dạng/phiên bản đưa dữ liệu vào mô hình tài liệu trung lập, rồi dùng chung semantics command/edit/render của PikDraw ở nơi đã kiểm chứng. Ghi ma trận version/representation/feature và tách kết quả xem/render, editability, metadata nguồn, export round-trip và khả năng sản xuất. “Dùng chung thuật toán” nghĩa là dùng chung model và semantics sau khi normalize, không phải ép mọi định dạng qua một parser hoặc converter có thể làm mất nghĩa.

## Thay đổi giao diện

Với thay đổi UI, đọc docs/design/ui-ux-standard.md và đối chiếu docs/architecture/workspace-layout.md. Ghi feature spec hoặc nội dung tương đương: đường menu, action ID, vị trí toolbox/toolbar, contextual property bar và Properties, phạm vi/trạng thái field, icon SVG, label ngắn, tooltip/focus help, status hint và hướng dẫn dùng. Ghi rõ mục không áp dụng. Dùng bề mặt hiện có; HTML/SVG cũ chỉ là tư liệu. Agent tự quyết định lựa chọn thường lệ trong scope và tiếp tục triển khai, không chờ người dùng điền form hay duyệt trước.

## Kiểm tra và bàn giao

- Chạy kiểm tra phù hợp với rủi ro của thay đổi. Build/test native dùng tooling/native/build.ps1; app chuẩn là build/win-dev/apps/desktop/pikdraw.exe.
- Với thay đổi hành vi, agent tự viết hoặc cập nhật ca nghiệm thu theo luồng dùng thật; dùng cấu trúc chung Given/When/Then và chọn số ca theo rủi ro, không chạy hay thêm một bộ test lớn mặc định. Xem docs/quality/feature-acceptance.md.
- Sau khi kiểm tra, xóa test source dùng một lần, fixture/output/log tạm; giữ test hồi quy có ích trong suite chuẩn và chỉ giữ artifact cần cho review.
- Test sources đã được khôi phục. P00d ghi nhận Debug CTest 63/63 trên source tích hợp tại thời điểm đó; kiểm tra source snapshot hiện tại trước khi báo pass.
- Báo đúng lệnh, kết quả, giới hạn và paths đã đổi. Không suy ra UX, performance, fidelity hoặc containment từ harness pass.
- Cập nhật docs/project-state.json khi task tích hợp và dùng handoff protocol tại docs/agents/PROTOCOL.md. Ghi ADR khi có quyết định kiến trúc mới.
- Tài liệu hướng người dùng viết bằng tiếng Việt; hợp đồng kỹ thuật ngắn gọn có thể dùng tiếng Anh.
