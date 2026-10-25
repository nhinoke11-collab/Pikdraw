# Histogram ảnh sau chỉnh sửa trong Properties

## Mục tiêu và phạm vi

Properties hiển thị histogram RGB của một `ImageObject` sau khi áp dụng graph
chỉnh sửa và đường A nét chấm biểu diễn alpha coverage. Phép tính chạy trên
worker, dùng snapshot bất biến và hủy khi selection hoặc revision đổi; nó không
tạo command hay thay đổi document. Histogram giữ các mã mẫu sRGB RGBA8/U16
chính xác. Đây chưa phải histogram theo mask, phép đo qua ICC/monitor/proof hay
composite trang. Bốn chip R/G/B/A* điều khiển đường nào đang hiển thị. Ảnh cần vượt working-memory quota được báo không khả dụng;
không thay bằng histogram nguồn hoặc thumbnail.

## UI spec

| Thuộc tính | Quyết định |
| --- | --- |
| Menu và action ID | Không áp dụng; histogram chỉ đọc và tự cập nhật theo snapshot |
| Toolbox / SVG icon | Không áp dụng; không thêm công cụ hoặc nút |
| Property bar | Không áp dụng; không có tham số thao tác |
| Bề mặt | Dock `Properties` hiện có, sau transform của một `ImageObject` đang chọn |
| Nhãn | `HISTOGRAM RGB SAU CHỈNH` / `EDITED RGB HISTOGRAM` |
| Scope | Kênh RGB sau graph; alpha chỉ hiển thị coverage, không áp dụng mask, ICC/proof hoặc composite trang |
| Field / state | Không có field nhập; bốn chip R/G/B/A* bật/tắt riêng. `Đang tính…` / `Calculating…`, chart, hoặc lỗi khi nguồn/profile/graph/quota không hợp lệ |
| Biểu diễn | R đỏ, G lục, B lam; A* màu hổ phách nét chấm và biểu diễn alpha coverage. Mỗi kênh tự chuẩn hóa. U16 giữ 65.536 mã trong API và chart gộp theo byte cao |
| Focus / tooltip / status hint | Click chip R/G/B/A* để hiện/ẩn kênh; status báo tên kênh và trạng thái. Hint chart: `RGB sau graph; A chấm = độ phủ; chưa ICC/proof` / `RGB after graph; dotted A = coverage; no ICC/proof` |
| Undo / persistence | Không tạo command; cache tạm keyed theo document/epoch/revision/source/mode, không lưu trong `.pr` |

## Điều kiện và lỗi

- Không chọn đúng một ảnh: ẩn chart và hủy job đang chạy.
- Worker đánh giá graph trên mẫu RGBA8 packed hoặc RGBA16LE rồi đếm mã kết quả;
  phép tính bị hủy khi selection/revision đổi.
- Edited histogram dùng tối đa 256 MiB working memory. Vượt quota, profile chưa
  được giải quyết, dữ liệu sai hoặc graph lỗi đều hiện không khả dụng; không dùng
  thumbnail hay số liệu nguồn làm kết quả thay thế.
- Tọa độ RGB biểu diễn mã kênh sau graph trong basis sRGB hiện hành, không phải
  ánh sáng tuyến tính, Lab, gamut hay soft proof. Đường A biểu diễn độ phủ
  alpha, không đổi theo ICC và không phải mask selection.

## Nghiệm thu

1. RGBA8 có graph Exposure: histogram edited theo pixel oracle sau chỉnh, còn
   mode Source đọc đúng các mã gốc; alpha coverage được biểu diễn riêng.
2. RGBA16LE giữ đúng mã U16 sau graph; chart chiếu 256 bucket theo byte cao.
3. Đổi selection/revision khi job chạy: kết quả cũ không được publish và worker
   dừng tại điểm cancellation.
4. Quota hoặc source profile không hợp lệ: chart báo không khả dụng, không rơi về
   histogram source.
5. Bật/tắt từng chip đổi đường hiển thị nhưng không đổi bins, bytes, `.pr`, Undo/Redo hoặc revision.

Release test/service evidence và giới hạn runtime được ghi trong roadmap image
editing. Screenshot/native window acceptance vẫn cần xác nhận trực tiếp trong app.
