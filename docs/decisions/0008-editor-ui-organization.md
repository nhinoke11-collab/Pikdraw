# ADR 0008: Tổ chức UI như một editor thiết kế chuyên nghiệp

Ngày: 16/10/2026. Trạng thái: Accepted theo yêu cầu chủ dự án về quy tắc thiết kế.
Phạm vi: tài liệu, phân loại chức năng và review; không chấp nhận một implementation UI mới bằng ADR này.

## Vấn đề

Tài liệu cũ trộn mẫu giao diện Cyber, màu sắc/trang trí và quyết định runtime với cách tổ chức chức năng.
Agent dễ thêm nút dài, panel hoặc hàng bar tùy ý, không xác định menu, thuộc tính và hướng dẫn của feature.

## Quyết định

[Chuẩn UX/UI](../design/ui-ux-standard.md) là nguồn duy nhất cho placement và tương tác.
[Workspace](../architecture/workspace-layout.md) mô tả khung vật lý; [phiếu UI](../design/feature-ui-template.md)
bắt buộc chốt menu chính, các bề mặt điều khiển, field/scope/state và help trước khi code UI.

Lấy cách tổ chức trong ảnh CorelDRAW người dùng cung cấp và tài liệu Corel/Illustrator làm tham khảo:
menu/thanh lệnh trên, property bar theo context, toolbox trái, canvas trung tâm, Properties phải và status hint dưới.
Ưu tiên SVG icon đồng bộ với tooltip/focus help, tên truy cập được; chữ trên button ngắn và đủ nghĩa.
Các bề mặt dùng chung action/source value, không tạo hành vi hoặc panel trùng lặp.

HTML/SVG/ảnh UI cũ giữ vai trò tham khảo lịch sử. Bỏ yêu cầu Cyber palette, glow/pulse, badge PRO,
pill workspace và màu/kích thước pixel lấy từ HTML như tiêu chí bắt buộc cho feature mới.
Theme/density phải thống nhất và phục vụ khả năng đọc/không gian canvas; không do từng agent tự chọn.

Không thay hợp đồng document, module dependencies, localization/source data hoặc chính sách một source/build.
Quy tắc không khóa Qt hay Win32; runtime thực tế vẫn theo project-state và evidence.

## Nghiệm thu và giới hạn

Agent triển khai tự phân tích, quyết định và hoàn thiện phiếu, tự kiểm placement trước code rồi tiếp tục trong scope.
Người dùng không phải điền hoặc duyệt từng phiếu; coordinator không là cổng ký duyệt lựa chọn thường lệ trước code.
Reviewer kiểm phiếu cùng thao tác/native evidence trong quy trình review/bàn giao hiện hành.
Đây là gate UX thủ công, chưa có harness checker tự đo dễ dùng hoặc đúng bố cục.
Chỉ sửa tài liệu ở task này; thay UI hiện có cần task implementation riêng có scope và kiểm chứng.
