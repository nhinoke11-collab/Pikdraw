# Chỉnh sửa PowerClip

Phạm vi: geometric clip trong mô hình tài liệu PikDraw. Đây là tính năng native;
không xác nhận mọi phiên bản CDR hoặc các hiệu ứng lens/alpha mask của CorelDRAW.
Kế hoạch và các gate: [PowerClip](../../roadmap/powerclip-editing-upgrade-20261021.md).

## Cách dùng

1. Chọn nội dung, chạy **Object > PowerClip > Place Inside Frame…**, rồi click khung đích.
   Khung đã có PowerClip nhận thêm nội dung. Hover chỉ gợi ý khung hợp lệ; Esc hủy.
2. Chọn PowerClip trên canvas và bấm **Edit Contents** ở đầu property bar, hoặc nhấn Enter.
   Chọn/kéo/đổi kích thước nội dung như đối tượng thông thường. Phần nằm ngoài khung được hiện khi sửa.
3. **Finish Editing** trở về một cấp. Nút vẫn có khi không chọn đối tượng nào.
   Esc hủy thao tác hoặc nhập số đang làm trước, lần tiếp theo mới thoát chế độ sửa.
4. **Edit Frame** chọn geometry của khung. Thay đổi geometry này giữ vị trí nội dung.
5. **Select Contents** chọn nội dung nhưng giữ hình xem đã cắt. Alt-click vào một clip
   dùng đường chọn nội dung này; Alt-drag vẫn theo thao tác nhân bản hiện có.

Property bar ưu tiên Edit/Finish, Edit Frame, Move with Frame, Fit và Properties.
X/Y/W/H/Angle của một đối tượng được hiển thị theo tọa độ trang và đơn vị đang chọn.
**Properties > Objects > PowerClip** cung cấp toàn bộ điều khiển. Các mục Style và Align
hiện có vẫn truy cập được; mở Properties của PowerClip đưa về phần Objects.
Layers hiển thị cây cha/con, phân biệt Frame/Contents; chọn một child mở đúng scope cha.

## Nhãn và trợ giúp

Đường menu là **Object > PowerClip**. Property bar và Objects Properties dùng cùng semantic
action IDs và trạng thái enable. Nhãn chính: **Edit Contents / Sửa nội dung**,
**Finish Editing / Kết thúc sửa**, **Edit Frame / Sửa khung**,
**Move with Frame / Đi cùng khung**. Tooltip/focus help giải thích reveal, scope, cách fit
và rằng Move with Frame vẫn cho phép sửa content. Status hint ghi target frame, Escape hủy
target-pick và nội dung thao tác hiện tại. Control name là text; keyboard/action dispatch không
phụ thuộc hover. Không áp dụng SVG icon mới vì không thêm toolbox/toolbar tool hay icon-only
control; menu, property bar và Properties dùng nút chữ theo native shell.

## Các thao tác khác nhau

| Lệnh | Kết quả |
| --- | --- |
| Make from Selection | Đối tượng trên cùng theo painter order làm khung; cần cùng owner/layer. |
| Current Position | Không tự dịch chuyển hoặc đổi kích thước nội dung. |
| Center | Chỉ căn tâm. |
| Fit | Giữ tỉ lệ, dùng hệ số nhỏ hơn để nằm trọn khung. |
| Fill | Giữ tỉ lệ, dùng hệ số lớn hơn để phủ khung. |
| Stretch | Thay đổi hai chiều độc lập. |
| Move with Frame | Liên kết transform của nội dung với khung; không khóa quyền sửa child. |
| Extract Contents | Đưa nội dung ra owner ngoài, giữ PowerClip rỗng. |
| Release Clip | Bỏ container clip, giữ frame và nội dung như các đối tượng cùng owner. |
| Copy Contents From… | Chọn clip nguồn để thêm các bản sao vào khung đích, giữ nội dung đích đã có. |
| Create Empty Frame | Tạo PowerClip rỗng từ Path/Text đủ điều kiện. |
| Convert to Regular Object | Chỉ áp dụng với khung rỗng; khung có nội dung dùng Release. |

Make chỉ tự căn giữa khi toàn bộ nội dung nằm ngoài bounds của khung.
Place với Current Position luôn giữ vị trí. Make/Place và fit kèm theo là một Undo unit.
Duplicate/Copy cấp ID mới cho cây clone, giữ backing ảnh bất biến; transform chỉ áp vào các root được chọn.

## Hợp đồng UI và kỹ thuật

- Các action dùng tiền tố `action.object.powerclip.`: `place_inside`, `make`, `edit`,
  `finish`, `select_contents`, `edit_frame`, `extract`, `release`, `lock`, `fit_center`,
  `fit_prop`, `fill_prop`, `stretch`, `copy_from`, `create_empty_frame`, `remove_frame`,
  `preserve_appearance`, `properties`.
- Menu hiện là section PowerClip trong menu Object của native shell; không thêm toolbox,
  standard toolbar, dialog hoặc panel riêng. Điều khiển dùng label văn bản ngắn theo bề mặt
  native hiện có; không dùng emoji làm nút. Mini-toolbar canvas cũ đã bỏ.
- Paint/hit của điều khiển PowerClip dùng chung layout descriptor. Menu, bar và Properties
  dùng cùng action/enable state/command. Tooltip và status giải thích thao tác và liên kết khung.
- Empty/mixed/singular/locked target không được lấy phần tử đầu để sửa ngầm.
  Fit và Extract không bật khi khung rỗng. Frame source không nằm trong content selection.
- Session gắn document/epoch/page và chain owner; Enter/Finish không ghi document hoặc Undo.
  Revision thay đổi phải kiểm lại owner; gesture/target pick cũ bị từ chối.
- Tile/raster editing/import ảnh lớn là foundation khác. PowerClip thao tác placement và
  source reference, không chỉnh lại pixel hoặc flatten scene vector.
- CPU giới hạn scratch clip đang sống và cấp theo ROI thấy trên màn hình. Vượt ngân sách
  báo frame chưa hoàn chỉnh; không đổi hoặc xóa tài liệu.

## Định dạng và đầu ra

Native giữ hierarchy, ID, clip flags, nguồn Path/Text/Image và affine transforms.
Text làm frame dùng glyph outlines tạm thời; font/outline không đủ điều kiện thì từ chối,
không chuyển source text thành path ngầm.

SVG writer duyệt hierarchy/painter order, giữ geometric clip và fill rule, đổi tọa độ Y-up
sang Y-down ở biên định dạng. Raster nhúng trong SVG chưa có đường xuất đã kiểm chứng,
nên báo Unsupported trước output; có thể dùng native hoặc raster export. Raster export dùng
view bình thường, không lấy ảnh reveal của chế độ Edit. SVG reader hỗ trợ clipPath
userSpaceOnUse với một compound path; objectBoundingBox, nhiều child cần phép union và
forward reference chưa được ánh xạ phải báo rõ thay vì bỏ clip.

CDR dùng adapter libcdr/SVG riêng để normalize; PDF/AI PDF-compatible/EPS chưa được mở thêm
capability clip bởi thay đổi này. DXF không có PowerClip semantic tương đương.
Canvas clip không tạo đường cắt CNC hoặc tự trim geometry sản xuất.

Corpus chạy ngày 22/10/2026 có 4 file CDR thật; đều import được nhưng không file nào chứa
clipping group. Vì vậy CDR geometric-PowerClip import/edit roundtrip vẫn chưa được xác minh.
Pixel checks hiện có xác nhận ca màu đơn giản trên CPU, D2D hardware và WARP; chưa phải
independent renderer oracle cho toàn bộ alpha/hole/ICC/effect cases.
