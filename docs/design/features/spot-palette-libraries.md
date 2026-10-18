# Thư viện màu Spot

Task: `spot-palette-library-ui-20261018` (`desktop`).

## Mục tiêu và phạm vi

Chọn màu Spot có tên từ thư viện, tìm theo tên, chỉnh tint và áp vào fill hoặc stroke hiện hành. PikDraw đã lưu `SpotColor` với tên, tint, alternate RGB và overprint; thay đổi này cung cấp danh mục thư viện và luồng chọn màu.

Thư viện cục bộ đọc tệp Adobe Swatch Exchange (`.ase`) do người dùng nhập. Chỉ giữ swatch có loại Spot và mô hình RGB, CMYK, Lab hoặc Gray. CMYK/Lab/Gray được đổi sang alternate RGB để xem trên màn hình; tên Spot và tint vẫn là dữ liệu nguồn. Tệp ASE nằm trong `%LOCALAPPDATA%/PikDraw/spot-palettes`, độc lập với tài liệu. Có thư viện ví dụ PikDraw; không đóng gói dữ liệu màu thương mại Pantone, HKS hay Roland. Nếu người dùng có quyền dùng các bảng đó và xuất được chúng thành ASE, tên file sẽ là tên thư viện trong danh sách.

## Vị trí và thao tác

| Mục | Quyết định |
|---|---|
| Đường mở | `Window > Properties > Appearance > Spot palette…`; Properties là dock hiện có, không tạo panel mới |
| Action ID | `ui.properties.appearance.spot_palette.open` (nút native trong Appearance; không tạo document command khi chỉ mở) |
| Properties | Nút ngắn “Spot palette…” dưới Document Palette; dùng target Fill/Stroke đang chọn |
| Toolbox / toolbar / property bar | Không áp dụng: đây là thư viện màu, không phải tool hoặc quick geometry control |
| Dialog | Chọn thư viện, tìm swatch theo tên/nhóm, xem màu alternate, nhập tint 0–100%, Import ASE, Apply hoặc Cancel |
| Icon SVG | Không áp dụng: nút có nhãn văn bản đầy đủ và điều khiển native có accessible name |
| Shortcut | Chưa gán; Enter áp dụng mục đang chọn, Escape hủy |

## Trạng thái và commit

| Field | Kiểu / phạm vi | Hành vi |
|---|---|---|
| Library / Thư viện | Danh sách palette người dùng đã nhập và PikDraw samples | App-local; không ghi vào tài liệu |
| Search / Tìm màu | Chuỗi Unicode | Lọc trực tiếp theo tên và nhóm; rỗng hiển thị mọi Spot trong thư viện |
| Tint / Sắc độ | Số nguyên 0–100%, mặc định 100% | Được kiểm tra trước khi áp dụng; alternate preview blend về trắng theo tint |
| Target / Đích áp dụng | Fill hoặc Stroke đang chọn trong Properties | Không selection: cập nhật default style; selection: một StylePatch/Undo, theo quy tắc hỗ trợ hiện có |

Import sao chép tệp ASE được người dùng chọn vào thư mục palette cục bộ; không sửa tệp nguồn. Tệp hỏng, quá lớn, không có Spot swatch hỗ trợ hoặc có trường vượt giới hạn bị từ chối với lỗi cụ thể. Cancel đóng picker mà không đổi tài liệu. Apply ghi Spot name/tint/alternate RGB và giữ cờ overprint của thuộc tính nguồn theo command style hiện có. Chỉ tệp palette được import mới chứa dữ liệu thư viện; document native lưu swatch đã áp dụng như SpotColor.

## Hướng dẫn và giới hạn

Nhãn nút: “Spot palette…” / “Bảng màu Spot…”. Tooltip/focus help: “Choose a named spot swatch for the active fill or stroke.” / “Chọn màu Spot có tên cho vùng tô hoặc viền hiện hành.” Status sau thao tác theo thông báo style command hiện có; Undo/Redo theo lịch sử tài liệu.

Để dùng Pantone/HKS/Roland, xuất các swatch Spot được cấp quyền sang ASE từ ứng dụng có hỗ trợ rồi chọn Import ASE. Adobe mô tả ASE là định dạng trao đổi swatch rắn, bao gồm Spot colors, giữa các ứng dụng của họ. PikDraw không sinh hoặc đổi tên dữ liệu thành thư viện tiêu chuẩn khi nguồn không cung cấp.

Màu hiển thị trên màn hình vẫn là alternate RGB preview; Lab/CMYK conversion trong parser chỉ phục vụ alternate preview, không tuyên bố proof màu giấy/mực hay output separation. Việc nhúng ICC vào tệp xuất là phạm vi riêng.

## Bằng chứng

- Thiết kế dựa trên UI native Win32 và Appearance Properties hiện có; không sao chép nhãn/biểu tượng thương hiệu từ ảnh tham chiếu.
- Validation/build/evidence runtime được ghi ở task khi hoàn tất.
