# Kế hoạch hệ đơn vị thống nhất — S00–S05

Trạng thái cập nhật 21/10/2026: **đã triển khai một phần S01–S04, chưa hoàn tất AC-UNIT01–12**. Registry/converter/parser nhận mm/cm/m/in/ft/yd/pt/pc; px vẫn yêu cầu DPI context. Suffix parse/format nay dùng chung geometry unit registry; parser nhận phân số `1/8in`, số hỗn hợp `1 1/2in` và biểu thức `5ft+6in`, có test mẫu số 0. Native v13 lưu đơn vị hiển thị riêng theo trang; v2–v12 mở với mm ngầm định. Có page/document commands Undo/Redo, `Layout > Page Display Units`, đổi đơn vị tại Settings và thước có tick/nhãn theo đơn vị trang; số đo/kích thước nguồn vẫn là mm. Shell suffix, Properties, status, quick-dimension, Pen, Contour, effect, text frame và một số page fields đã được nối từng phần. Ribbon của Page size, Object transform, Contour spacing và Drop Shadow X/Y/Feather hiện format theo đơn vị trang kèm suffix; Pen ghi đơn vị mặc định cạnh field. Khi bắt đầu sửa, buffer dùng đơn vị trang với độ chính xác đầy đủ; nhập suffix khác vẫn override rõ ràng. Targeted geometry/numeric tests và Pen shell test cho nhập `1 1/2in` pass. Pen Backspace reparse giữ nguyên page unit khi sửa góc trong biểu thức tọa độ. Automation rectangle và batch create rectangle/ellipse/text-position/line-polyline points, transform/pivot, duplicate offsets và stroke width nhận numeric fields kèm length_unit; legacy *_mm giữ nguyên, conversion sang mm diễn ra trước command writer, và px bị từ chối khi thiếu DPI basis. Regression kiểm inch/cm/ft qua geometry sau command; test_pikdraw_automation_rpc cùng test_pikdraw_mcp_server pass. Rulers/fields chưa được kiểm trực tiếp trên cửa sổ native; clipboard resource variants, mọi field Properties/toolbar và toàn bộ format gates còn mở. Pixel vẫn bị từ chối làm đơn vị trang vì không có một DPI basis duy nhất.

21/10 bổ sung: phát hiện Settings vẽ 8 lựa chọn nhưng click hit-test chỉ nhận 7; Pica đã được nối vào hit-test và regression CTest test_pikdraw_units_settings_ui pass. Đây là harness test, chưa thay native walkthrough trên màn hình.

21/10 bổ sung: Units & Snapping không còn ghi cứng “Grid enabled / 5.0 mm”; SnapConfig mặc định là 10 mm và Grid có thể tắt. Dòng trạng thái nay đọc trạng thái master/group/option hiện có và hiển thị khoảng lưới theo đơn vị trang, không đổi khoảng cách vật lý. Test formatter kiểm inch/cm và ON/OFF; `test_pikdraw_vector_snap` và `test_pikdraw_units_settings_ui` pass 2/2. Settings > Units & Snapping nay cho sửa khoảng lưới theo đơn vị trang hoặc hậu tố rõ; giá trị canonical mm được lưu trong user preferences và áp dụng ngay, không đổi geometry. Validation bounds 0,001–1.000.000.000 mm; regression controller/persistence pass. Native visual/focus walkthrough còn mở.

21/10 bổ sung: thông số preset khổ trang trên ribbon, thẻ preset ở hộp New Document và status sau khi chọn preset nay đổi cách hiển thị theo đơn vị trang; kích thước preset vẫn lưu bằng mm. Formatter có kiểm tra mm/in và `test_pikdraw_desktop_shell` pass 1/1. Chưa thay thế native walkthrough.

21/10 bổ sung: Line/Polyline numeric coordinates dùng đơn vị của trang hiện hành làm mặc định cho chiều dài không có suffix; hint đầu vào cũng hiện suffix đó. Regression xác nhận `#1;2` ở trang inch tạo điểm canonical `(25.4, 50.8) mm`; `test_pikdraw_drawing_tool_controller` pass 1/1.

21/10 bổ sung: guideline/radius/path HUD và tọa độ Quick Dimension hiển thị theo đơn vị trang; Quick Dimension nhận hậu tố ASCII, phân số hỗn hợp và cặp W×H/semicolon, không tách nhầm `1 1/2in`. Prompt nói rõ đơn vị mặc định và suffix override. Parser/hit-character helper có test shell mục tiêu; chưa phải kiểm thử bàn phím/UI native end-to-end.

21/10 bổ sung: live selection/text/guideline/shadow/Contour status, style-width menu/dialog và Text Frame Properties đổi sang cùng đơn vị trang. Ô độ rộng nét giữ giá trị canonical nếu chỉ sửa màu, đọc hậu tố đơn vị rõ ràng và báo lỗi thay vì âm thầm dùng lại giá trị cũ. Spacing kéo giữ floor 0,1 mm/nấc 0,1 mm hiện có; không lẫn với arc-tolerance floor 0,02 mm. Đổi label không đổi canonical mm; chưa có native visual walkthrough.

21/10 bổ sung: measurement tooltips cho page/object/corner/text/effects/distribute/cursor và guideline status lấy suffix/format từ đơn vị trang đang chọn. Giá trị góc bo chưa focus cũng được format trong cùng đơn vị; nhập vẫn qua parser chung và lưu canonical mm. Regression shell đổi sang inch kiểm tooltip khổ trang, corner, object, tọa độ status và guideline `25.4 mm → 1 in`; `test_pikdraw_desktop_shell` pass. Đây là harness verification, chưa phải native visual/focus walkthrough.

21/10 bổ sung: Layout > Page Display Units dùng tám semantic action ID `action.page.units.*`; các mục menu mm/cm/m/in/ft/yd/pt/pc dispatch tới cùng registry/handler, đổi metadata bằng command có Undo và giữ geometry canonical. Regression `test_layout_menu_unit_changes_only_display_metadata` đã chạy đủ tám route, kiểm action ID, scope trang, revision/no-op cùng đơn vị, kích thước vật lý và Undo. `test_pikdraw_units_settings_ui` cũng pass; native visual/focus walkthrough vẫn mở.

Liên kết: [kế hoạch tổng](../existing-feature-upgrade-20261020.md), [thứ tự thực thi](implementation-plan.md), [màu/schema](color-management-plan.md), [UI](../../design/features/editor-consistency-upgrade-20261020.md), [C1/C8/C9 và vector](../../architecture/vector-resource-interoperability.md).

21/10 bổ sung: Object Width/Height của một raster đơn nhận suffix `px` theo DPI nguồn riêng từng trục; EXIF orientation 5–8 đổi trục DPI khi orientation đã áp dụng. Thiếu DPI nguồn hoặc selection không phải một ảnh thì `px` bị từ chối, giữ buffer lỗi và không đổi document. Page display unit vẫn là đơn vị vật lý; không dùng OS DPI, zoom hay raster-effects DPI làm basis. `test_pikdraw_desktop_shell` pass cho nhập `100px`/`200px` theo 100/200 DPI, thiếu-DPI no-mutation và mapping trục EXIF. Pixel context của clipboard/automation, design px và mọi field khác còn mở.

21/10 bổ sung: Import review và recovery list hiển thị kích thước trang bằng formatter chung theo đơn vị hiển thị, không ghi cứng `mm`; dữ liệu vẫn canonical mm. `test_pikdraw_desktop_shell` pass sau khi đổi hai consumer. Native visual/focus walkthrough và các gate S04/S05 còn mở.

21/10 bổ sung: Dimension unit parser giữ nguyên ordinal mm/cm/m/in (0–3), nhận ft/yd/pt/pc, và từ chối enum sai hoặc phần số dư thay vì ép về một giá trị khác. Native v18 lưu `contour_spec` theo stable ObjectId, nhờ đó Dimension giữ unit kép/liên kết nguồn và Contour còn chỉnh sửa được sau save/reopen; v2–v17 giữ lối đọc cũ. `test_pikdraw_document_dimension_model`, `test_pikdraw_document_storage` và `test_pikdraw_document_storage_fidelity` pass 3/3, gồm Unit ft/pc, contour và file Dimension chứa unit ngoài phạm vi bị từ chối. Đây là lát cắt persistence của AC-UNIT02; semantic diff hình học toàn bộ tài liệu, clipboard, adapter formats và native UI walkthrough vẫn mở.

21/10 bổ sung: Dimension property bar cycle đủ mm/cm/m/in/ft/yd/pt/pc. Khi Dimension được chọn, thao tác đổi đơn vị lấy style từ object đang chọn rồi đồng bộ lại tool; hồi quy phát hiện và sửa trường hợp ribbon hiển thị một đơn vị nhưng click lại dùng style cũ của tool. Test xác nhận cả tám bước, stable ID, references/vị trí không đổi và một bước Undo/Redo; `test_pikdraw_units_settings_ui` pass 1/1. Vẫn còn mở kiểm tra trực tiếp visual/focus trên cửa sổ native và các field/format khác trong AC-UNIT06/10/11.

## 1. Kết luận khảo sát và quyết định

Nhận xét “chỉ hỗ trợ mm” đúng với phần lớn luồng nhập của shell hiện tại. Core đã hỗ trợ một số đơn vị nhưng các consumer chưa dùng chung:

| Nơi | Source hiện tại | Khoảng trống |
| --- | --- | --- |
| Numeric precision | `modules/vector/src/precision/numeric/numeric_input.cpp`: mm, cm, in, pt, deg, rad; expression, @/#, giới hạn token/depth | Bare length hiểu mm; thiếu m/ft/yd/pc; px chưa có context; ParseOptions chủ yếu decimal separator |
| Dimension | `core/document/include/pikdraw/document/annotations/dimension_style.hpp`: Millimeter/Centimeter/Meter/Inch, dual units, precision | Converter/formatter riêng; chưa đồng bộ page/field context; enum lưu file cần bảo toàn |
| Shell numeric | `apps/desktop/src/shell/native_window_numeric_edit.cpp`: scalar parsing và field mm | Không nhận đầy đủ suffix/expression như precision input; labels/range/step còn rời rạc |
| SVG | Adapter có mm/cm/in/pt/pc/px, CSS px theo 96/in | Không dùng unit tài liệu để diễn giải lại CSS px |
| DXF | UnitScale có inch/foot/mm/cm/m/yard theo INSUNITS | Cần admission/version và unitless policy, normalize đúng một lần |
| PDF/AI, EPS | Point theo 72/in; adapter riêng | PDF UserUnit/page box/rotation phải phối hợp plan PDF; không ép qua SVG chung |
| Raster | Kích thước vật lý từ pixels và source DPI X/Y, metadata có DPI | Tách DPI ảnh khỏi raster effects, document design px và OS DPI |

**Chốt:** geometry nguồn, transform, commands và automation `*_mm` tiếp tục dùng binary64/mm theo C1. Thêm đơn vị nhập/hiển thị; không nhân lại toàn bộ tọa độ khi chọn đơn vị khác. Giữ mm là default của file legacy không có metadata mới.

Contour default/minimum là **0,02 mm** theo người dùng; không đổi thành 0,005. Đơn vị UI chỉ đổi cách biểu diễn. C1 numerical epsilon, Boolean error budget, snap DIP và raster pixel là các đại lượng riêng.

## 2. Danh mục đơn vị và quy tắc số

| Đơn vị | ID ổn định đề xuất | Giá trị canonical | Phạm vi |
| --- | --- | --- | --- |
| mm | mm | 1 mm | Mặc định legacy, geometry, kỹ thuật |
| cm | cm | 10 mm | Layout, field chiều dài |
| m | m | 1.000 mm | Bảng hiệu, bản vẽ lớn |
| inch | in | 25,4 mm | Layout, kích thước quốc tế |
| foot | ft | 304,8 mm | Layout/Dimension, international foot |
| yard | yd | 914,4 mm | Layout/Dimension |
| point DTP | pt | 25,4/72 mm | Font size; length khi field cho phép |
| pica | pc | 25,4/6 mm | Typography/layout |
| pixel | px | 25,4 / DPI mm | Chỉ khi có loại pixel và DPI rõ ràng |

Quan hệ inch/mm/pt/pc và CSS pixel theo [W3C](https://www.w3.org/Style/Examples/007/units.en.html); inch = 25,4 mm chính xác theo [NIST](https://www.nist.gov/pml/owm/si-units-length). Foot ở đây là international foot; không tự diễn giải dữ liệu khảo sát US survey foot thành international foot, xem [NIST](https://www.nist.gov/pml/us-surveyfoot/revised-unit-conversion-factors). Khi format có code riêng cho survey foot, adapter phải nhận đúng hoặc báo unsupported.

1. Dùng hệ số rational/constants có nguồn thống nhất, convert ở boundary một lần; binary64 có sai số biểu diễn nên không hứa mọi vòng đổi có bit equality.
2. Parser giữ dimensional analysis: length + angle lỗi; length × scalar đúng; không nhận length × length vào field chiều dài. Giữ giới hạn độ dài input, token/depth và finite/range.
3. Bare number nhận đơn vị của **field đang focus**. Suffix rõ ràng ghi đè đơn vị field: field inch vẫn nhận `25.4mm`.
4. Giữ expression, @relative/#world, Cartesian/polar và deg/rad hiện có. Locale dấu phẩy thập phân cần separator tọa độ không nhập nhằng, ưu tiên dấu chấm phẩy và help mẫu theo locale.
5. Hỗ trợ `1/8in`, `1 1/2in`, `5ft+6in`. Dấu apostrophe/quote kiểu `5'6"` chỉ bật sau grammar/token tests và help; không được phá Unicode quote/text parser hoặc coi expression lỗi là 0.
6. Formatter không ghi ngược số đã làm tròn. Focus/blur không sửa source; tăng số chữ số hiển thị không thay precision geometry. Dimension rounding chỉ ảnh hưởng nhãn.
7. Degree/radian giữ converter góc riêng. Percent, counts, opacity, gradient offset và RGB channels không bị đổi bởi document length unit.

### Ngữ nghĩa pixel và DPI

- **CSS px:** luôn 96 px/in khi đọc SVG/CSS phù hợp spec; không đổi theo zoom, monitor hoặc document raster DPI.
- **Design px:** tùy chọn layout của tài liệu với `layout_pixel_dpi` explicit; mặc định 96 cho tài liệu mới chọn px. Đây là metadata vật lý, khác raster effects DPI.
- **Image pixel:** số mẫu ảnh; kích thước vật lý tính theo source DPI X/Y hoặc explicit placement. Ảnh DPI bất đẳng hướng phải dùng context trục; scalar conversion thiếu trục thì từ chối hoặc yêu cầu context rõ.
- **OS DPI/DIP:** chỉ layout UI/hit target; không làm đổi bản vẽ hoặc PDF/SVG export.
- Không dùng zoom làm DPI. Pixel field thiếu DPI hợp lệ báo lỗi; không lén lấy 300 của output preset hay 96 của màn hình.
- Đổi `layout_pixel_dpi` mặc định giữ mm geometry, chỉ đổi nhãn px. “Rescale artwork” nếu sản phẩm cần là lệnh khác có confirmation/Undo, ngoài yêu cầu đổi đơn vị.
- Font giữ DTP pt theo 72/in; nhập chiều dài ở font field có thể convert sang pt bằng service chung, không chuyển toàn bộ font sang mm UI.

## 3. Context và hợp đồng dùng chung

Đề xuất đặt registry/codec không phụ thuộc shell trong `core/geometry/include/pikdraw/geometry/units/` và `core/geometry/src/units/`, sau khi kiểm `registry/modules.json`. Expression parser hiện có ở vector tiếp tục làm parser biểu thức; tái dùng registry, không tạo parser thứ ba.

Hợp đồng tối thiểu:

~~~text
UnitId, QuantityKind {Length, Angle, Scalar}
UnitContext {display_length_unit, decimal_policy, pixel_basis?, dpi_x/y?,
             axis?, allowed_suffixes, formatting_precision}
parse_quantity(text, context) -> TypedQuantity | StructuredError
to_canonical(quantity, context) -> finite double | StructuredError
format_quantity(canonical, context) -> DisplayValue {text, suffix, exact_value}
~~~

Không lưu localized string làm UnitId. Không dùng một enum length để mã hóa angle/count. Public API vector cũ giữ wrapper với default mm, tránh đổi semantics khách hàng không truyền context.

Thứ tự authority: app preference cho tài liệu mới → document display unit → page override nếu người dùng bật → field override có ý nghĩa như font pt. View ruler override chỉ đổi view; không âm thầm đổi page/document metadata.

- Document/page unit đổi qua command, một Undo nếu lưu vào file.
- Ruler-only preference/view override không Undo document.
- Selected field vẫn bind target/session cũ; tab/page đổi không được reinterpret chuỗi đang nhập theo unit mới.
- `EditorContext` chứa UnitContext immutable; bar, Properties, ruler, status và precision input đọc cùng authority.
- Validation canonical trước khi format. Ví dụ Contour dưới 0,02 mm bị từ chối dù label inch đã làm tròn; tooltip có giá trị minimum theo unit hiện hành đủ chữ số.
- Step/nudge/grid có source physical value và display unit riêng; đổi unit không nhân step đã lưu lần nữa.

## 4. Các bước thực thi

### S00 — kiểm kê và khóa semantics trước migration

**Sở hữu:** registry/codec inventory, numeric tests hiện có, Dimension tests, docs/help liên quan. **Phụ thuộc:** U00.

1. Theo tất cả field có chiều dài: page, Transform, Node/handles/corners, Pen/Line, stroke/dash, Contour, effect sigma/offset, text frame, arrange, grid/nudge/ruler/status, Dimension và import/export dialogs.
2. Ghi field đang lưu mm/pt/pixel/count, command consumer, validator và formatter; không đoán chỉ từ label.
3. Đọc ordinal UnitType và reader/writer thực. Giữ 0..3 cho mm/cm/m/in của Dimension; ID mới có explicit encoding, reader reject unknown enum có reason.
4. Chốt fixtures legacy, Unicode/locale, API numeric mặc định mm, real-unit import. Không dựng baseline mới bằng cách đổi expectation để hợp code sai.

**Gate:** danh mục đủ các bề mặt đang có, AC-UNIT01/02 có fixture; không chỉnh geometry.

### S01 — converter và parser context

**Sở hữu:** geometry units, vector numeric header/source/tests, CMake đúng module. **Phụ thuộc:** S00; hoàn thành trước khi U02 migrate hết field.

1. Tạo registry và finite/overflow checks; tách format policy khỏi giá trị.
2. Cho parser nhận UnitContext optional, giữ API cũ mm; bổ sung suffix m/ft/yd/pc và px theo capability/context.
3. Bổ sung fraction/mixed fraction; ambiguity là lỗi có vị trí token, không partial parse.
4. Kiểm length/angle/scalar và range C1 sau đổi; input m/in lớn không vượt domain mm.
5. Thống nhất converter của Dimension/importer qua adapter nhỏ; tránh pull dependency vector vào core/document hoặc interop chỉ để đổi hệ số.

**Gate:** AC-UNIT01/03/04/05. Mở rộng `test_pikdraw_geometry`, `test_pikdraw_vector_numeric`.

### S02 — metadata, scope và migration

**Sở hữu:** document page/settings, commands, storage codec, Dimension style; phối hợp C01 màu và work PDF/storage đang dirty.

1. Chọn vị trí document defaults/page override, version/feature flags từ native reader/writer mới nhất. Source khảo sát là native v12; không hardcode v13 khi công việc khác đang thay schema.
2. Bổ sung explicit unit metadata và design pixel DPI có validation; chỉ một migration cho các thay đổi cùng đợt được chốt.
3. Legacy không metadata đọc là mm; Dimension units riêng vẫn giữ nguyên.
4. Unit-change command chỉ cập nhật metadata, không walk/scale object. Undo/Redo đúng page IDs và source pixels/geometry.
5. Save/reopen giữ overrides, dual-unit Dimension, file unknown/invalid units được report. Clipboard copy giữ source kích thước thật khi paste sang document khác unit.

**Gate:** AC-UNIT02/06/10/11; native semantic diff xác nhận không đổi geometry.

### S03 — shell và mọi field chiều dài

**Sở hữu:** U02 property bindings, numeric edit, controllers, rulers/grid/status, page/Dimension/text UI. **Phụ thuộc:** S01/S02 và U02; layout hoàn thiện theo U07.

1. Mỗi binding khai báo QuantityKind + canonical basis + allowed units + range/step + scope; shell bỏ scalar `stod` riêng sau khi consumer được migrate.
2. Migrate Transform/Page trước; tiếp Pen/Node/Shapes, Contour/Dimension, stroke/arrange, effects và text frame. Font size vẫn pt.
3. Bar và Properties dùng cùng parser, UnitContext và command; label suffix không ẩn khi cửa sổ hẹp.
4. Rulers/grid/status dùng converter chung; fit-page/zoom/hit/snap không bị nhân unit.
5. Đổi unit khi draft mở phải commit/cancel theo session policy; không reinterpret draft cũ. Tool gesture đang chạy khóa context unit đến kết thúc.
6. Contour default/floor 0,02 mm; legacy 0,005 lưu nguyên khi chỉ mở, hiển thị stored/effective diagnostic. Khi người dùng Apply/Edit mới, validator yêu cầu giá trị hợp lệ và command normalization có Undo; không migrate im lặng lúc deserialize.
7. Tránh clamp số hiển thị inch quá thô thành 0; cho precision thích ứng và exact value trong editor.

**Gate:** AC-UNIT06/07/08/09; native EN/VI và DPI UI thực.

### S04 — format, raster và automation

**Sở hữu:** interop từng adapter, capability/report, raster metadata, clipboard, automation boundaries. **Phụ thuộc:** S01/S02; giữ C8/C9.

| Định dạng/representation | Quy tắc normalize và metadata | View/edit/export/production |
| --- | --- | --- |
| SVG XML theo version/subset được adapter công bố | CSS units/viewBox/viewport; CSS px 96; % cần viewport context | Kiểm appearance và editable geometry riêng; giữ source units; export explicit size/viewBox; không suy round-trip chỉ từ render |
| DXF theo reader version, INSUNITS/unitless | Map unit code đúng; unitless có explicit import scale/policy, không đoán từ UI unit | Kiểm mm geometry, Dimension/entity metadata; export unit code/scale đúng; CNC gate riêng |
| PDF và AI có PDF representation | Point 72/in cùng UserUnit/page boxes/rotation theo adapter PDF | PDF view/editable subset/locked original tách biệt; original bytes không là geometry editable |
| AI không PDF-compatible | Adapter/worker capability riêng | Không nhận qua đổi extension; báo unsupported nếu chưa có reader |
| EPS/PostScript subset | Point 72/in, BoundingBox và transform qua worker | Có thể preview mà chưa editable; giữ loss report và kiểm lại output |
| CDR | Version/libcdr representation và normalization của adapter | SVG trung gian không bảo đảm giữ source units/effects; giữ provenance và loss report |
| Raster PNG/JPEG/TIFF… | Pixels + source DPI X/Y + orientation; missing DPI dùng policy có report | Không đổi pixel count khi đổi unit; output physical size/DPI và chất lượng ảnh kiểm riêng |
| Native/clipboard/MCP/COM | mm canonical + unit metadata; `*_mm` không đổi tên/nghĩa | Client cũ vẫn nhận mm; unit-aware API mới có typed unit/context, reject unknown |

Không đưa mọi format qua một converter. Hệ số thống nhất sau khi adapter đã giải mã semantics riêng. DPI import, layout px, effect rasterization và export output condition là bốn context khác nhau.

**Gate:** AC-UNIT10/11/12, fixture đơn vị hợp lệ + unitless/unknown; capability matrix ghi version/representation cụ thể từ registry thực.

### S05 — UI, nghiệm thu và tài liệu

**Sở hữu:** UI spec/help/catalog, tests hiện có, release evidence. **Phụ thuộc:** S00–S04/U07.

| Surface | Action/identity đề xuất, field | Hành vi/help |
| --- | --- | --- |
| Layout > Page Display Units | Semantic IDs `action.page.units.{millimeter,centimeter,meter,inch,foot,yard,point,pica}`; legacy menu actions `LayoutUnit*` là alias dispatch; active Page scope | Checkmark theo trang; đổi metadata bằng command/Undo, giữ kích thước mm; status báo đơn vị và kích thước thật không đổi |
| Settings > Preferences > Units & Snapping | Existing unit-choice buttons; active page plus saved default for new documents | Label nói rõ phạm vi hiện tại; suffix explicit override; không thay đổi hình học |
| Properties > Objects > Page units (khi không có selection) | Nhãn `Page units`; menu 8 đơn vị; lựa chọn hiện tại được đánh dấu | Đổi active-page display metadata qua cùng `LayoutUnit*` → `action.page.units.*`; Undo giữ nguyên page geometry |
| View > Rulers / ruler context menu | P: `action.view.ruler_units.focus`; `view.ruler_unit` | Use document units hoặc view override rõ; không Undo bản vẽ |
| Numeric bar/Properties | Binding semantic ID của U02, suffix button chung | Focus help có ví dụ `1in`, `25.4mm`, `5ft+6in`; giữ trạng thái Mixed/Error/Busy |
| Dimension > Style > Units | Giữ route hiện có; mở rộng enum/style qua shared codec | Primary/dual units, precision/rounding/suffix; không tác động kích thước nguồn |

Menu Layout và Settings dùng nhãn text, không có shortcut hoặc icon chuyên biệt; không thêm toolbox tool/toolbar Units (không áp dụng). Menu enum hiện tại dispatch qua semantic ActionDispatcher IDs và cùng active-page unit command. Các nút Settings hiện dùng cùng unit command nhưng cũng lưu lựa chọn làm mặc định tài liệu mới. Thước hiển thị suffix đơn vị ở góc; ruler labels follow active page. Properties của trang hiện hiển thị chiều rộng × chiều cao theo đơn vị hiện hành và có flyout 8 lựa chọn trong `Inspector > Properties > Objects` khi chưa chọn đối tượng; mỗi mục gọi lại action menu, giữ page scope và Undo. Nhãn/chevron native thay cho icon SVG (không áp dụng); tooltip nêu kích thước thật không đổi, ↑/↓ đổi mục, Enter áp dụng, Esc hoặc click ngoài đóng flyout; status xác nhận đơn vị sau khi chọn. Bài test shell xác nhận click, keyboard, action ID, Undo và hình học; UIA/focus walkthrough, property binding tổng quát, DPI layout và document-wide unit scope vẫn còn mở.

Usage: mở Page/Document Properties → chọn đơn vị → nhập số hoặc suffix rõ ở field → Enter commit → Undo chỉ khi đổi metadata/giá trị tài liệu. Mọi field nâng cao vẫn có Properties; bar chỉ chứa controls thường dùng.

## 5. Nghiệm thu

Các ca dưới đây vẫn là bộ nghiệm thu đầy đủ; regression Properties bổ sung chỉ đóng một lát cắt của AC-UNIT06/12.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-UNIT01 | Các đơn vị mm/cm/m/in/ft/yd/pt/pc, giá trị dương/âm/zero | Parse và convert hai chiều | Hệ số đúng; finite/range; sai số trong binary64 contract | Known rational fixtures ở geometry/numeric; không so rounded string |
| AC-UNIT02 | File legacy và geometry nhiều scale, native unit metadata mới | Đổi mm→in→m, Undo/Redo/save/reopen | Geometry/transform/IDs/source color không đổi; legacy mặc định mm | Document/storage semantic diff; geometry unchanged |
| AC-UNIT03 | Bare number, suffix khác field, expression/fraction, @/# | Nhập trên field và precision tool | Bare theo field; suffix explicit; cú pháp cũ còn chạy | Numeric parser + tool controller cùng expected mm |
| AC-UNIT04 | Locale EN/VI, comma decimal, tọa độ, malformed expression | Parse/format/focus/blur | Không ambiguity/partial parse; lỗi có vị trí; không mất precision | Parser limits + native IME/caret/focus |
| AC-UNIT05 | NaN/Inf/overflow/unknown unit và length+angle | Nhập/apply qua UI và automation | Reject trước writer, không mutation hoặc Undo | Boundary tests và revision check |
| AC-UNIT06 | Bar/Properties/ruler/status cùng document/page | Đổi unit, edit từ hai nơi, đổi page/tab | Cùng authority; override đúng scope; draft không sang target mới | Menu + Properties action/Undo + geometry regression đã pass; đồng bộ đơn vị hiển thị theo trang/tab đã pass; native numeric drafts now reject stale document/page/selection/tool context; các binding khác và phạm vi draft còn mở |
| AC-UNIT07 | Contour mới .02 mm và legacy .005, field inch/cm | Chỉ mở rồi edit/apply .02/.05/.1 | Mở không rewrite; minimum physical .02; dưới floor reject; Undo normalization rõ | `test_pikdraw_vector_contour` confirms .005 legacy load/render-floor/Apply normalization; `test_pikdraw_desktop_shell` now checks Properties in page-inch mode accepts explicit 0.02mm and rejects 0.005mm. Independent geometry oracle remains partial |
| AC-UNIT08 | Document design px, CSS px và ảnh DPI X/Y khác nhau | Đổi layout DPI, monitor DPI, zoom, effect DPI | Mỗi basis đúng; geometry mm giữ; thiếu context reject | Geometry/interop/raster + native 100–200% |
| AC-UNIT09 | Font pt, opacity %, count, Dimension dual units | Đổi document unit rồi chỉnh field | Font/count/% không bị convert sai; nhãn đo đúng | Typography/Dimension/drawing UI |
| AC-UNIT10 | SVG/DXF/PDF/EPS/CDR fixtures có units khác nhau | Import rồi export theo capability | Normalize một lần; metadata/loss report đúng; kích thước vật lý kiểm độc lập | Adapter/version fixtures; production gate riêng |
| AC-UNIT11 | Hai document units khác, clipboard/automation mm và save | Copy/paste, typed requests, reopen | Kích thước thật giữ; client cũ vẫn mm; không double conversion | Desktop shell kiểm native clipboard snapshot từ page mm sang document inch và so bounds canonical mm của rectangle/ellipse; storage/resource variants, automation RPC và typed-unit API còn mở |
| AC-UNIT12 | Cửa sổ hẹp, Properties đóng, keyboard và EN/VI | Mở Units, nhập inch/foot, sửa lỗi | Labels/suffix/help/Apply reachable; semantic state đúng | Native canonical app + focus/UIA; không suy từ screenshot |

**Bằng chứng triển khai 21/10/2026:** page/document command test bao gồm phạm vi trang/tài liệu, Undo, reject Pixel và giữ nguyên width_mm; native storage roundtrip từng đơn vị trang và v12 migration về mm implicit; shell test kiểm tra menu Layout, Properties flyout qua chuột và phím ↑/↓/Enter/Esc, semantic action ID, Undo và geometry canonical. `test_page_units_follow_active_page_and_document_tabs` xác nhận đơn vị theo trang qua Next/Previous, theo tab khi switch và sau khi đóng tab (inch/cm/ft); `test_pikdraw_desktop_shell` pass 1/1. `test_copy_cut_paste_ecosystem_and_array_sessions` mở rộng kiểm native clipboard snapshot copy từ page mm sang document inch; paste giữ bounds mm của cả hai đối tượng, không reinterpret theo đơn vị đích. Automation typed-unit RPC đã phủ rectangle, ellipse, text, polyline, transform, duplicate và stroke width; desktop `document.create` nhận `page_width/page_height + length_unit`, kiểm canonical 10–20.000 mm và trả kích thước mm. `test_pikdraw_mcp_pipe_integration` xác nhận đủ mm/cm/m/in/ft/yd/pt/pc và từ chối title sai kiểu mà không mở tab; cùng `test_pikdraw_automation_rpc` và `test_pikdraw_mcp_server` pass 3/3 sau thay đổi này. Clipboard resource variants, cross-field bindings/draft scope, pixel/DPI page mode, import/export matrix, UIA walkthrough và high-scale ruler review còn mở.

**Bổ sung 21/10/2026:** commit của native numeric field xác nhận lại document/root, active page, revision, view/tab generation, tool và selection; Contour target và Gradient stop cũng phải giữ nguyên. Chuyển trang qua Layout hủy draft; đổi tab hoặc đổi trang/vùng chọn qua state khác thì draft cũ bị từ chối trước writer. `test_numeric_drafts_reject_stale_page_and_selection_context` kiểm tra page ID đổi, selection đổi và đổi tab; `test_pikdraw_desktop_shell` pass 1/1. Đây chưa phải bằng chứng cho mọi control trong Properties và bar; các binding khác vẫn mở.

~~~powershell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_interop_raster_image_writer,test_pikdraw_document_commands,test_pikdraw_document_storage,test_pikdraw_document_storage_fidelity -TestRegex '^(test_pikdraw_desktop_shell|test_pikdraw_interop_raster_image_writer|test_pikdraw_document_commands|test_pikdraw_document_storage|test_pikdraw_document_storage_fidelity)$'
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell -TestRegex '^test_pikdraw_desktop_shell$'
~~~

Lệnh đầu ban đầu đạt 4/5: document, storage, ICC writer pass; shell chỉ fail vì assertion test đòi `0.5` ở tick step 1.0. Sau khi đổi fixture sang major step 0.5, lệnh shell thứ hai pass 1/1. Chưa có native UIA walkthrough, pixel/DPI page mode, clipboard/RPC, import/export format matrix hoặc high-scale visual ruler review.

Targets mở rộng theo phần sửa: `test_pikdraw_geometry`, `test_pikdraw_vector_numeric`, `test_pikdraw_document_dimension_model`, `test_pikdraw_document_dimension_commands`, `test_pikdraw_dimension_tool_controller`, `test_pikdraw_drawing_tool_ui`, `test_pikdraw_interop_svg_adapter`, `test_pikdraw_interop_dxf_adapter`, `test_pikdraw_automation_rpc`. Đọc CMake mới nhất trước chạy; dùng CTest regex đúng tên đã đăng ký, không suy tên test từ target.

Ví dụ nhóm nền đầu tiên:

~~~powershell
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_geometry,test_pikdraw_vector_numeric -TestRegex '^(pikdraw_geometry_tests|test_pikdraw_vector_numeric)$'
~~~

Release U12 chỉ đóng khi toàn bộ field trong S00 được migrate/kiểm hoặc có giới hạn explicit được giữ; không đánh dấu hoàn tất chỉ vì dropdown mm/in hoạt động. Không tuyên bố độ chính xác CNC/đo vật lý từ unit conversion tests.

