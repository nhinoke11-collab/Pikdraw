# Đóng góp PikDraw

Dùng workflow trong [AGENTS](AGENTS.md) và [START](docs/START.md), trực tiếp ở source gốc.
Mọi agent dùng chung quy tắc này. Nạp module/contract theo yêu cầu thay vì đọc tất cả docs.

Tính năng mới thuộc module sở hữu hành vi; dependency cập nhật registry/modules.json.
Thay đổi document/schema giữ migration, stable IDs và Undo; consumer dùng chung cần được đối chiếu.
Chọn checks theo rủi ro, báo lệnh và giới hạn thực. Dọn probe, output và log tạm sau kiểm tra;
giữ regression tests và fixture dùng lại trong suite.

Không đưa font, ảnh hoặc thiết kế khách hàng vào repo khi chưa có quyền phân phối.
Dependency mới ghi nguồn, phiên bản, license và runtime thực sử dụng.
README/project-state phản ánh code hiện tại; khả năng tương lai ghi riêng với trạng thái kế hoạch.
