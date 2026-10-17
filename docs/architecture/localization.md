# Kiến trúc bản địa hóa

**Trạng thái:** Đặc tả; catalog và runtime chuyển ngôn ngữ chưa được triển khai.
**Tham chiếu:** [Contract C7](../../contracts/c7-actions-ui.md), [bố cục workspace](workspace-layout.md).

## Nguyên tắc

- English (`en`) là ngôn ngữ giao diện mặc định; Vietnamese (`vi`) là ngôn ngữ tiếp theo. Settings là nơi người dùng chọn ngôn ngữ; có thể bổ sung locale sau.
- Chỉ dịch giao diện: menu, lệnh, Properties, thông báo, trợ giúp và tooltip. Không dịch hoặc sửa nội dung Unicode của tài liệu, mã màu/đơn vị sản xuất, stable IDs, schema hay dữ liệu máy.
- Mỗi hành động có ID ổn định dùng chung giữa menu, shortcut, toolbar và trợ giúp. Chuỗi hiển thị thuộc catalog theo locale; không gắn nhãn UI trực tiếp vào dữ liệu tài liệu.
- Dịch không được làm thay đổi ý nghĩa, khả năng thực thi hoặc ngữ nghĩa phím tắt của hành động. Tên định dạng, mã, đơn vị và giá trị production giữ theo contract.
- Thay đổi locale sẽ cập nhật các bề mặt UI sau khi runtime hỗ trợ; hành vi chuyển nóng, dự phòng locale, pluralization và kiểm tra catalog vẫn cần thiết kế/kiểm chứng.

## Dữ liệu và runtime dự kiến

Catalog ngoài source theo locale và khóa ổn định là hướng thiết kế. Schema, đường dẫn canonical, quy trình kiểm tra khóa trùng/thiếu và cơ chế đóng gói chưa được chốt thành implementation. Không có `core/localization` runtime hay catalog `locales/` được coi là đã phát hành chỉ từ đặc tả này.

Desktop host dự kiến cung cấp bản dịch cho các module UI mà không tạo phụ thuộc ngược từ core. Trước khi triển khai cần task riêng cho catalog, loader, fallback, Settings, cập nhật menu/toolbar/Properties/help, accessibility, kiểm tra thiếu dịch và regression đảm bảo dữ liệu tài liệu không đổi.

## Điều kiện nghiệm thu sau này

1. English mặc định và Vietnamese được chọn trong Settings.
2. Tất cả action IDs dùng chung và không hardcode text UI; catalog có kiểm tra tính đầy đủ và giá trị format.
3. Chuyển locale cập nhật title, menu, toolbar, Properties, tooltip, status và trợ giúp nhất quán.
4. Lưu/mở/undo/export giữ nguyên nội dung tài liệu, text Unicode, color, unit, stable IDs và dữ liệu production qua mọi lần đổi locale.
