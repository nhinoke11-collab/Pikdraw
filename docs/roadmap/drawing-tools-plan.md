# PikDraw — Kế hoạch công cụ vẽ, Pen, sửa node và hình học kỹ thuật 2D

> Tài liệu thiết kế ngày 15/10/2026; inventory source và trạng thái Dxx bên dưới là lịch sử. Pen core/controller/numeric hiện có trong review; P07 shell hookup còn thiếu và Node chưa triển khai. Xem [Pen README](pen-tool-20261016/README.md), project state và task board để biết hiện trạng. Với thay đổi UI, ghi spec theo [chuẩn UX/UI](../design/ui-ux-standard.md); agent tự quyết lựa chọn thường lệ trong scope.


Ngày: 2026-10-15. Trạng thái lúc viết: **đặc tả đề xuất**.
Catalog thực hiện: [drawing-tools-tasks.json](drawing-tools-tasks.json). Bổ sung nhóm điều khiển snap/modifier/shape trong mục 3.4–3.7; đây vẫn là kế hoạch chưa triển khai.

## 1. Phạm vi và bằng chứng hiện tại

Mục tiêu là một hệ công cụ dùng chung cho thiết kế vector và bản vẽ kỹ thuật: vẽ bằng chuột liên tục, nhập số khi cần, sửa lại hình học nguồn, lưu/mở và undo đầy đủ. Pen và Node là ưu tiên sản phẩm. Các mốc bên dưới là cổng nghiệm thu, không phải lời hứa đã đạt ngang toàn bộ Illustrator/CorelDRAW/AutoCAD.

Baseline Git: `ed5bd9c4f0dcb9b8c8bef749863e052c3d299419` và kiểm tra đọc working checkout main ngày lập kế hoạch. Main có các chỉnh sửa desktop chưa commit. Nhánh sửa pick/style `agent/codex/editor-pick-style-delivery-20261015`, commit `970273d`, đang chờ tích hợp. Kế hoạch không coi nhánh đó đã nằm trong main; D00 phải kiểm kê, review và chọn revision tích hợp trước khi tạo worktree implementation mới. Không ghi đè phần desktop đang sửa.

Các phát hiện từ mã, cần xác nhận lại trên revision tích hợp:

| Hạng mục | Thực tế hiện có | Khoảng trống |
| --- | --- | --- |
| Path nguồn | `core/geometry/.../curves/path.hpp`: Line/Quad/Cubic/Arc/Close, tọa độ double | Chưa có ID bền cho node/segment/subpath, loại node và lifecycle sửa node |
| Cung và biến đổi | `src/flattening/flattening.cpp` biến ArcTo thành dây cung; `Path2D::transformed` chỉ map các điểm/control | Phải sửa evaluator, bounds, flattening và affine transform của arc trước khi công bố công cụ CAD |
| Boolean/offset | `src/operations/boolean.cpp`, `offset.cpp` dùng bounds của path | Không đủ cho Weld/Trim/Contour hoặc bù dao thật |
| Tương tác | Select/drag, rectangle/ellipse, pan/zoom, workflow chỉnh một số thuộc tính | Chưa có phiên vẽ đường nhiều đoạn, Pen Bézier, Pencil hay Node editor hoàn chỉnh |
| Selection | Main có hit-test theo bounds; nhánh pick/style chứa sửa riêng | Phải thống nhất hit-test, khóa/ẩn layer, thứ tự vẽ và chọn vùng trước khi nối Node UI |
| Text/màu | Có dữ liệu/API nhưng UI còn thiếu | Nằm trong backlog UX riêng; không chặn phát triển kernel/interaction của Pen |
| Production/3D | Module ứng dụng còn planned | Không được biến thông số UI thành tuyên bố có CAM, G-code hoặc CAD constraint solver |

Không lấy `project-state.json`, số lượng action hoặc test harness làm chứng cứ hoạt động. Nhiều handler trong `desktop_window.cpp` còn rỗng; mỗi công cụ phải có đường đi thật từ input → preview → command → snapshot → render → save/reopen.

## 2. Cấu trúc nhóm công cụ

Toolbar dùng icon vector và tooltip có tên/phím tắt; nhấn giữ hoặc bấm mũi tên mở nhóm. Mỗi công cụ có entry trong menu/command search để không phụ thuộc phím tắt ẩn. Bảng thuộc tính phía trên thay theo công cụ; panel phải chỉ hiện tham số có hiệu lực.

| Nhóm | Công cụ | Mốc |
| --- | --- | --- |
| Chọn | Pick, Direct Selection/Node, chọn vùng Window/Crossing, Lasso node | R1–R2 |
| Đường thẳng | Line hai điểm; Polyline liên tục line/arc; Construction Line/Ray | Line/Polyline R1; arc/construction R3 |
| Đường cong | Pen Bézier; thêm/xóa/đổi loại node; Curvature; Pencil/Freehand; Smooth | Pen/Node R2; Pencil/Curvature R3 |
| Hình cơ bản | Rectangle/rounded rectangle; Circle/Ellipse; Polygon/Star | Hình chính xác R3; giữ rectangle/ellipse hiện có trong R1 |
| Hình học | Join/Break/Reverse; Trim/Extend; Weld/Intersect/Subtract/XOR; Offset; Fillet/Chamfer | R2 hoặc R4 theo kernel |
| Biến đổi | Move/Copy, Rotate, Scale, Mirror, Align/Distribute, linear/polar Array | R4 |
| Kỹ thuật | Đo chiều dài/góc/diện tích; kích thước chú thích; tuyến cơ khí 2D, bán kính uốn và kiểm tra khoảng hở | R5 |

Line tạo một đoạn độc lập rồi sẵn sàng vẽ đoạn tiếp. Polyline giữ cùng một đối tượng gồm nhiều đoạn nối tiếp. Pen có thể trộn đoạn thẳng và cubic Bézier trong một path. Cả ba dùng chung nhập số, snapping, preview và command lifecycle.

Chỉ hiện nút có đường đi hoạt động; capability chưa đạt được ẩn hoặc disable kèm lý do cụ thể. Không tạo menu dài với handler rỗng.

## 3. Quy tắc nhập liệu dùng chung

### 3.1 Phiên vẽ và trạng thái

`ToolSession` giữ draft riêng, một document ID/epoch/base revision và generation. Preview không ghi document và không tạo undo record mỗi mouse move. Draft gồm các đoạn đã xác nhận trong phiên và candidate cuối đang bám chuột. Khi finish, commit một command/transaction; khi hủy toàn phiên, document giữ nguyên. Đường đã finish vẫn sửa node được.

Các trạng thái tối thiểu: Idle → AwaitFirstPoint → PreviewSegment → DragHandle hoặc EditNumeric → PreviewSegment → Finish/Cancel. PanSuspended lưu chính xác trạng thái và pointer capture để quay lại. Chuyển tab/document, close, undo ngoài phiên, capture lost hoặc tool replacement phải kết thúc lifecycle theo quy tắc bên dưới.

| Input/tình huống | Hành vi bắt buộc |
| --- | --- |
| Click điểm đầu | Bắt đầu draft; chưa tạo đối tượng rỗng trong document |
| Click điểm tiếp | Xác nhận candidate; hiển thị preview đoạn mới |
| Drag khi đặt node Pen | Đặt anchor và điều chỉnh handle; không di chuyển anchor theo tay kéo |
| Enter khi đang nhập số hợp lệ | Đặt điểm/handle theo số đúng một lần; tiếp tục phiên |
| Enter khi không có ô nhập | Finish path mở; Line giữ công cụ cho đoạn độc lập kế tiếp |
| Esc trong ô nhập số | Bỏ phần đang gõ, khôi phục candidate/locks trước khi mở ô; chưa finish path |
| Esc khi kéo node/handle chưa thả | Hủy gesture hiện tại, khôi phục draft trước gesture; phiên còn mở |
| Esc khi preview đoạn mới | Bỏ candidate, finish các đoạn đã xác nhận thành path mở; draft chỉ có một điểm thì bỏ |
| Esc khi công cụ chờ điểm đầu | Thoát phiên, về Pick; Esc lúc Idle chỉ bỏ lựa chọn theo context |
| Hủy toàn phiên | Lệnh rõ ràng trong context menu và `Shift+Esc`; không để người dùng phải undo từng node |
| Backspace/Ctrl+Z trong draft | Undo bước draft gần nhất; có draft redo; không sửa lịch sử document phía dưới |
| Ctrl+Z khi không còn draft | Đi vào undo document; invalidate mọi preview/job cũ |
| Click node đầu với dấu đóng path | Đóng path thật bằng topology, không chèn đoạn trùng/zero length |
| Double click | Không được tạo thêm node trùng do chuỗi down/up/double-click; tùy công cụ: finish Pencil/Polyline hoặc node Corner của Curvature |
| Space kéo / chuột giữa / wheel | Tạm pan/zoom, tiếp tục đúng draft và các numeric locks; không đổi world coordinates của điểm đã đặt |
| Mất capture/focus khi đang drag | Rollback gesture chưa xác nhận, bỏ trạng thái phím modifier, giữ draft trước drag để tiếp tục/hủy |
| Chuyển công cụ chủ động | Finish draft hợp lệ; draft không hợp lệ giữ lại và báo lý do; có thao tác hủy toàn phiên rõ ràng |
| Chuyển tab/mở file/đóng tài liệu | Finish hoặc hỏi lưu/hủy draft theo lifecycle trước chuyển; tăng epoch/generation và cấm proposal muộn ghi nhầm tài liệu |

Lệnh Esc kết thúc path mở là mặc định đáp ứng thói quen vẽ CAD. Nếu thêm profile tương thích về sau, semantic gốc của command vẫn cố định; adapter phím chỉ ánh xạ input. Không sao chép toàn bộ shortcut ba sản phẩm gây xung đột.

Phiên vẽ dài có giới hạn samples/draft bytes và checkpoint phục hồi draft riêng qua storage service. Checkpoint không công bố path chưa finish là tài liệu đã lưu; sau crash người dùng chọn khôi phục hoặc bỏ draft trên base phù hợp. D05 phải phân biệt saved document, recovered draft và dirty state; không dùng auto-finish âm thầm làm cơ chế tiết kiệm bộ nhớ.

### 3.2 Nhập chiều dài, góc và tọa độ mà vẫn vẽ liên tục

HUD nhỏ đặt lệch con trỏ, tránh che điểm bắt. Khi chưa gõ, HUD chỉ hiển thị; nó không tự chiếm focus. Gõ số hoặc Tab trong canvas mới mở ô nhập. Click tiếp hoặc Enter đặt điểm; không mở modal.

```text
Thanh ngữ cảnh:  Pen | Corner / Smooth / Symmetric | Snap | Finish | Cancel draft

    anchor đã đặt ●──────────── preview ────────────╳ candidate
                                               L [250 mm 🔒]
                                               θ [30°    🔒]
                                               [Polar] [Relative]

Trạng thái: Enter đặt điểm · Tab đổi ô · Esc bỏ nhập/finish theo trạng thái · Space pan
```

Hai cách bố trí dùng chung dữ liệu:

- Tự do: chỉ hiện phép đo nhẹ, handle và snap hint. Các ô nhập xuất hiện khi cần.
- Kỹ thuật: luôn hiện `L`, `θ` hoặc `X`, `Y`; chip khóa, Ortho/Polar/OSnap và đường chiếu phụ. Có thể đổi ngay trong phiên.

Quy tắc tọa độ: world mm/binary64; hệ dựng hình 2D có gốc/trục quay riêng. Góc dương ngược chiều kim đồng hồ từ trục X của hệ dựng hình; chỉ adapter viewport đổi Y sang chiều màn hình. HUD ghi rõ World/Local và Absolute/Relative. View rotate không tự xoay hệ dựng hình.

| Cú pháp/điều khiển đề xuất | Ý nghĩa |
| --- | --- |
| `250`, Tab, `30`, Enter | Khóa L = 250 mm, θ = 30°, đặt endpoint rồi tiếp tục |
| `@250<30` | Polar tương đối từ node cuối, góc tuyệt đối theo hệ dựng hình |
| `@20;15` | Delta X/Y tương đối |
| `#120;80` | Tọa độ tuyệt đối trong hệ dựng hình đang chọn |
| `12.5cm`, `1in`, `24pt` | Đổi đơn vị có kiểm tra; inch = 25,4 mm, pt = inch/72 |
| Tab / Shift+Tab | Chuyển trường đang hiện; không rời canvas ngoài ý muốn |
| Biểu thức `2*125mm`, `(100mm+20mm)/2` | Parser số có kiểu đơn vị, grammar giới hạn; không dùng eval/script |
| Kéo chuột khi L đã khóa | Chuột chỉ quyết định góc; khi θ đã khóa, chuột chỉ quyết định L |

Số thập phân theo locale có thể dùng dấu chấm hoặc dấu phẩy nếu parser không mơ hồ; cặp tọa độ luôn dùng dấu chấm phẩy. Không nhận dấu phân nhóm hàng nghìn. NaN/Inf, phép chia 0, sai đơn vị, số quá miền hỗ trợ, L âm và input thiếu phải giữ ô nhập, báo tại chỗ và không commit.

Numeric locks chỉ áp dụng candidate hiện tại; sau khi đặt điểm chúng được bỏ. `Repeat last length/angle` là lựa chọn chủ động có chỉ báo. `L` của endpoint Pen là **độ dài dây cung giữa hai anchor**, không phải chiều dài đường cong. Khi chỉnh handle, HUD ghi `H` và góc handle. Độ dài cong thực chỉ là kết quả đo cho tới khi có bài toán inverse-curve riêng được nghiệm thu.

Sau đặt anchor bằng số, người dùng có thể dùng action `Chỉnh handle` ngay trên HUD để chuyển vào HandleAdjust, nhập H/θ, Enter rồi tiếp tục Pen. Không ép giữ chuột trong lúc gõ. Node type và phương án gãy/smooth hiện trên HUD và property bar.

Ưu tiên input: số đã khóa → ràng buộc chủ động của công cụ → snap tương thích → polar/grid → vị trí chuột. Snap không được ghi đè giá trị đã khóa. Nếu hai điều kiện không có nghiệm, giữ candidate cuối hợp lệ và chỉ ra điều kiện xung đột; không tự nới sai số để nhận điểm.

Phím số chỉ được bắt khi canvas/ToolSession có focus. Text editor, numeric control khác và IME composition được ưu tiên. Ô nhập có caret, select/copy/paste và đơn vị truy cập được bằng bàn phím; icon không là cách duy nhất nhận biết khóa.

### 3.3 Modifier và bắt điểm

Các phím `P` Pen, `N` Pencil, `L` Line, `Shift+L` Polyline, `F2` Node, `V` Pick và `F3` Snap chỉ là ứng viên, chưa được coi là binding mặc định cho tới khi D09 kiểm kê action registry và shortcut handler thực tế. Không gán F8/F10/Shift+F2 cho Ortho, Polar hoặc HUD: giao diện tham chiếu hiện quảng bá F8 Text, F10 Node và Shift+F2 Zoom to Selection. D09 phải kiểm chứng từng binding; nếu handler không hỗ trợ thì hoặc triển khai binding có kiểm thử, hoặc bỏ nhãn phím cũ khỏi UI trong cùng phạm vi. Ortho/Polar/HUD luôn có action có tên trong toolbar/menu và có thể remap; binding chưa kiểm chứng để trống.

Trong hồ sơ mặc định PikDraw, Shift ràng góc khi không có θ nhập số. Alt khi thao tác handle Pen/Node tách hai tay nắm. Ctrl tạm chuyển Direct Selection trong Pen rồi quay lại phiên khi chưa kéo handle. Space/chuột giữa dành cho pan. Các hồ sơ và tổ hợp phím theo context được khóa ở mục 3.5. Chọn nhiều node dùng Shift+click trong Node; bảng trạng thái giải thích action theo context.

Direct Selection tạm sửa node thuộc draft hiện tại vào cùng local history. Muốn sửa đối tượng committed khác phải finish/hủy draft trước; không âm thầm thay base revision rồi rebase draft. Một thay đổi document từ bên ngoài phiên làm proposal cũ mất hiệu lực và cho phép người dùng chạy lại thao tác trên revision mới.

Snap radius mặc định đề xuất 8 DIP; chuyển sang world theo camera/DPI, không dùng một tolerance mm cố định ở mọi zoom. Candidate chứa source ObjectId/NodeId/SegmentId, loại snap, tham số t, vị trí world và revision. Lọc theo numeric locks/ràng buộc chủ động trước; chỉ các nghiệm tương thích mới được xếp hạng theo snap override của người dùng → loại ưu tiên → khoảng cách màn hình → thứ tự source ID ổn định. Có hysteresis để hint không nhảy liên tục; ngưỡng được khóa bằng fixture ở D01.

R1: Endpoint, midpoint đoạn thẳng, grid, trục ngang/dọc, polar increment. R3/R4: center, quadrant, analytic intersection, perpendicular, tangent, extension, parallel, nearest on curve và tracking từ các điểm tham chiếu đã nhận. Midpoint Bézier theo t=0,5 và điểm nửa chiều dài phải là hai khái niệm có tên riêng. Không bắt vào bbox thay hình học thật.

Default chỉ xét hình nhìn thấy, dùng làm tham chiếu được; đối tượng/layer khóa có thể làm reference khi bật tùy chọn, nhưng không được sửa. Snap xuyên layer ẩn/tài liệu khác mặc định tắt. Near-tangent, nhiều nghiệm và trùng hình phải có preview lựa chọn, không đoán âm thầm.

### 3.4 Bảng bật/tắt và cấu hình bắt điểm

Có nút nam châm trên thanh trạng thái: click bật/tắt snap tổng, mũi tên mở popover các checkbox, action riêng mở bảng cấu hình có thể ghim bên cạnh canvas. Tắt tổng giữ nguyên mọi lựa chọn con để bật lại đúng cấu hình trước đó. Tắt một nhóm cũng giữ mask loại snap của nhóm; `Chọn tất cả/Bỏ chọn tất cả` là action riêng, không gộp vào nút bật nhóm.

```text
[x] Bắt điểm đối tượng / lưới / đường dóng                 F3
  [x] Đối tượng    [x] Endpoint  [x] Midpoint  [ ] Nearest
                  [x] Center    [ ] Quadrant  [ ] Intersection
  [ ] Quan hệ     [ ] Tangent   [ ] Perpendicular  [ ] Parallel
  [ ] Trang/Lưới  [ ] Grid      [ ] Page edge      [ ] Guide

Hướng: ( ) Tự do  ( ) Ortho  (o) Polar   Bước [45° v]
[x] Hiện gợi ý   [x] Hiện loại bắt điểm   Bán kính hút [8 DIP]
Hồ sơ phím [PikDraw v]   Công cụ [Theo cấu hình chung v]
```

Sơ đồ minh họa trạng thái một cấu hình, không phải toàn bộ mặc định và không chứng minh UI hiện có. Các tùy chọn nâng cao chỉ được chọn khi runtime capability tương ứng đã nghiệm thu; phần chưa có hiển thị disabled kèm lý do và mốc R1/R3/R4.

| Nhóm checkbox độc lập | Lựa chọn |
| --- | --- |
| Điểm trên đối tượng | Endpoint, node nguồn, midpoint đoạn thẳng, center analytic, quadrant, nearest; Bézier t-midpoint và half-length là lựa chọn có tên riêng |
| Quan hệ hình học tự nhận | Intersection, perpendicular, tangent, parallel, extension, tracking từ reference points |
| Trang và tham chiếu | Page edge/center, guides, construction geometry, grid; snap bbox edge/center nếu thêm phải là lựa chọn riêng có nhãn rõ và mặc định tắt |
| Hướng | Free / Ortho / Polar; bước góc 15°/30°/45°/90° hoặc danh sách góc tùy chọn; hướng tham chiếu World/Local X hoặc đoạn trước |
| Hiển thị | Marker, tooltip loại snap/đối tượng, đường dóng, kích thước marker, màu tương phản, radius DIP, snap acquisition/release hysteresis |
| Chính sách đối tượng | Current layer/all visible layers, locked-as-reference, snap vào chính path đang vẽ, loại trừ handle/control point hoặc chỉ nhận khi sửa node |
| Theo công cụ | Dùng cấu hình chung hoặc override cho Line/Polyline/Pen/Node/Shapes/Pencil; nút khôi phục riêng công cụ |

`F3` là binding ứng viên cho automatic snapping của đối tượng/quan hệ/trang/grid/construction và chỉ được đăng ký nếu D09 xác nhận không chiếm một lệnh hiện có. Ortho/Polar và khóa số là điều kiện dựng hình riêng; tắt snap không bỏ các điều kiện này. UI phải hiển thị đồng thời, ví dụ `Snap OFF · Polar 45°`. Hướng dùng một enum Free/Ortho/Polar: action có tên bật Ortho thay Polar và ngược lại; bật mode đang hoạt động lần nữa trở về Free. Binding phím cho các action hướng chưa mặc định cho tới khi hoàn tất audit. Smart guides có thể vẫn hiển thị khi snap tắt, nhưng không được kéo điểm ngầm.

Mặc định đề xuất: Vector bật endpoint/node/straight-midpoint và guides đã có capability; grid, auto tangent/perpendicular và Polar tắt. Kỹ thuật bật cùng bộ đó và Polar 45°, bổ sung center/quadrant/intersection khi R3 đạt; relation nặng opt-in. Node ưu tiên node/handle của path đang sửa. Pencil chỉ snap điểm đầu/cuối khi bật, không hút mọi mẫu nét vẽ; auto-close là checkbox khác mặc định tắt và phải có preview.

Nguồn cấu hình: hồ sơ người dùng/workspace → override theo công cụ → modifier tạm của gesture. `EffectiveSnapConfig` bất biến ghi config generation, tool context, capability generation và document revision. Job query cũ sau đổi mask/F3/profile không được công bố hint hay candidate. Không cần tạo document revision cho thay đổi checkbox/phím; preferences lưu riêng người dùng. Page guides/construction geometry và các tham số hình học đã commit vẫn là dữ liệu tài liệu có undo/persistence.

Loại snap/nhóm đã tắt phải được lọc trước khi chạy query nặng. Intersection/tangent chỉ xét tập ứng viên cục bộ từ spatial index và reference đang dùng, không tính mọi cặp đối tượng mỗi frame. Có quota/time budget và preview pending rõ ràng; query chưa có nghiệm kiểm chứng không được hiện marker chắc chắn.

### 3.5 Hồ sơ Ctrl/Shift/Alt và quy tắc tổ hợp

Tài liệu chính thức thể hiện hai thói quen khác nhau: [CorelDRAW constrain objects](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Constrain-objects.html) mô tả Ctrl cho constrain, Shift cho vẽ từ tâm; [Adobe keyboard shortcuts](https://helpx.adobe.com/illustrator/using/default-keyboard-shortcuts.html) mô tả Shift constrain và Alt kéo từ tâm cho các shape áp dụng. Vì vậy PikDraw cung cấp hồ sơ `PikDraw`, `Illustrator-inspired`, `Corel-inspired`, `Custom`; đây là các ánh xạ có phạm vi, không tuyên bố tương thích mọi phím của sản phẩm tham khảo.

| Context/action | PikDraw / Illustrator-inspired | Corel-inspired |
| --- | --- | --- |
| Line/Polyline/Pen: khóa hướng tạm | Giữ Shift | Giữ Ctrl |
| Rectangle/Ellipse: giữ tỷ lệ 1:1 | Giữ Shift | Giữ Ctrl |
| Rectangle/Ellipse: vẽ từ tâm | Giữ Alt | Giữ Shift |
| Từ tâm + tỷ lệ 1:1 | Shift+Alt | Ctrl+Shift |
| Polygon/Star: khóa góc quay | Giữ Shift | Giữ Ctrl |
| Kéo node: khóa hướng chuyển động | Giữ Shift | Giữ Ctrl |
| Pick Move/Rotate: khóa hướng/góc | Giữ Shift | Giữ Ctrl |
| Pick Scale: giữ tỷ lệ, scale từ tâm | Shift, Alt; kết hợp Shift+Alt | Ctrl, Shift; kết hợp Ctrl+Shift |
| Kéo handle: khóa góc handle | Giữ Shift | Giữ Ctrl |
| Kéo handle: tách hai tay nắm | Giữ Alt; Shift+Alt vừa khóa góc vừa tách | Giữ Alt; Ctrl+Alt vừa khóa góc vừa tách |
| Pen: Direct Selection tạm khi chưa drag | Giữ Ctrl | Giữ F2 trong phiên Pen; thả để về Pen |
| Node: thêm/bỏ chọn một node | Shift+click | Shift+click |
| Tạm bỏ automatic snap trong gesture | Giữ S khi canvas có focus | Giữ S khi canvas có focus |
| Pan tạm | Space+drag / chuột giữa | Space+drag / chuột giữa |

F2 ngoài một phiên Pen đang mở là action chuyển sang Node như bảng công cụ chung. Trong Corel-inspired, giữ F2 khi Pen đang mở là chế độ Node tạm trên cùng draft; key repeat không tạo nhiều session. Mọi action có nút/menu tương đương và được remap. `S` không bị bắt trong ô text/số hoặc IME. Nếu custom profile muốn dùng modifier khác để bỏ snap, validator phải kiểm tra context và AltGr trước khi nhận.

Không dùng Alt toàn cục cho cả bỏ snap và tách handle. Trong handle drag của hồ sơ mặc định, Ctrl không khởi động Direct Selection giữa gesture. Khi đã vào Node tạm, Shift phục vụ multi-selection hoặc hướng node theo pointer phase, không đồng thời tạo anchor Pen. Chord hợp lệ thực hiện một action hoặc composite action đã định nghĩa; profile xung đột bị báo và không được áp.

InputRouter resolve theo `focus scope → tool → pointer phase → exact chord → named action`. Text/numeric/IME scope ưu tiên trước canvas; nhận diện AltGr, các shortcut hệ thống và key repeat. Ctrl+Z/Ctrl+A và các lệnh discrete đã nhận phải được xử lý trước action modifier kéo, không vô tình đặt điểm hoặc kích hoạt công cụ khác. Press/release modifier trong lúc drag phải tính preview từ anchor/reference gốc và raw pointer, không dùng kết quả đã snap của frame trước làm nguồn. Mất focus/capture, Esc hoặc release ngoài cửa sổ phải xóa trạng thái modifier để tránh kẹt phím. Đổi profile lúc đang drag được xếp pending, có nhãn rõ; gesture hiện tại dùng binding cũ, profile mới kích hoạt ở gesture kế tiếp sau khi xóa trạng thái phím tạm. UI luôn hiển thị các tổ hợp đang có hiệu lực.

### 3.6 Hướng cố định và xử lý xung đột

Khóa hướng tạm chọn góc gần nhất trong bộ đã cấu hình, ví dụ bước 45° tạo 0/45/90/135/180/225/270/315°. Người dùng được chọn bộ 15°, 30°, 45°, 90° hoặc danh sách như 0/22,5/90/135°, đặt offset trục và chọn tham chiếu theo đoạn trước. 360° chuẩn hóa về 0°. Bộ trống, góc không hữu hạn hoặc bước không hợp lệ phải từ chối.

Polar tự nhận hướng khi con trỏ vào vùng hút; khóa hướng bằng modifier giữ hướng trong toàn gesture theo bộ góc. D01 phải chốt ngưỡng acquisition/release có hysteresis và giới hạn rất gần gốc để không nhảy góc khi đoạn gần zero. Xem trước đường chiếu, marker và nhãn góc thực tế, không chỉ vẽ một đường dóng đẹp.

Số θ do người dùng nhập là khóa chủ động ưu tiên cao; modifier không sửa θ đó. Snap target ngoài đường đã khóa không được hút endpoint khỏi hướng; chỉ chọn nghiệm tương thích trong tolerance số học đã khóa. Khi khóa circle/square và người dùng nhập W/H khác nhau, UI báo xung đột, cho phép bỏ khóa hoặc sửa số, không âm thầm đổi kích thước. Giữ S chỉ bỏ auto-acquisition; numeric, circle/square và hướng khóa bằng modifier vẫn còn tác dụng.

Ví dụ: Line Shift kéo 45°, gõ L=100mm rồi Enter tạo đoạn dài 100mm theo góc 45°; Alt kéo Ellipse từ một center đã bắt điểm, thêm Shift thành Circle; release Shift quay lại ellipse từ cùng center. Việc nhấn/thả phím không tạo thêm node hoặc undo unit.

### 3.7 Hình từ tâm, đồng tâm và đa giác đều

| Năng lực | Thao tác/tham số |
| --- | --- |
| Hình từ tâm | Rectangle/Ellipse hỗ trợ corner hoặc center; modifier tạm và nút khóa mode From center. Circle theo center-radius và Polygon theo center-vertex vốn đã từ tâm; center modifier không được di chuyển lại tâm |
| Giữ tỷ lệ | Rectangle → square; Ellipse → circle. Shape đã có bản chất circle/regular polygon vẫn giữ định nghĩa nguồn, không bị kéo thành hình tự do |
| Đồng tâm | Bắt center analytic của circle/ellipse/polygon hoặc nhập center XY; action `Dùng tâm đối tượng này`, tùy chọn `Giữ tâm cho hình tiếp theo` có chỉ báo và Esc kết thúc chế độ |
| Đa giác đều | N cạnh, center, radius ngoại tiếp R hoặc bán kính nội tiếp r hoặc chiều dài cạnh a, rotation; vertices tính từ cùng center và một radius |
| Chỉnh khi đang kéo | Up/Down tăng/giảm N khi canvas có focus; ô N cho nhập chính xác. Mục tiêu giới hạn N=3..512 trước khi D01 đo/review; không nhận N lẻ kiểu 5,5 hoặc N<3 |
| Định hướng đa giác | Vertex-up hoặc edge-up, khóa rotation theo bộ góc; circumscribed/inscribed có nhãn đúng radius đang điều khiển |
| Sau khi tạo | Sửa N/R/r/a/rotation/center qua property bar; giữ nguồn parametric cho tới action Convert to path có chủ ý |

Công thức oracle: `a = 2R sin(π/N)`, `r = R cos(π/N)`. Đổi R/r/a giữ N/center/rotation; sai số và trường hợp giới hạn theo D01. Không gọi star là đa giác đều lồi; star có số đỉnh và hai bán kính riêng.

Center snap là trùng vị trí tại lúc tạo. Hai hình đã bắt cùng tâm vẫn độc lập sau khi di chuyển một hình. Quan hệ concentric tồn tại lâu dài thuộc solver R6 hoặc feature graph riêng đã review, có biểu tượng phân biệt. `Giữ tâm` chỉ là session reference tới source ID/revision; source di chuyển/xóa/đổi tài liệu thì invalidate và yêu cầu chọn lại, không tiếp tục dùng tâm stale. Bbox center không được trình bày như center analytic.

Shape cùng hệ dựng hình dùng đơn vị thật và affine semantics đúng: một circle bị nonuniform scale khi đánh giá trở thành ellipse; UI phải phản ánh đúng loại/kích thước hiện tại. Mirror/rotate/frame change không đổi tâm world ngoài ý muốn. Toggle snapping/đổi profile không sửa hình đã commit.

## 4. Pen và Node: danh mục nghiệm thu

Các baseline được tham khảo từ [Pen của Adobe](https://helpx.adobe.com/uk/illustrator/using/tool-techniques/pen-tool.html), [preview đường của Adobe](https://helpx.adobe.com/illustrator/desktop/draw-shapes-and-paths/draw-shapes/preview-paths-drawn.html), [loại node của CorelDRAW](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Mac/CorelDRAW-en/CorelDRAW-Node-types.html). Những quy tắc input bên trên là thiết kế của PikDraw; không tuyên bố phím/hành vi giống hệt mọi phiên bản của các ứng dụng tham khảo.

| Năng lực | Hành vi PikDraw yêu cầu | Mốc |
| --- | --- | --- |
| Line + cubic cùng path | Click tạo corner; drag tạo cubic; rubber band đúng hình học sẽ commit | R2 |
| Cusp/Corner | Hai handle độc lập; có thể bỏ một bên để chuyển curve–line | R2 |
| Smooth | Hai handle thẳng hàng, chiều dài độc lập; xử lý handle dài 0 xác định | R2 |
| Symmetric | Hai handle đối nhau, cùng độ dài; có marker loại node | R2 |
| Auto-smooth | Tính handle theo lân cận, không overshoot tùy tiện; thay đổi lân cận có preview | R3 |
| Tạo/chỉnh handle | Mouse, keyboard và số H/θ; tách/gộp liên kết tay nắm; giữ anchor | R2 |
| Đặt lại anchor trong lúc vẽ | Action có tên và drag tạm; không mất node/handle trước đó | R2 |
| Đóng/tiếp tục path | Nhận endpoint của path mở, đảo hướng khi cần, đóng bằng topology | R2 |
| Tự thêm/xóa node trong Pen | Cursor +/− có tooltip; tùy chọn auto add/delete; action riêng luôn có | R2 |
| Chèn node vào cubic/quad | Split bằng de Casteljau, hình học giữ nguyên trong sai số số học | R2 |
| Xóa node | Chế độ giữ hình bằng fit có báo sai số hoặc nối thẳng chủ động; không gọi xóa là luôn exact | R2–R3 |
| Kéo segment | Reshape bằng thao tác trực tiếp, giới hạn phạm vi hai node liên quan; có undo | R3 |
| Chọn/sửa nhiều node | Click/Shift, Window/Crossing/Lasso, move/nudge, tọa độ chính xác, căn hàng | R2 |
| Line ↔ curve | Line → cubic giữ đường thẳng; curve → line là thay đổi có chủ ý, preview độ lệch | R2 |
| Split/Join/Break | Stable topology và source IDs; join hai endpoint có thứ tự/direction rõ | R2 |
| Reverse/close/open | Giữ hình học/style/winding; close có đoạn đóng; open chọn vị trí cắt | R2 |
| Smooth/Simplify | Error bound mm, preview trước/sau và số node; bảo vệ corner/endpoint | R3 |
| Curvature tool | Click smooth, double-click corner, thêm/bớt điểm và refit cục bộ | R3 |
| Pencil/Freehand | Theo chuyển động chuột, resample và fit có error bound, tiếp tục/nối/redraw một đoạn chọn | R3 |
| Chính xác kỹ thuật | HUD endpoint/handle, snaps, Ortho/Polar, unit expressions | R1–R3 |

Node hiển thị bằng hình + màu: corner vuông, smooth tròn, symmetric hình thoi; node được chọn có fill, node chưa chọn có viền; handle phân biệt incoming/outgoing. Có thể chỉnh kích thước marker theo DPI và tương phản sáng/tối. Chỉ vẽ handle của vùng/node liên quan; không tạo 100.000 native widgets cho node.

Trong Pen, ưu tiên hit của chính draft: đóng tại node đầu → tiếp tục endpoint → action thêm/xóa node được bật → đặt điểm mới. Cursor và tooltip phải cho thấy action trước click. Arc có gizmo tâm/bán kính/tiếp tuyến riêng; không giả handle cubic cho arc analytic. Kéo arc endpoint phải theo tham số/nhánh nghiệm đã preview; chuyển arc sang cubic là action explicit kèm sai số, không là tác dụng phụ của Node tool.

Join hai endpoint khác vị trí mặc định tạo bridge segment có preview; `Merge endpoints` là action khác, chọn target hoặc midpoint và báo dịch chuyển. Merge trong tolerance topology nhỏ vẫn phải giữ lựa chọn người dùng; không áp tolerance màn hình để tự weld hình sản xuất. Join nhiều path phải giữ object chủ, style nguồn và stable mapping; nếu khác style phải hỏi lựa chọn trong preview công cụ, không tự mất metadata.

Freehand dùng chuỗi mẫu thô bounded trong phiên; tốc độ lấy mẫu không là mật độ node nguồn. Có stabilization với độ trễ đo được, corner detection và fit worker hủy được. Chỉ kết quả cuối đã kiểm tra mới commit. Simplify không chạy tự động trên hình CAD nhập vào.

Pressure/variable-width brush, mesh, B-spline/NURBS và công cụ thư pháp là các gói mở rộng có dữ liệu riêng sau R5; không gắn vào nhãn Pen complete. Baseline Pen/Node hoàn tất khi từng dòng R2/R3 bên trên có evidence. Mọi tuyên bố parity phải ghi rõ ma trận tính năng và giới hạn.

## 5. Công cụ hình học kỹ thuật

Thiết kế nhập tọa độ, Polar/Ortho và object snap lấy ý tưởng từ [Autodesk: precise point input](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-392167BC-8032-44D9-B4A9-DF4AC00DF5C4.htm) và [precision controls](https://help.autodesk.com/cloudhelp/2024/ENU/AutoCAD-MAC-GettingStarted/files/GUID-85D8C41C-9C3E-440F-BD60-61848DD27219.htm). Catalog dưới đây là phạm vi PikDraw đề xuất, không phải mô tả đầy đủ AutoCAD.

| Công cụ | Tham số và yêu cầu chính |
| --- | --- |
| Line/Polyline | Điểm đầu/cuối, L/θ, ΔX/ΔY; tiếp tục, close, undo node, line/arc trong cùng path |
| Circle | Tâm–bán kính/đường kính; 2 điểm, 3 điểm; tangent–tangent–radius khi kernel đủ, hiển thị nhiều nghiệm |
| Arc | 3 điểm; start–center–end; center–radius–sweep; cung tiếp tuyến. Điểm thẳng hàng/sweep vô hiệu phải báo lỗi |
| Rectangle/Polygon | Góc quay, width/height, center/corner; polygon nội/ngoại tiếp; rounded rectangle radius giới hạn |
| Construction geometry | Đường vô hạn/ray, trục/gốc dựng hình xoay được, parallel/perpendicular, đường chia góc; layer non-output |
| Trim/Extend | Chọn biên cắt + phía giữ, nearest valid intersection; finite/ray extension rõ; preview và giao dịch nguyên tử |
| Fillet/Chamfer | Line–line trước: radius R hoặc hai khoảng cách D1/D2; trim/keep gốc; R không đủ chỗ trả diagnostic |
| Curve fillet | Line–arc/arc–arc sau; Bézier tổng quát cần task riêng với nhiều nghiệm và error budget |
| Offset/Contour | Distance mm, side, join miter/round/bevel, miter limit, open cap; xử lý lỗ/self-intersection, giữ source và tham số |
| Boolean | Union, Difference, Intersection, XOR trên topology path thật; kiểm tra hướng/lỗ/touching/open path trước áp dụng |
| Move/Rotate/Mirror/Scale | Base point, copy, góc/scale chính xác; hình học local và transform đúng, hỗ trợ reflection/nonuniform scale |
| Array/Align | Linear/polar count-spacing-angle, preview; giai đoạn đầu bản sao độc lập, không quảng cáo constraint liên kết |
| Measure/Dimension | Đo L, góc, bán kính, chu vi/diện tích có đơn vị; dimension chú thích liên kết source IDs; sửa số chưa tự điều khiển hình |
| Tuyến cơ khí 2D | Centerline, đoạn 0/45/90 hoặc góc tự do, bán kính uốn, đường bao theo width, kiểm tra clearance và góc gãy |

Routing R5 là dựng tuyến 2D có hỗ trợ bán kính/khoảng hở. Tuyến giữ centerline + width + bend radius; đường bao/clearance là hình dẫn xuất có nguồn revision. Giao/T-junction chỉ tạo liên kết bằng action explicit. Không tự biến nét cắt nhau thành cùng node. Không đánh đồng công cụ này với autorouter PCB, mạng ống có solver, mô phỏng cơ khí hoặc CAM.

Tách ba loại điều kiện: (1) khóa tạm trong thao tác; (2) tham số local có thể sửa của một shape/fillet/route; (3) ràng buộc sketch liên đối tượng tồn tại lâu dài. R1–R5 làm (1), rồi (2) có graph phụ thuộc bounded và cycle detection. Solver sketch cho coincident, horizontal/vertical, parallel/perpendicular, tangent, concentric, equal và driving dimensions là gói R6 riêng: cần chọn solver/license, ADR, diagnostics under/over-constrained, rollback và benchmark. Không vẽ biểu tượng constraint rồi coi như đã giải.

Construction và dimension objects phải có schema/source ID, renderer, hit-test và persistence của chính chúng trước khi UI công bố. Khả năng SVG/PDF/DXF tương ứng chỉ nhận sau fixture; không tự hứa export annotation hoặc toolpath.

## 6. Mô hình dữ liệu và kiến trúc đề xuất

### 6.1 Authoring path, ID và cung chính xác

D01 phải chốt ADR/contract trước D02–D04. Phương án đề xuất:

- `EditablePath` có ordered subpaths, stable `SubpathId`, `NodeId`, `SegmentId`; node có position, node kind và incoming/outgoing handle local. IDs do document cấp, không tái sử dụng sau delete; undo phục hồi identity.
- Segment có loại Line/Quadratic/Cubic/CircularArc/EllipticArc và tham số nguồn. Topology mở/đóng được lưu explicit, anchor đầu của vòng đóng không bị nhân đôi. Arc giữ center/radii/rotation/sweep hợp lệ hoặc dạng analytic tương đương đã khóa semantics.
- Document lưu authoring path làm nguồn duy nhất. `Path2D` cho consumer cũ là immutable view/adapter dẫn xuất; không duy trì hai bản source có thể lệch nhau. Geometry kernel chỉ nhận value hình học và trả mapping, không phụ thuộc UI hay ID allocator của document.
- Node kind là ý định liên kết handle. Smooth = tiếp tuyến thẳng hàng, hai độ dài độc lập; Symmetric thêm điều kiện bằng độ dài. Đó là G1/local editing semantics; không hứa C2 hoặc curvature continuity.
- Source giữ local coordinates + affine transform. Đo/snap dựng trong world double. Circle bị scale không đều trở thành elliptic geometry khi đánh giá; reflection đổi orientation đúng. Không map endpoint rồi giữ rx/ry cũ như code hiện tại.
- Render dùng origin-relative float và tolerance hiển thị. Manufacturing nhận đường nguồn với error budget riêng; approximate preview không được thay source, bounds chính xác không suy từ ảnh.
- Shared contracts C1/C2/C3/C5/C7 và consumer ảnh hưởng phải review; dependency registry chỉ đổi bằng task riêng nếu thật sự cần.

D03/D04 phải kiểm tra phiên bản container hiện được tích hợp rồi chọn version mới; không mặc định nhánh main và nhánh storage cùng version. Migration gán ID deterministic cho path cũ, giữ Quad/Cubic/Arc/source màu/text; file mới mở bằng reader cũ phải báo không hỗ trợ. Save/reopen so semantic graph, không chỉ pixel. Các shape parametric/feature graph/annotation đến mốc sau cần schema extension và consumer review trước.

### 6.2 Commands và công việc nặng

Các command dự kiến: AddPath, ReplacePathGeometry, MoveNodes, SetNodeKind, SplitSegment, RemoveNodes, JoinSubpaths, ReverseSubpath và UpdateShapeParameters. Mỗi command giữ before/after hoặc delta đủ undo; mapping IDs của split/join phải định nghĩa. Không mutate snapshot bằng const_cast hoặc dùng index node làm ID bền.

Draft giữ delta trên path đang sửa, không copy toàn document mỗi pointer event. Một drag/một finished path/một Boolean là một undo unit; local undo trong draft có trace riêng. Snapshot/index/render cache cùng revision. Writer kiểm tra stale revision trước publication.

Fit/Simplify/Boolean/Offset/curve intersections nặng chạy bounded worker. Proposal mang document ID/epoch/base revision/job generation, source IDs/versions, tolerance và algorithm version. Strict base-revision rejection là mặc định; sau cancel/undo/reopen/switch document không được áp kết quả muộn. Job hỏng/cancel không giữ UI spinner vô hạn, không commit nửa path.

Pen/Node cần renderer hỗ trợ fill/stroke/curve/closure thật; thiếu renderer phải sửa trước screenshot nghiệm thu. D00 xem xét nhánh renderer/pick/style đã review để dùng lại, không tái tạo một thuật toán kém hơn trong UI.

### 6.3 Ranh giới module và tổ chức file

| Module | Trách nhiệm; nhóm file dự kiến |
| --- | --- |
| `core/geometry` | `curves/evaluation`, `curves/split`, `curves/metrics`, `queries/nearest`, `queries/intersections`, `fitting`, `operations/boolean`, `operations/offset`, `operations/fillet` |
| `core/document` | `model/editable_path`, `commands/path_edit`, `commands/node_edit`, `storage/path_codec`, `storage/migrations` |
| `modules/vector` | `interaction/tool_session`, `interaction/input_router`, `precision/numeric_input`, `precision/snap_query`, `tools/line`, `tools/pen`, `tools/node`, `tools/pencil`, `tools/curvature`, `tools/shapes` |
| `apps/desktop` | `tools/tool_palette`, `tools/context_bar`, `tools/numeric_hud`, `tools/pen_presenter`, `tools/node_presenter`, input adapter và accessibility |
| `core/rendering` | Overlay path/node/handle, incremental invalidation, render adapter của geometry mới |
| `modules/production` | Annotation/measurement và derived route checks; không sở hữu Pen hay mutable path riêng |

Tên là file map đề xuất; chỉ tạo khi có trách nhiệm thật. Public header nhắm 80–180 dòng không rỗng, cap 300; implementation nhắm 150–350, cap 1000; private implementation header và test source cap 1000; entry/composition cap 250; CMake cap 250. Đọc [quy tắc tổ chức code](../development/code-organization.md). Không thêm logic công cụ vào các file `native_window_events.cpp`/`native_window_paint.cpp` đang lớn; adapter chỉ route sự kiện và render presenter. Không minify, `.inc` lách cap hoặc tạo hàng loạt file getter trống.

Không phụ thuộc đổi Qt/Skia để bắt đầu lõi tương tác. UI adapter bám host đã nghiệm thu; quyết định đổi shell/backend cần task/ADR độc lập và số liệu.

## 7. Lộ trình giao cho agent

Catalog là kế hoạch, **không tự tạo lease và không là scheduler**. Tất cả entry ban đầu `planned`; coordinator thực hiện task create/claim/worktree sau khi prerequisites đã tích hợp và semantic vectors đã khóa. `proposed_owned_paths` phải chuyển thành owned paths thật, không cấp quyền từ tài liệu này.

| Mốc | Task | Kết quả dùng được và exit gate |
| --- | --- | --- |
| R0 — nền đúng | D00–D04 | Revision tích hợp rõ; semantics/ID/schema khóa; cung/bounds/affine đúng; undo và save/reopen path/node đủ |
| R1 — dựng đường chính xác | D05–D09 | Line/Polyline có Esc/Enter, L/θ/XY, pan/zoom không đứt lệnh, snap endpoint/midpoint; UI native dùng được |
| R2 — Pen và Node cốt lõi | D10–D11 | Trộn thẳng/cong, handle/node kinds, close/resume/split/join, chọn và sửa node; native input và save/reopen |
| R3 — đường cong và hình học | D12–D15 | Pencil/Curvature/Simplify; analytic circle/arc/shapes; snap nâng cao; nghiệm thu đầy ma trận Pen R2/R3 |
| R4 — chỉnh hình kỹ thuật | D16–D19 | Boolean/Offset thật, Trim/Extend, Fillet/Chamfer, transform/array; errors/units/topology đúng |
| R5 — đo và tuyến | D20–D22 | Measurement/dimension chú thích và route cơ khí 2D; capability rõ, ảnh app và benchmark revision tích hợp |
| R6 — gói sau | Task riêng sau review R5 | Sketch constraints liên đối tượng, driving dimensions, solver và graph lớn; CAM/3D thuộc roadmap riêng |

Có thể phát hành nội bộ R1 rồi R2, không chờ toàn bộ CAD mới cho người dùng thử. Không nghiệm thu chỉ bằng parser/schema hay ảnh mockup. Mỗi mốc cần UI đi hết luồng với dữ liệu nguồn lưu lại đúng. Thời gian được ước tính lại sau D00/D01 và benchmark đầu R1; số lượng agent không làm các dependency nối tiếp biến mất.

Hai lane có thể chạy đồng thời sau D01: geometry D02 và document D03 với owned paths disjoint; D04 storage chỉ bắt đầu sau D03, dùng path storage hẹp để tránh document lease rộng. D06 numeric và D07 snap có thể song song sau D05; D08 line chờ cả hai. Pen và Node dùng cùng path command contract; không cùng sửa facade/shared CMake. Integrator sở hữu các file đăng ký target/routing chung. Chưa có phân công chạy agent trong lượt lập kế hoạch này.

Catalog gồm 23 gói cha D00–D22. Gói đa module như D04/D12/D14/D20/D21 phải tách thành các task hợp đồng, producer và consumer với owned paths cụ thể trước dispatch. Các loại object mới của D14/D20/D21 cần task schema/codec/render đi trước phần UI; catalog liệt kê yêu cầu này, không cấp quyền feature agent sửa file chung. Trong R1, D06 và D07 có thể chạy song song; D08 chờ cả hai để nối một luồng hoàn chỉnh.

Mỗi task phải nhận: integrated base SHA, contract version, bounded context, exact owned paths, acceptance fixtures, reviewer module tiêu thụ và performance budget. Thay semantics phải quay lại coordinator; không sửa oracle hoặc expected output để khớp implementation. Không cherry-pick cả nhánh UI đang dirty hoặc copy đè worktree để chạy nhanh.

## 8. Nghiệm thu và chống kết quả giả

### 8.1 Tình huống chức năng bắt buộc

| Vector kiểm thử | Oracle/điều kiện chấp nhận |
| --- | --- |
| Polyline mouse + numeric | Từ (0,0), `@100<0`, `@50<90`, Esc → ba node (0,0), (100,0), (100,50), path mở, một undo unit |
| Đóng path | Click node đầu → closed topology, không node trùng, fill/winding đúng; reopen giữ nguyên |
| Esc theo context | Bỏ text đang nhập; bỏ handle drag; finish path; Shift+Esc hủy toàn phiên; không dư object rỗng |
| Pen trộn line/curve | Node kinds/handles đúng, line–curve không gãy ngoài ý định; preview bằng geometry sẽ commit |
| Split cubic tại t | Đối chiếu de Casteljau analytic ở nhiều t; sampled geometry trước/sau giữ trong tolerance, IDs mapping đúng |
| Smooth/Symmetric | Collinearity và handle lengths đúng theo loại; reflection/zero handle không NaN |
| Freehand/Simplify | Hausdorff/deviation bound theo corpus độc lập, giữ corner/endpoints, report actual error; không chỉ so node count |
| Cung/ellipse | Center/radius/sweep/endpoint, bounds và length theo oracle analytic; affine/reflection/nonuniform scale không thành dây cung |
| Chính xác số | `1in` = 25,4mm; world/local axes, signed angle, decimal locale, 20m origin + chi tiết nhỏ, overflow/invalid input |
| Snap | Các điểm không bám bbox; cùng thao tác ở DPI 100/150/200 và nhiều zoom cho cùng source target; nhiều nghiệm deterministic |
| Checkbox/master | Tắt/bật F3 và group giữ child masks; config generation cũ không công bố candidate; checkbox chưa có capability disabled |
| Modifier theo profile | Cùng semantic trace ở hai profile cho cùng geometry; Alt tách handle không tắt snap; Ctrl constrain trong Corel không khởi động Node tạm |
| Modifier trong drag | Nhấn/thả constraint/center/S/pan ở các thứ tự, release ngoài cửa sổ/focus loss; anchor nguồn không drift, không kẹt mode hoặc thêm undo |
| Hướng và numeric | Khóa 0/45/90° kết hợp L chính xác; θ nhập số thắng modifier; snap ngoài hướng bị bỏ; conflict giữ hình và báo đúng |
| Hình đồng tâm/regular | Center analytic + N=3/4/5/6/512, R/r/a theo oracle; Ctrl+Shift hoặc Shift+Alt cho cùng centered-square/circle theo profile |
| Đổi tài liệu | Undo/cancel/close-reopen/switch tab trong khi fit/offset trả muộn → reject 100% schedules fixture |
| Persistence | Save/reopen/undo/redo giữ Node/Segment/Subpath IDs, curves/arcs, styles/source CMYK/spot/ICC/overprint và font data không liên quan |
| Geometry chuyên sâu | Holes, touching, self-intersection, reversed winding, tiny sliver; output đúng topology hoặc diagnostic, không bbox fallback |
| Native UX | Chuột/phím thật hoặc native messages có trace; menu và shortcut tới cùng command; numeric focus/IME/pan/resize/capture-loss |

Không xuất cutter path từ source invalid. Ngưỡng hình học được D01 phân loại: sai số floating-point analytic, fit error do người dùng chọn, tolerance sản xuất và snap DIP là các budget khác nhau. `0,01mm` chỉ là mục tiêu khảo sát production trên fixtures đã duyệt, không dùng epsilon toàn cục cho mọi thuật toán.

### 8.2 Hiệu năng và evidence

Máy mục tiêu 8GB/iGPU; đo thực tế trên máy văn phòng trước máy RTX. Scene gồm 100.000+ nodes có Line/Cubic/Arc, lỗ, layer và object lớn 20m. Trace phải có chọn node, kéo handle, numeric entry, snap, pan/zoom trong draft và commit; không thay bằng canvas tĩnh hoặc tài liệu rỗng.

Budget đề xuất cần chốt D01: input xử lý + chuẩn bị preview p95 ≤ 4ms; total visible frame target ≤ 16,7ms ở profile tương tác 60Hz. Đo riêng dispatch, snap, geometry, cache, raster, present và input-to-visible p95/p99; ghi số frame miss. Số source redraw CPU khoảng 37–42ms là tham chiếu từ nhánh pick/style (source `e977cc2`), chưa là baseline main hay chứng cứ 60 FPS: cần damage/cache/overlay reuse và đo lại khi đang edit. Nếu chỉ đạt chế độ software 30Hz, phải gắn nhãn hạn chế và điều chỉnh milestone bằng review, không tự giảm corpus.

RAM: report process private/working-set peak, cache/undo/draft/jobs retained bytes, recovery cleanup và queue bounds. Mục tiêu sơ bộ process ≤ 1,5GB cho corpus chuẩn là đề xuất D01, không phải số đo; loại trừ hợp lệ tài nguyên input lớn phải khai báo. Không tiếp tục giữ snapshot/job vô hạn để đạt frame time.

Mỗi gate lưu: SHA + dirty status, hash source/exe/fixtures, toolchain/OS/hardware/backend, câu lệnh, raw results/seed/input trace và limits. CLI `--screenshot` offscreen chỉ xác nhận paint của app; native input trace và OS/present measurement cần evidence riêng. Ảnh mockup HTML không là bằng chứng runtime. Ảnh native bước cuối phải cho thấy đoạn dài/góc đã nhập, node/handle được chọn và file mở lại, so với dữ liệu nguồn.

Test consumer phải có oracle ngoài implementation; gate/oracle do reviewer sở hữu. Harness doctor/ownership/dependency/LOC checks là điều kiện bổ sung, không thay phép đo geometry và UI. Benchmark record phải gắn revision tích hợp; không dùng lại số benchmark nhánh cũ sau thay runtime.

## 9. Thứ tự công việc kế tiếp

1. Coordinator nhận D00: kiểm kê main dirty và các nhánh sửa đã review; chọn source tích hợp, chốt khả năng renderer/storage/selection thật.
2. D01 khóa input state, units/axes, node IDs/arc semantics, errors/tolerances, migrations và semantic vectors với consumer review.
3. D02/D03/D04 tạo nền đúng; D05–D09 giao bản nội bộ Line/Polyline + nhập L/θ + Esc dùng được.
4. D10/D11 giao Pen/Node; sau đó D12–D15 hoàn tất nhóm đường cong, shapes và snapping.
5. Chỉ mở D16–D22 khi mốc tương ứng đã tích hợp và kiểm chứng. UX SVG/text/màu được điều phối thành task riêng; dùng chung action routing, units và document semantics đã khóa.

Tài liệu này không đổi runtime, contracts hoặc project-state implemented; các lựa chọn schema/solver và budgets chỉ thành quyết định sau task/ADR và evidence tương ứng.
