# Kế hoạch đại tu nhóm Hiệu ứng và hệ Appearance

Ngày: 25/10/2026. Trạng thái: **kế hoạch dựa trên source hiện tại; chưa triển khai, build hoặc nghiệm thu runtime**. Phạm vi: `core/document`, `core/rendering`, `core/geometry`, `modules/vector`, `modules/raster`, `modules/interop` và `apps/desktop`. Giữ thiết kế Win32 Native Shell, dữ liệu `.pr`, các chức năng/shortcut hiện hữu và local edits của checkout gốc.

## 1. Mục tiêu và tham khảo

Người dùng chọn đối tượng, thêm hiệu ứng thường dùng, **kéo tay nắm hoặc thanh trượt bằng chuột và thấy canvas cập nhật trong lúc kéo**, chỉnh lại cùng node sau khi bỏ chọn/chọn lại, đổi thứ tự hoặc tắt tạm, rồi Undo/Redo, lưu/mở và xuất mà biết rõ phần nào giữ được. Các effect live phải giữ hình/text nguồn sửa được. Việc rã/đổi sang ảnh là lệnh riêng có cảnh báo về khả năng sửa và màu.

| Tham khảo chính thức | Cách áp dụng có chọn lọc |
| --- | --- |
| [CorelDRAW: Vector effects](https://product.corel.com/help/CorelDRAW/Documentation-Mac/CorelDRAW-en/CorelDRAW-Vector-effects.html) và [Toolbox](https://product.corel.com/help/CorelDRAW/540111147/CorelDRAW-en/CorelDRAW-Toolbox.html) | Nhóm các công cụ tương tác như Shadow, Contour, Blend, Envelope, Distort; phân biệt hiệu ứng vector và bitmap theo target. |
| [Illustrator: Appearance attributes](https://helpx.adobe.com/illustrator/using/appearance-attributes.html) và [Apply/modify effects](https://helpx.adobe.com/illustrator/desktop/special-effects-styles/apply-filter-effects/apply-and-modify-effects.html) | Một danh sách effect có thứ tự, enable, chỉnh, xóa và scope rõ; menu và Properties dẫn tới cùng node. |
| [Illustrator: Graphic Styles](https://helpx.adobe.com/illustrator/desktop/special-effects-styles/apply-graphic-styles/apply-graphic-styles.html) | Preset tái sử dụng là giai đoạn sau; trước hết phải có semantics node, color và export ổn định. |
| [Microsoft: Slider design](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/slider) và [Visual feedback](https://learn.microsoft.com/en-us/windows/apps/develop/input/guidelines-for-visualfeedback) | Thanh kéo dành cho giá trị tương đối cần phản hồi tức thì; ô số đi kèm cho giá trị chính xác. Hit target, trạng thái focus/hover và phản hồi liên tục phải phù hợp chuột, DPI và bàn phím. |

Không sao chép icon, shortcut hoặc thuật toán của ứng dụng tham khảo. Ưu tiên công việc in ấn, biển hiệu, cắt/khắc và thiết kế vector của PikDraw; hiệu ứng nhìn đẹp nhưng không thể cắt phải được đánh dấu trong production.

## 2. Kiểm kê đúng source ngày 25/10/2026

| Mảng | Hiện trạng đã đối chiếu | Rủi ro phải xử lý |
| --- | --- | --- |
| Model | `Appearance` trong `effect_stack.hpp` nằm ở `PathObject`/`TextObject`, có object/fill/stroke opacity, sáu BlendMode và node có ID/thứ tự/enable. `EffectParameters` mới gồm `GaussianBlurEffect`, `DropShadowEffect`. | Chưa có cùng model effect cho Group/Image. Không coi Gradient là effect: đó là fill paint; Contour hiện là `contour_spec` riêng trên Path. |
| Command/workflow | `document_workflow_appearance.cpp` có add/remove/reorder/enable/edit/clear và lệnh `SetAppearanceCommand`; `document_workflow_effect_draft.cpp` có draft snapshot/Apply/Cancel. Có đường mở effect hiện hữu bằng `selected_effect_index` để không thêm node trùng khi kích hoạt lại. | Nhiều logic lặp theo `variant.index()`/vị trí; multi-selection khác cấu trúc stack bị khóa chỉnh. `set_effect_draft_parameters` dựng lại preview snapshot ngay khi preview bật, nên kéo slider nhiều mẫu liên tiếp có nguy cơ làm nghẽn UI; chưa có slider effect chung. |
| Rendering | Software compositor xử lý sigma X/Y riêng; `d2d_effects_pipeline.cpp` gộp sigma X/Y bằng `max` cho Blur/Shadow; GPU từ chối Shadow blend khác Normal khi thiếu backdrop. | Preview, CPU và D2D có thể khác nhau. Phải đo pixel/halo/bounds, định nghĩa fallback và thông báo; không nói là đồng bộ chỉ vì cùng tên effect. |
| Storage/export | `appearance_codec.cpp` có type ID/schema cho hai node, reader từ chối type/schema bắt buộc chưa biết; native reader giữ tương thích. `svg_exporter.cpp` hiện báo toàn bộ effect stack là unsupported. | Mỗi node mới cần codec, validation, schema migration, save/reopen, fidelity report và xuất raster/vector theo năng lực; không âm thầm làm phẳng hoặc bỏ hiệu ứng. |
| UI | `Effects` menu có Blur, Shadow, Contour, Break Contour Apart, Clear; Extrude/Bevel là entry M2 disabled. Toolbox slot 7 `Blur` thực chất là flyout `Appearance` gồm Properties/Blur/Shadow/Contour/Gradient; slot 8 Shadow, 9 Contour, 10 Gradient vẫn riêng. Paint/click/focus/tooltip dùng nhiều mảng và group ID số nguyên. | Tên nhóm, item và trạng thái nằm rải trong `native_window_toolbox.cpp`, `native_window_paint_docks.cpp`, `native_window_chrome_clicks.cpp`, `native_window_menu_actions.cpp`, `native_window_tooltips_guides.cpp`. Cần catalog chung và phân vai flyout/tool/menu. |
| Contour | `ContourToolController`/`ContourFeature`, `ApplyContourCommand`, `RemoveContourCommand`, `BreakContourApartCommand` tồn tại ngoài `Appearance.effects`. | `Effects > Clear` chỉ xóa stack, không xóa Contour; UI phải giải thích hoặc thêm lệnh Clear All Effects với scope khác. Không di trú Contour vào stack bằng cách đổi tên field. |
| Ảnh | `RasterEditKind` và edit graph của bitmap khác effect stack của Path/Text. | Filters ảnh đi qua `Bitmaps`; không thêm cùng một nút UI nếu backend render/codec chưa có. |

Những kết luận trên là audit source, không phải nghiệm thu chuột, DPI, màu in, thời gian frame hay xuất file thực.

## 3. Kiến trúc logic cần thống nhất trước khi thêm effect

### 3.1. Ba họ effect và một giao diện quản lý

1. **Appearance filter/compositing**: Blur, Shadow, Glow, Feather, Overlay. Input là surface của Path/Text; output là ảnh trình bày. Node có ID, thứ tự, enable, tham số theo mm/màu nguồn. Bản đầu chỉ mở trên loại đã render và lưu được.
2. **Geometry feature**: Contour, Offset Path, Transform Repeat, Roughen/Zigzag, Blend, Envelope, Distort. Input/output là hình học hoặc quan hệ nguồn có thể sửa; renderer chỉ tiêu thụ kết quả. Contour đang là feature riêng; dùng adapter `EffectListItem` để cùng xuất hiện trong Properties nhưng không giả làm `EffectNode` hay đổi file cũ.
3. **Bitmap edit/filter**: Blur ảnh, Sharpen, Noise, Pixelate/Vignette qua raster graph bất biến; menu chính `Bitmaps`, có mục liên kết trong Properties khi chọn Image. Không nhét ảnh vào `PathObject::appearance` chỉ để dùng chung UI.

Properties có một mục **Appearance > Effects** hiển thị cả các item được hỗ trợ của target, nhưng badge phân biệt `Live surface`, `Vector geometry`, `Bitmap edit`. Mỗi item mở đúng editor và cùng một command mà menu/toolbox sử dụng. Gradient, opacity và blend là paint/compositing fields; được hiển thị gần effect list nhưng không biến thành node mới.

### 3.2. Hợp đồng thống nhất

`EffectDefinition` ở tầng document/feature catalog có stable type ID, phiên bản, target capability, parameter schema, đơn vị, default/preset IDs, work/halo bound, thứ tự được phép và production/export class. Desktop bọc label/icon/help theo locale; renderer cung cấp capability CPU/D2D, không phụ thuộc desktop. Một ID type không được đổi nghĩa sau khi file đã lưu.

Luồng: menu/toolbox/Properties/action → resolve target và scope → `EffectEditRequest` trên snapshot + epoch/revision/generation → validation + preview proposal → một command/transaction → snapshot mới → render/cache/export. Mở panel, chọn node, đổi hover hoặc Cancel không ghi Undo. Với field, preview và commit dùng cùng tham số đã validate; khi source đổi hoặc tab đóng, proposal cũ bị bỏ. Job nặng chạy ngoài UI event loop, có cancellation, quota và progress.

Command thao tác bằng **node ID/feature ID**, không lấy index của selection tại thời điểm commit. `selected_effect_index` hiện có tiếp tục làm adapter UI trong migration; edit/reselect phải giữ ID và revision không đổi cho tới khi thực sự sửa. Multi-selection có stack khác nhau được trình bày Mixed, không tự san bằng; lệnh Batch Add áp cùng effect mới và một Undo, còn Batch Edit chỉ bật khi các node có cùng type/scope theo chính sách rõ. Group/PowerClip/layer cần target tường minh; chỉ mở Group/Image sau khi model, renderer, codec và export cùng hỗ trợ.

### 3.3. Một semantics cho render, màu và bounds

- Chốt thứ tự chính thức: source geometry/paint → geometry feature có provenance → surface effect stack theo thứ tự node → object opacity/blend → clip/group composite. Trước khi đổi runtime, lập bảng thứ tự **đang chạy** cho CPU, D2D, text, Contour và PowerClip; trường hợp khác quy tắc đích phải migration hoặc có lý do hiển thị.
- Sửa sai khác anisotropic sigma: D2D phải cho kết quả X/Y tương đương CPU, hoặc capability báo dùng CPU khi X≠Y; không âm thầm lấy `max`. Shadow blend khác Normal cần backdrop đúng painter order và group isolation; nếu chưa có, disable mode/stack không tương thích và giữ source.
- Halo/bounds/invalidation phải bao gồm offset, blur radius, stroke, transform và DPI. Cache key gồm object/feature ID, revision, effect params, backend, zoom/raster DPI, color/profile và clip context; cache có budget/eviction. Preview quality có thể giảm nhưng export dùng cùng semantics với chất lượng đích, không dùng thumbnail làm output sản xuất.
- Màu hiệu ứng là `ColorValue` giữ ICC/CMYK/spot/provenance khi phép tính cho phép. Alpha và blend cần quy tắc premultiply/linear light được kiểm bằng oracle độc lập; soft effect sinh pixel nên cảnh báo khi xuất cutter/path hoặc spot/overprint không giữ được. Không tự quy đổi màu thành RGB nguồn chỉ vì màn hình hiển thị RGB.
- Render quá budget phải cho diagnostic có đối tượng, effect và cách giảm tham số/đổi DPI; không để màn hình lặng lẽ mất effect. Save vẫn giữ authored source; export phải báo loss hoặc từ chối theo định dạng và lựa chọn của người dùng.

## 4. Danh mục hiệu ứng, thứ tự ưu tiên

| Ưu tiên | Hiệu ứng / vị trí | Semantics và target giai đoạn đầu | Gate bắt buộc |
| --- | --- | --- | --- |
| P0 | Gaussian Blur, Drop Shadow, Contour hiện có | Giữ node/feature và shortcut; thống nhất renderer, draft, Properties, Clear scope, fidelity. | CPU/D2D parity, reselect không trùng node, save/reopen và export loss rõ. |
| P1 | Feather / Làm mềm biên | Alpha edge fade hướng vào/ra, chiều rộng mm; Path/Text. Không đồng nhất với Gaussian Blur toàn hình. | Vùng giữa và bounds đúng; nền trong suốt/CMYK/spot có cảnh báo. |
| P1 | Outer Glow / Phát sáng ngoài | Mask alpha nở ra + blur, màu và opacity; Path/Text. | Không tô phủ ruột đối tượng; bounds và compositing ổn định. |
| P1 | Inner Glow / Phát sáng trong | Glow chỉ bên trong alpha nguồn, có edge/center mode; Path/Text. | Không tràn ra ngoài clipping; thứ tự với fill/stroke rõ. |
| P1 | Inner Shadow / Bóng trong | Shadow bị giới hạn trong alpha đối tượng, offset/blur/color/opacity; Path/Text. | Không biến thành Drop Shadow đặt sai vị trí; lỗ và text glyph đúng. |
| P1 | Block/Long Shadow / Bóng khối | Hình bóng sắc cạnh có độ dài/góc, ưu tiên vector cuttable khi geometry hợp lệ; Path/Text hiển thị, Convert Text to Curves trước output cắt. | Phân biệt bản live nhìn và đường cuttable; không gọi soft shadow là vector. |
| P2 | Offset Path / Viền lệch | Một đường offset đơn theo mm, join/cap/miter; Path đóng/mở; tái dùng geometry Contour nhưng command/semantics riêng. | Path còn sửa node; không tự nhân thành nhiều step của Contour. |
| P2 | Transform Repeat / Lặp biến đổi | Bản sao live có count, translate/rotate/scale và nguồn ID; Path/Text. | Stable provenance, không tạo n bản source giả mỗi frame; Expand có Undo. |
| P2 | Roughen và Zigzag / Nhám, răng cưa | Biến đổi đường theo amplitude/wavelength/seed cố định, có giới hạn node; Path. | Seed lưu file; preview/commit/export cùng hình; không sinh nổ topology. |
| P2 | Color Overlay / Phủ màu | Blend một màu lên alpha của Path/Text, giữ fill gốc; không thay source fill. | Màu ICC/spot, opacity, blend và print policy rõ. |
| P3 | Blend giữa hai đối tượng | Nội suy hình/paint giữa hai source ID với count và mapping; Path tương thích. | Đổi source cập nhật preview, xóa source/Undo không treo tham chiếu. |
| P3 | Envelope / Biến dạng khung | Warp hình qua lưới/4 biên có handle; Path/Text tùy glyph mapping. | Text còn sửa được hoặc có Convert to Curves rõ; pixel oracle và topology. |
| P3 | Distort / Biến dạng | Push/Pull, Twister, Zipper với biên độ thực và seed nếu ngẫu nhiên; Path. | Bounded, không tự giao mất dữ liệu im lặng; renderer/export parity. |
| P3 | Bevel/Emboss, Extrude/Perspective | Hiệu ứng chiều sâu riêng cho vector với ánh sáng/góc/độ sâu; giữ entry M2 disabled tới khi backend thật. | 2D appearance vs geometry 3D/cutter tách bạch; tính color/profile và export. |
| P4 | Blur ảnh, Sharpen, Noise, Pixelate, Vignette | `Bitmaps > Effects/Filters`, ImageObject qua raster graph; không dùng Appearance v1. | Tile/U8/U16/alpha/profile, cancellation, codec/legacy reader và export. |

Không cần mở tất cả cùng lúc. Giai đoạn P1 cho người dùng nhiều hiệu ứng thông dụng trước; mỗi entry chỉ bật khi đường UI → model → renderer → lưu/mở → Undo → export/loss report đã đi xuyên. Preset như “Bóng mềm”, “Glow neon”, “Nổi nhẹ” lưu **tham số effect**, không lưu ảnh chụp; không ghi đè style khác nếu người dùng chỉ chọn một effect.

## 5. UI/UX cụ thể

### 5.1. Menu, toolbox và action

Menu chính theo `docs/design/ui-ux-standard.md`: `Effects > Appearance…` mở/focus Properties; `Effects > Blur > Gaussian Blur…/Feather…`; `Effects > Shadow & Glow > Drop Shadow…/Inner Shadow…/Outer Glow…/Inner Glow…/Block Shadow…`; `Effects > Vector > Contour…/Offset Path…/Transform Repeat…/Roughen…/Zigzag…/Blend…/Envelope…/Distort…`; `Effects > Depth > Bevel…/Extrude…`; `Effects > Clear > Selected Effect/Clear Live Effects/Clear All Effects…`. Submenu mới chỉ hiện các action đã triển khai; entry Extrude/Bevel M2 hiện hữu giữ disabled kèm lý do cho đến khi hoàn thiện, không thêm nút giả. `Effects > Clear Live Effects` giữ semantics của `EffectsClear` hiện tại; `Clear All Effects` là action mới có preview số effect/geometry feature sẽ bị xóa, bao gồm Contour. Lệnh này không xóa fill, stroke, gradient, opacity hay blend của đối tượng.

| Bề mặt | Quyết định và ID |
| --- | --- |
| Action hiện hữu | Giữ `MenuActionId::EffectsGaussianBlur`, `EffectsDropShadow`, `EffectsContour`, `EffectsBreakContourApart`, `EffectsClear`, `action.effects.contour.focus`, `action.tool.contour`; map sang catalog/handler chung, không đổi shortcut `Ctrl+F9`, `Ctrl+K` khi còn đúng context. |
| Action mới đề xuất | Prefix `action.effects.` và các suffix chính xác: `appearance.focus`, `feather`, `outer_glow`, `inner_glow`, `inner_shadow`, `block_shadow`, `offset_path`, `transform_repeat`, `roughen`, `zigzag`, `color_overlay`, `blend`, `envelope`, `distort`, `bevel`, `extrude`, `clear_selected`, `clear_all_effects`. Ví dụ ID đầy đủ `action.effects.inner_shadow`. Đăng ký từng ID cùng lúc với capability, không chỉ thêm hằng số. |
| Toolbox | Giữ số slot 7/8/9/10 và mask settings. Slot 7 đổi nhãn thành `Effects / Hiệu ứng`, mở nhóm thao tác tương tác và Properties; slot 8 Shadow, slot 9 Contour là alias nhanh hiện hữu. Gradient vẫn ở slot 10/Fill, không là effect. Gaussian Blur từ flyout cũ giữ lối menu/Properties trước khi bỏ item thừa; không cắt đường truy cập đang dùng. |
| Flyout | Một `EffectToolDescriptor` cấp item, action, enable reason, icon, label, help, rect DIP, keyboard order cho paint/hit/focus/tooltip; chiều cao lấy số item thực và clamp trong viewport. Không copy mảng 5 item/`group_id == 3` sang file thứ sáu. |
| Standard toolbar | Không thêm effect riêng; mở Properties bằng action hiện có nếu có nút chung. |
| Context menu | `Edit Effect…` và `Remove Effect` chỉ xuất hiện khi chọn một node thật; gọi cùng action/command theo node ID. |
| Shortcut | Giữ shortcut hiện có. Effect mới chưa gán mặc định để tránh xung đột; thêm qua catalog shortcut và kiểm IME/text field. |

Các công cụ cần kéo trên canvas (Shadow, Contour, Blend, Envelope, Distort, Block Shadow) được ưu tiên trong toolbox. Hiệu ứng chỉ nhập tham số (Blur/Glow/Feather/Overlay) nằm ở menu và Properties; mục flyout cũ được chuyển sau khi đường menu/Properties đã đạt parity. Flyout không làm mất tool đang vẽ khi chỉ hover/focus; rời tool lúc đang drag tuân thủ chính sách draft/capture hiện hành.

### 5.2. Property bar và Properties

Property bar có tối đa 3–5 control nhanh cộng nút `Thuộc tính ▸` mở đúng section, ví dụ Shadow: tay nắm canvas + Offset, Blur, Opacity, nút Properties; Glow: Radius, Opacity, Color, nút Properties; Contour giữ Direction/Spacing/Steps theo khả năng đang có. Với tham số tương đối thường kéo, hiển thị slider gọn kèm số/đơn vị; ô số vẫn có để nhập chính xác. Các nút Apply/Cancel/Remove/Expand ở action group dưới Properties hoặc phím thích hợp; không nhét một hàng dài lên bar. Bar và dock dùng cùng field descriptor, parser đơn vị, giá trị và command.

Properties > Appearance > Effects có list theo thứ tự thật, icon loại, tên, enable, thumbnail nhỏ có thể tắt, nút Edit/Move/Duplicate/Remove. Click một item focus editor với các nhóm **Source/Scope**, **Geometry/Distance**, **Color & Transparency**, **Quality/Export**, **Actions**. Contour là item `Vector feature` trỏ tới `ContourToolController`; không cho kéo qua surface stack cho đến khi thứ tự render có phép toán được định nghĩa. Reorder chỉ bật với cặp node được renderer hỗ trợ. Mixed selection hiển thị giá trị Mixed và giải thích số object áp dụng; không hiển thị một trị số giả.

| Field/scope | Giá trị, trạng thái và Undo |
| --- | --- |
| Target | Object ID/type, Path/Text ban đầu; Group/Image disabled có lý do. Hidden/locked/content-locked không nhận commit. Scope object/fill/stroke chỉ hiện khi node và renderer hỗ trợ; không tự suy ra scope từ selection. |
| Distance/radius/offset | mm thật, parser đơn vị trang, hữu hạn và trong giới hạn canvas/work; mặc định theo effect, ví dụ Glow radius 1 mm, Feather 0.5 mm. Zoom không đổi giá trị authored. Field lỗi giữ nội dung để sửa, không cập nhật preview. |
| Color/opacity/blend | `ColorValue` và swatch hiện đúng profile nguồn; opacity 0–100%. Blend mode chỉ bật khi backdrop/isolation được renderer hỗ trợ; unsupported nêu lý do. |
| Quality | Preview Auto/High là view state; raster DPI từ thiết lập tài liệu, không sửa file mỗi khi zoom. Export Fidelity cho thấy vector/bitmap/unsupported theo định dạng và đích in/cắt. |
| Draft/command | Mouse up sau khi kéo hoặc Enter/Apply ghi một Undo nếu giá trị thay đổi; Esc/Cancel bỏ preview; enable/reorder/remove/duplicate là command theo node ID, mỗi click một Undo. Mở panel, focus field, đổi ngôn ngữ không ghi revision. |

SVG icon cần thiết kế mới dưới `apps/desktop/resources/icons`: `effect-feather.svg`, `effect-inner-shadow.svg`, `effect-outer-glow.svg`, `effect-inner-glow.svg`, `effect-block-shadow.svg`, `effect-offset.svg`, `effect-repeat.svg`, `effect-roughen.svg`, `effect-blend.svg`, `effect-envelope.svg`, `effect-distort.svg`. NativeWindow hiện vẽ nhiều glyph trực tiếp; chỉ báo icon SVG hoàn thành khi pipeline nạp SVG hoặc glyph tương đương được render/hit/focus thực sự. Không chép biểu tượng Corel/Adobe.

Tooltip mỗi item hai dòng: tên EN/VI + shortcut **đã đăng ký**, rồi một câu tác dụng và target. Status hint theo state, ví dụ `Kéo tay nắm bóng để chỉnh độ lệch · Shift khóa góc · Esc hủy`; khi edit số: `Enter xem trước · Apply lưu một bước Undo · Esc bỏ bản nháp`. Help/F1 hoặc `Help > Effects` có 3–5 bước cho từng effect, phạm vi Path/Text/Image, cách sửa/xóa, loss khi xuất và ví dụ production. Focus keyboard, screen reader name, light/dark, 1280×720/1366×768 và DPI 100–200% phải nghiệm thu native.

### 5.3. Ưu tiên chuột và preview canvas trực tiếp

| Tương tác | Hành vi bắt buộc |
| --- | --- |
| Bắt slider | Track đủ dài để đặt giá trị bằng chuột; thumb nhìn thấy rõ, hit rect lớn hơn nét vẽ, các control không chồng hit rect ở 100–200% DPI. Hover đổi cursor/track highlight; nhấn vào track chuyển thumb đến vị trí đó và bắt đầu kéo. Rect paint và hit lấy từ cùng layout DIP. |
| Kéo | Mouse down chụp pointer và mở `EffectDragSession` từ snapshot gốc; pointer move cập nhật số/đơn vị ở bar/Properties và preview canvas từ **cùng một bộ tham số**. Kéo ra ngoài track vẫn giữ capture, giá trị clamp theo miền hợp lệ. Không tạo command/Undo cho từng pixel. |
| Thả/hủy | Mouse up ở bất kỳ vị trí nào hoàn tất giá trị cuối, một transaction/Undo nếu thật sự thay đổi. Esc hoặc mất capture do đóng tab/chuyển document hủy và trả canvas về snapshot gốc; click không đổi giá trị không ghi revision. Không để canvas giữ một preview cũ sau khi thả/hủy. |
| Giá trị nhanh/chính xác | Opacity 0–100% dùng slider tuyến tính; radius/feather/length dùng miền slider dễ dùng theo workflow và tự mở rộng để chứa giá trị đã lưu, không clamp dữ liệu khi chọn lại. Nhấp ô số để nhập mm/% chính xác, double-click giá trị để chọn toàn bộ; Shift khi kéo tăng độ tinh chỉnh nhưng không thay đơn vị. Preset và ô số dùng cùng draft/validation như slider. |
| Tay nắm canvas | Shadow/Contour và các tool sau này ưu tiên kéo trực tiếp trên đối tượng; handle có hit target DIP lớn hơn glyph, nổi bật khi hover/active. Drag hiển thị đường dẫn, giá trị X/Y hoặc khoảng cách cạnh handle mà không che hình chính; Property bar/Properties đổi theo cùng draft. |
| Multi-selection | Nếu giá trị khác nhau, hiển thị `Mixed`, slider ở trạng thái không giả một giá trị. Kéo hoặc nhấn track áp một giá trị chung lên các target hợp lệ trong một transaction; object khóa không bị sửa và số object áp dụng được báo trước. |

Preview bật mặc định khi mở/chỉnh effect. Trạng thái nhìn thấy là **đang kéo**, **đang dựng**, **đã đồng bộ**, hoặc **không thể dựng**; thanh trượt phản hồi tức thì, nhưng canvas chỉ được ghi “đã đồng bộ” khi frame hiển thị đúng generation và tham số hiện tại. Nếu effect nặng, giữ frame hợp lệ gần nhất với badge “Đang cập nhật” và giá trị frame đó; không trình bày frame cũ như kết quả của thumb mới. Có thể giảm độ phân giải preview trong lúc kéo nếu cùng semantics và color transform; mouse up phải yêu cầu frame cuối ở chất lượng đúng trước khi báo hoàn tất hiển thị. Không dùng preview thấp làm export.

Đường triển khai: `native_window_style_events.cpp`/property bar phát mẫu pointer vào một `EffectInteractionSession`; coalesce các mẫu về giá trị mới nhất theo lịch vẽ thay vì render ngay trong từng `WM_MOUSEMOVE`. Worker/renderer dùng immutable snapshot, effect node ID, epoch/revision và generation; kết quả cũ không được publish. `DocumentWorkflow::rebuild_effect_draft_preview` hiện dựng snapshot đồng bộ được giữ như adapter cho click/nhập số, còn drag đi qua scheduler có cancellation, work/memory quota và kết quả theo generation. `native_window_canvas.cpp` lấy preview snapshot/frame hợp lệ mới nhất; invalidation bao cả bounds cũ và halo mới. Một source tính `SliderValue → EffectParameters` phục vụ bar, Properties, canvas handle và numeric field.

Mục tiêu đo **đề xuất, chưa phải kết quả** trên fixture công bố: thumb và số phản hồi ở frame UI kế tiếp; pointer-to-preview p95 ≤100 ms với một đối tượng hiệu ứng thường dùng ở 1440×900; thao tác kéo không block UI quá 50 ms; sau mouse up frame cuối xuất hiện p95 ≤200 ms nếu workload trong budget. Với ca lớn, ưu tiên tiếp tục nhận chuột, báo progress và đo thời gian thực tế; không ép FPS giả. Ghi timestamp pointer sample, schedule, render start/end, present và revision/generation để đo input-to-display; snapshot/offscreen test không thay thế thử NativeWindow bằng chuột.

## 6. Kế hoạch chuyển source theo giai đoạn

| Giai đoạn | Source cụ thể và thay đổi | Gate |
| --- | --- | --- |
| E0: chốt semantics và preview hiện có | `effect_stack.hpp`, `appearance_codec.cpp`, `document_workflow_appearance.cpp`, `document_workflow_effect_draft.cpp`, `object_compositor.cpp`, `d2d_effects_pipeline.cpp`, `svg_exporter.cpp`: bảng source→render→save→export cho Blur/Shadow/Contour; sửa parity X/Y, fallback blend và báo loss. Thêm interaction session/scheduler cho drag với generation, coalescing và preview canvas mặc định. | Pixel oracle CPU/D2D/WARP, node ID/reselect, Undo, save/reopen v2–v25, SVG report; AC-EFX-11/12. Không đổi layout trước khi source ổn định. |
| E1: catalog + UI chuột | `native_window_menus.hpp`, `native_window_menu_actions.cpp`, `native_window_toolbox.*`, `native_window_paint_docks.cpp`, `native_window_chrome_clicks.cpp`, `native_window_style_*`, `native_window_tooltips_guides.cpp`: action/descriptor chung, list Properties và focus, Clear scope; slider layout/hit/paint chung, on-canvas handle và readout dùng cùng draft. | Menu/toolbox/Properties/shortcut cùng action và enable reason; slider capture/drag/Esc, hit/paint/focus DIP khớp, legacy slot/settings còn. |
| E2: hiệu ứng thường dùng | Thêm Feather, Glow, Inner Shadow, Color Overlay theo từng surface node: model+validation+command+codec trước, sau đó Software/D2D parity, UI, tests, export preflight. Block Shadow đi nhánh geometry feature để có output vector cuttable, chỉ dùng chung catalog/UI. Mỗi effect ở file theo domain, không dồn vào `appearance_commands.cpp` hay shell file quá 2000 dòng. | Mỗi node/feature đạt acceptance; chưa đạt thì không hiện action bật được. |
| E3: geometry effects | `core/geometry` tính hình, `core/document` feature/command/provenance, `modules/vector` controller/preview, desktop handles/Properties. Tái dùng Contour/offset nhưng không đổi `contour_spec` của file cũ khi chưa có migration. | Stable source ID, topology, Undo/Redo, save/reopen, expand/export path/cutter có kiểm chứng. |
| E4: complex vector/depth | Blend/Envelope/Distort/Bevel/Extrude triển khai từng phần với quota và explicit conversion; bỏ nhãn M2 disabled chỉ khi một chuỗi đi xuyên hoạt động. | Đúng hierarchy/transform/text, preview/production riêng, không source loss im lặng. |
| E5: bitmap filters | `modules/raster` edit graph/tile evaluator, `core/document` versioned node/reader, `apps/desktop` Bitmaps/Properties và `modules/interop` export. | U8/U16/alpha/profile, cancel/stale, save/reopen reader cũ, fidelity/print. |

Giữ chiều dependency theo `registry/modules.json`: document/geometry không phụ thuộc desktop; rendering tiêu thụ snapshot, không sửa source; UI không cài thuật toán effect. Mọi thay đổi file `.pr` cần đọc được dữ liệu cũ và không im lặng bỏ node mới. Existing docs `drop-shadow-properties-ui.md`, `contour-properties-ui.md`, `object-opacity-and-properties.md` là đặc tả thành phần; khi triển khai, dùng tài liệu này cho nhóm/capability và giữ phần hành vi hiện đã kiểm của từng tính năng.

## 7. Nghiệm thu và mức sẵn sàng

| ID | Given / When / Then |
| --- | --- |
| AC-EFX-01 | Given Path/Text có Blur anisotropic; When render ở CPU, D2D và WARP nhiều zoom/DPI; Then halo, alpha, color và bounds đạt sai số pixel đã công bố; backend không tự lấy `max(sigmaX,sigmaY)` rồi báo giống nhau. |
| AC-EFX-02 | Given một Shadow đã tùy chỉnh; When bỏ chọn/chọn lại rồi mở qua menu, flyout, Properties; Then đúng node ID, tham số, số node và revision cũ cho đến lần edit thật. |
| AC-EFX-03 | Given stack hai node và Contour; When enable/reorder/remove/Clear Live/Clear All Effects; Then đúng scope, Contour chỉ bị xóa bởi lệnh có chỉ định, fill/opacity không đổi, một thao tác một Undo, Undo/Redo khôi phục đúng thứ tự và ID. |
| AC-EFX-04 | Given Glow/Inner Shadow/Feather trên path có lỗ, gradient, text glyph và clip; When preview/Apply; Then không tràn mask sai, source sửa được, preview/commit cùng tham số. |
| AC-EFX-05 | Given CMYK/ICC/spot và non-Normal Shadow; When render/print/export; Then color transform và backdrop policy được báo đúng, không mất màu/hiệu ứng âm thầm. |
| AC-EFX-06 | Given file `.pr` cũ và mới; When save/reopen và thử reader legacy; Then ID, order, enable, params, color/profile giữ nguyên; type bắt buộc chưa hiểu bị từ chối có lỗi rõ. |
| AC-EFX-07 | Given SVG/PDF/bitmap/cutter output; When export effect; Then từng định dạng giữ vector, rasterize có chấp thuận, hoặc báo unsupported/loss chính xác; không coi source `.pr` round-trip là SVG fidelity. |
| AC-EFX-08 | Given selection mixed/locked/group, dock đóng, flyout 1–2 cột, hai locale và DPI khác nhau; When dùng chuột/bàn phím; Then enable, labels, field values, focus, hit/paint, tooltip/hint đồng bộ; mở dock không tạo Undo. |
| AC-EFX-09 | Given effect rất lớn hoặc job nặng; When pan/zoom/chỉnh source/Esc/đóng tab; Then cancel/epoch guard chặn proposal cũ, không chặn UI, cache/temp được thu hồi; diagnostic còn source. |
| AC-EFX-10 | Given path được dùng cho cắt/khắc; When thêm soft shadow/glow hoặc Block Shadow vector; Then production preflight phân biệt raster-only và đường cắt thật, không gửi soft shadow thành contour máy cắt. |
| AC-EFX-11 | Given Shadow/Blur/Glow có slider và canvas preview bật; When click track rồi kéo liên tục, thả ngoài track hoặc Esc; Then thumb, số, handle và canvas dùng cùng tham số/generation, thả tạo đúng một Undo, Esc không đổi document, không frame cũ treo. Kiểm bằng input chuột thật và timestamp tới present. |
| AC-EFX-12 | Given effect vượt budget và liên tục đổi giá trị; When kéo nhanh/đổi selection/đóng tab; Then UI vẫn nhận chuột, chỉ generation mới nhất được present, trạng thái “Đang cập nhật” hoặc lỗi nói rõ frame nào đang thấy, không commit stale. Đo p50/p95 input-to-display, peak memory và số frame bị bỏ trên CPU/D2D/WARP. |

Regression đặt ở `core/document/tests/storage`, `core/document/tests/commands`, `core/rendering/tests/effects`, `modules/vector/tests`, `modules/raster/tests`, `modules/interop/tests/writers` và `apps/desktop/tests`; dùng fixture giữ trong suite, oracle độc lập cho pixel/màu và export. Đo peak scratch/cache, invalidation, latency preview/commit trên tài liệu nhiều object và zoom lớn; đặt budget trước khi kết luận tối ưu. Build/test qua `tooling/native/build.ps1` và khóa chung; chỉ test/benchmark không chứng minh cảm giác UI native, DPI vật lý hay màu bản in. Dọn output generated theo AGENTS.md.

**Điều kiện hoàn tất:** mỗi effect được công bố riêng khi có đường UI thật → target/scope → model/command → CPU/D2D hoặc fallback được báo → save/reopen → export/loss report → Undo/Redo → trợ giúp. Tài liệu này không xác nhận những hiệu ứng mới đã được triển khai.
