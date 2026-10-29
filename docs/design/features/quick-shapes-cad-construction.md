# Kế hoạch mở rộng Quick Shapes cho bản vẽ kỹ thuật 2D

Ngày: 25/10/2026. Trạng thái: **đang triển khai theo giai đoạn; P0 chưa hoàn tất/nghiệm thu runtime**. Nền solver analytic cho Circle/Arc, factory path sửa được, Circle Center/Radius trong Quick Shapes và kiểm tra layer đích đã được nối. Các phương pháp dựng Circle/Arc còn lại, construction spec/codec, preview đa điểm, Properties, save/reopen và nghiệm thu output vẫn đang chờ. Phạm vi là vẽ mới và sửa các hình tròn/cung tròn chính xác trên canvas PikDraw; giữ nguyên Rectangle, Ellipse, Polygon, Path Drawing, Dimension và nhóm Chú thích đang có. Ưu tiên thao tác chuột, snap và nhập số theo đơn vị trang, với preview đồng bộ canvas.

## 1. Tình huống thực tế và quyết định sản phẩm

Người dùng đang dựng bản vẽ tấm cơ khí: vẽ lỗ Ø20 mm từ hai đầu đường kính đã đo; vẽ vòng tròn đi qua ba điểm lắp; nối hai vị trí bằng cung R30; hoặc dựng cung đi qua ba mốc bất kỳ. Mỗi hình phải có tâm/bán kính/góc thật trong mm, chọn được bằng Node/Dimension, sửa được sau khi lưu/mở `.pr`, giữ layer/paint/Undo, và báo rõ khi ba điểm gần thẳng hàng hoặc bán kính không thể đạt.

**“Đường tròn qua 2 điểm” có hai nghĩa khác nhau:** P0 đặt tên `Circle — 2 Diameter Points`: hai điểm là **hai đầu đường kính**, nên tâm là trung điểm và R bằng nửa khoảng cách. `Circle — 2 Points + Radius` là bài toán khác, có hai tâm khả dĩ và cần chọn phía; chỉ mở ở P1. Không gọi một đường tròn xác định bởi hai điểm bất kỳ là nghiệm duy nhất. `Arc — 3 Points` dùng điểm đầu, một điểm **nằm trên cung**, điểm cuối; phân biệt với `Center–Start–End`, nơi click thứ ba chỉ chọn **hướng bán kính cuối** và được chiếu lên đường tròn.

Tham khảo chính thức: [AutoCAD CIRCLE](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-Core/files/GUID-C60B6D5D-AAEB-420F-917F-6E6B47E92F48.htm) liệt kê Center/Radius, 2P, 3P và Tangent–Tangent–Radius; [AutoCAD ARC](https://help.autodesk.com/view/ACD/2026/ENU/?caas=caas%2Fdocumentation%2FCIV3D%2F2014%2FENU%2FfilesACD%2FGUID-30ECFD30-A1D6-4D60-9DD1-B487603F6772-htm.html) có tổ hợp ba điểm, tâm, bán kính, góc và hướng; [LibreCAD Drawing Tools](https://docs.librecad.org/en/latest/ref/tools.html) tách Circle, Curve và Polyline arc theo phương pháp dựng. PikDraw chọn tập phù hợp workflow hiện có; không sao chép menu, icon hoặc shortcut của các ứng dụng đó.

## 2. Baseline source cần giữ và điểm cần sửa khi triển khai

| Thành phần | Đã thấy trong source ngày lập plan | Hệ quả thiết kế |
| --- | --- | --- |
| Toolbox | `ToolboxSlotId::QuickShapes = 2`, flyout group 1 có Rectangle/Ellipse/Polygon; Path Drawing group 2 có Line/Polyline/Pen/Node. `R`, `E` đã dùng; click chính gọi tool dùng gần nhất. | Mở rộng **cùng Quick Shapes**, không thêm slot mới, không đẩy Circle/Arc sang Path Drawing chỉ vì chúng là path. Giữ trạng thái active tool, hover/focus/flyout và last-used độc lập. |
| Tạo shape | `ShapeFactory` tạo `PathObject` có `authoring_source`; `NativeWindow::commit_created_shape` tạo Rectangle/Ellipse/Polygon qua `AddObjectCommand`. | Dùng cùng style, layer, document command và selection. Preview/commit phải dùng một proposal/solver, không phát triển một nhánh paint có geometry khác. |
| Hình tròn hiện tại | `Path2D::make_ellipse` dùng **bốn cubic Bézier**; ellipse đang là shape trực quan, kể cả khi rx=ry. | Công cụ Circle kỹ thuật mới dùng cung tròn giải tích. Không đổi ngầm hình Ellipse/đối tượng cũ thành “đường tròn chính xác”. |
| Arc hiện có | `Path2D::ArcTo`, `geometry::EllipticArc`, `EditablePath` arc segment và adapter đã có; DXF import tạo ARC bằng một `ArcTo`, CIRCLE bằng hai nửa cung. | Đưa phần dựng circle/arc dùng chung vào `core/geometry`, tái sử dụng cho Quick Shapes và adapter DXF sau review; không copy thuật toán riêng vào desktop. |
| Snap/nhập số | Snap Preferences/OSnap, worker index, parser đơn vị và `NumericFieldDescriptor` đã có trong Path Drawing/desktop. | Dùng cùng chính sách snap và mm/world coordinates; bán kính, đường kính, góc là field semantic mới, không đọc số đo từ bounding box/pixel. |
| Layer/commit | `commit_created_shape` hiện kiểm `layers.front()` trước khi ghi vào `active_layer_index()`, có thể lệch nếu layer đầu khóa/ẩn. Shape commit còn bật quick-dimension post-commit theo W×H. | A0 sửa/đo gate theo **layer đích thực**, dùng hierarchy/editability; nhập số Circle/Arc trước commit, không ép W×H hoặc tạo hai Undo. |
| Lưu/xuất | `.pr` đang dùng payload schema v25/container v26 khi có LayerId/group/role; DXF importer có subset CIRCLE/ARC, **chưa có DXF writer**; SVG writer là subset Path/Text. | Nếu lưu construction metadata mới, thiết kế migration/version tiếp theo theo C5; native save/reopen là gate P0. DXF export và mở lại trong CAD là gate riêng, không suy từ DXF import. |

Các roadmap cũ chỉ làm ngữ cảnh. Baseline ở bảng phải kiểm lại ngay trước code vì checkout có nhiều local edits; không ghi đè những thay đổi đó.

## 3. Danh mục công cụ và thứ tự ưu tiên

| Mốc | Tool / phương pháp | Cử chỉ chuột và kết quả | Giá trị sử dụng |
| --- | --- | --- | --- |
| P0 | **Circle — Center/Radius** | Click tâm → kéo/click điểm bán kính hoặc nhập `R`; có lựa chọn Diameter nhập `D`. | Lỗ, trụ, vòng đồng tâm với tâm đã snap. |
| P0 | **Circle — 2 Diameter Points** | Click hai đầu đường kính, preview tâm và Ø; click thứ hai commit. | Dựng lỗ theo hai mép đã đo, không tính tâm thủ công. |
| P0 | **Circle — 3 Points** | Click ba điểm trên chu vi; sau hai điểm, di chuột preview đường tròn ứng viên; điểm ba commit. | Dựng vòng qua ba mốc khảo sát/lắp ráp. |
| P0 | **Arc — 3 Points** | Click điểm đầu → điểm qua → điểm cuối; preview hướng cung đi qua điểm thứ hai. | Đường cong biên dạng đã biết ba mốc. |
| P0 | **Arc — Center/Start/End** | Click tâm → điểm đầu → click hướng bán kính cuối; preview cung và góc quét; có đổi CW/CCW. | Cung tâm/bán kính cố định, ví dụ rãnh/quỹ đạo. |
| P0 | **Arc — Start/End/Radius** | Click hai đầu → nhập/kéo R; khi có hai nghiệm, rê phía mong muốn hoặc chọn `Flip side` trên canvas/Properties trước commit. | Nối hai mốc bằng cung bán kính được chỉ định. |
| P1 | **Circle — 2 Points + Radius** | Chọn hai điểm trên chu vi, nhập R, chọn một trong hai tâm ứng viên. | Dựng vòng R cố định qua hai mốc; từ chối khi R < nửa dây cung. |
| P1 | **Circle — Tangent/Tangent/Radius** | Click gần vị trí tiếp xúc trên hai line/circle/arc hợp lệ → nhập R → chọn nghiệm preview. | Lỗ/vòng tiếp xúc hai cạnh đã có. Không nhận Bézier bất kỳ khi chưa có solver và oracle. |
| P1 | **Arc — Tangent from endpoint** | Chọn endpoint và hướng tiếp tuyến của Line/Arc/Polyline → click endpoint mới hoặc nhập R/góc; preview G1. | Tiếp nối đường biên trơn mà không căn góc bằng mắt. |
| P1 | **Concentric Circle/Arc** | Chọn circle/arc nguồn → kéo/nhập khoảng cách hoặc R mới; một object mới trên layer đích. | Vòng đồng tâm và biên dạng theo bề dày. Dùng solver offset hiện có khi semantics khớp, không tự thay Contour. |
| P2 | **Ring/Washer** | Click tâm → nhập đường kính ngoài/trong; compound path có hole và fill rule đúng. | Bản vẽ vòng đệm, lỗ khoan vòng. Không chỉ vẽ hai hình chồng lên nhau rồi gọi là một vùng kín. |
| P2 | **Slot/Obround** | Chọn hai tâm đầu rãnh hoặc kéo khung → nhập width/R; một path kín có hai nửa tròn giải tích. | Rãnh dài gia công, biển hiệu, CNC. |
| P2 | **Arc/Line mixed Polyline** | Trong Path Drawing, chuyển đoạn tiếp theo Line ↔ Arc với R/góc; cùng `EditablePath`, một Undo. | Biên dạng kỹ thuật liên tục. Đây là nâng cấp Polyline, không thêm tool Quick Shapes trùng. |

`Fillet`/bo góc là **lệnh sửa hai cạnh có thể trim/extend** và thuộc nhóm Cut & Shape/Node; Quick Shapes chỉ tạo cung độc lập hoặc tiếp tuyến không sửa nguồn. Center Mark/Centerline là dấu trình bày thuộc Dimension/Chú thích, không tạo thêm đường sản xuất trong Quick Shapes. Ellipse tiếp tục là tool riêng; nâng nó lên ellipse giải tích/axis handles là mốc độc lập sau khi Circle/Arc đạt gate.

## 4. Hợp đồng hình học, model và lưu file

### 4.1. Solver chung và tình huống suy biến

`CircleSolution` chứa tâm, R dương hữu hạn, điểm/ràng buộc đã dùng và diagnostic; `ArcSolution` thêm start angle, signed sweep, hướng, branch và endpoints. Toàn bộ dùng mm/binary64, world Y-up; góc đo CCW từ +X, không lấy dấu từ tọa độ màn hình Y-down. Hai điểm trùng hoặc quá gần theo **dung sai model có giải thích** thì từ chối. Circle 3P tính circumcenter bằng tọa độ tịnh tiến/scale để tránh mất chính xác khi tọa độ rất lớn; ba điểm thẳng/gần thẳng không tạo vòng bán kính khổng lồ hoặc NaN. Tolerance hình học tách khỏi snap radius DIP và sai số render.

Arc 3P phải đi qua cả ba điểm trong giới hạn số học; không tự đổi major thành minor. Center–Start–End giữ R theo điểm đầu, click cuối chọn tia/hướng và preview điểm cuối thực trên đường tròn. Start–End–Radius yêu cầu R ≥ chord/2; hai tâm/major-minor là các nghiệm khác nhau, chọn bằng vị trí chuột/toggle có nhãn và xem trước rõ. Sweep bằng 0/full turn không ghi thành `ArcTo` một đoạn endpoint trùng; full circle dùng hai cung bán nguyệt nối kín. Tangency P1 chỉ nhận segment/transform có solver kiểm chứng; nghiệm nhiều chọn theo vị trí click gần điểm tiếp xúc rồi cho `Flip candidate`, không tự chọn nghiệm khác sau khi pointer rời canvas.

### 4.2. Đối tượng chỉnh sửa được

P0 lưu `PathObject` với `EditablePath` giải tích: circle là **hai EllipticArc bán nguyệt có hai bán trục bằng nhau và closure hợp lệ** (`Path2D::ArcTo` dùng rx=ry), arc mở là một arc segment khi sweep nằm trong miền biểu diễn. `Path2D` chỉ là biểu diễn render/export phát sinh từ cùng source; không polygonize hoặc thay bằng bốn cubic của `make_ellipse`. Open arc mặc định `has_fill=false`, stroke theo style hiện có; circle theo tool default có fill None cho bản vẽ kỹ thuật, nhưng người dùng đổi fill/stroke bằng Appearance như path thường. Không tự đổi default của Ellipse cũ. Bản nháp chỉ chứa solution/proposal, không thêm object giả vào document.

Để sửa tâm/R/Ø/góc trong Properties sau lưu/mở, thêm **typed, versioned construction spec** liên kết ObjectId và các node/segment ID của `EditablePath`; không đẩy tiếp chuỗi ad hoc vào `contour_spec`. Spec ghi phương pháp tạo chỉ như provenance khi điểm nguồn không còn associative; chỉ tuyên bố liên kết động với source object khi có SourceReference và lifecycle riêng. Khi kéo handle hoặc sửa số, regenerate source trong một command, giữ ID các node/segment có nghĩa; nếu Node edit phá tính tròn, detach construction spec tường minh và giữ path editable, không để Properties hiện R sai. Nonuniform scale/shear biến circle thành ellipse; ghi trạng thái `Converted to Ellipse` hoặc yêu cầu lựa chọn, không báo Circle R cũ như đúng. Mirror/rotation/uniform scale cập nhật center, radius, sweep, orientation nhất quán.

`AddConstructionShapeCommand`/`UpdateConstructionShapeCommand` dùng writer/transaction hiện có, validate snapshot epoch/revision, layer/group owner, finite values, source ID và quota trước commit. Tạo một shape là một Undo; đổi mode/tool, Esc/Backspace hoặc click không hợp lệ không làm bẩn tài liệu. Copy/paste/duplicate remap ID nội bộ và style/layer, không biến circle analytic thành ellipse cubic. Codec `.pr` lưu spec mới với version/legacy reader, reader v2–v26 tiếp tục mở được; chỉ chọn payload/container revision sau khi audit C5 và mọi consumer. Không nêu `.pr` mới là đã tương thích trước khi có save/reopen test.

### 4.3. Đồng bộ renderer, công cụ khác và output

`GeometryConstructionProposal` là nguồn chung cho overlay, Properties và commit; overlay lấy chính arc/circle analytic và status của solver. Software/D2D, selection/hit-test, snapping center/quadrant/tangent/intersection, culling/damage, Node, Dimension Radius/Diameter/Arc Length, Contour/Trim/Boolean và CNC preflight phải nhận cùng path semantics. Appearance/Color/ICC, layer visible/locked/printable, group/PowerClip owner, z-order và Artboard của scene trang giữ như shape hiện tại. Không chạy solver/worker nặng trong `WM_MOUSEMOVE`; coalesce mẫu pointer, cache snap index, hủy proposal stale theo revision/generation. Không dùng flattening preview làm nguồn của kích thước hay máy cắt.

| Output | Gate cần kiểm |
| --- | --- |
| `.pr` | Giữ analytic source, construction spec, IDs, style/layer, Undo sau edit và save/reopen; file v2–v26 cũ mở đúng. |
| SVG | Writer xuất arc/path và paint đúng sweep/closure; kiểm lại bằng parser độc lập và pixel oracle. SVG có thể không giữ mode dựng/associative metadata của PikDraw, báo rõ. |
| DXF | Hiện chỉ có **import** ARC/CIRCLE subset. DXF writer CIRCLE/ARC, unit/layer/transform và mở–sửa–lưu trong CAD đích là mốc liên thông riêng; không tuyên bố round-trip đã có. |
| PDF/AI/CDR/EPS | Không phải source xuất trực tiếp của task này; nếu pipeline tương lai nhận shape, báo riêng representation/version, render, editability, provenance, round-trip và production theo C8/C9. |
| Print/CNC/raster | In và raster dùng scene/render đúng; CNC chỉ từ geometry được preflight với sai số/kerf/profile, không suy tính sản xuất từ hình preview hay DXF import thành công. |

## 5. Phiếu UI/UX: chuột, thanh nhanh và Properties

**Menu chính:** `Tools > Quick Shapes > Rectangle`, `Ellipse`, `Polygon`, `Circle`, `Arc`; `Circle`/`Arc` mở submenu phương pháp sau khi có handler. Mục Rectangle/Ellipse/Polygon hiện ở Tools chuyển vào nhóm bằng cùng action ID, giữ `R`/`E`; không có menu cấp cao mới. **Toolbox:** giữ slot QuickShapes=2; flyout hiển thị Rectangle, Ellipse, Polygon, Circle ▶, Arc ▶, mốc P1/P2 nằm trong submenu theo nhu cầu. Click thân slot dùng phương pháp cuối đã dùng, click mũi tên mở flyout; đang focus/hover không đổi active tool. **Standard toolbar:** không thêm vì toolbox là đường thao tác chính; menu và Help vẫn truy cập được khi ẩn toolbox.

Action IDs đề xuất: `action.tool.circle.center_radius`, `.diameter_2p`, `.through_3p`, `.through_2p_radius`, `.tangent_tangent_radius`; `action.tool.arc.through_3p`, `.center_start_end`, `.start_end_radius`, `.tangent_from_endpoint`; `action.tool.shape.ring`, `.slot`; `action.shape.construction.properties.focus`, `.flip_candidate`, `.detach_spec`. Các suffix trong nhóm `action.tool.circle.`/`action.tool.arc.` có một descriptor chung: label, icon, cursor, click-state, enable reason, help ID và test. Existing `action.tool.rectangle`, `.ellipse`, `.line`, `.polyline`, `.pen` giữ nguyên. Không gán shortcut mới trước khi kiểm catalog; `R`/`E` không đổi.

| Ngữ cảnh | Property bar (3–5 control nhanh + `Thuộc tính ▸`) | Properties chi tiết và scope |
| --- | --- | --- |
| Circle draft | Method, R/Ø selector + ô số có unit, stroke width, fill None/paint nếu phù hợp. | Construction: center X/Y, R, Ø, điểm P1–P3 theo method, trạng thái snap/solution; Appearance và Layer. Tool default riêng với draft/selection, không ghi document khi chỉ chọn method. |
| Arc draft | Method, R hoặc included angle theo method, CW/CCW, Flip candidate. | Center/start/end X/Y, R, start/end angle, signed sweep, major/minor, chord length, tangency source/candidate và error; field chỉ enable nếu solver hỗ trợ. |
| Một Circle/Arc đã chọn | R/Ø, center/angle khi spec còn hợp lệ, stroke/Appearance theo vùng sẵn có. | Transform, Construction, Appearance, Layer, Action Group (`Detach Construction`, Convert if needed); sửa bar/Properties cùng command và Undo. |
| Nhiều shape / mixed | Field chung hiển thị `Mixed`, chỉ Batch Edit khi cùng loại/semantics và operation được định nghĩa. | Không giả giá trị 0, không đổi branch của các arc khác nhau trong một thao tác ngầm. |
| Locked/hidden/read-only/không tài liệu | Tool hoặc field disabled với lý do và cách tiếp tục; không tạo draft/ghi object trên layer khác. | Mở/focus Properties không sửa document; đủ lối vào khi dock đóng hoặc bar hẹp. |

Field semantic: `shape.circle.center.x/y`, `radius`, `diameter`, `method`, `source_point[i]`; `shape.arc.center.x/y`, `radius`, `start_angle`, `sweep_angle`, `chord`, `direction`, `branch`. Unit parser nhận đơn vị trang và hậu tố tường minh; radius/diameter finite >0, 3P không gần thẳng hàng, angle/sweep hữu hạn và trong miền quy định. Slider/tay nắm phù hợp để rà bán kính/góc bằng chuột, luôn kèm ô số chính xác; phương pháp/branch là lựa chọn rời rạc, không dùng slider giả. Tool defaults, document style, draft, selected object là các scope riêng, hiển thị rõ trên panel. Thay số valid preview ngay; commit Enter/click cuối chỉ một Undo. Mất focus/dock đóng không bỏ bản nháp đang nhập; Esc hủy hoặc lùi một bước theo state rõ.

**Trình tự thao tác:** sau mỗi click, canvas giữ điểm đã chốt và vẽ hình ứng viên theo pointer; snap marker và trị số R/Ø/góc hiện gần cursor/status nhưng không che mục tiêu. Right-click/Enter hoàn tất chỉ khi đủ dữ kiện; Backspace bỏ điểm cuối; Esc một lần lùi bước, Esc khi không còn bước hủy; Space pan tạm rồi trở về tool và điểm đã chốt. Với Circle 3P, hint sau hai điểm nói rõ điểm thứ ba nằm **trên đường tròn**; với Arc Center–Start–End, hint nói click cuối chỉ hướng và sẽ chiếu lên R. Khi solver invalid, preview không biến thành một hình hợp lệ giả, status nêu lý do và vẫn cho sửa điểm/số. Kéo tay nắm sau khi chọn cập nhật canvas, Property bar và Properties từ một draft; mouse-up một Undo. Dùng quy tắc preview/coalescing của [UI/UX mục 5.3](../ui-ux-standard.md), không hứa FPS khi chưa đo.

**Icon/Help:** asset SVG đề xuất `quick-circle-center.svg`, `quick-circle-diameter-2p.svg`, `quick-circle-3p.svg`, `quick-arc-3p.svg`, `quick-arc-center.svg`, `quick-arc-radius.svg` dưới `apps/desktop/resources/icons`; không sao chép icon AutoCAD. Native shell đang có icon/glyph vẽ trực tiếp, nên gate là asset được paint/hit/focus thật trên runtime. Label EN/VI ngắn (`Circle — 2 Diameter Points / Tròn qua hai đầu đường kính`, `Arc — 3 Points / Cung qua ba điểm`); tooltip/focus help hai dòng nói phương pháp và shortcut thật nếu có; status từng bước. `Help > Quick Shapes` có ví dụ 3–5 bước, cách đổi nghiệm, nhập R/Ø, sửa lại, giới hạn DXF/CNC. Kiểm flyout chuột/bàn phím, 1280×720, 1366×768, DPI 100–200%, light/dark, IME và screen reader.

## 6. Lộ trình triển khai và gate

| Mốc | Ownership source và việc cụ thể | Gate trước khi mở UI tiếp theo |
| --- | --- | --- |
| P0a — audit/nền | `core/geometry`: solver finite/degenerate, builder analytic Circle/Arc. `core/document`: construction spec/command/storage migration. `modules/interop`: xác nhận lại DXF import helper. Audit tất cả `std::visit`, model/renderer/selection/snap/Dimension/production và bug gate `layers.front()` trong shape commit. | Geometry oracles độc lập; legacy `.pr` v2–v26; typed spec không làm hỏng Rectangle/Ellipse/Polygon; active layer thực được kiểm. |
| P0b — Circle | `modules/vector/src/tools/shape/`: proposal/session cho Center/R/Ø, 2 diameter endpoints, 3 points. `apps/desktop/src/tools/` và shell action/flyout/bar/Properties/canvas overlay. | Một người dùng vẽ ba Circle bằng chuột và nhập số, sửa R bằng handle/Properties, Undo và save/reopen; render CPU/D2D cùng hình. |
| P0c — Arc | Cùng session/solver cho 3P, Center–Start–End, Start–End–Radius; signed sweep/branch, reject degenerates. | Tạo minor/major và CW/CCW đúng, không đổi hướng khi zoom/DPI hoặc redo; Node/Dimension/Snap đọc đúng arc analytic. |
| P1 — tiếp tuyến | Solver line/circle/arc bounded cho 2P+R, TTR circle, tangent arc; dùng provenance/source reference nếu muốn associative update, nếu chưa thì thông báo kết quả độc lập. | Nhiều nghiệm được chọn tường minh, zero/near tangency có oracle; không tự trim nguồn hoặc gọi Fillet giả. |
| P2 — sản xuất/biên dạng | Ring/Slot và mixed Polyline Arc; PowerClip/layer/print/CNC preflight; DXF writer chỉ khi được giao triển khai và kiểm format/version. | Hole/winding, transform, cutter error budget, SVG/DXF appearance/editability/round-trip tách riêng. |

Không thêm hàng loạt `if (active_tool_ == N)` vào file shell dài: descriptor tool + controller/session theo domain, shell chỉ route input/paint ngắn. Core không phụ thuộc desktop. Các test có giá trị đặt trong `core/geometry/tests`, `core/document/tests`, `modules/vector/tests`, `core/rendering/tests`, `apps/desktop/tests` và `modules/interop/tests`; output tạm theo AGENTS.md, dùng `tooling/native/build.ps1` với target phù hợp khi code thật. Task tài liệu này không chạy build.

## 7. Nghiệm thu đo được

| ID | Given / When / Then và bằng chứng |
| --- | --- |
| AC-QS-01 | Given P1=(0,0), P2=(20,0) mm; When dùng Circle 2 Diameter Points; Then center=(10,0), R=10, Ø=20 mm, two analytic semicircles, one Undo. |
| AC-QS-02 | Given (0,0),(20,0),(10,10); When Circle 3P; Then center=(10,0), R=10 mm và cả ba điểm nằm trên chu vi trong tolerance model; ba điểm gần thẳng hàng bị từ chối có lý do. |
| AC-QS-03 | Given radius 10 mm, endpoints cách nhau 15 mm; When Arc Start–End–Radius rồi Flip; Then hai nghiệm khác nhau có endpoint/R đúng; R<7.5 mm bị từ chối trước commit. |
| AC-QS-04 | Given ba điểm của cung hoặc tâm/điểm đầu/tia cuối; When vẽ CW/CCW, minor/major; Then hướng, signed sweep, endpoint preview = commit = save/reopen; không dựng arc endpoint trùng một đoạn. |
| AC-QS-05 | Given Circle/Arc analytic; When Node edit, Radius/Diameter/Arc Length Dimension, OSnap center/quadrant/intersection, transform mirror/uniform/nonuniform; Then semantics đúng hoặc convert/detach có thông báo; không dùng bounds làm R. |
| AC-QS-06 | Given active layer ẩn/khóa và layer đầu khác trạng thái; When click tool/commit; Then quyền được kiểm theo **layer đích**, không ghi sang layer 0, không có fake success; PowerClip owner đúng. |
| AC-QS-07 | Given bản vẽ có RGB/CMYK/spot paint, stroke/fill, nhiều layer và page; When tạo, sửa, copy/paste, Undo/Redo, lưu/mở; Then source analytic, IDs, color/profile, layer/z-order và spec đúng; file `.pr` v2–v26 vẫn mở. |
| AC-QS-08 | Given Circle/Arc; When render Software/D2D/WARP, export SVG/raster, in và thử production; Then pixel/geometry oracle, output capabilities và sai số được báo riêng; DXF export không được báo pass khi chưa có writer. |
| AC-QS-09 | Given cửa sổ hẹp, nhiều DPI, flyout Quick Shapes và tool Pick/Path Drawing; When click/hover/focus/Space pan/Esc/mở Properties; Then active tool/draft/flyout không tự đổi, controls có hit target đúng, canvas preview đồng bộ. |
| AC-QS-10 | Given 1.000 Circle/Arc và drag nhanh; When pan/zoom/sửa R/cancel; Then culling/damage và generation không trình bày preview stale; đo p50/p95 pointer-to-present, peak memory và kích thước `.pr`, không suy FPS từ test headless. |

**Điều kiện công bố P0:** từ menu/toolbox → snap/nhập số → preview → command/source analytic → Node/Dimension → Undo/Redo → `.pr` save/reopen → render/output đều được kiểm trên cùng source và EXE canonical. Dựng được hình bằng một API hoặc chỉ có icon trong flyout chưa đạt.
