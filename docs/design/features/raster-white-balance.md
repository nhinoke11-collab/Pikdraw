# Cân bằng trắng tương đối cho ảnh raster

Action action.bitmap.adjust.white_balance nằm trong Bitmaps > White Balance… / Cân bằng trắng….
Action chỉ bật khi chọn đúng một ảnh có nguồn được giữ lại, mẫu đã được giải nghĩa là sRGB và tài liệu
không khóa nội dung. Nếu không đủ điều kiện, action bị disable; dialog cũng nêu rõ cần chọn ảnh sRGB
và xử lý profile chưa áp dụng trước.

Modal cung cấp hai ô số theo tab order: Relative temperature (−100…100%) / Nhiệt độ tương đối và
Green–magenta tint (−100…100%) / Tint xanh lá–đỏ tía. Focus help nói đây là chỉnh tương đối trên
linear-light sRGB, không phải Kelvin; alpha giữ nguyên. Apply ghi một node RasterWhiteBalanceParams
bằng SetRasterEditNodeCommand; mở lại sửa cùng node ID, Cancel không đổi tài liệu. Status báo preview
đang dựng và nhắc Ctrl+Z. Menu action dùng mô tả EN/VI làm trợ giúp; vì đây là mục menu nên không có
SVG icon riêng, toolbox, property bar hay Properties control trong bản hiện tại.

Với t = temperature_shift_percent / 100 và q = tint_percent / 100, evaluator nhân các kênh linear
RGB lần lượt với 2^(0.5t + 0.15q), 2^(-0.30q), 2^(-0.5t + 0.15q), rồi mã hóa lại sRGB và clamp
vào [0, 1]. Công thức có hành vi identity khi cả hai trường bằng 0, nhận RGBA8 và RGBA16LE, kiểm tra
giá trị hữu hạn trong ±100%, giữ nguyên alpha, polling cancellation và memory budget. Đây là điều
khiển tương đối cho ảnh sRGB thông thường; không suy ra nhiệt độ màu vật lý, camera RAW, illuminant,
vùng xám chọn mẫu hay profile khác sRGB.

Node dùng kind ổn định WhiteBalance trong graph schema v7, algorithm version 1. Reader hiện nhận
schema v1–v8 và từ chối White Balance được khai báo trong schema thấp hơn v7. Preview cache hash cả
temperature và tint; geometry gate xem node là thao tác cần sRGB processing.

Regression coverage:

- U8/U16: nhiệt độ dịch màu xám theo thứ tự R > G > B; tint dương nâng R/B so với G; alpha giữ nguyên.
- Identity: đầu vào không đổi theo byte; tint ngoài ±100% bị từ chối.
- .pr: tham số và ID qua save/reopen; evaluator tạo pixel khác nhau; Undo/Redo phục hồi graph.
- Menu/action: action ID và nhãn tiếng Anh/Việt có mặt.

Chưa kiểm tra dialog trong cửa sổ native trực tiếp. CUA không tìm thấy ứng dụng Windows đang mở ở lượt
kiểm tra trước. Chưa có picker neutral point, thanh trượt, live preview trong dialog, điều khiển
Kelvin/RAW hay đồng bộ với Properties.
