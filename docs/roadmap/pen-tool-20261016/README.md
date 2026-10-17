# Pen, Node và vẽ chính xác

Thiết kế và acceptance cho nhóm công cụ này. Tài liệu không xác nhận tính năng đã tích hợp.

## Trạng thái source ngày 16/10/2026

- Pen controller/core và numeric Pen đã có trong các task P03–P05 đang review.
- P07 đăng ký shell chưa hoàn tất; Pen chưa dùng được từ menu/toolbox của app.
- Node editing chưa triển khai.
- P00b khôi phục test sources; P00d ghi nhận Debug CTest 63/63 trên snapshot tích hợp của task. P00c validation đang review; Release chưa chạy. Kiểm tra task board và source snapshot trước khi dùng lại kết quả.

Audit trong thư mục này là snapshot trước P03–P05, không còn mô tả source hiện tại. Task board là nguồn trạng thái mới nhất.

## Tài liệu

1. [Audit lịch sử](audit.md)
2. [UI spec](ui-spec.md)
3. [Hành vi và độ chính xác](behavior-and-precision.md)
4. [Phân rã implementation](implementation.md)
5. [Acceptance](acceptance.md)
6. [Task catalog](tasks.json) và [validation](validation.json)

Các mốc A/B/C mô tả năng lực đích: Pen và nhập số; Pen/Node cơ bản; rồi từng khả năng snap nâng cao. Chỉ công bố capability sau khi có integration và evidence phù hợp. Thay đổi contract cần review consumer; UI feature ghi spec theo chuẩn UI hiện hành.

Ví dụ sản phẩm và tài liệu tham khảo trong các trang con là định hướng, không phải lời hứa tương thích với Illustrator, CorelDRAW hay AutoCAD.
