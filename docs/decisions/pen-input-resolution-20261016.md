# Quyết định triển khai Pen input

Ngày 16/10/2026 · Task `pen-implementation-p01-semantics-20261016`.
**Đặc tả triển khai theo contract hiện tại; chưa thay contract chung hoặc chứng nhận consumer.**
Các module owner vẫn review trên task tích hợp trước khi tuyên bố capability nghiệm thu.

## Bối cảnh đã kiểm

- [D01 drawing semantics](../../contracts/drawing/semantics.json) khai báo mm/binary64, world Y up,
  CCW từ construction +X, các forms `@L<theta`, `@dx;dy`, `#x;y`, node modes và session stamps;
  trạng thái còn `proposed_pending_consumer_review`.
- [D06 parser](../../modules/vector/include/pikdraw/vector/precision/numeric/numeric_input.hpp) trả
  độ dài mm, góc deg, `ConstructionFrame{x_axis_angle_degrees}`; `#x;y` là world, `@` dùng frame quay,
  chưa có frame origin tự do.
- [D09 runtime](../../apps/desktop/src/shell/native_window.cpp) hiện nối Line/Polyline/Snap.
  Native input dùng Shift khóa hướng, Ctrl cho Ortho, F3 mở Snap controls; Pen, H/θ và World Y angle
  field chưa được nối.
- [PathNode model](../../core/document/include/pikdraw/document/model/editable_path.hpp) không cho
  Corner/AutoSmooth có stored handles; Smooth/Symmetric cần hai handle hợp lệ đối chiều.

Các mô tả D01/D10/D11 trong tài liệu cũ là hợp đồng/roadmap, không là tính năng Pen đã có.

## Quyết định input

1. **Không đổi grammar D06.** `@L<θ` luôn đo θ CCW từ +X của `ConstructionFrame` (mặc định world
   +X), kể cả khi HUD đang hiển thị góc so với +Y. `@dx;dy` quay delta theo frame hiện hành;
   `#x;y` luôn là world XY tuyệt đối.
2. **Angle field có reference rõ.** `World X`, `World Y`, `Frame X` hoặc `Previous segment` là cách
   nhập/hiển thị của Pen. Giá trị angle chuyển thành một world ray trước khi tạo candidate; đổi cách
   hiển thị không đổi world ray đã khóa. World Y +30° CCW hướng về trái trên, tức world θ=120°.
3. **Length `L` là dây cung anchor-to-anchor.** Trên segment Line, khoảng cách thực đúng L; trên
   segment Cubic, L khóa khoảng cách giữa anchors, không khóa chiều dài đường cong. Handle length `H`
   đo từ anchor đến control point đang sửa.
4. **Một nguồn ràng buộc cho một candidate.** Typed locks → hard direction/profile modifier → snap tương
   thích → Polar/grid acquisition → pointer thô. Numeric value không bị snap/Shift/Ctrl âm thầm thay.
   Nếu drivers cùng xác định endpoint thì kiểm consistency; mâu thuẫn giữ lần preview hợp lệ trước,
   báo field gây xung đột và chặn xác nhận. Không tự ưu tiên tọa độ cuối hoặc nới epsilon.
5. **Solver mốc A giới hạn.** L+θ cho endpoint trực tiếp; XY/delta pair cho endpoint; L một mình để
   pointer chọn ray; θ một mình để pointer chọn bán kính. L+ΔX cho hai nhánh ±ΔY; pointer chọn nhánh,
   không có nhánh xác định thì yêu cầu chọn. Trường hợp vô nghiệm, overflow, dimension sai, tọa độ ngoài
   ±1.000.000 mm hoặc NaN/Inf không thêm node.
6. Numeric field là focus scope riêng: Tab/Shift+Tab chuyển field, Enter áp dụng đúng candidate hiện
   tại, Esc khôi phục candidate trước lần sửa. Text/IME và caret luôn thắng shortcut canvas; deg/rad
   không bị xử lý như phím chọn tool. Blur/mở Properties/pan giữ draft và text theo lifecycle.

## Quyết định node và modifier

| Profile/context | Binding mục tiêu khi Pen/Node được nối | Ranh giới |
| --- | --- | --- |
| PikDraw / Illustrator-inspired, drag anchor | Shift hard-lock hướng theo bước profile | Không dùng để đổi node mode hoặc toggle snap |
| Corel-inspired, drag anchor | Ctrl hard-lock hướng theo bước profile | Không gán lại Ctrl+Z/chord reserved |
| Pen handle drag | Alt tách In/Out; Shift (PikDraw) hoặc Ctrl (Corel) khóa hướng; kết hợp tách + khóa | Tách chỉ trong gesture handle |
| Tạm sang Node khi Pen draft còn active | Giữ Ctrl (PikDraw) hoặc giữ F2 (Corel) trước pointer down | Chỉ draft hiện tại; không bắt chord khi focus numeric/IME |
| Node selection | Shift+click multi-select | Không cùng pointer event đặt anchor Pen |
| Snap acquisition | S giữ để bypass automatic snap khi canvas focus, sau conflict audit | Không bỏ numeric locks, Ortho/Polar, hoặc biến marker thành magnet |
| Pan trong Pen/Node | Space+drag hoặc middle mouse | Suspend/resume draft; không tăng document revision |

Binding trong bảng là **profile target**, chưa quảng cáo trong runtime trước khi từng key/pointer
sequence được kiểm. PikDraw bản hiện tại dùng Ctrl cho Ortho ở D09. P07 phải phân giải conflict theo
pointer phase/focus trước khi dùng Ctrl làm temporary Node trong Pen; có thể để thao tác tạm qua nút
Node/action trên property bar cho tới khi router chứng minh chord. Không đổi hành vi Line, shortcut
toàn cục hoặc binding F3 trong task Pen.

F3 hiện mở bảng Snap theo handler D09; contract mô tả F3 toggle snap master. Dùng action controls
đang có làm nguồn thực tế và tạo task tích hợp riêng nếu muốn đồng bộ semantics F3; Pen không tự đổi.
Pen lọc numeric-incompatible candidates trước khi chọn snap, dùng snap stamp của D07; relation chưa
có capability thì disable, không giả marker.

## Geometry/source constraints

- Click creates Corner/Line. Drag/HandleAdjust tạo Cubic với endpoint mode hợp model. Một đầu chỉ có
  outgoing hoặc incoming handle thì lưu Cusp, không khai Smooth bất đối xứng.
- Corner không mang handle. Smooth có incoming/outgoing collinear, đối hướng, dài độc lập; Symmetric
  thêm hai độ dài bằng nhau; không hứa C2. H=0 trong Cusp là remove handle; zero pair không lưu thành
  Smooth/Symmetric invalid. AutoSmooth chưa thuộc mốc A.
- Preview chỉ là tool state. Finish/new path là một atomic document command/revision; cancel không có
  command. Khi resume object/subpath cũ, giữ stable IDs/style; undo hoặc source revision khác làm
  proposal cũ stale, không auto-rebase.
- Pan/zoom/rotate camera không đổi world coordinates. Object transform áp dụng nghịch đảo chính xác
  lên world input; singular transform bị từ chối. Không biến Path2D flatten hay screenshot pixel thành
  nguồn/geometry oracle.

## Câu hỏi cần consumer review trước khi khóa D01

1. D09 hiện dùng Shift-angle/Ctrl-Ortho, trong khi D01 profile đề nghị Ctrl temporary Node cho Pen.
   UI Desktop/Vector phải quyết định mapping theo phase mà không phá Ortho hay chord dành riêng.
2. D01 source schema cho Corner không handle và Smooth bắt buộc hai handle; UI cần chỉ rõ chuyển Cusp
   khi bỏ một handle và xử lý click/drag ở node đầu/cuối.
3. D06 frame chỉ có góc trục X; origin local tự do cần DTO/contract extension và vectors mới ở task
   riêng, không thể giả rằng parser hiện tại đã hỗ trợ.
4. `@L<θ` angle X-fixed là quy tắc grammar có chủ đích. Trước triển khai desktop cần consumer test để
   người dùng không nhập nhầm khi field đang hiển thị World Y; HUD phải đọc được reference grammar.
5. Policy Esc/Backspace draft cần so khớp ToolSession/D01 và native D09 actual handlers trước khi
   feature claim tương thích.

Không sửa semantic JSON/ADR, IDs, CMake hoặc runtime trong task P01. D01 owner/affected consumers
quyết định có cần version/schema delta hay không trong một task có phạm vi và review cụ thể.
