# PikDraw — định hướng sản phẩm

Trạng thái: **đặc tả đề xuất; chưa có ứng dụng thiết kế hoạt động**. Kho mã khởi đầu tổ chức kiến trúc và quy trình phát triển. Không diễn giải tài liệu này thành cam kết tương thích file, hiệu năng hoặc ngày phát hành.

## Mục tiêu

PikDraw là ứng dụng desktop mã nguồn mở dành cho thiết kế vector chuyên sâu, quảng cáo, in ấn và thi công biển hiệu. Chỉnh ảnh cơ bản, bản vẽ CAD 2D, chuẩn bị CNC và 3D cơ bản dùng chung tài liệu để giảm việc dựng lại thiết kế giữa các công đoạn.

Ba nhóm người dùng chính:

| Người dùng | Công việc cần hoàn thành |
| --- | --- |
| Thiết kế quảng cáo | Biển bạt, backdrop, poster, decal, chữ và bố cục chính xác |
| Xưởng in/cắt | Kiểm tra màu, ảnh, khổ in; xuất file in và đường cắt đúng đơn vị |
| Thi công biển hiệu | Kích thước, vị trí lắp, vật liệu, số lượng, hình đùn và phối cảnh cơ bản |

Luồng mẫu: thiết kế biển → duyệt khách → xuất nền in → chuẩn bị đường cắt chữ → ghi kích thước thi công → xem phối cảnh 3D. Mỗi đầu ra lưu phiên bản tài liệu nguồn; thay đổi nguồn phải báo đầu ra cần cập nhật.

## Phạm vi nền tảng

| Nhóm | Năng lực mục tiêu |
| --- | --- |
| Vector | Bézier, sửa nút, Boolean, offset, contour, clipping, mask, gradient, opacity, bố cục, hiệu ứng có thể sửa lại |
| Chữ | Tiếng Việt, shaping, font fallback có cảnh báo, text frame, chữ theo đường cong; giữ chữ nguồn khi tạo outline |
| Tài liệu | Nhiều trang, layer, đơn vị, tỷ lệ, nhập số chính xác, undo/redo, tự lưu, phục hồi, tài nguyên liên kết/nhúng |
| Raster | Crop, resize, rotate, mask, độ sáng/tương phản, cân màu, làm nét; xử lý có thể điều chỉnh lại |
| In | ICC/CMYK, bleed, crop marks, kiểm tra độ phân giải tại kích thước in, chia khổ, preflight và PDF xuất in |
| CAD/CNC | Snap, tọa độ, kích thước liên kết, block/instance, phân loại cắt/khắc/khoan, phát hiện đường hở/trùng, xuất DXF |
| 3D cơ bản | Đùn từ đường nguồn, độ dày tấm/chữ, khung biển, vật liệu, camera và bản xem trước |
| Mở rộng | Workspace, command, panel, xử lý tài liệu, bộ nhập/xuất qua API plugin có phiên bản |

CAM theo máy, postprocessor G-code, nesting tối ưu và mô hình 3D phức tạp cần các mốc riêng. Phiên bản đầu không điều khiển máy CNC. Quyết định này giữ yêu cầu sản xuất rõ ràng và cho phép nghiệm thu từng đầu ra.

## Tương thích file: đo ba lớp riêng

1. **Hiển thị:** hình thức, chữ, màu và trang có đúng không?
2. **Chỉnh sửa:** đường, chữ, nhóm, clipping và hiệu ứng còn là đối tượng thích hợp không?
3. **Xuất/lưu:** có thể ghi định dạng đích với mức bảo toàn nào?

| Định dạng | Hướng triển khai đề xuất | Điều kiện nghiệm thu |
| --- | --- | --- |
| PDF | Nhập nhiều trang và các đối tượng được nhận dạng; xuất in qua bộ chuyển đổi riêng | Tách chữ sống/outline/ảnh; báo thiếu font, blend và transparency phải chuyển dạng |
| CDR | Khảo sát reader bên ngoài trong worker | Ma trận phiên bản và hiệu ứng; chưa cam kết lưu ngược CDR |
| AI | Khảo sát phần PDF tương thích và reader phù hợp | Phân biệt biểu diễn đồ họa với dữ liệu chỉnh sửa riêng của Illustrator; chưa cam kết lưu AI nguyên vẹn |
| EPS | Chuyển đổi cách ly, trích vector theo khả năng bộ đọc | Kiểm tra font, clipping, màu và các phần raster hóa |
| DXF | Adapter hình học 2D | Đơn vị, tọa độ, layer, cung, spline, polyline, block/instance; danh mục entity hỗ trợ phải công khai |
| SKP | Adapter 3D sau khảo sát SDK và giấy phép | Hình học, nhóm/component, transform, vật liệu; khả năng ghi xét riêng |
| SVG | Adapter vector và tài nguyên | Namespace, text, transform, gradient, clipping; tính năng ngoài mô hình phải báo rõ |
| PNG/JPEG/TIFF | Adapter ảnh | Kích thước pixel, orientation, alpha nếu có và profile màu |

Hiện toàn bộ adapter ở trạng thái **thiết kế/chưa kiểm chứng**. Không dùng nhãn “hỗ trợ đầy đủ” trước khi có bộ fixture, kiểm tra hiển thị và kiểm tra chỉnh sửa. DXF và SKP là tên định dạng được hiểu từ yêu cầu ban đầu “dfx/spk”.

Tài liệu gốc của PikDraw cần bảo toàn dữ liệu chỉnh sửa; đặc tả định dạng phải công khai. File nhập giữ dấu vết nguồn và báo cáo chuyển đổi. Tài nguyên được nhúng chỉ khi quyền sử dụng cho phép; giữ liên kết và thông tin nguồn khi chưa thể nhúng.

## Lộ trình theo đầu ra

| Mốc | Đầu ra review được | Điều kiện đi tiếp |
| --- | --- | --- |
| Foundation | ADR, module boundaries, command/document contracts, bộ fixture, prototype render | Đo nền render, giới hạn dữ liệu, giấy phép dependency |
| Vector alpha | Mở/lưu native, chỉnh đường/chữ, nhiều trang, phục hồi | Không mất dữ liệu trong kiểm thử; undo/redo và nền vector đạt gate |
| Production beta | PDF/DXF theo phạm vi công bố, preflight, bộ hồ sơ 2D | Xưởng thử file trên quy trình in/cắt thực tế |
| Workspace mở rộng | CAD 2D, 3D đơn giản, plugin SDK | Hợp đồng ổn định, plugin lỗi không phá tài liệu, hiệu năng có baseline |

Ưu tiên chất lượng vector, tương thích và ổn định xuyên suốt các mốc. Danh sách tính năng lớn không thay thế tiêu chí hoàn thành. Xem [kiến trúc](../architecture/architecture.md), [workspace](../architecture/workspace-layout.md) và [quality gates](../quality/gates.md).
