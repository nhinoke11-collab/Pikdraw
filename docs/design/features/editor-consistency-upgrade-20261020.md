# Đặc tả nâng cấp UI/UX và tính đồng bộ editor — 20/10/2026

Trạng thái: **đặc tả để triển khai U01/U02/U07/U08/U09/U12, chưa là giao diện đã nghiệm thu**.

Liên kết: [kế hoạch tổng thể](../../roadmap/existing-feature-upgrade-20261020.md), [chuẩn UI/UX](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md), [template chuẩn](../feature-ui-template.md).

Đặc tả chuyên sâu: [quản lý màu C00–C09](../../roadmap/existing-feature-upgrade-20261020/color-management-plan.md), [hệ đơn vị S00–S05](../../roadmap/existing-feature-upgrade-20261020/units-plan.md). Hai context này là nền của property binding, không phải dropdown độc lập thêm sau UI.

Mục tiêu: sửa tính rời rạc trong điều khiển, cách nhóm thông số, trạng thái và phản hồi; giữ toàn bộ chức năng đang có và hướng desktop vector/CAD của PikDraw. UI phải giúp người dùng biết đang sửa gì, ở đâu, kết quả đã ghi hay chỉ là preview, và hủy/Undo được như thế nào.

## 1. Quyết định bố cục

- Giữ menu trên; standard toolbar cho file/Undo/view; một property bar thay theo context; toolbox trái có flyout; canvas giữa; Properties/Layers chung hệ dock phải; status dưới.
- Không đổi nền UI sang web/Qt để thực hiện đợt này. C++/Win32 vẫn là runtime; logic binding/layout không phụ thuộc GDI paint để có thể được consumer khác dùng sau.
- Giữ document tabs và page navigation thành hai khái niệm. Chuyển tab/page chỉ đổi context/view trừ thao tác chỉnh sửa rõ ràng.
- Properties có sections: Transform, Appearance, nội dung của tool/object, Advanced. Field ít dùng nằm trong section thu gọn; luôn có đường mở/focus từ menu hoặc nút có tên trên bar.
- Layers là tab/panel chuyên dụng cùng dock, không chiếm thêm một cột mặc định. Field hiện có trong Style dialog được giữ reachable và gọi cùng binding/command.
- Menu mở panel và lệnh sửa giá trị là hai action khác nhau. Không sửa source khi chỉ mở panel hoặc đổi tab section.

### Token và khả năng thích ứng

| Thành phần | Quyết định đề xuất | Kiểm chứng |
| --- | --- | --- |
| Font | Font UI hệ thống, mục tiêu 12–13 DIP; hỗ trợ dấu tiếng Việt, không tải font mạng | EN/VI, 100–200%, đo text thật trước layout |
| Icon | 16/20 DIP, cùng viewBox/nét; click target ưu tiên 28–32 DIP, tối thiểu 24 DIP | Focus/click rect chung, không chỉ kiểm kích thước hình vẽ |
| Toolbox | Cột 40 DIP ban đầu; group flyout giữ công cụ và tùy biến visibility đã có | Tool dài quá chiều cao dùng scroll/overflow keyboard; Pick luôn reachable |
| Property bar | Một vùng ổn định, nhóm target → geometry → appearance → tool; field quan trọng trước | Có thể dùng cặp field trong nhóm; không tự thêm hàng bar cho mỗi feature |
| Properties | Rộng khởi đầu 300 DIP, resize/clamp theo canvas; lưu kích thước người dùng | Khi hẹp có tab/section/scroll và đường mở lại; không ép canvas theo phần trăm cố định |
| Khoảng cách | Bước 4/8 DIP, field/control height theo token và text metrics | Paint/hit/focus lấy cùng layout result |
| Màu | Nền trung tính, một accent cho active/focus; semantic None/Mixed/error/busy ngoài tín hiệu màu | Light/dark/high contrast và disabled readability |
| Overflow | Ẩn nhóm phụ khỏi bar vào overflow có tên hoặc Properties; giữ field đang focus đến khi commit/cancel xong | Không cắt unit/Apply/Cancel, không chặn tool do dock đóng |

Đây là baseline để đo và tinh chỉnh trên app native. Không gắn nhãn UI tốt chỉ vì đạt các số DIP; cần workflow/focus/legibility thực.

## 2. Một model cho paint, thao tác và dữ liệu

~~~mermaid
flowchart LR
    D[Snapshot + selection + session] --> C[EditorContext projection]
    C --> A[Action state]
    C --> P[Typed properties]
    A --> L[Layout và semantic controls]
    P --> L
    L --> V[Paint / hit-test / focus / tooltip / UIA]
    V --> I[User intent]
    I --> E[Edit session / command validation]
    E --> W[Document writer]
    W --> D
~~~

Semantic control identity không phụ thuộc vị trí hoặc label dịch. Layout pass tạo bounds/visibility/role/tab order; paint, pointer, keyboard, tooltip/focus help và UIA dùng cùng cây. Không duy trì các RECT riêng rẽ cho cùng control trong file paint và click.

UI state là projection từ authority hiện có, không tạo document cache riêng cho bar hoặc Properties. Mỗi event phải gắn context/session đang sửa để không áp dụng sang tab/selection mới.

Scope hợp lệ: App, Document, Page, DrawingDefaults, Selection, ToolDraft, View. Một field chỉ ghi scope được hiện rõ. Không dùng DrawingDefaults làm giá trị giả của selection không phù hợp.

## 3. Bản đồ menu/action/tool và migration

`E`: ID/enum/route có trong source đã khảo sát; registration/hành vi vẫn cần U00 xác nhận. `P`: canonical action ID đề xuất để thống nhất đường gọi, chưa triển khai. Không đổi shortcut công bố trước khi registry/handler/tooltip cùng đạt.

| Nhóm / đường chính | Identity hiện có → canonical | Toolbar/toolbox | Property bar / Properties |
| --- | --- | --- | --- |
| File > New/Open/Save/Save As/Import/Export | E: `action.file.new/open/save/save_as/import/export` theo hằng ActionId tương ứng | Standard file icons; không đặt trong toolbox | File options ở dialog đúng workflow; Properties không áp dụng cho thao tác file một lần |
| File > Recover Autosave | E: `action.file.recover_autosave` | Không thêm nút thường trực; file workflow ít dùng | Recovery dialog hiện có, vẫn giữ preview/select/recover/cancel |
| Edit > Undo/Redo/Cut/Copy/Paste/Duplicate | E: `action.edit.*` đúng constant đang có | Undo/Redo và clipboard thường dùng; không thành tool mode | Không Properties riêng; đối tượng sau lệnh dùng Properties chung |
| View > Zoom / Snap | E: view actions và MenuActionId::ViewSnapGrid/ViewSnapObjects; E: `action.tool.snap_panel` | Zoom/view shortcuts, Snap toggle; không sửa geometry | View scope; Snap section/options giữ đầy đủ tùy chọn được hỗ trợ |
| Tools > Pick / Node | E: `action.tool.select`, `action.tool.node_edit`; MenuActionId::ToolNode | Toolbox Pick/Node, icon SVG semantic | Transform hoặc Node, handle/segment/corners theo selection |
| Tools > Rectangle/Ellipse/Polygon | E: rectangle/ellipse ActionId; P: `action.tool.polygon` nếu chưa có canonical registration | Quick Shapes flyout, nhớ last-used tool | Size/constraint/sides; đầy đủ rotation/defaults trong Properties |
| Tools > Line/Polyline/Pen | E: `action.tool.line/polyline/pen` | Path Drawing flyout | Existing precision input/stroke; Pen/Path section giải nghĩa scope draft |
| Tools > Knife | E: `action.tool.knife` và controller đang có | Toolbox Knife; giữ shortcut hiện có | Chỉ options có backend; unsupported source có reason |
| Object > Transform / Align and Distribute / Shaping | E: các `action.object.align_*`, Boolean và transform IDs; giữ MenuActionId aliases | Context shortcuts nếu đang có; không thêm cột công cụ | Transform/Arrange; target anchor, reference và keep source/target |
| Object > Group/Order/PowerClip | E: object group/order/powerclip IDs | Context menu và shortcut cùng command | Hierarchy/Clip section; Edit/Finish rõ isolation context |
| Object > Properties | E: `action.object.properties.focus`, MenuActionId::ObjectProperties | Nút mở Properties có tên, không phải toggle tài liệu | Focus section đúng context, không revision/Undo |
| Window > Properties / Layers | E: MenuActionId::WindowProperties và panel Layers hiện có; P: `action.window.properties.toggle`, `action.window.layers.toggle` | Không đặt vào toolbox | Bật/tắt panel; focus route khác toggle route |
| Object > Gradient Fill / Tools > Interactive Gradient | E: MenuActionId::ObjectFillGradient/ToolGradient; P: `action.tool.gradient` nếu chưa canonical | Toolbox Gradient hiện có | Stops/geometry; detailed fill section theo target Path |
| Effects > Contour / Break Apart | E: `action.tool.contour`, `action.effects.contour.focus`, `action.object.contour.break_apart` | Toolbox Contour hiện có | Spacing/direction/steps; join/miter/tolerance được hỗ trợ trong Properties |
| Effects > Gaussian Blur / Drop Shadow | E: MenuActionId::EffectsGaussianBlur/EffectsDropShadow; P: `action.effects.blur.focus`, `action.effects.shadow.focus` | Giữ appearance shortcuts/toolbox entries hiện có; focus/edit effect đã có | Blur sigma và Shadow feather/offset/opacity là field riêng; color/blend/stack đầy đủ trong Properties, không chỉ dialog. Màu swatch nguồn dùng cùng page `IccPreview` như color dialog/canvas, fallback `to_preview_rgba` khi không có ICC transform; source channels không đổi |
| Tools > Dimension… | E: toolbox tool 10 và dimension controller; P: `action.tool.dimension` để chuẩn hóa | Toolbox Dimension hiện có | Kiểu đo/các điểm/label; styles/units/precision/associativity đã có capability |
| Text > Character/Paragraph / Text Properties | E: text menu enums và `action.tool.text`; P: canonical `action.text.properties.focus` | Toolbox Text; giữ binding thực tế, không đồng nhất F8/T bằng phỏng đoán | Font/size/style/alignment; Character/Paragraph/Frame |
| Layout > Page Setup / Page navigation | E: LayoutPageSetup/Previous/Next/Add/Duplicate/Delete và orientation enums | Page navigation riêng document tabs | Page/Document scope; page size/units/mode, không giả selection geometry |
| Color conversion / Select Same / View Proof | E: Edit color selection enums, `document_color_ui` IDs, `action.color.convert_profile`, `action.view.proof_colors/gamut_warning/overprint_preview` | Palette/proof controls theo capability | Color document/page/selection scope; ICC page conversion yêu cầu profile nguồn đã bind |
| Edit > Select Same > Fill or Stroke Color | E: `MenuActionId::EditSelectSameAnyPaintColor`; cùng handler từ menu chính và context menu | Không có toolbox/toolbar/property bar/Properties hoặc icon; đây là lệnh một lần đổi selection | So sánh source value ±0,005 gồm profile, alpha, Spot alternate/overprint, fill, stroke và gradient stops; tìm trên active page trong Path/Text paints. Status hỏi chọn đối tượng nguồn nếu selection không có paint |
| Settings > Color & ICC Profiles > Assign ICC to active page… | E: `action.color.assign_profile` qua popup hiện có; command page scope, Undo/Redo | Không có toolbox/icon; popup có nhãn profile trang hiện hành | Page profile chọn đúng RGB/CMYK; giữ channels; canvas thử preview theo ICC, báo fallback nếu Windows CMM từ chối; không ngầm Convert |
| Edit > Convert Active Page ICC Profile… | E: `action.color.convert_profile`, `MenuActionId::ColorConvertActivePageProfile`; alias trong profile manager | Không có toolbox/toolbar/icon; một lệnh page-level | Destination ICC + rendering intent trong workflow; một command/Undo đổi process colors và page binding |
| Edit > Convert Color Mode… | E: giữ `document_color_ui` actions và Convert mode hiện có | Menu màu hiện có | Document/Page/Selection scope; chuyển channel theo report/Undo; profile-aware Assign riêng với Convert |
| Layout > Page Display Units / Document Properties > Units | E: `action.page.units.millimeter`, `.centimeter`, `.meter`, `.inch`, `.foot`, `.yard`, `.point`, `.pica`; legacy `MenuActionId::LayoutUnit*` dispatch tới cùng semantic ID. P: document unit focus | Unit suffix ở numeric bar; không thêm toolbar/toolbox riêng | Active Page scope; đổi display unit bằng command/Undo, giữ kích thước thật; document-level unit control và design pixel DPI còn mở |
| View > Rulers > Units / Settings > Preferences > Units | P: `action.view.ruler_units.focus`, `action.settings.units.focus` | Ruler context menu; Preferences hiện có | View override khác app default tài liệu mới; không sửa geometry/source |
| Settings > Preferences/Shortcuts/AI Connections | E: Settings enums và `action.settings.ai_connections` | Không đặt trong toolbox | Preferences modal/dock hiện có; app scope, key không ghi vào log/document |
| Help > Tool Help / AI Automation | E: `action.help.ai_automation`; P: `action.help.active_tool` | Help menu/F1 chỉ khi binding được đăng ký | Usage help theo tool; focus help không làm source edit |

Chuỗi rút gọn `action.file.new/open` trong bảng biểu thị các ID riêng, không phải ID mới chứa dấu slash. Constants chi tiết tại `apps/desktop/include/pikdraw/desktop/actions/action_id.hpp`; enum tại `native_window_menus.hpp`.

`Select Same Fill or Stroke Color` dùng nhãn tiếng Việt `Chọn cùng màu tô hoặc viền` ở Edit và context menu. Đây là lệnh chọn một lần trên paints của active page, không sửa document và không tạo Undo; menu dùng keyboard navigation native, không có shortcut/icon. Status báo kết quả hoặc yêu cầu chọn một đối tượng có paint trước. Properties/bar/toolbox không áp dụng vì lệnh không có giá trị liên tục để chỉnh. Matcher so sánh source values với tolerance legacy ±0,005, gồm profile binding, alpha, Spot alternate/overprint và gradient stops; proof/display context không đổi source matcher. Native focus/accessibility walkthrough vẫn cần nghiệm thu.

Đối với route thay đổi label/group: giữ legacy shortcut/action alias tới command hoặc panel mới. U00 phải lập mapping tất cả entry hiện có; không bỏ riêng field hoặc action mà bảng nhóm này chưa liệt kê từng cái.

### Gán ICC cho trang hiện tại — contract runtime đang có

- Đường đi: Settings > Preferences > Color & ICC Profiles > Assign ICC to active page…; dùng popup ICC hiện có và bộ chọn tệp Windows, không tạo panel/web UI mới.
- Action: `action.color.assign_profile`. Phạm vi là PageId hiện hành, không phụ thuộc selection; disable với document read-only/PDF print-locked.
- Chọn RGB hay CMYK theo color mode của trang. Tệp phải qua kiểm tra cấu trúc ICC, model và quota; command lưu bytes/hash bất biến, không đổi channel, alpha hoặc paints. Undo/Redo đổi binding page như một transaction.
- Canvas dùng profile của trang active khi Windows CMM mở được; nếu không, giữ channel và dùng preview fallback, đồng thời báo trạng thái. Đây không phải paint-level conversion, soft proof hay gamut oracle.
- Nhãn: “Assign ICC to active page…” / “Gán ICC cho trang hiện tại…”. Popup hiển thị label đã resolve hay unresolved. Không có icon SVG/toolbar/toolbox vì đây là lệnh metadata một lần. Help mô tả ở Color management scope…; sau gán, status báo thành công hoặc fallback, lỗi sai model/quota có thông báo.
- Property bar không áp dụng vì đây không phải thao tác theo selection. Page Properties chưa có field profile chi tiết; bổ sung row/page inspector vẫn là hạng mục C07/U08. Native focus/visual walkthrough chưa chạy, nên không tuyên bố UI runtime nghiệm thu.

### Chuyển màu trang sang profile ICC đích — contract runtime đang có

- Đường chính: Edit > Convert Active Page ICC Profile…; alias: Settings > Preferences > Color & ICC > Profile manager > Convert page colors to destination ICC… . Action ID `action.color.convert_profile`; scope là active PageId. Không có document/selection batch trong phiên bản này.
- Enablement yêu cầu tài liệu editable và active page có ICC RGB/CMYK đã resolve làm source. Lệnh không chạy transform trong `can_execute`; source binding và revision được kiểm lại trong command trước commit.
- Workflow mở bộ chọn destination `.icc/.icm`, xác thực RGB/CMYK profile theo bytes/quota, rồi mở keyboard-traversable rendering-intent menu: Perceptual, Relative colorimetric (mặc định), Saturation, Absolute colorimetric. Black point compensation hiện disabled kèm lý do Windows CMM API không hỗ trợ cờ này.
- Trước commit, confirmation nêu source, destination, intent, thay đổi channel và one-step Undo. Một transaction chuyển fill/stroke/gradient-stop/shadow/text process paints và page binding. Alpha/overprint giữ nguyên; Spot, Gray và raster pixels giữ nguyên. Mismatched/stale source, unsupported profile hoặc transform failure từ chối nguyên tử.
- Không có toolbox/toolbar/property-bar/Properties field vì đây là lệnh một lần có Page scope, không phải tool draft hay thuộc tính liên tục; destination và intent chỉ sống trong request xác nhận, không ghi preference/document. Không có icon SVG riêng. Menu native dùng nhãn rõ; focus help ở Color management scope…; status nêu số process paints, intent và Ctrl+Z; lỗi nêu lý do, tài liệu không đổi.
- Không có pixel/image ICC transform, selection batch, BPC, gamut oracle, paper/ink proof hay round-trip production guarantee. Native keyboard/focus/visual walkthrough và colorimetric oracle chưa chạy; các test chỉ xác nhận action/capability và command behavior.

### Chuyển toàn tài liệu sang profile ICC đích

- Đường đi: **Edit > Convert Document ICC Profile…**, cạnh lệnh chuyển ICC trang. Action ID action.color.convert_document_profile.
- Enablement yêu cầu tài liệu editable và mọi trang có ICC RGB/CMYK đã resolve. Tài liệu legacy có trang chưa resolve phải gán ICC từng trang trước; không tự ghép profile theo tên.
- Dialog dùng bộ chọn ICC RGB/CMYK, chọn một rendering intent dùng chung và confirmation nêu số trang, profile đích, đổi channel cùng one-step Undo. Backend tạo một transform cho mỗi profile nguồn khác nhau và preflight toàn bộ trang trước khi ghi.
- Mỗi trang dùng profile đang gắn làm nguồn; trang đã ở đích được bỏ qua. Thiếu transform, model không khớp hoặc paint process sai model làm hủy toàn lệnh, không đổi một phần. Một command duy nhất đổi process channels và page bindings, giữ active page; Undo/Redo khôi phục toàn bộ.
- Nhãn EN/VI là Convert Document ICC Profile… / Chuyển profile ICC toàn tài liệu…. Không có icon riêng; toolbar, toolbox, property bar và Properties không áp dụng. Focus/help nằm trong lời xác nhận; status báo profile, số màu process, rendering intent và Ctrl+Z. Không có shortcut riêng.
- Spot, grayscale và raster pixels giữ nguyên. Chưa có paint-level profile binding, selection conversion, BPC, gamut oracle, paper/ink proof, monitor profile hay production guarantee. Native visual walkthrough và independent colorimetric oracle vẫn mở.

## 4. Binding các trường và trạng thái

Key dưới đây là **semantic key đề xuất**. Range thực lấy từ model/validator hiện hành, không nhân bản hằng số UI; giá trị mới chỉ được mở khi backend/domain và tests chấp nhận.

| Field / label EN — VI | Nơi hiển thị | Type, unit và range/default | Scope/source | Mixed/enable và commit |
| --- | --- | --- | --- | --- |
| transform.x/y — X/Y | Bar + Transform | finite mm; limit model; không ghi số đã làm tròn hiển thị | Selection geometry bbox/anchor, không là camera | Multi-selection là bbox chung; Enter/pointer-up một transform command |
| transform.width/height — Width/Height / Rộng/Cao | Bar + Transform | mm, degeneracy theo loại object; scale ratio có validator | Selection/source transform | Locked/read-only disable; group shear/rotation không được đoán từ rounded bbox |
| transform.rotation/anchor/aspect — Angle/Anchor/Lock | Bar + Transform | degrees; anchor enum; bool; preserve source transform basis | Selection hoặc DrawingDefaults có nhãn | Mixed tách state; chọn anchor không làm xoay; lock theo policy đã có |
| shape.polygon.sides/rotation — Sides/Rotation / Cạnh/Góc | Sides bar; đầy đủ Shape | int 3–256 theo capability hiện tại; degrees | ToolDraft/DrawingDefaults hoặc parametric source nếu còn editable | Không tái dùng key contour.steps; ngon/closed path đã convert không giả còn parametric |
| path.numeric — Precision input / Nhập chính xác | Bar hoặc ô nhập ToolSession hiện có; help chi tiết | Typed XY/length/angle theo parser tool, mm/degrees; giữ cú pháp hiện có | Pen/Line/Polyline draft | Input chưa hợp lệ không commit; không ép scalar parser vào cú pháp tọa độ |
| quick_dimension.width/height — Quick Dimension / Kích thước nhanh | Status prompt sau khi vẽ shape; nhập ngay trên canvas | Bare length theo đơn vị trang; hậu tố rõ như `1in`, `25.4mm` ghi đè; một số hoặc cặp `W;H`/`WxH` | Shape vừa tạo, giữ target ID tới Enter/Esc | Mixed fraction như `1 1/2in` phải được parse thành một chiều dài; Enter một command, Esc không sửa source |
| node.position/handles/type — Node/Handles / Điểm/Tay nắm | Bar + Node | mm, smooth/symmetric/cusp và handle basis theo contract | Stable node/segment IDs trong draft/selection | Nhiều node Mixed đúng; source target change hủy/giữ draft rõ |
| node.corner.radius/type — Radius/Type / Bán kính/Kiểu góc | Bar + Live Corners | mm; round/scallop/chamfer; range geometry validator | Stable selected nodes | Giữ compact controls đã có; thao tác canvas và field chung solver, một Undo |
| contour.spacing/steps/direction/join/miter/tolerance — Contour / Viền | Spacing/direction/steps bar; miter/tolerance trong Contour Properties | Canonical mm, unit suffix theo field; `NumericFieldId::ContourSteps`, int 1–99; join enum; miter finite >=1; tolerance default/min 0,02 mm | ToolDraft liên kết source Path | Input mới dưới floor reject; legacy 0,005 mở không rewrite, stored/effective rõ; Apply đúng target/revision; steps không dùng PolygonSides |
| dimension.type/units/precision/label/style — Dimension / Kích thước | Type/label bar; đủ Dimension | Enums/decimal precision theo loại đã hỗ trợ; source mm độc lập display unit | Annotation + target geometry refs | Unsupported type không chọn được; edit source recompute đúng; đổi display không sửa geometry |
| appearance.fill/stroke/color/width — Fill/Stroke / Tô/Nét | Swatch/width bar; đầy đủ Appearance | Source ColorValue RGB/CMYK/spot/gray, mm; None khác transparent | Selection/DrawingDefaults có nhãn | Mixed và None có biểu tượng riêng; cap/join/dash chỉ khi backend hỗ trợ |
| appearance.object/fill/stroke.opacity — Opacity / Độ đục | Object quick + Appearance chi tiết | Hiển thị 0–100%, source normalized 0–1; alpha màu riêng | Property target chính xác | Không nhân opacity hai lần; chọn nhiều object không thành group compositing |
| gradient.stop.offset/color/opacity/midpoint — Stop / Điểm màu | Quick stop + Gradient Properties | Offset/opacity 0–100%; midpoint 5–95%; Numeric IDs tách `GradientStopOffset/Opacity/Midpoint`; stop ID ổn định | Gradient paint draft của targets | Stable stop ID + stop generation; sửa color source giữ profile; Apply/Cancel một session |
| gradient.geometry — Angle/Length/Center / Góc/Dài/Tâm | Bar đủ chỗ, Gradient Properties đầy đủ | Degrees/mm; transform/object basis hiện hành | Paint geometry draft | Không đụng transform.x/y của object khi đang sửa tâm Gradient |
| effect.blur.sigma_x/y — Blur / Độ mờ | Quick sigma; Blur Properties | mm sigma >=0, upper/work budget theo engine | Stable effect instance, local coordinates | Default creation và edit-existing tách rõ; reject budget vẫn giữ source |
| effect.shadow.offset/feather/color/opacity/blend — Shadow / Đổ bóng | Quick offset/feather/opacity; full Shadow Properties | Length theo đơn vị trang, source color + normalized opacity, supported blend enum; `ShadowOffsetX/Y`, `ShadowFeatherX/Y`, `ShadowOpacity` IDs distinct from `BlurSigmaX/Y` | Stable effect instance/session | Đủ color/blend/stack options; giữ rule backdrop/order theo C9 |
| text.font.family/size/style — Font/Size/Style / Phông/Cỡ/Kiểu | Bar + Character | Font identity; pt 1–2880 hiện có; flags theo source | Text selection hoặc inline draft | Mixed font/runs giữ Mixed; missing font có diagnostic và choice; IME không bị canvas shortcut ăn |
| text.paragraph/frame — Paragraph/Frame / Đoạn/Khung | Quick alignment; Paragraph/Frame; `TextFrameWidth/TextLineSpacing` IDs | Width mm 0–limit; line spacing 25–1000%; frame length và line-spacing percent có kiểu input riêng | Text layout/draft | Caret/cluster/source text giữ nguyên khi đổi panel; Enter/Apply theo text draft policy; không dùng Shadow IDs/sentinel |
| page.size/units/color — Page / Trang | Không selection; Page Properties | Model page dimensions/orientation/display units/color scope | Target PageId | Không hiển thị như đang resize object; đổi page trong job không đổi destination tự động |
| bitmap.placement/source — Image / Ảnh | Transform + Image Properties | Source size/DPI/EXIF/profile/link/embed theo capability; W/H accepts `px` only for one raster with trusted source X/Y DPI, swapping axes after applied EXIF orientation 5–8 | Image object + immutable asset | Missing DPI rejects while preserving edit; reference-only báo scope; preview RGBA8 không là nguyên bản 16/32-bit |
| snap.enabled/kinds/radius/grid.interval — Snap / Bắt điểm | Toggle nhanh + Snap Properties; Settings > Preferences > Units & Snapping báo trạng thái grid | Kind support/radius DIP; khoảng lưới canonical mm, nhãn theo đơn vị trang | App/tool preference + view context | Không sửa artwork/Undo; trạng thái master/group/grid phản ánh cấu hình; đổi display unit không scale khoảng cách; unsupported có reason |
| arrange.reference/spacing/order — Arrange / Sắp xếp | Theo multi-selection; Arrange Properties | Target enum, mm spacing, stable order | Selection/page theo reference rõ | Không chạy expensive geometry từ can_execute; Apply một transaction |

Nếu có field được source hỗ trợ mà chưa nằm trong table, triển khai phải bổ sung semantic key và binding, giữ chức năng. Table là contract nhóm, không cho phép xóa field cũ.

### Màu và đơn vị là context dùng chung

Các dòng ghi mm trong bảng mô tả **canonical source basis**; field chiều dài hiển thị/nhập theo UnitContext mm/cm/m/in/ft/yd, pt/pc hoặc px khi field cho phép. Bare number theo unit field, suffix explicit ghi đè; font vẫn pt, percent/count/angle có type riêng. Bar/Properties/rulers/status dùng cùng converter, không ghi ngược số đã làm tròn. DPI ảnh, design px, raster effects và DPI UI tách biệt. Field thiếu pixel context báo lỗi, không lấy zoom hoặc monitor DPI làm hệ số.

Color controls hiển thị model/channels/alpha/profile nguồn; proof/display chỉ đổi cách xem. Shadow ribbon/dialog swatch, Gradient stop, Spot palette swatch và canvas dùng effective page `IccPreview` khi khả dụng; numeric values giữ nguồn. Palette/status pixel sampling và monitor-profile consistency chưa được nghiệm thu. Assign ICC giữ số; Convert Active Page ICC Profile chuyển RGB/CMYK process paints của trang theo profile đích với một Undo. Intent chọn tường minh; BPC và batch scope chưa khả dụng. `File > Export Raster…` có TIFF/JPEG, active/all pages, DPI và RGB ICC output. Renderer chuẩn hóa qua page ICC, writer chuyển sRGB sang profile RGB đích và có thể nhúng profile đó; giới hạn xuất và việc native visual chưa nghiệm thu nằm ở `raster-export-20261021.md`. Profile CMYK/Gray output, selection export và pixel oracle cho profile đích ngoài sRGB chưa hỗ trợ. Existing Convert mode vẫn chuyển channels. Không badge Proof/Overprint ON khi consumer chưa hoạt động; approximation/unknown có label và cách xử lý. Shortcut Proof hiện tại Ctrl+Alt+Y, đọc registry khi hiển thị; Ctrl+Y giữ Redo.

Tooltip/focus help unit: “Đổi đơn vị nhập/hiển thị, giữ kích thước thật”; numeric help có ví dụ `1in`, `25.4mm`, `5ft+6in`. Color help: “Assign giữ số màu; Convert chuyển số theo profile đích”; status lỗi nêu profile/field/target và cách sửa. Icons unit/ruler/color/profile/proof dùng SVG cùng bộ hiện có hoặc asset semantic mới qua U07; text unit và accessible name bắt buộc. Không thêm toolbox tool riêng cho Color Management/Units (không áp dụng).

### Khoảng lưới trong Units & Snapping

- Đường mở: Settings > Preferences > Units & Snapping (tab index 2).
- Trường Grid interval nằm dưới trạng thái bật/tắt Snap to Grid; nhãn văn bản trong button hiển thị cả giá trị hiện tại và đơn vị. Nhấp để sửa, Enter lưu ngay vào drawing preferences của máy này, Esc hủy. Đây không đổi geometry, page metadata hay Undo history.
- Bare number dùng đơn vị của trang đang mở; suffix rõ như `10mm` hoặc `1/2in` ghi đè. Lưu bằng canonical mm, validation từ 0,001 đến 1.000.000.000 mm; ngưỡng nhỏ không được đổi theo zoom hoặc DPI màn hình.
- Focus help/status nêu đơn vị mặc định, cách suffix override, Enter/Esc và lỗi giới hạn. Không có Action ID riêng vì đây là field của Settings; menu khác, toolbar, toolbox, property bar, Properties và icon không áp dụng.

### TAC preflight trong Color & ICC

- Đường mở: Settings > Preferences… (MenuActionId::SettingsPreferences, phím Ctrl+J) → tab Color & ICC (index hiện tại 3) → nhóm Total area coverage (TAC). Đây là route Settings hiện có; chưa có canonical string ActionId riêng cho tab.
- Không thêm toolbox, toolbar hay contextual property-bar entry. Properties không áp dụng cho preflight đầu ra có phạm vi Document/Page/Selection; dialog Settings là bề mặt cấu hình/kiểm tra hiện có và không sửa tài liệu khi mở.
- Control và scope: Document quét mọi trang; Page quét trang hiện tại; Selection quét vùng chọn hiện tại. Limit là số nguyên thủ công 100–400%. Check chỉ đọc; Fix là lệnh riêng, một bước Undo và có thể đổi ngoại hình. Đổi scope/limit hoặc đổi snapshot làm báo cáo mất hiệu lực; Fix từ chối báo cáo cũ.
- Label ngắn: Tài liệu, Trang, Vùng chọn, %, Kiểm tra, Sửa / Document, Page, Selection, %, Check, Fix. Không cần icon riêng; dùng button có nhãn văn bản.
- Tooltip/focus help: scope mô tả phạm vi quét; limit ghi Enter áp dụng, Esc hủy và nêu rằng ngưỡng chưa gắn profile; Check nêu tính chỉ đọc cùng spot/raster chưa đánh giá; Fix nêu Undo và khả năng đổi ngoại hình. Status phản hồi lỗi nhập, kết quả quét, phạm vi thiếu selection và kết quả sửa.
- Giới hạn hiển thị: chỉ vector process CMYK được tính/sửa; spot và raster được đếm riêng nhưng chưa đánh giá. Manual limit chưa lấy từ ICC/output condition. Không gọi đây là proof hoặc đảm bảo separations/overprint.

### Quy tắc numeric/text chung

- Giá trị lưu là typed source value; chuỗi hiển thị có thể làm tròn nhưng focus/edit không làm source mất precision.
- Dấu thập phân theo locale và cú pháp tool phải phân biệt. NaN/Inf/overflow/trailing junk bị từ chối; range ở một validator dùng chung.
- Empty, Mixed, None, Disabled, Busy, Error và Unsupported là các state riêng, không dùng chung chuỗi 0 hoặc cùng màu mờ.
- Enter commit field/session theo feature; Esc hủy edit đang focus trước khi hủy tool. Tab commit chỉ khi hợp lệ; nếu không, giữ field/error và không chuyển ghi sang context khác.
- Click selection/tab/page mới khi đang sửa field: target đã bind phải được validate; field cũ được commit hợp lệ vào target cũ hoặc cancel/giữ theo policy được công bố, không commit vào target mới. Source/epoch đổi thì reject.
- Khi IME composition diễn ra, không lấy Escape/Enter/arrows của canvas. Undo local text draft và Undo document có ranh giới rõ.
- Numeric increment/drag có start/update/commit/cancel; gộp theo session+target+property, không gộp thao tác khác chỉ vì gần thời gian.
- Blur/Shadow/Gradient/Contour preview sử dụng draft/derived snapshot. Khác biệt preview và committed thể hiện bằng trạng thái gọn, không chỉ đổi màu nút.

## 5. Ma trận context

| Context | UI cần hiện | Hành vi |
| --- | --- | --- |
| Không document | New/Open; Properties empty state ngắn | File actions có capability; object/tool fields disable đúng lý do |
| Có page, không selection | Page/Document hoặc DrawingDefaults được ghi tên rõ | Không hiển thị giá trị object giả; tool defaults không sửa mọi object |
| Một object | Transform/Appearance + nhóm đặc thù | Full supported fields reachable và đúng source |
| Nhiều object | Bbox selection + shared/mixed properties | Target/reference rõ; sửa field áp dụng đúng semantics cho tập |
| Locked/reference-only/unsupported object | Thông tin source và lý do không sửa được | Không mở một control có vẻ editable nhưng không có tác dụng |
| Tool draft / inline text / numeric editing | Context/session target và preview state | Không overwrite input đang gõ bởi projection mới; validation đúng session |
| Job busy | Progress/cancel phù hợp; vẫn pan/navigate nếu an toàn | Result gắn đúng target; không chặn toàn app vô thời hạn |
| Error/invalid input | Error gần field, help ngắn và cách sửa | Chưa commit thì source/history không đổi |
| Panel đóng / cửa sổ hẹp | Menu focus route và overflow có tên | Mọi tham số vẫn reachable bằng keyboard; không tự ép mở lại panel đã đóng |

## 6. Icon, nhãn, focus help và status

Dùng assets trong `apps/desktop/resources/icons` đã có khi đúng nghĩa. Icon mới đề xuất đặt theo semantic ID dưới cùng thư mục, cùng viewBox/stroke và có license/provenance. Đợt tài liệu không tạo/đóng gói icon.

| Semantic | Icon/label đề xuất | Tooltip + focus help EN / VI | Status và usage |
| --- | --- | --- | --- |
| Open properties | Existing properties asset nếu có; P: `properties-open.svg`; label Properties / Thuộc tính | Open properties for the current selection / Mở thuộc tính vùng chọn | Focus đúng section; mở không tạo Undo |
| Transform | Existing move/scale/rotate icons; X/Y/W/H có label/unit | Position/Size/Angle of selection / Vị trí/Kích thước/Góc vùng chọn | Drag to transform; Esc cancels / Kéo để biến đổi; Esc hủy |
| Pen/Node | Existing Pen/Node icons, giữ active/focus | Tên tool + shortcut từ registry, một câu scope | Start → add/bend/edit → finish/cancel, modifier chỉ quảng bá sau xác nhận |
| Contour | Existing contour icon; label Contour / Viền | Edit contour spacing and steps / Sửa khoảng cách và số bước viền | Select path → change parameters → preview → Apply; sửa lại không nhân operation |
| Gradient | Existing gradient icon; label Gradient / Chuyển sắc | Edit fill gradient and stops / Sửa chuyển sắc tô và điểm màu | Select target → edit stop/handles → Apply hoặc Cancel |
| Blur/Shadow | Existing appearance icons; labels Blur/Shadow / Làm mờ/Đổ bóng | Edit selected effect / Sửa hiệu ứng đã chọn; trường hợp mới ghi Add effect | Select object → focus existing/add explicit → adjust → Apply; advanced ở Properties |
| Dimension | Existing dimension icon; label Dimension / Kích thước | Measure source geometry / Đo hình học nguồn | Chọn kiểu/điểm → đặt nhãn → sửa style/units; linked/detached rõ |
| Layers visibility/lock | Existing eye/lock assets nếu có; P asset cùng hệ | Show/Hide; Lock/Unlock + tên target; không chỉ icon trống | Toggle cùng command; tree/canvas selection đồng bộ |
| State Mixed/None/Error/Busy | Pattern/glyph riêng + accessible text; không emoji | Nêu state và lý do/cách tiếp tục | Error cần xử lý ưu tiên hơn hover hint |

Tên message đề xuất `ui.properties.*`, `ui.tool.*`, `ui.status.*`, `ui.validation.*`; chưa là catalog đã triển khai. U01/U07 tái dùng catalog hiện có hoặc thêm catalog trung tâm nếu còn thiếu, không thêm ternary language trên mỗi paint branch.

Tooltip xuất hiện cả hover và focus. Shortcut đọc từ registry đã hiệu lực, không hardcode từ tài liệu upstream. Hints ngắn một bước; help tool gồm 3–5 bước, phạm vi tham số, cách sửa lại, Cancel/Undo và giới hạn geometry/format thực.

## 7. Luồng sử dụng đại diện

1. **Chỉnh vùng chọn:** Pick đối tượng → sửa W trên bar → Properties cập nhật cùng giá trị → Undo một bước → sửa X trong Properties → canvas/bar cùng đổi. Với nhiều đối tượng có màu khác nhau, Appearance hiện Mixed đến khi chọn giá trị mới.
2. **Vẽ và sửa path:** Pen → nhập theo cú pháp precision hiện có → preview và snap cùng solver → kết thúc → Node chỉnh handles/corners bằng canvas hoặc số → Undo/Redo → save/reopen giữ node IDs và geometry.
3. **Sửa lại hiệu ứng:** chọn đối tượng có Shadow → mở Shadow từ toolbox/menu → focus instance hiện có → chỉnh offset/color/blend trong cùng Properties → preview → Apply một Undo; deselect/reselect vẫn đúng một node.
4. **Chuyển context khi đang nhập:** bắt đầu nhập Gradient stop → chuyển tab/selection → session cũ được xử lý theo policy, tuyệt đối không sửa stop của context mới. Lỗi validation có đường sửa/hủy và không để status cũ treo mãi.
5. **Bản vẽ lớn:** bật Snap/Contour preview → pan/zoom vẫn phản hồi → thay nhanh tham số → chỉ preview mới nhất được nhận; Cancel thu hồi job → file nguồn và history không đổi.
6. **File import có loss:** Open/Import → chọn representation/mode có capability → xem report → Apply → Properties phân biệt editable/reference/conversion. Viewport đẹp không là điều kiện duy nhất để cho xuất sản xuất.
7. **Đổi đơn vị:** Page/Document Properties → chọn inch/foot/m… → rulers/bar/Properties cùng cập nhật → nhập `1in` hoặc số theo unit field → Enter. Geometry không thay do đổi display; Dimension style/dual units giữ override đã chọn.
8. **Quản lý màu:** chọn paint → xem source model/profile → Assign hoặc Convert explicit → xem scope/quality/report → Apply một Undo. Bật Proof chỉ đổi view; xuất file dùng source/output condition, không lấy pixels từ proof viewport.

## 8. Migrate, nghiệm thu và giới hạn

Migrate theo từng feature: tạo projection/binding → gắn control/model chung → trỏ menu/shortcut/legacy panel về command đó → mở rộng regression → kiểm native rồi mới dùng mặc định cho feature. Mỗi bước có thể quay về implementation cũ đã đúng mà không thay schema hoặc bỏ chức năng.

Giữ một authority sau khi migrate; legacy entrypoint là alias, không được tồn tại hai nguồn validation/giá trị. File lớn tách theo domain phù hợp dưới 2.000 dòng, không tạo file riêng chỉ để chứa vài control.

Gate dùng các AC-UP-SYNC, AC-UP-UX và AC-UP-TOOL trong kế hoạch chính. Các kiểm tra cần có:

- Mở/focus Properties khi dock đang đóng, bar hẹp, nhiều field mixed; panel navigation không revision/Undo.
- Keyboard/tab order/focus-visible, flyout arrows/Enter/Esc, numeric caret/selection/clipboard và Vietnamese IME.
- Native control accessibility: semantic ID/name/role/value/state và UIA inspection; custom paint không tự là bằng chứng accessible.
- 1366×768/1920×1080, DPI 100/125/150/200%, light/dark/high contrast; thiếu monitor/thiết bị nào ghi rõ chưa nghiệm thu.
- Source data, effect/node/stop IDs, source font/color/provenance và user preferences giữ qua Undo/Redo/save/reopen.
- AC-C06-UI-01: mở Settings/Color & ICC, hover scope/limit/Check/Fix thấy help đúng phạm vi và giới hạn; nhập ngưỡng ngoài 100–400 bị giữ lại và không đổi revision.
- AC-C06-UI-02: Check trên Page không đổi revision; báo process CMYK vượt ngưỡng cùng số spot/raster chưa đánh giá.
- AC-C06-UI-03: Fix chỉ áp dụng trên báo cáo hiện hành, ghi một transaction có thể Undo, giữ K/alpha/overprint; Undo khôi phục chính xác giá trị nguồn.
- Native captures phải gắn đúng source build/context; mockup/layout tests không thay bằng chứng tương tác.

Mục không áp dụng: panel editor node graph riêng, UI cộng tác cloud, brush/raster suite mới, animation timeline, scene 3D và workflow CAM mới. Đây là chức năng mới ngoài đợt cải thiện các công cụ hiện có, không phải lý do xóa module/tùy chọn đang có trong repo.
