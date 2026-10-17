# Hành vi Pen và nhập số chính xác

**Thiết kế, chưa triển khai.** Kế thừa [drawing semantics](../../../contracts/drawing/semantics.json),
[ADR path/input](../../decisions/drawing-path-input.md), [runtime](../../architecture/document-runtime.md).
Những chi tiết mới/khác cần P01 và consumer review trước code; không sửa contracts bằng tài liệu này.

## 1. Nguồn và ranh giới module

`desktop input/action → vector Pen/Node/ToolSession → document command → immutable snapshot`.
Geometry cung cấp evaluate/split/bounds/tangent; render đọc snapshot và overlay, không sở hữu nguồn.
Không đưa Win32/Skia/renderer types vào path/storage/API. Không thêm dependency vector→desktop.

Path nguồn dùng mm/binary64 và typed ObjectId/SubpathId/NodeId/SegmentId; `Path2D` là adapter
đọc dẫn xuất, không giữ song song hai nguồn mutable. Preview không tăng revision. Finish path
mới hoặc tiếp tục path cũ là một atomic command/undo unit, giữ IDs cũ và mapping của topology.
Node edit trên committed path là một undo unit mỗi gesture/Apply. Fill rule, stroke, colors,
alpha/ICC/overprint và transform không bị reset khi nối/resume/edit.

Làm việc ở world cho numeric/snap. Với đối tượng có transform T, điểm nguồn mới = inverse(T)
của điểm world; hướng/handle phải transform đúng như vector. Nonuniform scale không được dùng
một scale factor chung để suy ra chiều dài world. Transform suy biến: báo không sửa được,
không reset T hoặc flatten toàn path. Cubic line không thay source arc/quadratic đang có;
P09/P10 phải hỗ trợ thao tác đúng hoặc disable rõ cho segment chưa xử lý được.

## 2. Quy tắc vẽ/handle

| Hành vi | Kết quả |
| --- | --- |
| Click rồi click | Anchor Corner không handle, source segment Line |
| Drag tại anchor | Drag vượt threshold 3 DIP đề xuất tạo hướng outgoing; incoming đối nhau, Symmetric khi cả hai có nghĩa; ngưỡng chốt P01 |
| Thẳng→cong | Kéo tại anchor chung hoặc Adjust Handles, không đổi vị trí anchor trước |
| Cong→thẳng | Action tách/bỏ outgoing handle; segment tiếp theo là Line; đoạn cong trước giữ nguyên |
| Cusp | Incoming/outgoing độc lập; cho phép absent một bên |
| Smooth | Hai handle đối nhau/collinear, độ dài độc lập, liên tục G1; không hứa C1/C2 |
| Symmetric | Đối nhau và dài bằng nhau; không đồng nghĩa continuity C2 |
| Zero handle | H=0 trong Cusp là remove handle. Trong Smooth/Symmetric, yêu cầu đổi Cusp/remove rõ ràng; không serialize invalid node |
| Handle endpoint | Endpoint mới với một handle lưu Cusp; chỉ lên Smooth/Symmetric khi hai handle hợp lệ, UI không báo mode sai |
| Alt handle drag | Tách hai tay nắm; constrain modifier + Alt vừa tách vừa khóa hướng; không tắt snap |
| Đổi node mode | Preview phạm vi hai segment kề, không di chuyển anchor; chọn mode không hợp lệ giữ nguồn cũ + lý do |
| Double-click | Kết thúc path mở theo thiết kế Pen; chuẩn hóa chuỗi down/up/doubleclick để không thêm anchor trùng |
| Click node đầu với cursor Close | Dùng start NodeId; tạo closing segment khi cần; không thêm anchor cuối trùng |
| Cubic vòng kín hai anchor | Hợp lệ nếu hai node khác nhau và curve hợp lệ; không áp quy tắc polyline “≥3 vertex” lên mọi curve |
| Resume endpoint | Hover có dấu tiếp tục; click endpoint bắt đầu draft từ source snapshot; start endpoint đảo traversal và swap handles khi cần |
| Resume rồi hủy | Path cũ nguyên trạng; chỉ finish mới replace source nguyên tử |
| Bridge / Merge | Hai action khác nhau; Bridge thêm segment, Merge có survivor/mapping. Khoảng cách màn hình không cho phép tự merge |

Một endpoint trùng anchor trước nhưng cubic có handle tạo loop là trường hợp đặc biệt phải
có action/mode explicit và vector riêng ở P01; mốc A không tự sinh loop từ click zero-distance.
Phím/node kind mới chỉ có ý nghĩa sau bindings trace, xem [ui-spec](ui-spec.md).

### Profiles

- PikDraw/Illustrator-inspired: Shift constrain, Alt tách handle; Ctrl chuyển Node tạm khi chưa drag.
- Corel-inspired: Ctrl constrain, Alt tách handle; giữ F2 chuyển Node tạm trong phiên.
- S bỏ automatic snap khi canvas focus; không bỏ numeric/hard constraints; không bắt S của Ctrl+Shift+S.
- Space+drag/middle mouse pan tạm; wheel zoom; nhả modifier tính từ raw pointer và anchor gốc.

Tạm Node chỉ sửa draft hiện tại. Sửa committed path khác phải finish/hủy draft trước.
Node mode Shift+click thêm/bỏ chọn; không đồng thời đặt anchor Pen. Exact chord và pointer phase
được resolve sau focus scope; Ctrl+Z/Ctrl+A/Save As, AltGr và IME ưu tiên đúng ngữ cảnh.
Đổi profile khi drag: pending đến gesture kế tiếp, không đổi hành vi giữa lần kéo.
P01 đối chiếu behavior D09 hiện có; không ghi các profile này đã chạy trên shell.

## 3. State machine và lịch sử

| State/sự kiện | Hành vi bắt buộc |
| --- | --- |
| Idle → Pen | Chờ điểm đầu, không tạo object rỗng |
| PlaceAnchor/DragHandle | Pointer down giữ snapshot gesture; preview có generation, up xác nhận đúng một bước draft |
| NumericEdit endpoint + Enter | Parse/validate mọi field, đặt đúng một anchor; trở lại preview đoạn kế, chưa finish toàn path |
| HandleAdjust + Enter | Xác nhận H/θ của handle, trở lại Pen; không đặt thêm anchor |
| Preview + Enter | Finish các segment đã xác nhận thành path mở; không tự lấy vị trí hover cuối |
| NumericEdit + Esc | Bỏ text edit hiện tại, khôi phục candidate/locks trước mở editor; draft vẫn còn |
| DragHandle/DragNode + Esc/capture loss | Rollback gesture chưa confirm, giữ draft trước gesture |
| Preview + Esc | Bỏ candidate chưa confirm, finish path mở đã đủ segment; chỉ một anchor thì bỏ |
| Chờ điểm đầu + Esc | Thoát về Pick |
| Shift+Esc / Cancel Draft | Hủy toàn phiên; resume không sửa source cũ |
| Backspace/Ctrl+Z trong draft | Undo một thao tác draft; Ctrl+Y/Ctrl+Shift+Z redo draft; khi đang edit text dùng text history trước |
| Draft history hết | Không xuyên xuống undo document khi còn phiên chưa giải quyết; finish/cancel rồi mới document undo |
| Không draft + document Undo/Redo | Revision tăng đơn điệu; hủy generation preview/query cũ |
| Focus loss | Rollback gesture chưa xác nhận, clear modifiers, giữ anchors/locks/history để resume |
| Pan/zoom/resize | Giữ world draft, làm mới camera stamp; query camera cũ không được publish |
| Đổi tool chủ động | Finish draft hợp lệ; draft có lỗi giữ lại với Finish/Cancel; không auto-discard |
| Đổi tab/open/close document | Resolve draft qua Finish/Discard/Cancel; Cancel chặn chuyển. Sau chuyển không giữ token cũ |
| Document edit bên ngoài | Strict revision: draft stale, không auto-rebase; Retry có preview/explicit confirm từ snapshot mới hoặc Cancel |
| Commit fail | Không mất draft; document không half-commit; chưa công bố “saved” |

Checkpoint nháp là gói P06 riêng: có schema, base identity/epoch/revision và checksum/limits.
Không serialize lại toàn chuỗi node mỗi pointer move; dùng append/delta hoặc chunks copy-on-write.
Mặc định theo ToolSession hiện có: draft payload 8 MiB, history payload 6 MiB,
session payload 16 MiB, 256 history entries; checkpoint theo storage limit thực tế.
P02/P06 đo trước khi tăng quota. Chạm giới hạn giữ bản cuối hợp lệ và cho Finish/Cancel;
không silently evict undo đã hứa. Recovery mở draft riêng trên base phù hợp, không ghi đè file.

## 4. Chiều dài và góc so với X/Y

World: X sang phải, Y lên trên. Góc dương CCW. Screen Y đảo chỉ tại viewport adapter.
Camera pan/zoom/rotate không thay hệ số liệu. Đơn vị nguồn mm, hiển thị theo preference tài liệu.

Với anchor A=(Ax,Ay), L>0 và θx tính từ +X world:

```text
P = A + (L*cos(θx), L*sin(θx))
θx = normalize360(φref + θinput)
φref(World X) = 0°; φref(World Y) = 90°
θdisplay = normalize360(θworld - φref)
```

Theo +Y, θ=+30° hướng về bên trái của +Y: θworld=120°. Muốn sang phải dùng −30°
(hoặc 330°). UI phải ghi `Angle from +Y · CCW`, có glyph trục/mũi tên và trợ giúp;
không dùng góc nhọn unsigned làm mất quadrant. Reference chỉ là cách diễn đạt hướng,
không phải xoay artwork. Đổi reference khi có lock giữ nguyên hướng world và đổi số hiển thị.

`Previous segment`: φref là hướng tangent đi vào anchor trước; Line dùng vector segment,
Cubic dùng đạo hàm tại endpoint. Khi đạo hàm zero, thử đạo hàm bậc cao theo analytic policy
được P01 khóa; nếu không xác định thì disable + lý do, không giả 0°. Frame quay có cùng quy tắc.
Mốc C mới thêm frame origin tùy ý; parser hiện có chỉ chứa góc X, không tự giả origin có sẵn.

### Các cách nhập cùng tồn tại

| Thao tác/input | Nghĩa và trạng thái |
| --- | --- |
| Gõ L, Tab, θ, Enter | Đặt điểm theo reference được ghi ở field; gõ số kích hoạt numeric focus, không mở modal |
| Chỉ khóa L | Pointer quyết định hướng, khóa θ thì pointer chỉ quyết định L |
| `@250<30` | Relative polar; góc luôn từ +X active construction frame (world nếu unset), không phụ thuộc dropdown World Y |
| `@20;15` | ΔX/ΔY theo active frame từ anchor cuối; không có frame dùng world |
| `#120;80` | Absolute world XY, luôn world bất kể frame/reference hiển thị |
| `12.5cm`, `1in`, `24pt` | Reuse unit parser; inch=25,4 mm; pt=25,4/72 mm |
| `(100mm+20mm)/2` | Typed expression; ra 60 mm; không eval/script |
| `1,5cm` | Chỉ khi locale decimal-comma; dấu `;` phân cách tọa độ, không nhận grouping |
| L trong Pen cong | Độ dài dây cung giữa hai anchor; nhãn/help rõ. Không khóa chiều dài cong thật |
| H/θ handle | H tính từ anchor đến handle đang sửa, θ theo reference; incoming/outgoing được ghi rõ |

Không tự đổi nghĩa `@L<θ` theo dropdown Y: field hiển thị lại endpoint đúng với suffix
`Frame X` khi edit cả expression; numeric model không giữ hai angle driver cạnh tranh.
P05 phải có regression input như `@100<0` trong lúc UI reference Y vẫn đi sang +X frame.

### Hệ ràng buộc nhỏ, không phải CAD solver đầy đủ

Nguồn ưu tiên: numeric locks → hard tool constraints → snap tương thích → soft polar/grid → raw pointer.
Numeric θ không bị Shift/Ortho/snap ghi đè. Khi chế độ hard mâu thuẫn, đánh dấu driver xung đột,
giữ preview hợp lệ cuối cùng và chặn commit; cho bỏ khóa/đổi mode, không tự nới tolerance.

- L+θ xác định một endpoint; XY pair xác định một endpoint. Driver dư được check consistency.
- L+ΔX: giải ΔY=±sqrt(L²−ΔX²), nếu hợp lệ; pointer quyết định nhánh và UI chỉ báo nhánh.
  Không có pointer/hướng trước rõ thì yêu cầu chọn nhánh, không đoán. L<abs(ΔX) báo vô nghiệm.
- θ+ΔX hoặc θ+ΔY suy ra còn lại; hướng gần trục làm mẫu số gần zero phải dùng giải robust,
  không chia trực tiếp không kiểm. Thiếu driver không được tự dùng 0.
- Nhiều driver không nhất quán: không sửa driver nhập trước; không cập nhật một tọa độ rồi bỏ tọa độ lỗi.
- Tolerance số học consistency đề xuất `1e-8 mm + 1e-12*max(1 mm, |giá trị so sánh|)`;
  P01 khóa với consumers. Đây không phải snap radius, manufacturing tolerance hoặc bảo đảm cắt thật.
- Tọa độ/handle world và source sau inverse transform đều phải trong miền ±1.000.000 mm hiện có.
  Parser tối đa 256 bytes như D06, bounded tokens/depth; input vượt hạn, NaN/Inf/div0/sai dimension
  giữ text/error và không thêm node. Zero-length line không commit.

Sau confirm endpoint, locks bỏ cho candidate tiếp theo; Repeat là opt-in có chỉ báo và có đường tắt tắt.
Thay đơn vị/locale/reference chỉ đổi hiển thị; giá trị binary64 và geometry không round theo số chữ UI.
Nhập số không tạo persistent dimension: sửa node sau này được phép đổi L/θ cũ.

## 5. Node và topology

P09: click/Shift select, Window/Crossing/Lasso, drag/nudge và delta cho multi-node; single XY
là world anchor. Multi absolute XY disabled để tránh collapse mọi node vào một chỗ.
Handle move theo node; preview source hình học thật, không snap vào bbox như analytic center.
Node selection là interaction state, undo topology remap selection qua ID mapping.

P10: split line/quadratic/cubic tại t∈(0,1) bằng exact construction/de Casteljau;
giữ endpoint IDs và original segment ID cho nửa đầu, cấp ID node/nửa sau, không tái dùng ID đã xóa.
Delete nội bộ nối thẳng phải nhãn rõ và preview thay đổi; “giữ hình” bằng fitting chỉ có ở task
nâng cao riêng có error bound, không mặc định thực hiện. Curve→line là thay đổi có chủ ý;
line→cubic collinear giữ hình. Reverse đảo thứ tự/đổi incoming-outgoing, bảo toàn rendered geometry;
winding direction thay đổi theo thứ tự traversal và cần kiểm compound paths/fill rule.
Open/Close chọn cut/start rõ ràng, không xóa segment ngoài ý muốn. Join khác object/style chỉ thực hiện
với policy style/mapping explicit; mốc B có thể giới hạn cùng object/style và báo lý do.

## 6. Snap nâng cao và tính năng khác biệt

Mốc A dùng đúng Endpoint/Source node/Line midpoint/Grid và hướng đã có D07; giữ radius DIP,
hysteresis và stamp theo contract. Không hiện tangent/intersection nếu chưa tính nghiệm.
P14 triển khai từng capability: nearest/t-midpoint, analytic intersections, tangent/perpendicular,
parallel/extension/tracking; half-length cần tích phân/inversion bounded riêng, không nhầm t=0,5.
Ưu tiên local candidate set/index, quota và cancellation; không all-pairs mỗi pointer event.

Tangent/perpendicular là quan hệ hình học của segment mới với source đang tham chiếu, không tự
điều chỉnh toàn chuỗi Bézier. Nhiều nghiệm có branch preview/cycle action có tên; vô nghiệm báo rõ.
Reference deleted/moved hoặc revision/config/camera đổi: loại proposal muộn, không commit quan hệ stale.
Persistent constraints, true arc-length driving, auto-smooth/simplify/Pencil/Curvature, CAM và AutoCAD
command compatibility nằm ngoài mốc A/B; cần task/oracle riêng, không là điều kiện để Pen cơ bản xuất hiện.
