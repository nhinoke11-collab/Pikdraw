# PikDraw agent instructions

## Quy trình làm việc tinh gọn (Streamlined Workflow)

- Làm việc trực tiếp trong source gốc: C:/Users/Administrator/Downloads/PikDraw. Không tạo worktree, clone hay nhánh task riêng.
- Tuyệt đối loại bỏ chu trình harness hành chính rườm rà (task create/claim/handoff/lease). Triển khai trực tiếp và linh hoạt theo yêu cầu người dùng.
- Cho phép Git commit rõ ràng sau khi hoàn thành tính năng và test pass (`tooling/native/build.ps1 -Test`), giúp working tree sạch sẽ, tránh dồn ứ hàng chục file dirty chưa commit.
- Giữ nguyên local edits của người dùng; không tự ý reset, stash hoặc xóa code đang viết dở khi chưa được người dùng xác nhận.

## Mã và hành vi

- Runtime desktop hiện tại là C++20 / Win32 Native Shell: Direct2D 1.1 / D3D11 hardware hoặc WARP dựng canvas qua ViewportRenderer; frame được staging/readback về pixel buffer rồi GDI chép vào backbuffer và vẽ overlays. SoftwareRasterizer là CPU fallback/oracle. Qt 6 và Skia vẫn là thiết kế định hướng chờ toolchain đồng nhất ABI.
- Đọc registry/modules.json để biết dependency. Core/module không phụ thuộc ngược vào apps/desktop.
- Document writes đi qua command/transaction và một writer logic. Giữ stable IDs, undo/redo, đơn vị thật, Unicode, màu nguồn và metadata. Async result phải bị từ chối khi revision/epoch/job generation đã cũ.
- Importer và plugin ngoài process trả dữ liệu chưa tin cậy; host kiểm tra capability, kích thước, revision và nội dung trước khi áp dụng.
- **Ngưỡng độ dài file (Tối đa 2000 dòng)**: Mỗi file code chuẩn tối đa **2000 dòng** (khuyến nghị 300–1800 dòng). **Tuyệt đối không nối đuôi (append)** tính năng mới vào các file đã vượt 2000 dòng; phải tách file theo nhóm tính năng/domain rõ ràng. Xem `docs/development/code-organization.md` và `docs/roadmap/architecture-remediation-plan-20261018.md`.

## Lập kế hoạch tính năng dùng tài nguyên vector

Khi lập kế hoạch hoặc đặc tả cho chức năng đọc/nhập, chỉnh sửa, render, chuyển đổi hay dùng tài nguyên vector tải từ mạng, phải đọc [hướng dẫn tương thích tài nguyên vector](docs/architecture/vector-resource-interoperability.md) và đối chiếu C8/C9 cùng capability hiện tại. Phạm vi thường gồm PDF, AI, CDR, EPS, SVG và DXF; nêu rõ định dạng nào liên quan hoặc không áp dụng.

Plan phải mô tả adapter riêng theo định dạng/phiên bản đưa dữ liệu vào mô hình tài liệu trung lập, rồi dùng chung semantics command/edit/render của PikDraw ở nơi đã kiểm chứng. Ghi ma trận version/representation/feature và tách kết quả xem/render, editability, metadata nguồn, export round-trip và khả năng sản xuất. “Dùng chung thuật toán” nghĩa là dùng chung model và semantics sau khi normalize, không phải ép mọi định dạng qua một parser hoặc converter có thể làm mất nghĩa.

## Thay đổi giao diện

Với thay đổi UI, đọc docs/design/ui-ux-standard.md và đối chiếu docs/architecture/workspace-layout.md. Ghi feature spec hoặc nội dung tương đương: đường menu, action ID, vị trí toolbox/toolbar, contextual property bar và Properties, phạm vi/trạng thái field, icon SVG, label ngắn, tooltip/focus help, status hint và hướng dẫn dùng. Ghi rõ mục không áp dụng. Dùng bề mặt hiện có; HTML/SVG cũ chỉ là tư liệu. Agent tự quyết định lựa chọn thường lệ trong scope và tiếp tục triển khai, không chờ người dùng điền form hay duyệt trước.

## Kiểm tra, ghi log ngắn gọn và bàn giao

- Chạy kiểm tra đúng mục tiêu thay đổi. Build/test native qua `tooling/native/build.ps1`; ưu tiên chỉ định `-Test -TestTarget <target_name> -TestRegex <regex>` thay vì build lại toàn bộ test suite. App chuẩn là `build/win-dev/apps/desktop/pikdraw.exe`.
- Với thay đổi hành vi, ưu tiên mở rộng test hiện có của module thay vì tạo file test `.cpp` rời rạc mới. Sau khi kiểm tra, xóa ngay test source dùng một lần, ảnh chụp `.bmp`/`.png` tạm, fixture/output/log tạm. Xem docs/quality/feature-acceptance.md.
- **Báo cáo và cập nhật trạng thái**:
  - Báo cáo kết quả trực tiếp, súc tích cho người dùng kèm các lệnh kiểm tra và bằng chứng thực tế. Không tạo phiếu handoff hay tài liệu snapshot trung gian.
  - `docs/project-state.json` chỉ lưu trạng thái snapshot hiện hành (tối đa 8.000 byte); **tuyệt đối không nối đuôi (append) nhật ký lịch sử từng ngày hay bảng số liệu dài** vào `project-state.json`.
- Báo đúng lệnh, kết quả, giới hạn và paths đã đổi. Không suy ra UX, performance, fidelity hoặc containment từ harness pass.
- Tài liệu hướng người dùng viết bằng tiếng Việt; hợp đồng kỹ thuật ngắn gọn có thể dùng tiếng Anh.
