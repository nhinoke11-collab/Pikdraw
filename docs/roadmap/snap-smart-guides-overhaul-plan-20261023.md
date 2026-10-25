# Đại tu Snap, Smart Guides và khoảng cách thông minh

Ngày: 23/10/2026. **Đã nối Snap/Smart Guides UI với Select, Quick Shapes, Pen, Node move và Artboard; consumer nâng cao còn mở.**
Phần bổ sung của [Select/toolbox](select-tool-toolbox-interaction-plan-20261023.md) và
[Page/Artboard](page-artboard-workspace-overhaul-plan-20261023.md).
Kế thừa [OSNAP/Inference](core-tools-and-dimensions/03-advanced-osnap-inference.md) về geometry, stamp và nghiệm;
không dùng baseline cũ trong roadmap đó thay cho capability source hôm nay.

## 1. Mục tiêu theo hai ảnh người dùng

- Kéo object tới gần object khác tự hiện đường gióng ảo, nhận đúng góc/cạnh/tâm và bắt dính có kiểm soát.
- Căn cùng mép trên/dưới/trái/phải, cùng tâm ngang/dọc, cạnh sát cạnh và góc trùng góc.
- Hiện khoảng cách ngang/dọc; lặp lại khoảng hở của cặp kế bên hoặc đặt object ở giữa hai object với hai gap bằng nhau.
- Khi resize/rotate, có gợi ý cùng kích thước/góc khi phù hợp; không tự scale/rotate trong thao tác Move.
- Có dropdown **Snap To** như ảnh 2 để bật từng nguồn; bật/tắt tổng và tạm bypass, cùng Settings/Properties chi tiết.
- Giữ công cụ người dùng chọn, preview trùng commit, một gesture một Undo, không tự đổi sang Quick Shapes.

Ảnh 1 là tham khảo spacing indicators và đường căn; con số trong ảnh không được dùng làm thông số mặc định.
Ảnh 2 là tham khảo phân nhóm Objects/Page/Grid/Guidelines; không bật checkbox cho capability chưa có.
Guide ảo là hỗ trợ thao tác tạm, không tự lưu thành guideline/document object hoặc persistent constraint.
“Bắt dính” không có nghĩa Boolean union, hàn node, tránh va chạm tự động hoặc sửa object đích.

## 2. Đối chiếu tài liệu CorelDRAW và Illustrator

Đã tra ngày 23/10/2026, ưu tiên trang trợ giúp chính thức:

| Nguồn | Nội dung xác nhận | Quyết định PikDraw |
| --- | --- | --- |
| [CorelDRAW — Snap objects](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Snap-objects.html) | Chọn nguồn snap riêng, marker/mode, radius, self-snap và tạm tắt; có thể chọn điểm nắm trên object rồi kéo tới điểm đích | Giữ điểm nắm tương đối; panel Snap To; master không xóa tùy chọn con; self exclusion rõ ràng. |
| [CorelDRAW — Alignment guides](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Alignment-guides.html) | Căn cạnh/tâm, margin inset/offset, nhóm con/group bounds, spacing đều, gợi ý cùng size/góc | Tách alignment, spacing, dimensions và margin thành nhóm; không dồn mọi loại vào “node snap”. |
| [CorelDRAW — Dynamic guides](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Dynamic-guides.html) | Acquire điểm tham chiếu, đường kéo dài theo góc, nhãn khoảng cách và tick tùy chọn | Tracking có lifecycle/giới hạn điểm; marker ảo khác source point; không biến thành geometry nguồn. |
| [Illustrator — Smart Guides options](https://helpx.adobe.com/illustrator/desktop/measure-and-align/grids-and-guides/smart-guides-options.html) | Alignment/spacing/construction guides, labels và tolerance được cấu hình riêng | Một bộ preference, ít controls trên thanh; Properties/Settings chứa chi tiết. |
| [Illustrator — Work with Smart Guides](https://helpx.adobe.com/in/illustrator/desktop/measure-and-align/grids-and-guides/work-with-smart-guides.html) | Guides hỗ trợ object/artboard khi tạo, move, transform; hiện theo cạnh/tâm | Nối Select/shape/Artboard vào cùng kết quả giải; overlay không thay thế tọa độ snap thật. |
| [Illustrator — Distance Guides](https://helpx.adobe.com/ca/illustrator/desktop/measure-and-align/grids-and-guides/use-distance-guides-for-accurate-placement.html) | Xem khoảng cách object–object và object–artboard | Có chế độ đo khi hover không ghi document; không chiếm Alt đang dùng duplicate drag. |
| [Illustrator — cập nhật Snap](https://helpx.adobe.com/illustrator/desktop/new-features/whats-new.html) | Công bố Quick Access, rotated objects, active-artboard/isolation filter và advanced settings | Có scope filter và ưu tiên rõ; kiểm version thay vì sao chép quy tắc từ bản cũ. |

Lưu ý phiên bản: trang Adobe overview 11/02/2026 còn ghi Smart Guides không dùng cùng Snap to Grid/Pixel Preview,
trong khi trang cập nhật có tùy chọn ép grid và tinh chỉnh snapping. Một số deep link từ trang cập nhật redirect về Rulers.
Không coi các trang đó là một hợp đồng đồng nhất cho mọi phiên bản. PikDraw chọn arbitration minh bạch ở mục 5;
không tự tắt Smart Guides hoặc tự lượng tử hóa mm sang pixel chỉ vì chọn grid.

Default, tolerance DIP, dwell, ranking và shortcut dưới đây là quyết định PikDraw cần nghiệm thu,
không phải con số chuẩn hoặc cam kết parity do Corel/Adobe quy định.

## 3. Source audit hiện tại

| Thành phần | Bằng chứng hiện tại | Kết luận cho triển khai |
| --- | --- | --- |
| Snap configuration | `snap_config.hpp/.cpp` có group/option/priority, generation, acquire 8 DIP/release 12 DIP, per-tool override | Tái dùng; default nguồn chỉ Endpoint/SourceNode/LineMidpoint và CurrentLayerOnly; preference drawing lại có grid riêng, cần hợp nhất effective state. |
| Geometry providers | `snap_index.cpp:255–268` khai capability và có các nhánh center/quadrant/nearest/t-mid/half-length/intersection/perp/tangent/tracking/grid | Source đã tiến xa hơn baseline 17/10; phải kiểm provider theo primitive và consumer, không gọi tất cả đã chạy trong UI. |
| Nguồn layout thiếu | `is_supported_snap_candidate` chưa nhận PageEdge/PageCenter/Guide/BboxEdge/BboxCenter; chưa thấy loại EqualSpacing/LayoutAlignment | Checkbox/enum không là implementation; cần provider riêng cho bố cục. Bbox center không được gọi analytic center. |
| Index/context | Index dựng từ `source.objects()` của active page; stamp có document/epoch/revision/camera/config/capability/tool/query | Cần PageId/scene/scope generation và exclusion set cho multi-selection; đổi active page có thể không đổi revision. |
| Select gesture | `native_window_selection.cpp:660–699`: Move gọi `gesture.update(pointer_current_world_)`; resize tính transform trực tiếp; rotate Shift theo bước 15° | Chưa thấy nối solver alignment/spacing vào nhánh này. Đây là điểm chính cần sửa để có hành vi trong ảnh. |
| Line/Polyline | `drawing_tool_controller.cpp:125–217` dựng stamp, dùng SnapSourceIndex, direction và candidate | Tái dùng pipeline worker/query, không viết bộ snap mới trong Select. |
| Pen | Có `PenToolController::update_from_snap_query`; shell `pen_snap_point` còn có đường Shift angle riêng | Audit native event consumer; API có mặt chưa chứng minh shell dùng đủ object snap. Giữ handle drag riêng. |
| Inference | Có `SnapStatusController`, dwell 300 ms, tối đa 4 acquired points, lock/tracking; search chưa thấy production call site ngoài class | Cần nối runtime. `find_best_tracking_ray` dùng aperture 6 mm/min-ray 5 mm, phải đổi sang screen DIP trước khi bật rộng. |
| Menu/UI | ViewSnapGrid/ViewSnapObjects hiện disabled trong menu, handler là nhánh return; Snap panel canvas chỉ có 4 nguồn cơ bản | Thay bằng action/preferences chung, không chỉ bỏ disabled flag. F3 hiện mở Snap and Direction, không mặc nhiên là master toggle. |
| Preferences | `DrawingPreferences` lưu master, 4 nguồn, grid/direction/profile; field nâng cao chưa đủ | Migrate settings có version; không giữ menu/Select/Pen mỗi nơi một bool. |

Paths gốc: `modules/vector/{include/pikdraw/vector,src}/precision/snap/`, `apps/desktop/src/tools/`,
`apps/desktop/src/shell/`. Đây là source audit, chưa chạy native tests/visible pointer acceptance trong lượt lập plan.

## 4. Phạm vi bắt điểm và guides

| Nhóm | Semantics yêu cầu | Mặc định mới / giới hạn |
| --- | --- | --- |
| Objects | Source nodes/endpoints, line midpoint; center/quadrant/nearest/curve relations theo provider có thật | Bật nhóm Objects; chọn từng kiểu, không gọi tâm bbox là analytic center. |
| Object alignment | Mép min/max X/Y, tâm X/Y, edge-to-center; góc chỉ là hai điều kiện trục đồng thời hoặc source corner | Bật trong preset Layout; nhãn “Căn mép/tâm” khác “Node/Center nguồn”. |
| Edge contact | Mép phải–trái hoặc trên–dưới chạm nhau theo bounds được chọn, gap = 0 | Bật khi object có overlap trên trục còn lại hoặc trong cửa sổ lân cận hợp lệ. Không weld source. |
| Equal spacing | Lặp gap liền kề, hai gap bằng nhau khi chen giữa, hàng ngang/dọc | Bật; đo cạnh–cạnh, không thay bằng tâm–tâm khi size khác nhau. |
| Dynamic/Construction | Acquire source feature, tracking ray/extension, intersection của guides, polar reference | Chỉ bật provider đã nối đúng; dwell 300 ms/tối đa 4 điểm là nền hiện có; tracking mặc định cần thao tác acquire. |
| Intelligent dimensions | Resize bằng width/height của hàng xóm; rotate bằng góc nguồn hợp lệ | Chỉ khi resize/rotate; Move không được tự đổi size/góc. Bbox AABB width không giả intrinsic width của object xoay. |
| Page/Artboard | Edges/corners/edge midpoints/center; margin/bleed chỉ khi model có thật | Bật vùng active; artboard provider theo kế hoạch Page/Artboard, không lấy khung paint làm geometry. |
| Guidelines/Grid | Guides có scope rõ, grid origin/step theo model mm | Guidelines bật khi có provider; preset Layout grid tắt; giá trị người dùng đã lưu được giữ. Hiện grid và snap grid độc lập. |
| Text | Text frame, baseline thật và glyph anchors khi shaping/layout có contract | Frame bounds trước; baseline/glyph disabled có lý do nếu chưa có provider, không đoán bằng bbox của chữ. |
| Pixels/Baseline Grid | Theo pixel-preview/document raster basis hoặc text baseline grid thật | Chưa đủ model/provider thì disabled, không giả pixel theo zoom màn hình hoặc dựng checkbox thành công rỗng. |

New Layout preset: master/Objects/alignment/spacing bật, page/guides bật khi capability có; hidden tắt,
mọi layer visible trong active scene làm reference; locked có thể tham chiếu nhưng không sửa.
Giữ preference và CurrentLayerOnly đã lưu của người dùng; không reset sang preset mới khi nâng cấp.
Một source dùng group top-level theo mặc định, children chỉ trong isolation hoặc tùy chọn rõ ràng.

### Bounds dùng để đo

- Default `Geometry bounds`: source path/text frame/image placement sau transform, không halo blur/shadow.
- `Stroke bounds` là lựa chọn riêng nếu có provider extent đáng tin cậy; không tăng bbox bằng stroke/2 trong mọi trường hợp join/miter/transform.
- Có nhãn/tooltip phân biệt. Hai đường path gap 0 có stroke vẫn có thể chồng mực; UI không gọi đó là “chạm mép mực”.
- Layout dùng axis-aligned world bounds theo X/Y; snap cạnh nghiêng dùng finite source segment và nhãn quan hệ hình học riêng.
- Không dò cạnh hình bên trong raster; ảnh chỉ có placement rect trừ khi người dùng trace thật.

## 5. Solver: từ thao tác thô đến vị trí đã snap

### 5.1. Không snap con trỏ thay cho object

Lúc pointer-down chốt snapshot, scene/scope, moving IDs, điểm nắm và bounds/anchors nguồn của selection.
Từ raw pointer delta tạo raw transform. Solver sinh correction theo các feature **sau raw transform**,
rồi áp correction lên toàn selection; giữ offset con trỏ–điểm nắm, không kéo tâm object vào cursor.
Multi-selection dùng union layout bounds hoặc source anchors đã chọn; không bóp từng object về một grid khác nhau.

Candidate gồm relation kind, source/target feature IDs, axis/DOF, correction transform, distance DIP,
residual/error bound, reference chain và query stamp. Overlay lấy chính candidate đã được solver nhận.
X và Y có thể nhận hai references khác nhau nếu đồng thời thỏa các ràng buộc; không ghép hai tọa độ tạo một “source node” không tồn tại.

### 5.2. Khoảng cách đều — định nghĩa có thể test độc lập

Với intervals theo X, A=[a0,a1], B=[b0,b1], C=[c0,c1], width của B là w:

- Gap A→B: `gAB = b0 - a1`; gap B→C: `gBC = c0 - b1`.
- Chen B giữa A/C: `b0 = (a1 + c0 - w) / 2`, `g = (c0 - a1 - w) / 2`.
  Chỉ là “hai khoảng hở đều” khi g ≥ 0; thiếu chỗ phải báo overlap, không hiển thị như gap dương.
- Lặp gap của cặp A/B khi kéo C: `c0 = b1 + gAB`. Theo Y dùng min/max nhất quán với world Y-up.
- Ví dụ fixture độc lập: A=[0,20], B=[30,50], kéo C rộng 35 → vị trí lặp gap là C=[60,95], gap 10 mm;
  **không** dùng khoảng cách tâm bằng nhau vì width C khác A/B.
- Ví dụ chen giữa: A=[0,20], C=[80,110], B rộng 20 → B=[40,60], mỗi gap 20 mm.
- Contact riêng: B.left=A.right → gap 0; không yêu cầu một cặp gap tham chiếu khác.

Chỉ so các neighbors có overlap trên trục vuông góc hoặc cùng hàng/cột trong ngưỡng cho phép;
không lấy hai object bất kỳ ở xa trên canvas chỉ vì tọa độ một trục gần nhau.
Gaps tham chiếu ưu tiên cặp liền kề trong lane, phạm vi view/scope hiện tại; không so mọi cặp O(n²).
Object che giữa làm cặp không còn liền kề; chọn reference theo thứ tự ổn định.

### 5.3. Ưu tiên, chống rung và constraints

1. Numeric locks chính xác/axis/aspect/center modifier thuộc tool là hard constraints; candidate trái chúng bị loại hoặc giải trên DOF còn lại.
   Không áp snap rồi chiếu về trục khiến marker nói đúng nhưng geometry sai. Nhập số rõ không bị snap sửa số.
2. Target đã acquire giữ trong release radius nếu vẫn hợp lệ; candidate ngoài acquire radius không được bắt mới.
   Dùng 8/12 DIP hiện có làm mặc định ban đầu, configurable; không dùng mm làm bán kính tương tác.
3. Trong vùng capture, ưu tiên feature người dùng chủ đích nắm/khóa → source point cụ thể → tiếp xúc/căn cạnh-tâm
   → equal spacing → guide/page/grid theo profile, với screen distance và stable IDs phá hòa.
   Priority không được kéo object tới reference xa vượt aperture; vector point ranking hiện có được giữ trong provider của nó.
4. X/Y là ràng buộc 1D; corner/node là 2D; chỉ nhận tổ hợp tương thích và trong tolerance của tổng correction.
   Khi target mất hiệu lực hoặc vượt release radius, nhả ngay và tìm lại; không “dính cứng” cả khi kéo xa.
5. Grid và guides có thể cùng bật; marker/hint cho biết nguồn thắng. “Force grid” là lựa chọn explicit,
   chỉ nhận giải giữ đúng grid lattice; không âm thầm bật pixel rounding hoặc tắt option con.
6. Candidate thay thế khi mục tiêu cũ không hợp lệ hoặc rõ ràng tốt hơn theo hysteresis; không giật qua lại vì thứ tự hash/index.
   Cho phép `Next snap target` qua action cấu hình được; Tab vẫn phục vụ field/focus theo plan Select.

Bypass tạm trả raw transform nhưng giữ constraints số/hướng của tool; nhả bypass khôi phục snap tại pointer hiện tại.
Master Off lưu nguyên checkboxes con. Bật/tắt preference chỉ đổi config generation, không document revision/Undo.

## 6. Guides/markers/HUD và giao diện Snap To

### 6.1. Phản hồi trực tiếp

- Marker khác nhau cho node, midpoint, layout edge/center, intersection; source đích highlight nhẹ.
- Đường gióng từ reference tới feature đang căn, kết thúc gần đối tượng; không phủ toàn canvas bằng nhiều đường vô hạn.
- Hai gap bằng nhau hiện hai đoạn đo có mũi tên và cùng nhãn khoảng hở, như ý nghĩa ảnh 1.
  Đường căn hàng trên/dưới/tâm chỉ hiện khi chính quan hệ đó được nhận.
- Nhãn gần vùng thao tác: `Căn mép trên`, `Tâm dọc`, `Khoảng hở X: 10 mm`, `Tiếp xúc cạnh`;
  định dạng theo page unit, phép tính giữ double, không round giá trị commit theo nhãn.
- Tối đa khởi đầu hai đường căn + hai đoạn spacing + hai nhãn quan trọng; giảm nhiễu và tránh con trỏ/handle/field.
  Có toggle show guides/measurements riêng với snap; tắt đường gióng không làm mất trạng thái snap nhận biết qua marker/hint.
- Mất target/bypass/cancel/tool/page/scope thì xóa overlays cũ; không lưu/export các nét hướng dẫn.
  Không đưa tọa độ con trỏ thường trực trở lại status bar đã yêu cầu bỏ.

### 6.2. Phiếu UI và cấu hình chung

Đối chiếu [UI standard](../design/ui-ux-standard.md), [workspace](../architecture/workspace-layout.md)
và [acceptance](../quality/feature-acceptance.md).

| Bề mặt | Quyết định |
| --- | --- |
| Standard toolbar | Magnet split-button trên cùng: thân bật/tắt master; mũi tên mở `Snap To`. Không thêm một hàng toolbar hoặc đưa Snap vào toolbox như drawing tool. |
| Menu | Giữ `View > Snap Objects / Grid / Page / Guidelines / Smart Guides / Equal Spacing`; `View > Snap Properties` mở dock. Không khai báo Settings path chưa có runtime. |
| Action IDs | Dùng semantic `action.snap.master.toggle`, `action.snap.objects.toggle`, `action.snap.page.toggle`, `action.snap.guides.toggle`, `action.snap.grid.toggle`, `action.snap.smart_guides.toggle`, `action.snap.settings.focus`. ID mới chỉ đăng ký khi handler/provider có thật. |
| Snap To popup | Master, object points, grid, page/artboard, guidelines, Smart Guides, equal spacing và link Properties; checked state lấy từ `DrawingToolController`. Click checkbox giữ popup mở; mở/đóng không đổi tool hoặc document. |
| Properties | `Window > Properties` / `View > Snap Properties` hiển thị Snap ở dock hiện có. Có nhóm, candidate được engine hỗ trợ, bán kính acquire/release DIP và policy lớp hiện hành/đối tượng khóa/đường đang vẽ/lớp ẩn; lưu cùng preferences và không thuộc Undo. |
| Property bar | Không nhét dãy checkbox; giữ tool-specific numeric fields. Tối đa control snap context thiết yếu nếu đã có chỗ; toàn bộ cấu hình ở popup/Properties/Settings. |
| Fields nâng cao | Radius acquire/release DIP, candidate được hỗ trợ, group policy và target policy. Bounds/stroke, margin, dwell/tracking và priority chỉ xuất hiện khi có provider và consumer đã kiểm chứng; không hiển thị checkbox bật giả. |
| Field scopes | Snap preference là app/per-tool/view profile; grid/guideline/page geometry theo document model thật; operation bypass/target lock là gesture tạm. Không sửa grid/page source khi chỉ đổi tolerance. |
| Shortcuts | Giữ F3 mở/focus Snap and Direction theo binding hiện có; không đổi ngầm thành master toggle. Bypass/cycle/measurement dùng catalog/profile, chỉ khi canvas có focus; Ctrl/Shift/Alt giữ chức năng hiện có. |
| Alt/measurement | Alt+drag hiện phục vụ duplicate, không giành cho Distance Guides. Đo khoảng cách bằng action “Xem khoảng cách”/binding không xung đột, hoặc Alt-hover idle nếu profile đã phân biệt được. |
| Icon/label | Tái dùng hệ SVG magnet/guide/grid; asset ID độc lập tool ID, painter thật dùng asset. `Bắt điểm`, `Gióng thông minh`, `Khoảng cách đều`; không dùng emoji làm icon. |
| Tooltip/focus/status | Tên + binding thật; mô tả ngắn loại snap/scope. Status ưu tiên lỗi → gesture → nguồn snap, ví dụ “Kéo để đặt · Khoảng hở X: 10 mm · Esc hủy”. |
| Không áp dụng | Không panel canvas thường trực che artwork, không GPU/theme badge, không persistent constraint solver, 3D hoặc raster edge detection. |

Thay panel Snap canvas hiện tại bằng các đường mở cùng cấu hình; không xóa Ortho/Polar/numeric/profile đang có.
Nếu cần giữ panel cũ trong giai đoạn chuyển tiếp, nó chỉ là consumer cùng state, không giữ một bản preferences riêng.

## 7. Kiến trúc, công cụ và lifecycle

### 7.1. Bộ giải dùng chung

- `modules/vector` sở hữu candidate/constraint solving và policy geometry/layout; desktop điều phối session, worker, view và overlays.
- Tái dùng SnapSourceIndex cho source points/curves; thêm layout feature index cho object bounds/intervals/page/guides.
  Có thể là cùng snapshot/index bundle, không dựng index riêng cho mỗi tool và mỗi frame.
- Providers: source geometry, layout anchors, lane spacing, page/artboard, grid/guides và acquired tracking.
  Consumer nhận một `SnapSolution` gồm accepted transform/point và overlay evidence, thay vì query hai lần ra hai target khác nhau.
- Worker hiện có xử lý build index; moving selection chỉ tính anchors/preview transform nhẹ trên UI.
  Loại moving roots cùng mọi descendant khỏi references; `current_source_object_id` đơn hiện tại chưa đủ.
- Key cache/stamp phải có PageId/scene, isolation/scope generation, artboard filter, document/epoch/revision,
  camera/config/capability/tool/query và interaction generation khi cần. Active page đổi không tăng revision vẫn phải invalidate đúng.
- Bộ tracking hiện dùng world-mm aperture phải nhận camera/DIP, không được nối thẳng vào runtime ở mọi zoom.
- Culling/interval search theo view và lane; curve-intersection hiện có vòng pair trong aperture, cần đo và chặn budget
  cả pair visits/refinement, không chỉ tổng candidates. Không build all-pairs spacing hoặc source intersections mỗi mouse move.

### 7.2. Theo tool

| Tool | Input cho solver / quy tắc |
| --- | --- |
| Select Move | Selection baseline + grab offset → raw translation → accepted translation; cạnh/tâm/gap của object mới là moving features. |
| Select Resize | Moving handle/edges + fixed anchor + aspect constraints; same-size/alignment chỉ trên DOF hợp lệ. Không xê dịch anchor đối diện. |
| Rotate | Pivot + angle DOF; source angle/parallel guides theo capability; không lấy AABB width làm góc nguồn. |
| Quick Shapes | Origin/center và resize handle; Shift/Ctrl giữ square/circle/from-center; preview và created geometry cùng solution. |
| Line/Polyline/Pen | Snap endpoint/anchor theo source geometry và acquired rays; Pen kéo handle không chạy endpoint snap để đổi anchor ngoài ý định. |
| Node | Chỉ node/handle thực sự đang kéo; self-path tùy chỉnh, không tham chiếu node đang biến đổi như target cố định. Snap không merge node/topology. |
| Text | Frame placement/resize và baseline đủ capability; IME/text input luôn sở hữu keyboard; không đổi glyph positions bằng layout snap ngầm. |
| Artboard | Region handles/edges/center trong scene/workspace transform; move-content theo plan Page/Artboard, moving artwork loại khỏi refs khi cần. |
| Dimension/Gradient/Contour | Dimension anchors dùng source refs đủ nghĩa; các effect handles có constraint/đơn vị riêng. Không ép mọi handle qua bbox snap. |
| Guides/Pasteboard | Guide dragging có scope; snap artwork ngoài page được; hidden scene/page không tự làm reference trừ scope overview explicit. |

Alt duplicate preview giữ original object làm reference chỉ khi tool chủ đích cho phép, không self-snap vào chính ghost.
PowerClip/group scope phải tôn trọng picker/culling và visible clipped geometry; không snap child vô hình ngoài clip như điểm đang thấy.
Với clipped bounds chưa có provider đúng, dùng frame được gắn nhãn rõ hoặc disable child reference, không bbox giả.

### 7.3. Commit và hủy

- Lúc pointer-up đánh giá vị trí cuối bằng cùng pipeline; không phụ thuộc có event Move cuối hay không.
  Cùng raw position/stamp cho cùng accepted solution; cập nhật preview từ solution trước command commit.
- Một gesture một command/transaction, Undo khôi phục toàn bộ selection về baseline; object đích không đổi.
- Escape, capture loss, đổi tool/page/document/isolation, stale revision/config làm nhả locks và hủy/recompute theo policy;
  kết quả worker cũ không hiện marker hoặc sửa phiên mới.
- Hover/acquire/đo khoảng cách/mở settings không ghi document/Undo. Master Off/bypass không xóa preference nguồn.
- Index chưa sẵn sàng/hỏng/budget exceeded: raw input + hard constraints vẫn hoạt động, không reuse target stale;
  báo trạng thái nhẹ, không giả “snap thành công”. Khi index sẵn sàng không tự nhảy object nếu không có input tiếp theo.

## 8. Import/export và tài nguyên vector

Đã đối chiếu [vector interoperability](../architecture/vector-resource-interoperability.md),
[C8](../../contracts/c8-import-fidelity.md), [C9](../../contracts/c9-appearance-effects.md)
và capability source đã audit ở hai plan liên quan. Task Snap không đổi parser hoặc bật format support mới.

| Format/representation | Nguồn snap sau normalize | Giới hạn |
| --- | --- | --- |
| SVG subset; DXF 2D subset | Native paths/groups/text/image và units do adapter riêng phát, host đã validate | Source segments đủ capability mới dùng analytic snaps; không lấy polygon preview làm curve chuẩn. |
| PDF page-set; AI PDF-compatible page-set | Native editable objects hoặc full-page source reference theo intent | Reference khóa chỉ làm page/frame reference hợp lệ; không bịa nodes/glyphs từ preview. Không suy AI private artboards. |
| CDR RIFF/ZIP qua libcdr 0.1.9/SVG | Native object/group/clip của subset có thật | Bảo toàn scope/source metadata; không claim tất cả CDR effects/center semantics. |
| EPS DSC subset/DOS EPS qua PDF | Native geometry đã normalize, BoundingBox/page dùng đúng representation | Không tự tạo CAD relations, topology hoặc artboard authoring thiếu ở nguồn. |

Native model sau normalize dùng chung snap/command/edit/render. Adapter vẫn riêng theo format/version;
không chuyển mọi input qua một parser hoặc rasterize để lấy điểm.
Overlay/measurement/acquired rays không được export. Chỉ transform/geometry command đã commit được lưu/xuất.
Không đổi màu/ICC/metadata/effect ID vì snap; không gọi edge contact là đường cắt sản xuất đã kiểm chứng.

## 9. Triển khai theo bước và file

| Bước | Công việc / đường dẫn | Điều kiện xong |
| --- | --- | --- |
| SG0 — baseline/state | Audit providers/consumers/tests; hợp nhất `drawing_preferences.*`, SnapConfig và semantic actions/menu | Một effective state; F3/menu/popup/Properties đồng bộ; unsupported có lý do. |
| SG1 — Select/layout | Mới `modules/vector/.../precision/snap/layout_snap.*`, layout feature index/solution; nối `native_window_selection.cpp` và gesture adapter | Move bắt mép/tâm/corner/contact, không lệch grab offset, cả multi-selection; overlay và command cùng solution. |
| SG2 — spacing/guides | Spacing provider/lanes, overlay trong file domain mới `native_window_smart_guides.*`; camera DIP vào `snap_status_controller.*` | Hai gap/lặp gap đúng như ảnh, chống rung; tracking có runtime consumer; đo không ghi document. |
| SG3 — constraints/tools | Shape/Pen/Node/Text/resize/rotate adapters; `drawing_tool_controller.*`, native pointer/tool event files | Không phá Shift/Ctrl/numeric/IME; giữ input owner, các tool dùng đúng kind/DOF, không duplicate command. |
| SG4 — Page/Artboard | Thêm scope/region providers theo model và transform của Page/Artboard plan; mở rộng SnapQueryStamp/index invalidation | Không lẫn page/scene dù revision giống; artboard overlap không duplicate refs; active filter/isolation đúng. |
| SG5 — UI/settings | Magnet split-button, dropdown, Properties, Settings/persist migration, icons/help; menu/toolbar painting và geometry chung | Dùng được bằng mouse/keyboard/DPI; không tool switch, không click xuyên; không thêm hàng controls dài. |
| SG6 — acceptance | Mở rộng tests hiện có, build app chuẩn, visible UI + benchmark | Các gate bên dưới có evidence; không gọi source/enum/helper pass là UX hoàn tất. |

SG0–SG2 có thể triển khai cho page hiện có sau Select P0, không phải chờ toàn bộ Artboard hoặc solver CAD.
SG4 hoàn tất sau W2/W3 của Page/Artboard. Không sửa tài liệu OSNAP cũ thành “đã hoàn tất”; chỉ tái dùng quy tắc/provider phù hợp.
Giữ registry dependency; core/module không phụ thuộc apps; mọi file code tối đa 2.000 dòng.
Chia source theo domain trong target hiện có; không append vào file quá ngưỡng, không thêm test source dùng một lần.
Không task lease/worktree/nhánh riêng; không reset/stash/commit kèm local edits của công việc khác.

## 10. Acceptance và bằng chứng

| AC | Given / When | Then / oracle |
| --- | --- | --- |
| SG-01 | Source options đang cấu hình; toggle master/off/bypass bằng mọi entry point | Con options còn nguyên, hiệu lực đồng nhất; no revision/Undo; nhả bypass trả snap đúng tool. |
| SG-02 | Hai rect; grab tại nhiều điểm thân rect rồi kéo căn cạnh/tâm/corner | Corrected transform căn moving feature đúng, grab offset giữ; không biến thành cursor-to-center snap. |
| SG-03 | Hai rect cạnh sát, stroke khác nhau; đổi Geometry/StrokeBounds | Contact theo đúng bounds mode và nhãn, target không đổi; không báo chạm mực khi chỉ path chạm. |
| SG-04 | A=[0,20], B=[30,50], C rộng 35; kéo C gần hàng | C=[60,95], hai gap 10 mm, nhãn/đường đo đúng; center-spacing sai phải bị test phát hiện. |
| SG-05 | A=[0,20], C=[80,110], B rộng 20; thêm một case không đủ chỗ | B=[40,60], gap 20; case g<0 không báo equal positive gap; thử tương tự theo Y. |
| SG-06 | Neighbors khác hàng, object che giữa và nhiều gaps gần bằng | Chọn cặp liền kề/lane hợp lệ, deterministic; không snap theo object xa không liên quan. |
| SG-07 | Pointer dao động gần hai targets; đi vào 8 DIP, rời 12 DIP | Acquire/retain/release đúng, không rung; cùng vị trí kết quả không phụ thuộc hash iteration. |
| SG-08 | 100/125/150/200% DPI, nhiều zoom/pan và overview transform | Screen aperture nhất quán; không 6 mm bất biến; tọa độ model/distance nhãn chính xác theo đơn vị trang. |
| SG-09 | Multi-selection/group/PowerClip đang kéo | Self roots/descendants không làm refs sai; cả nhóm cùng translation; clip/scope tôn trọng, một Undo. |
| SG-10 | Numeric exact, Shift axis/aspect, Ctrl center và Alt duplicate | Constraint hợp lệ giữ nguyên; snap không sửa số; original/ghost references theo policy; không merge node. |
| SG-11 | Resize/rotate với đối tượng xoay/shear và hàng xóm cùng size/góc | Chỉ đúng DOF bị đổi; fixed anchor không trôi; không dùng AABB để báo intrinsic size/angle sai. |
| SG-12 | Pen/Line/Polyline/Node/Text, kéo handle và IME đang active | Đúng source snap/khóa hướng; handle/anchor không bị đổi sai; keyboard không mất chữ; test native dispatch. |
| SG-13 | Hover source rồi acquire tracking, lock/clear; pan hoặc mất focus | Dwell/state/ray đúng screen metric; guide ảo không lưu; Escape xử lý đúng một lớp, không hủy nhầm session. |
| SG-14 | Hai pages/scenes có cùng revision, đổi active artboard/isolation | Không lấy index/marker target cũ; active-artboard filter/hidden/locked/layer policy đúng; locked chỉ reference. |
| SG-15 | Index build đang chạy, stale result, budget/error hoặc delete target | Raw+constraints hoạt động; result cũ bị reject; không nhảy object khi worker tự hoàn tất. |
| SG-16 | Pointer-up ở vị trí khác last Move; commit/cancel/Undo/Redo | Preview/command cùng final solution; final geometry trong error bound; đúng một transaction, không sửa refs. |
| SG-17 | Dropdown/Properties/Settings/menu + field sai + narrow/DPI | Cùng checked/value/disabled state, focus/help đúng; click ngoài đóng không vẽ; không đổi tool hay auto Quick Shapes. |
| SG-18 | Import subsets và PDF reference lock; move snapped, native save/reopen, export | IDs/source metadata/color/effects giữ; không tạo source nodes cho reference; overlays không xuất. |
| SG-19 | Hai screenshot case bằng chuột thật trên app chuẩn | Thấy guide/nhãn gap đúng và object thật sự snap; helper/marker-only pass không đủ. |
| SG-20 | 1k/10k/100k features, drag trong scene dày, index cold/warm | Ghi query/solve/overlay p50/p95/max, input→visible và RAM; không O(n²) spacing toàn scene, không block UI vì rebuild. |

Mục tiêu ban đầu cho SG-20: query+solve p95 ≤4 ms trên fixture tương tác 10k features/máy chuẩn được ghi rõ;
luồng pointer/frame so baseline cùng máy và fixture, regression p95 mục tiêu ≤10%. Đây là mục tiêu cần đo, chưa có kết quả.
Lỗi geometric dùng residual/error budget mm độc lập với capture DIP; layout arithmetic fixtures kiểm tọa độ double với tolerance phù hợp,
curve oracle theo kế hoạch OSNAP, không dùng tolerance bắt điểm 8 DIP làm độ chính xác sản xuất.

Mở rộng tests hiện có:

- `modules/vector/tests/precision/snap/test_snap.cpp`, `test_direction_snap.cpp`, selection/gesture/input tests khi bị chạm.
- `apps/desktop/tests/test_drawing_tool_snap_input.cpp`, `test_drawing_tool_ui.cpp`, `test_snap_status_controller.cpp`.
- `apps/desktop/tests/test_desktop_shell*.cpp`, `test_canvas_view.cpp`, `test_pen_native_shell.cpp` cho consumer/Win32/pointer.
- Reuse import/storage/export fixtures khi cần kiểm committed geometry/metadata; không tải ảnh khách hàng hoặc để screenshot/output tạm trong repo.

Lệnh dự kiến sau triển khai, tên target đã đối chiếu CMake:

```powershell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_vector_snap,test_pikdraw_vector_direction_snap -TestRegex '^test_pikdraw_vector_(snap|direction_snap)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_drawing_tool_controller,test_pikdraw_drawing_tool_ui,test_pikdraw_snap_status_controller -TestRegex '^test_pikdraw_(drawing_tool_controller|drawing_tool_ui|snap_status_controller)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_canvas_view,test_pikdraw_pen_native_shell -TestRegex '^test_pikdraw_(desktop_shell|canvas_view|pen_native_shell)$'
.\tooling\native\build.ps1 -Target pikdraw
```

Chọn subset theo code đã sửa; thêm module regression đúng target nếu đổi shared API. Không dùng Force/NoLock vượt build lock.

## 11. Trạng thái triển khai — 2026-10-23

Solver layout đã nối vào thao tác Move/Resize của Select. Move hỗ trợ căn cạnh/tâm, tiếp xúc bounds và
khoảng hở bằng nhau theo X/Y; Resize chỉ cho tay nắm đang kéo bắt điểm, giữ cạnh neo đối diện và giữ
tỉ lệ khi Shift. Cả hai dùng stable feature key, hysteresis và ngưỡng DIP; overlay lấy từ geometry preview
đã chấp nhận. Select tham chiếu object khác, Page/Artboard khi phù hợp và guidelines. State được khóa theo
revision/page/view/selection/layer/isolation. Không tạo Undo/document dirty khi đổi setting.

Quick Shapes bắt endpoint lúc tạo; preview và geometry commit dùng cùng điểm Snap. Artboard Tool dùng
cùng cấu hình cho create/move/resize; resize bắt cạnh đang kéo và giữ cạnh neo. Pen endpoint gọi SnapIndex
với stamp/camera/config hiện hành; Shift direction lock và kéo handle Pen giữ quyền ưu tiên riêng. Node
Move gọi SnapIndex theo tool context và loại path đang sửa theo target policy. Line/Polyline tiếp tục dùng
query path hiện có.

Snap đã nối vào UI chung: split-button bật/tắt master, popup nguồn cơ bản, View toggles và dock
`Snap Properties`. Panel cho phép group/candidate được engine hỗ trợ, acquire/release radius, current
layer, locked references, self path và hidden-layer policy. `test_pikdraw_snap_properties_ui` bấm qua
toolbar/popup/dock, xác nhận các setting được lưu và khôi phục preferences gốc; controller test kiểm
schema persistence và config engine.

Kiểm chứng subset ngày 2026-10-23: 12/12 pass gồm `test_pikdraw_vector_snap`, `test_pikdraw_desktop_shell`,
`test_pikdraw_artboard_tool_ui`, `test_pikdraw_artboard_snap_ui`, `test_pikdraw_snap_properties_ui`,
`test_pikdraw_quick_shape_snap_ui`, `test_pikdraw_pen_native_shell`, `test_pikdraw_drawing_tool_controller`,
`test_pikdraw_drawing_tool_ui`, `test_pikdraw_snap_status_controller`, `test_pikdraw_editor_tools` và
`test_pikdraw_node_tool_controller`. Chưa có chuột/pixel trực quan trên canonical app hoặc benchmark.
Còn mở: snap góc khi Rotate; Text frame/IME; Node handle/segment và Pen handle theo semantics riêng;
nested group/PowerClip scope; tracking lines động, margin/bleed constraints, thứ tự ưu tiên tùy chỉnh và
đo hiệu năng ở nhiều DPI. Không xem kết quả này là parity đầy đủ CorelDRAW/Illustrator; grid/guide object
candidates chỉ được bật theo provider/capability hiện có.
