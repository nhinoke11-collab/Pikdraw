# Phiếu UI — Gradient fill

Ngày gốc: 17/10/2026; kiểm tra runtime: 23/10/2026.
Trạng thái: **Gradient UI/runtime đã được triển khai một phần**. Hành vi live commit hiện tại và giới hạn
đã xác minh nằm ở mục 9; mục tiêu tổng thể vẫn theo [plan](../../roadmap/gradient-20261017.md) và
[AC](../../quality/gradient-acceptance.md).
Áp dụng [chuẩn UI](../ui-ux-standard.md) và [workspace](../../architecture/workspace-layout.md).

## 1. Quyết định và phạm vi

Path fill Linear/Radial ellipse; selection đơn hoặc batch tương thích. Canvas là chỗ sửa hướng/hình,
bar giữ điều khiển thường dùng, Properties giữ mọi thông số được hỗ trợ. Reuse dock Properties > Style
hiện có, thêm nhóm Fill > Gradient; không tạo panel Gradient độc lập hay toolbar nổi.
Gradient nằm trong Object/Fill; không vào Effects vì không là filter. Không có document thì không bật.
Chọn tool/mở panel không tự áp Gradient hoặc tạo Undo. Text/stroke/group/bitmap ngoài V1.

## 2. Entry points và stable action IDs đề xuất

| Bề mặt | Placement / hành vi |
| --- | --- |
| Menu chính mở/focus | `Object > Fill > Gradient…` → `object.fill.gradient.properties`; focus Properties > Style > Fill > Gradient, không sửa paint. |
| Menu chọn tool | `Tools > Gradient` → `tool.gradient.activate`; chỉ đổi tool, không tự tạo paint. Đây là action khác mở panel. |
| Toolbox | Thêm Gradient vào flyout Appearance hiện có, cùng group semantics Alt+Down, Up/Down, Enter, Esc. Nút chọn tool có active state, không giả one-shot apply. |
| Standard toolbar | Không thêm: toolbox + menu đủ entry points; giữ bar chung gọn. |
| Bar | Khi Gradient tool hoạt động hoặc đang sửa Gradient: Type, ramp/selected stop Color, Position, Reverse, nút có tên `Gradient properties`. Không thêm một hàng bar. |
| Properties | Style > Fill: None / Solid / Gradient, rồi nhóm Type, Color stops, Geometry, Advanced và trạng thái live edit/Undo. Effect stack hiện có vẫn tách biệt. |
| Mở lại khi dock đóng/hẹp | Menu `Object > Fill > Gradient…` luôn truy cập khi ngữ cảnh hợp lệ; mở dock/focus đúng section, không bật lại dock khi người dùng chỉ đổi tool. |
| Context menu | `Fill > Gradient…` alias cùng action mở panel; không handler riêng. |
| Shortcut | V1 chưa gán global shortcut. Không quảng bá G/F1 từ Adobe/Corel; nếu task sau gán phải audit conflict, register và test. |
| Help | `Help > Gradient` → `help.gradient`, mở hướng dẫn trong bề mặt help hiện có; nếu chưa có help host, G06 thêm nội dung đọc được bằng keyboard qua cùng action, không handler rỗng. |

Action semantics đề xuất: `gradient.type`, `gradient.stop.add`, `gradient.stop.delete`,
`gradient.stop.color`, `gradient.stop.position`, `gradient.stop.opacity`, `gradient.segment.midpoint`,
`gradient.geometry`, `gradient.reverse`, `gradient.fit`, `gradient.spread`. Mỗi thao tác hoàn tất ghi một
command; Undo/Redo dùng history chuẩn của document.
Action registry string ID ánh xạ một lần sang enum menu/tool event native nếu shell dùng enum;
không có hai command pathways. Message IDs là `gradient.*.label/tooltip/hint/error`, EN mặc định + VI.

## 3. Wireframe trách nhiệm

```text
Property bar: [Linear v] [ramp + stop] [Color] [Pos 35 %] [Reverse] [Gradient properties]

Properties / Style / Fill / Gradient
  Type       [Linear] [Radial]
  Stops      <ramp trên nền checkerboard>
             [danh sách stop có số và swatch] [+] [Delete]
             Color [nguồn RGB/CMYK…]  Opacity [100 %]  Position [35 %]
             Midpoint to next [50 %]
  Geometry   Linear: Start X/Y, Angle, Length
             Radial: Center X/Y, Radius X, Aspect, Angle
             [Fit to object]
  Advanced   Spread [Pad v]   Method [sRGB] (read-only V1)
             Imported skew: giữ nguyên; số affine nâng cao chỉ khi thực sự hỗ trợ
  Sửa trực tiếp · Ctrl+Z hoàn tác
```

Đây là wireframe text, không phải ảnh native hoặc UI đã pass. Ramp, selected-stop markers, midpoint
diamond và canvas handles có outlines/focus rõ trên nền sáng/tối; checkerboard phân biệt alpha,
không dùng màu làm tín hiệu duy nhất. Icon chứ không dùng ký tự ASCII của wireframe khi triển khai.

## 4. Fields và binding

Mỗi field dùng Gradient draft để kiểm tra trước khi commit; thao tác ramp/canvas commit khi thả chuột,
các nút commit sau click, numeric field commit khi xác nhận hoặc rời focus hợp lệ. Bar, Properties và
Vector Tool cùng trỏ tới một `shared_ptr<GradientPaint>` do controller nối khi mở phiên. Dirty flags,
target list và session token vẫn thuộc controller/presenter riêng; chưa gom metadata này thành một
`ActiveGradientSession` aggregate. Giá trị ban đầu dưới đây dùng khi tạo Gradient
mới cho path đang chọn; mở lại paint hiện có luôn giữ giá trị đã lưu.

| Field ID / EN — VI | Vị trí | Kiểu/unit/range/default | Scope/enable/Mixed/commit |
| --- | --- | --- | --- |
| `fill.kind` Fill — Tô màu | Properties | None/Solid/Gradient; giữ selection hiện có | Mỗi đổi loại hoàn tất thành một command; None/Solid cũng đi qua writer/Undo chuẩn. |
| `gradient.type` Type — Kiểu | Bar + Properties | Linear/Radial; Linear mới | Draft; mixed hiện `Mixed`, đổi type giữ ramp và reset geometry theo bounds mỗi object; không copy geometry object đầu. |
| `gradient.stops` Color stops — Điểm màu | Ramp hai nơi, list Properties | 2–256; default 0% màu solid đã nhớ, 100% RGB trắng opaque; None dùng màu fill nhớ | Stable stop IDs; active stop là view state. Batch khác cấu trúc disable stop-level; cho action áp gradient chung tường minh. |
| `gradient.stop.color` Color — Màu | Bar + Properties | ColorValue hợp lệ; model/alpha nguồn giữ nguyên | Active stop; CMYK/spot preview có giới hạn theo plan, không giả proof. Color picker cập nhật draft, Escape hủy picker rồi về draft. |
| `gradient.stop.opacity` Stop opacity — Độ đục điểm | Properties | 0–100%, default 100 | Riêng source alpha/fill/object opacity; không thay color alpha khi nhập ô này. |
| `gradient.stop.position` Position — Vị trí | Bar + Properties | 0–100%, offset double; default theo stop | Ties cho hard edge; nhập ngoài range báo lỗi, không silent clamp. No active stop → disable. |
| `gradient.segment.midpoint` Midpoint — Điểm giữa | Properties + diamond | 1–99%, default 50 | Interval sau active stop; cuối/zero-length interval disable, không sửa terminal invariant. |
| `gradient.start.x/y` Start X/Y — Điểm đầu X/Y | Properties | Object-local mm, finite trong giới hạn geometry host; giữa cạnh trái bounds | Chỉ Linear; biểu diễn matrix, không giữ source song song. |
| `gradient.angle` Angle — Góc | Properties | Độ; display [0,360), nhận số finite normalize modulo 360; 0° +X, CCW dương | Object-local, không góc screen Y-down; giữ skew nếu chỉ rotate matrix. |
| `gradient.length` Length — Chiều dài | Properties | mm >0, giới hạn max theo geometry host; W local | Linear; giữ hướng và chiều ngang vuông góc hiện có; lỗi singular không apply. |
| `gradient.center.x/y` Center X/Y — Tâm X/Y | Properties | mm finite; tâm bounds | Radial; source local. |
| `gradient.radius.x` Radius X — Bán kính X | Properties | mm >0, default W/2 | Radial; ellipse tạo qua matrix. |
| `gradient.aspect` Aspect — Tỉ lệ | Properties | % = ry/rx×100, >0; mặc định H/W×100; max theo inverse/geometry bounds | Radial; không tự gán 100 khi mixed. |
| `gradient.spread` Spread — Lặp | Properties | Pad/Repeat/Reflect; Pad mới | Draft; EN labels `Pad`, `Repeat`, `Reflect`; VI `Kéo màu biên`, `Lặp`, `Lặp đối xứng`. |
| `gradient.method` Method — Nội suy | Advanced Properties | `sRGB` read-only V1 | Tooltip giải thích preview; không dựng dropdown có Classic/Perceptual chưa hỗ trợ. |
| `gradient.reverse` Reverse — Đảo màu | Bar + Properties | One-shot action | Giữ geometry, đảo stops/midpoints; một command Undo. |
| `gradient.fit` Fit — Khớp đối tượng | Properties | One-shot action | Reset matrix về bounds từng object theo type; giữ ramp và source colors. |

Precision: display mm theo formatter hiện có, position/midpoint/opacity hiển thị tối thiểu 0,1%;
model vẫn binary64. Nhập `mm`, `%`, `deg` theo grammar hỗ trợ, decimal dot/comma theo numeric policy
host; unsupported unit là lỗi tại field. G00 phải chốt finite max geometry + inverse threshold trước
implementation; UI dùng chung validator, không tự đặt range nhỏ hơn chỉ vì control hẹp.

Imported affine skew được giữ. V1 không có editor skew sáu số đầy đủ: lúc matrix có skew, các field
geometry chỉ enable các phép đổi giữ được phần còn lại (translate/rotate/uniform scale); Aspect/Length
không thể phân rã ổn định thì disable với lý do và cho `Fit` tường minh. Không decomposed-edit làm mất skew.

## 5. Context và mixed selection

| Context | Tool/bar/Properties | Hint và policy |
| --- | --- | --- |
| Không document | Disable sửa/tool; help đọc được | `Open a document to edit a gradient.` / `Mở tài liệu để sửa Gradient.` |
| Document, không selection | Mở section read-only, bar gợi ý chọn path | `Select a path.`; không thay new-object defaults. |
| Một path có fill hoặc None | Tool khả dụng; chưa drag/đổi field thì giữ source | Solid→Gradient chỉ tạo draft lúc người dùng bắt đầu drag/chọn kind. |
| Nhiều path tương thích | Type/Spread/Reverse/Fit có batch semantics; tất cả atomic | Ramp giống cấu trúc/offset/type/midpoint, bỏ qua IDs riêng từng object, thì stop-level sửa theo ordinal mapping đã pin; màu khác hiện Mixed. |
| Nhiều ramp khác topology | Stop editor disabled, vẫn xem mixed | Nút có tên `Apply same gradient` / `Áp cùng Gradient`: mở draft mới với default ramp trên mỗi bounds, không âm thầm lấy object đầu. |
| Có locked/read-only/Text/group/bitmap | Disable toàn batch write | Lý do cụ thể; không tự bỏ đối tượng không hợp lệ. |
| Đang nhập text/số/IME | Field giữ caret, select/copy/paste; không intercept canvas shortcuts | Esc hủy field edit trước; Esc tiếp hủy draft. |
| Draft stale/source đổi | Cancel draft + invalidate preview | Hint ngắn, giữ committed paint và Undo history. |
| Busy/quota/render error | Giữ draft hợp lệ nếu source chưa đổi; Apply có thể disable khi chưa preview được an toàn | Thông báo có lý do; không vẽ fallback rồi báo hoàn tất. |

Batch không có shared Gradient spanning nhiều object trong V1: mỗi object có paint/IDs/matrix riêng.
Stop color/opacity/position patch đồng nhất trên các stop tương ứng; field không sửa giữ nguyên từng object.
Canvas handles chỉ khi một path; batch dùng Properties, không chọn ngầm active object.

## 6. Gestures, keyboard và draft

1. Chọn path → chọn Gradient tool → drag trục Linear hoặc tâm→bán kính Radial; giữ pointer capture.
   Shift khóa hướng mỗi 15° trong source local cho Gradient; đây là quyết định mới cần G04/G06 kiểm,
   không kế thừa profile Line/Polyline hoặc shortcut Adobe một cách ngầm định.
2. Drag có preview cùng sampler; pointer-up commit một Undo. Drag ngắn hơn 2 DIP bị coi click,
   không tạo Gradient vô tình. Với Radial, trục phụ dùng radius/aspect handle; không đổi geometry của path.
3. Click pin chọn; click ramp hoặc trục canvas để thêm stop; Delete/Backspace chỉ xóa stop đang chọn
   khi còn trên 2 stops. Nếu canvas/object focus thì route delete theo host, không xóa object khi field/ramp có focus.
   Có nút Add/Delete; thao tác keyboard/list đầy đủ vẫn cần xác minh trên app.
4. Tab/Shift+Tab qua các control hiện có. Arrow/list navigation và Preview/Cancel/Apply là mục tiêu
   thiết kế, chưa xác minh trong runtime live-commit hiện tại;
   sửa vị trí qua field, không dùng cùng Arrow vừa chọn vừa move. Midpoint qua field là keyboard equivalent.
5. Thay đổi hợp lệ được commit theo từng thao tác; ramp/canvas drag tạo một command khi thả chuột,
   numeric field tạo một command khi xác nhận. Ctrl+Z/ Ctrl+Y hoàn tác/làm lại; Escape hủy gesture
   chưa hoàn tất hoặc input đang lỗi, không đảo ngược thao tác đã commit.

Không tự bật Properties sau khi người dùng đóng. Focus loss giữ field buffer; input-invalid không clear
draft hoặc tự Apply. Pointer capture lost hủy drag, cancel generation; không commit điểm cuối không biết.
Save trong lúc preview draft lưu committed snapshot, có hint thay đổi chưa được commit. Undo/Redo
sau commit khôi phục paint và IDs đúng.

## 7. Icons, labels, help và accessibility

Asset đề xuất trong `apps/desktop/resources/icons/`: `tool-gradient.svg`, `gradient-linear.svg`,
`gradient-radial.svg`, `gradient-reverse.svg`. Dùng cùng viewBox/stroke/tokens hệ icon hiện có, tự vẽ
hình học đơn giản, ghi provenance original PikDraw theo license repo; không trích asset Adobe/Corel.
Render asset qua pipeline icon native hiện có; file SVG nằm trên disk không đủ chứng minh icon hiện trên app.
Color/stop symbols có outline/shape riêng; None/Mixed/Disabled/Busy/Error phân biệt bằng chữ/hình.

| Nội dung | EN | VI |
| --- | --- | --- |
| Tool label/name | Gradient | Chuyển sắc |
| Tool tooltip + focus help | Edit a path fill using gradient handles. | Sửa màu chuyển sắc bằng tay nắm trên đối tượng. |
| Panel opener | Gradient properties | Thuộc tính Gradient |
| Reverse tooltip | Reverse color stops without moving the gradient. | Đảo điểm màu, giữ vị trí chuyển sắc. |
| Idle hint | Select a path, then drag to create a gradient. | Chọn đường rồi kéo để tạo chuyển sắc. |
| Drag hint | Drag to set the gradient. Shift: 15° steps. Esc: cancel. | Kéo để đặt chuyển sắc. Shift: bước 15°. Esc: hủy. |
| Stop hint | Select a stop to edit color and position. | Chọn điểm để sửa màu và vị trí. |
| Live edit hint | Gradient edit saved. Ctrl+Z to undo. | Đã lưu chỉnh sửa Gradient. Ctrl+Z để hoàn tác. |
| Minimum stops | Keep at least two color stops. | Cần giữ ít nhất hai điểm màu. |
| Range error | Enter a position from 0% to 100%. | Nhập vị trí từ 0% đến 100%. |
| Stale | Unapplied gradient changes were cancelled because the selection or document changed. | Đã hủy Gradient chưa áp dụng vì lựa chọn hoặc tài liệu thay đổi. |

Chỉ đưa hint Shift lên runtime sau binding có test. Label ngắn, help dài ở Help, không button chứa câu.
Help `help.gradient`: (1) chọn Path và tool; (2) kéo Linear/Radial; (3) thêm/chọn stop đổi màu/opacity;
(4) nhập geometry/spread trong Properties; (5) Ctrl+Z/Redo hoặc mở sửa lại. Nêu rõ cách hủy gesture,
2-stop minimum, reset midpoint khi thay topology, source/local axes, batch limitations, preview CMYK/spot,
chưa có stroke/Freeform/print certification và giới hạn export SVG midpoint/method.

Geometry controls không lộ class/job ID/backend trong UI. Accessible names cho ramp, stop N + vị trí,
midpoint, type toggles và icon; focus outline không chỉ đổi màu. Win32 UIA/MSAA exposure phải kiểm trực tiếp
hoặc accessibility inspector; label/tooltip trong source không tự chứng minh screen reader hoạt động.

## 8. Narrow window và bằng chứng

Theo tokens workspace: Properties khoảng 280–340 DIP, resizable; icon 16/20 DIP, hit target ít nhất
24×24 DIP; canvas handles có hit radius đề xuất 6 DIP, painted marker 8 DIP, overlap chọn stop đang focus
rồi stop cuối theo z-order. Nút control vẫn giữ minimum hit target; DIP→pixel một lần tại native boundary.
Narrow bar giữ Type, ramp selection và opener có tên; Position/Reverse vào overflow hoặc Properties,
không giảm font/cắt units. Dock scroll phải giữ mọi control truy cập được bằng keyboard và scroll.

G06/G10 kiểm Win32 ở 1366×768, 1920×1080; 100/125/150/200% theo thiết bị có thể kiểm, light/dark/high
contrast, dock đóng/mở, mixed/locked/invalid/focus. Báo cụ thể tổ hợp chưa chạy. AC-G08–G12 là đường
nghiệm thu, không suy pass UX từ mockup, controller tests hoặc một ảnh. Phiếu đã tự phân tích; agent tự
quyết routine spacing/labels trong scope rồi triển khai, không chờ user duyệt form.

## 9. Trạng thái runtime đã kiểm tra

Native shell/controller tests xác nhận live commit/Undo, preview snapshot có dùng khi render canvas,
8 preset, chỉnh ramp stop/midpoint, kéo ra ngoài để xóa, thêm/xóa stop trên trục và click stop trên Path
đã transform mà không ghi Undo. Đây là bằng chứng harness, chưa phải xác nhận thao tác người dùng trong
app đã cài.

| Phần | Trạng thái hiện tại |
| --- | --- |
| Live edit và hit tolerance | Properties và Vector Tool cùng dùng shared paint pointer; runtime chọn handle bằng khoảng cách screen ≤8 px rồi đổi pointer world sang object-local qua transform. Session metadata chưa gom thành một `ActiveGradientSession` aggregate. |
| Ramp | Dùng chung `GradientSampler`; hỗ trợ chèn, kéo stop/midpoint, kéo xuống xóa; mỗi gesture commit một Undo. |
| Presets và numeric IDs | 8 preset theo catalog, ID riêng cho các trường Gradient, Reverse/Distribute/Fit và Undo có test. |
| Canvas overlay | Có màu stop, midpoint và add/delete theo trục; shell test bao phủ một Path affine transform. Ma trận zoom 10–3200%, DPI/theme và nhóm transform khác chưa chạy. |
| Chưa hoàn tất | Angle Dial/nút góc nhanh, eyedropper/double-click pin, accessibility inspector/UIA, launch/visible CUA ở nhiều kích thước, đo p50/p95 hoặc chứng minh 60fps. |

Không gọi preview snapshot là hiệu năng 0 ms/60 fps; chưa có phép đo present/input-to-visible. Canonical
`pikdraw.exe` đã build, nhưng build và test harness không thay cho launch/runtime visual inspection.
