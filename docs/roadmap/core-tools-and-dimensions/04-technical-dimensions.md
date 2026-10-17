# Plan 04 — Technical Dimensions: annotation liên kết và styles

Ngày: 17/10/2026. Trạng thái: **Planned**; chưa có DimensionObject hoặc DXF writer trong baseline đã đọc.
Đọc [master](../master-implementation-plan-core-tools-and-dimensions.md); mã gói `D`, thay phần Dimensions
của master cũ và D20 trong catalog drawing-tools. Đây là annotation engine, không phải driving sketch solver.

## 1. Kết quả và các mốc bàn giao riêng

Người dùng đo geometry nguồn, đặt annotation có style, sửa source thì số đo theo đúng liên kết,
đổi text/arrow placement qua grips/Properties, Undo và lưu/mở lại giữ cả số liệu lẫn refs.
Kiểu nét/chữ, đơn vị, dung sai và xuất format được nhận bằng từng gate, không gọi toàn bộ engine hoàn tất từ một screenshot.

| Mốc | Scope bắt buộc và điều kiện | Không được suy rộng |
| --- | --- | --- |
| DIM-native-basic / D00–D04 | Horizontal/Vertical/Aligned, Angular hai line, Radius/Diameter của circle thật; Smart dispatch các loại đã có; style cơ bản; refs/Undo/native codec/render/Properties | Chưa đủ 8 tools hoặc CAD interoperability |
| DIM-native-full / D05 | Arc Length circular arc, Ordinate từ datum, Multi-Leader, advanced styles/grips/relink; đầy đủ 8 flyout entries theo bảng bên dưới | Không có solver, GD&T, paper-space/annotative scale hoặc chứng nhận ISO/DIN |
| DIM-SVG / D06 | Explicit appearance export qua writer subset sau loss report, giữ native gốc | SVG group không còn associative dimension trong Illustrator/Corel |
| DIM-DXF-entity / D07a | Writer profile cố định cho source geometry + dimension entities/styles/picture, mở đúng trong AutoCAD test build | Có entity DIMENSION chưa chứng minh liên kết khi sửa geometry |
| DIM-DXF-associative / D07b | Source handles + DIMASSOC/objects/refs theo target profile; source-app edit/save/reopen | Không suy AutoCAD pass sang SolidWorks/MasterCAM/version khác |
| DIM-PDF / D08 | Candidate writer tách riêng; chỉ lên lịch sau khi có PDF export substrate, font/color policy và fixture | PDF hiện chưa hỗ trợ; không phải dependency chặn bản native |

8 entries = Smart, Linear, Aligned, Angular, Radius & Diameter, Arc Length, Ordinate, Multi-Leader.
Smart là dispatcher theo source eligible, không phải kiểu geometry/constraint mới.
Đo perimeter/area là measurement service mở rộng, không tự thêm từ D20 nếu chưa có scope/holes/integral oracle.

## 2. Tham khảo chính thức và các kết luận

| Nguồn đã tra | Điều liên quan và quyết định PikDraw |
| --- | --- |
| [Adobe Dimension objects](https://helpx.adobe.com/illustrator/desktop/measure-and-align/plot-and-measure/about-dimension-objects.html) và [tool settings](https://helpx.adobe.com/illustrator/desktop/measure-and-align/plot-and-measure/dimension-tool-options-and-settings.html) | Dimension gồm đường đo, gióng và chữ; phân biệt defaults công cụ với Properties của annotation đã tạo |
| [Adobe Linear Dimensions](https://helpx.adobe.com/illustrator/desktop/measure-and-align/plot-and-measure/plot-linear-dimensions-of-objects.html) | Aligned/H/V khác nhau; tài liệu có giới hạn automatic update khi distort, nên không mặc định mọi vendor liên kết như nhau |
| [CorelDRAW Drawing dimension lines, bản help 2018](https://product.corel.com/help/CorelDRAW/540227992/Versions/EN/Documentation/CorelDRAW-Drawing-dimension-lines.html) | Tham khảo legacy cho property bar, unit/precision/extension và dynamic text; không gọi nguồn này là kiểm chứng CorelDRAW 2026 |
| [AutoCAD 2026 Associative Dimensions](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-Core/files/GUID-8B7E8C96-C30D-409E-881E-7942871E80DA.htm) | Liên kết, liên kết một phần và mất liên kết là các trạng thái phải xử lý; PikDraw cần relink và broken-reference UX |
| [AutoCAD 2026 Primary Units](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-Core/files/GUID-2D1177AB-3130-4522-A631-AA28493529DC.htm) | Format, digits, rounding, prefix/suffix/zero suppression độc lập; không đổi model bằng làm tròn text |
| [DXF Common Dimension Codes 2026](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-DXF/files/GUID-EDD54EAC-A339-4EBA-AEA6-EC8066505E2B.htm) | Có type, style, definition points, picture block và WCS/OCS; không xuất một tag DIMENSION rồi coi xong |
| [DXF DIMASSOC 2020](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-DXF/files/GUID-C0B96256-A911-4B4D-85E6-EB4AF2C91E27.htm) | Association là object/reference data riêng, cần geometry handles và semantic point refs; header flag không tạo liên kết thay cho graph |

Các nhóm style/range/default dưới đây là đề xuất PikDraw. Không đặt tên preset `Standard_ISO`/`Mechanical_DIN`
hay gọi box text là GD&T khi chưa có tiêu chuẩn, font và oracle phù hợp.

## 3. Baseline, dependency và kiến trúc đề xuất

Đọc core document/geometry/typography/rendering và modules production/interop AGENTS/README, C1/C2/C3/C4/C5/C8/C9.
`ObjectVariant` hiện có Path/Text/Group/Image, chưa Dimension. Native writer hiện hữu có version riêng;
không chọn số version mới trước khi kiểm snapshot có clipboard/effects migrations của task khác.
`modules/production` hiện là planned module, chưa có CMakeLists trong audit; phải dựng target/registry có scope nếu sử dụng.

### 3.1. Phân trách nhiệm không đảo dependency

- `core/geometry`: math đo nguồn và construction basis, không biết ObjectId/document/UI/vendor parser.
- `core/document`: typed Dimension/Style/SourceReference/commands và read-only resolver/evaluator/layout facade;
  giữ source data/refs/Undo/schema. Document hiện được phép phụ thuộc geometry và typography theo registry.
- Layout facade là hàm thuần trên snapshot+resolved font metrics, trả primitives trung lập (paths/text runs,
  glyph placement/bounds), không HDC, GPU handle, dialog hoặc cached pixel. Vị trí này là đề xuất D00 cần ADR/consumer review;
  nếu đổi sang module chung khác phải sửa registry trước, không tạo dependency vòng bằng cách để renderer gọi production.
- `core/typography`: resolve/shaping/metrics/glyph outline theo API hiện hữu, không phụ thuộc document.
- `core/rendering`: vẽ DimensionLayout, culling/hit-test/text masking; không tự sửa số/source reference.
- `modules/production`: validation/measurement workflow nếu cần, đọc APIs document/geometry, không sở hữu renderer.
- `modules/interop`: adapter riêng từng format/version đọc canonical data/layout và tạo output/report;
  không phụ thuộc `apps/desktop`, không gọi Win32 screenshot để xuất PDF/SVG.
- Desktop: tool session/picking/grips/Properties/style manager qua command. Automation/clipboard consumers
  của ObjectVariant phải được inventory/update bằng task có lease riêng; không sửa task đang chạy.

D00 chốt ADR/schema và **inventory mọi visitor**: object type, validation, clone/duplicate/group, snapshots/bounds,
renderer/hit-test/culling, native codec/migration, interop reject/export policy, automation capability/schema và tests.
Không thêm variant rồi để default visitor trả bounds rỗng hoặc silently skip annotation khi save/export.
Renderer không phụ thuộc vector/production; document không phụ thuộc ngược những modules đó.

### 3.2. Model đề xuất

```text
DimensionObject: ObjectId, schema, kind, references[], placement, style_id, typed overrides,
                 layer/order/visibility/lock, user note/template, provenance where present
DimensionStyle: stable StyleId, name, version, LinesArrows/Text/Fit/Primary/Dual/Tolerance data
SourceReference: object_id, subpath_id?, element_id?, selector, parameter/interval?, branch?,
                 secondary_source? (intersection), last_resolved_fallback, reference_policy
Evaluation: valid/partial/broken/unsupported + raw_mm_or_rad + resolved_points + diagnostics
DimensionLayout: neutral paths/text runs/bounds/grip points + snapshot/style/font identity
```

Selectors: node anchor, segment endpoint/t parameter/span, line direction, analytic center/circle radius,
intersection pair và datum axes; đợt đầu chỉ selectors đã có resolver. Không lưu pointer, array index hoặc
text đã format làm authority. Fallback chỉ để hiển thị annotation mất liên kết, không là số đo current đã chứng nhận.
Annotations không tham chiếu annotation/derived contour trong đợt đầu; graph depth hữu hạn, cycle reject.
Explicit world points là non-associative ref hợp lệ và được nhận diện trong Properties.

## 4. Hành vi đo và tương tác

### 4.1. Công thức và điều kiện

Hình học đã qua composite transform về world mm; basis 2D orthonormal O,u,v theo C1.
Không đo screen pixels hoặc effects/render bounds. Đợt đầu world frame; Ordinate dùng datum 2D persisted riêng,
không kéo theo UCS 3D hay machine origin/G-code.

| Tool / proposed action | Definition và workflow | Unsupported/degenerate |
| --- | --- | --- |
| Smart / `action.tool.dimension.smart` | Hover source line→Aligned, analytic circle→Diameter, circular arc→Radius; chọn hai line→Angular. Marker và preview luôn cho biết loại trước click đặt | Không đo radius trên ellipse/quadratic/cubic; không đo chord rồi gắn nhãn curve length |
| Linear / `.linear` | A→B→placement; H=abs((B−A)·u), V=abs((B−A)·v); chọn axis explicit hoặc preview từ placement rồi khóa trước commit | A=B reject; projection=0 không tạo zero fake annotation |
| Aligned / `.aligned` | A→B→offset; L=hypot(dx,dy); signed placement giữ phía đường đo | Zero length reject; bbox width không thay độ dài cạnh |
| Angular / `.angular` | Hai finite nonzero source lines, định hướng hai rays và sector qua placement; atan2(cross,dot), normalize theo sector chọn | Parallel/collinear/zero rays có outcome rõ; reflex angle là lựa chọn có preview, không tự đổi 90↔270 |
| Radius & Diameter / `.radial` | Analytic circle/circular arc sau world transform; R theo basis circle, Diameter=2R, chọn kiểu R/Ø | Nonuniform scale/shear ra ellipse → invalid radius dimension, không lấy major axis làm R |
| Arc Length / `.arc_length` | Circular arc L=R·abs(sweep), phần cung và signed traversal giữ metadata | Ellipse/Bézier length không tự map cùng kiểu; generic curve length cần task/profile riêng |
| Ordinate / `.ordinate` | Datum ID+origin/basis, feature point P và axis X/Y; value signed (P−O)·axis; leader placement riêng | Camera pan/zoom không đổi datum; không đồng nhất coordinates bản vẽ với tọa độ máy |
| Multi-Leader / `.leader` | Arrow point(s)→elbow/landing→Unicode note; annotations riêng kind, không có measurement giả | New branch count/text giới hạn; không ép vào DIMENSION type code |

Numeric/draft: click hoặc snap nguồn; local preview tới placement; Enter/last click tạo một command,
Esc hủy step trước rồi tool draft, Undo local point trước document history khi session còn draft.
Pan/zoom/focus không làm mất confirmed source refs. Tab/Shift+Tab duyệt field; IME/text edit không bị canvas shortcuts chiếm.
Source đổi giữa draft và commit → strict reject/restart từ source mới, không tự đo lại rồi ghi lên revision khác.

Smart state cụ thể: một click source eligible khóa kind rồi vào placement; click placement luôn đặt annotation,
không âm thầm đổi sang angle chỉ vì con trỏ đi qua cạnh thứ hai. Muốn đo hai cạnh trong Smart, chọn hai line trước
khi gọi tool hoặc dùng `Add reference / Thêm tham chiếu` (`action.dimension.add_reference`) ở phase source-pick;
pick line thứ hai rồi mới vào angular placement. Chọn kind từ flyout cũng chuyển rõ về source-pick của kind đó.
Không dùng cùng một click vừa chọn cạnh thứ hai vừa commit đường đo.

### 4.2. Associativity, topology và Undo

- Move/style/transform giữ IDs thì resolver đo lại từ snapshot hiện tại; camera-only không đổi raw value.
  Sửa geometry và những canonical reference remaps cần thiết nằm trong **cùng writer transaction**, không tạo Undo thứ hai từ UI observer.
- Thay đổi từ Node, Transform, automation hoặc import replacement phải qua cùng reference-update/validation path;
  không chỉ nghe mouse-up. Cache invalidation không thay thế cập nhật ref schema.
- N01 topology map phải gồm intervals/orientation. Split giữ leading segment ID **không có nghĩa ref cả cạnh chỉ còn nửa đầu**:
  SpanRef có thể map sang chuỗi segments nếu selector hỗ trợ; nếu không đủ bằng chứng thì Broken, không đổi số đo ngầm.
  Ref t map về subinterval chứa t; reverse map 1−t và swap directed endpoints. Param exact split boundary có rule chọn NodeId mới.
- Node merge/delete/convert loại khác nghĩa: relink khi semantic mapping duy nhất, còn lại Partial/Broken với reason.
  Không tìm node gần để đoán ID. Missing source giữ fallback có marker/tên mất liên kết; Properties hiện số raw last-known riêng,
  không cho export profile yêu cầu current measurement coi fallback là valid. Undo deletion khôi phục đúng refs/IDs.
- Relink là explicit command: chọn endpoint/segment/source mới, preview số mới và Apply; detach tạo fixed world refs rõ,
  không xóa annotation mất nguồn tự động. Delete annotation không xóa source.
- Duplicate source+annotations trong cùng batch remap links nội bộ sang IDs mới; duplicate annotation riêng vẫn đo source cũ.
  Clipboard/cross-document remap/reject policy cần consumer task, không giữ dangling IDs trùng tình cờ trong tài liệu đích.
- Group/ungroup và composite transform phải không double-transform measurement/placement.
  Di chuyển riêng annotation sửa placement, không dịch nguồn; scale object source đo world mới nhưng style size không scale kép.

### 4.3. Grips và layout

Grips riêng cho hai definition refs, offset đường đo, text placement và arrow flip. Drag definition grip chọn lại source,
drag text không thay raw measurement; arrow click đi qua action flip có Undo. Double-click text mở note/template editor,
không nhập một số để thay kích thước hình. Full text override chỉ D05, flagged trong Properties và raw measurement vẫn đọc được.

Text layout dùng font identity/metrics/glyph thật cho `Ø`, `°`, `±`, decimal và tiếng Việt; missing font/substitution
có diagnostic. Text height default là **em size mm** theo typography API, không tự gọi cap height/ISO standard.
Best Fit dùng shaped text bounds + arrow extents + gaps; deterministic inside/outside/leader policy.
Đề xuất inside khi usable span ≥ text extent + 2 arrow sizes + 2 gaps; ngắn hơn theo fit enum,
không cắt chữ hoặc auto giảm font. Text luôn đọc được, angle policy tránh upside-down bằng quy tắc rõ trong D02.
Background mask chỉ che vùng text layout, giữ alpha/order đúng C9; không vẽ rectangle màu canvas cố định để giả mask.

Source measure/layout/paint phải cùng revision. Cache key epoch/revision, dimension/style/font metrics generation,
placement/overrides, layout algorithm; camera chỉ thêm vào cache raster/hit projection. Async layout cũ bị bỏ.
Thay global style invalidate users theo StyleId; overrides giữ nguyên; một style edit transaction, không 1 Undo mỗi annotation.
Không O(all objects × mouse events); dependency reverse-index được đo và rebuild từ snapshot, không authority persisted.

## 5. Style: sáu nhóm đủ tham số, scope và defaults

Preset ban đầu `PikDraw mm`. Document style data persisted; defaults cho tool mới tách khỏi override của selection.
Tạo/chỉnh/xóa style là command. Không xóa style đang dùng trừ explicit replace-all; clone style cấp ID mới;
rename không đổi ID. Multi-selection hiện Mixed/inherited/override thực, không lấy object đầu.

Tất cả scalar finite và bị giới hạn theo C1/typography/output admission. Bảng là schema đề xuất D00 chốt version.

| Field ID / EN — VI | Kiểu/range/default đề xuất | Scope / enable / thời điểm commit |
| --- | --- | --- |
| `dim.style` / Style — Kiểu | StyleId; default PikDraw mm | Tool default hoặc selected annotations được ghi nhãn; Apply style là một command |
| `dim.line_width` / Line width — Nét đo | mm >0..10; 0.25 | Style/override; preview rồi Apply; width nguồn không đổi khi zoom |
| `dim.ext_width` / Extension width — Nét gióng | mm >0..10; 0.25 | Style/override; cùng renderer width semantics |
| `dim.ext_offset` / Extension gap — Khe gióng | mm 0..1000; 1.5 | Style/override; không dời definition point |
| `dim.ext_overhang` / Overhang — Nhô gióng | mm 0..1000; 2 | Style/override |
| `dim.line_color/ext_color/text_color` / Colors — Màu | Source typed color; black | Style/override; giữ color/profile metadata, không ép CMYK sang RGB |
| `dim.arrow_type` / Arrow — Mũi tên | ClosedFilled/ArchitecturalTick/Dot; ClosedFilled | Style/override; layout extent riêng theo type, D05 cho types mở rộng |
| `dim.arrow_size` / Arrow size — Cỡ mũi tên | mm >0..1000; 2.5 | Style/override; no auto fabrication interpretation |
| `dim.flip_first/second` / Flip arrows — Lật mũi tên | bool false | Selected annotation override, grips cùng command |
| `dim.font` / Font — Phông chữ | Resolved FontIdentity; font hệ thống có glyph cần thiết | Style/override; không bắt tải Plus Jakarta Sans; substitution phải báo |
| `dim.text_size` / Text size — Cỡ chữ | em mm >0..1000; 3 | Style/override; computed metrics, không tuyên bố ISO |
| `dim.text_position` / Text position — Vị trí chữ | Above/Inline; Above | Style/override; inline gap theo text thật |
| `dim.text_alignment` / Text alignment — Hướng chữ | Aligned/Horizontal; Aligned | Style/override; readable-angle policy |
| `dim.text_gap` / Text gap — Khe chữ | mm 0..1000; 0.75 | Style/override; Fit group |
| `dim.text_mask` / Text mask — Nền che chữ | bool true | Style/override; alpha/clipping verified D02 |
| `dim.basic_frame` / Text frame — Khung chữ | bool false | Style/override D05; chỉ box, không GD&T |
| `dim.fit` / Fit — Bố trí | BestFit/ArrowsFirst/TextFirst/BothOutside; BestFit | Style/override; advanced modes D05 |
| `dim.baseline_spacing` / Baseline gap — Khoảng tầng | mm >0..1000; 8 | Document style; baseline/continue tool chỉ bật khi D05 có handler |
| `dim.units` / Units — Đơn vị | mm/cm/m/inch; mm | Style/override; convert từ raw mm, source không đổi |
| `dim.decimals` / Precision — Số lẻ | integer 0..6; 2 | Style/override, độ chính xác text không là tolerance hình học |
| `dim.round_increment` / Round to — Làm tròn | mm ≥0..1000; 0=digits only | D05; round step trước format digits, không đổi raw source |
| `dim.decimal_separator` / Decimal — Dấu thập phân | Dot/Comma; Dot | Document style, không đổi theo UI language tự động |
| `dim.prefix/suffix` / Prefix, Suffix — Tiền, Hậu tố | Unicode ≤256 code points mỗi field; empty | Style/override; unit symbol là field riêng, tránh lặp mm/Ø/R |
| `dim.show_unit` / Show unit — Hiện đơn vị | bool true | Style/override; Radius/Ø glyph tự theo kind, user prefix không chèn double |
| `dim.zeros.leading/trailing` / Zero display — Số 0 | bool false/true cho suppression | Style/override; decimal handling/−0 normalize trong formatter |
| `dim.angular_units/decimals` / Angle — Góc | Deg/Rad; Deg, decimals 0..6 default 2 | Style/override angular only; raw radians |
| `dim.dual.enabled/units/decimals` / Dual units — Đơn vị kép | false/inch/2; units cùng whitelist | D05, convert raw mm độc lập; factor suy từ unit, không multiplier tùy ý |
| `dim.tol.mode` / Tolerance — Dung sai | None/Symmetric/Deviation; None | D05, style/override; annotation only, không geometry tolerance |
| `dim.tol.magnitude` / ± — Đối xứng | ≥0 finite; 0.05 mm khi bật | Symmetric only; angular có unit riêng, không dùng mm cho góc |
| `dim.tol.upper/lower` / Upper, Lower — Trên, Dưới | Signed finite, lower≤upper; +0.05/−0.05 mm | Deviation only; length nominal+lower không âm; validate theo kind |
| `dim.tol.text_ratio` / Tolerance size — Cỡ dung sai | >0..1; 0.7 | D05, layout/shaping thật |
| `dim.datum/axis` / Datum, Axis — Mốc, Trục | DatumId, X/Y; world origin/X ban đầu | Ordinate only; document command, không đổi machine settings |
| `dim.note/template` / Note — Ghi chú | Unicode bounded 4096 code points, measurement token `<>` khi dùng | Selection; double-click/grip cùng editor; full override D05 có state explicit |
| `dim.association` / Association — Liên kết | Read-only Valid/Partial/Broken/Fixed, refs và raw value | Properties > References; Relink/Detach là lệnh khác |

Rounding policy đề xuất: nearest, ties away from zero, dùng cùng formatter cho UI/layout/export và independent fixtures;
không cộng một epsilon tùy tiện. Inch = **25.4 mm chính xác** theo unit conversion, không dùng hệ số cắt ngắn 0.0393700787.
Với 25.4 mm, dual phải ra 1 in trước format; cả tolerance và angular conversion dùng đúng dimension type.
Style scale đợt đầu 1:1; model measurement factor luôn 1. Annotative/paper-space/viewports/alternate arbitrary scale
chờ schema/output/task riêng, không thay số đo để tạo bản vẽ tỷ lệ.

## 6. Phiếu UI và thao tác chính xác

Menu chính `Tools > Dimensions > Smart / Linear / Aligned / Angular / Radius & Diameter / Arc Length / Ordinate / Multi-Leader`.
IDs `action.tool.dimension.*` theo bảng; `Tools > Dimensions > Dimension Styles…` → `action.dimension.styles.focus`.
Toolbox: nhóm Dimensions dùng native flyout pattern, đặt gần measurement tools theo toolbox hiện tại,
không áp cứng Slot 8 hay tái đánh số Tool 8 đang là Pen trong source. Standard toolbar: không thêm button.
`Object > Properties` / named `Dimension properties` → `action.dimension.properties.focus`, focus đúng section.
Relink/Detach và Flip Arrows là actions riêng dưới `Object > Dimensions > …`, shared commands với grips.

Shortcut D **đề xuất** cho Smart sau audit actual registry/WM handlers và focus tests; không cấp Ctrl+Shift+D cho Styles
vì native menu đang có Step and Repeat. Styles luôn mở bằng menu/Properties. Không gán phím tắt vendor khác để lấp ô.

Property bar: kind, Style, unit/precision và Flip Arrows khi phù hợp. Prefix/suffix, dual/tolerance nâng cao trong
Properties; cả sáu nhóm style đã hỗ trợ phải chỉnh được ở **Properties > Dimensions**, không chỉ trong dialog.
Style manager là view quản lý danh sách/style usage trong cùng hệ dock/dialog khi cần; nó dùng cùng value model,
không là nơi duy nhất chỉnh tham số phụ. Section References hiện link state, raw value và Relink/Detach.
Draft/selection/style-default scopes được ghi nhãn riêng. Enter hoặc Apply commit một transaction; mở panel/đổi tab không Undo.

| Context | Tool/bar/Properties và state |
| --- | --- |
| Không document | Author tool disabled; không giả Standard style document. App preset chọn cho new document nếu có capability riêng |
| Document, không selection | New dimension defaults; không sửa annotation đã tồn tại |
| Draft đang chọn điểm/placement | Hiện active kind/source/step; preview ngoài document; lỗi input không mất confirmed refs |
| Một Dimension / nhiều Dimensions | Tất cả supported groups + Mixed/inherited/override; batch Apply atomic; không chỉ sửa object đầu |
| Source locked nhưng visible | Có thể read-only measurement nếu target policy cho; không kéo source bằng dimension grip |
| Annotation locked/reference-only/unsupported source | Disable mutation với lý do; cho inspect raw/link state khi có |
| Broken refs | Hiện trạng thái mất liên kết, fallback phân biệt current value; Relink/Detach truy cập được |
| Busy/numeric/IME/focus/tab đổi | Không chiếm text shortcuts; focus Properties giữ draft; tab/document đổi hủy transient job/draft, không commit |

SVG đề xuất `dimension-smart.svg`, `dimension-linear.svg`, `dimension-aligned.svg`, `dimension-angular.svg`,
`dimension-radius.svg`, `dimension-arc.svg`, `dimension-ordinate.svg`, `leader.svg`, `dimension-style.svg`;
asset registry/viewBox/theme/license theo hệ hiện tại, không emoji/glyph một chữ thay icon tool.
Message IDs `ui.dimension.*`; label EN/VI ví dụ `Aligned/Kích thước xiên`, `Styles/Kiểu đo`, `Relink/Liên kết lại`.
Tooltip/focus: `Aligned Dimension — Measure between two points / Kích thước xiên — Đo giữa hai điểm`.
Hints theo phase: `Pick first point / Chọn điểm đầu` → `Pick second point / Chọn điểm cuối`
→ `Place dimension; Esc cancels / Đặt đường đo; Esc hủy`. Broken-ref/error có text+shape, không chỉ màu.

Help `Help > Dimensions`, `action.help.dimensions`: chọn kind → lấy source points → đặt offset/text → sửa style/Properties
→ đổi source, Undo và kiểm link; nhấn mạnh text không điều khiển geometry và export từng profile.
Tab order, focus ring, keyboard flyout/menu, accessible field names, EN/VI/DPI/themes/narrow window kiểm theo master.

## 7. Task, owned paths và gate bắt buộc

Tất cả **planned**; shared schema/CMake/visitors/shell/interop matrix không được claim chồng task khác.
Một task có thể chia nhỏ hơn sau inventory, nhưng không bỏ consumer/migration gates.

| Task / phụ thuộc | Owned paths candidates | Deliverables/gate |
| --- | --- | --- |
| D00 / baseline; phối hợp N01 | Plan này; C1/C2/C3/C4/C5 delta đúng phạm vi; ADR mới; new annotation reference vectors | Schema/refs/layout/dependency ADR, visitor inventory, topology mapping contract, budgets/version policy review |
| D01 / D00 | New `core/document/include/pikdraw/document/annotations/`, `src/annotations/`, model/commands/codec/tests; shared object/validation/clone/snapshot/storage/CMake tuần tự với C02 | Dimension/Style commands + resolver, atomic reference updates, bounded native migration, unknown type rejection; target đề xuất `test_pikdraw_document_dimensions` |
| D02 / D01 | Geometry measurement helpers/tests, annotation layout facade, typography helpers chỉ cần thiết; renderer annotation/hit/culling/tests/CMake | Neutral layout + real glyphs + style basic, raw math/units/arrow/text masks, no dependency reverse |
| D03 / D02 | New `apps/desktop/src/tools/dimension_tool_controller.*`; native menu/actions/toolbox/properties/resources/localization/tests/CMake | Basic author/pick/style/grips/save/Undo, native Properties/help; shared shell tích hợp tuần tự |
| D04 / D01+N01/N03 mapping, D03 flow | Annotation resolver/commands/reference tests, exact writer/source command integration points và clone/consumer deltas sau lease | Move/split/reverse/merge/delete/undo/group/duplicate/automation parity; không cần chờ toàn Contour hoặc advanced snap |
| D05 / D03+D04; S03/S04 cho advanced pick | Dimension model/layout/controller/Properties/tests; production target/registry chỉ khi có scope cụ thể | Arc/Ordinate/Multi-Leader, six style groups, grips/relink/dual/tolerance/baseline; unsupported picks disable |
| D06 / DIM-native-full | Exact `modules/interop` SVG writer/loss report/tests/matrix và export workflow consumer | Explicit vector appearance export; native semantics loss visible; source/destination preservation |
| D07a / D05 | New DXF writer header/source/tests under interop; capability matrix/CMake; export workflow; source-app fixture report | Pin target R2018/AC1032 candidate + exact app build; DIMENSION/style/picture/source geometry, current association status reported |
| D07b / D07a+N/S mappings | DXF writer refs/OBJECTS/handles tests; approved source-app fixture evidence | Association graph round-trip + source edit test trong AutoCAD; không nhận từ entity count |
| D08 / PDF export substrate riêng | PDF writer/backend/licensing/typography/color và scoped consumer tasks | Proposed later; no PDF capability before adapter/fixture gate, không chặn native |
| D09 / các mốc đã thực sự nhận | Plan, per-profile capability report và project-state khi lease riêng | Ghi mốc nào đạt/không, exact snapshot/source/exe/profile, handoff theo protocol |

Budgets D00 đề xuất: tối đa 10000 annotations/document, 1024 styles, 16 refs/annotation, 32 leader branches,
text 4096 code points, bounded glyph/output counts và measured layout scratch. Limits cần preflight/checked arithmetic,
không chỉ check số objects sau allocate. Benchmark source edit với 1000 dependent dimensions;
mục tiêu CPU update/layout p95 ≤16.7 ms trên catalog/máy khai báo, nếu không đạt dùng cancellable derived layout,
giữ source writer responsive và không hiển thị last revision như current. Không suy FPS/present/latency từ số này.

## 8. DXF và các representation không được gộp

D07a ưu tiên một profile **candidate ASCII DXF R2018 / AC1032, model space 2D mm**, exact writer/API/license và
AutoCAD build chốt từ spike. R12/R2000 chưa lên lịch, không hứa mọi CAD tool; profile đổi phải update fixtures.
Theo source hiện đọc DXF là import-only `dxf-entities-v1`, chỉ LINE/ARC/CIRCLE/LWPOLYLINE; không mở DIMENSION import
chỉ vì viết được exporter. Adapter import Dimension sau này là task worker/host validation/C8 riêng.

Writer cần HEADER/units, TABLES/DIMSTYLE/text style, source geometry entities/handles/layers,
dimension type-specific definition points, WCS/OCS/extrusion basis, picture blocks và Unicode/text policy.
R/Ø/angle/ordinate có mappings riêng. Arc Length và Multi-Leader cần pin entity/profile riêng theo tài liệu target,
không nhét vào common DIMENSION type 0..6. Nếu writer chưa hỗ trợ: reject/report hoặc export appearance **explicit**,
không đổi âm thầm thành các line/text trong profile `DIM-DXF-entity`.

D07b cần non-graphical association objects/refs/reactors/dictionaries theo version, handle mapping tới source entities
và point semantics/parameters. Ghi $DIMASSOC hoặc type=1 không tự tạo liên kết; test bắt buộc sửa geometry trong AutoCAD,
regen/save/reopen và xem annotation cập nhật. Source source-segment split/curve conversion không map thì export báo loss.
PikDraw SourceReference không được ghi nguyên vào DXF như thể native CAD hiểu NodeId của PikDraw.

| Format/version/representation | Xem/render | Edit / metadata / native save | Export round-trip / production |
| --- | --- | --- | --- |
| Native schema/version D01 | Sau math/layout/render gates | Dimension/Style/refs/raw/notes/units/font/color/IDs preserved; native reopen gate | Không manufacturing claim, annotation mặc định không là toolpath |
| SVG 1.0/1.1 static subset hiện có | Nhập geometry subset trước native dimension; không suy live vendor dimensions | Imported numeric text/path không tự reverse-engineer annotation | D06 expand appearance chỉ, báo mất associativity/style semantics; native source giữ |
| DXF ASCII `dxf-entities-v1` import hiện có | Chỉ source geometry subset; DIMENSION unsupported | Units/layers/source mapping được kiểm riêng | Chưa writer trong baseline; D07 gates mới cập nhật từng profile |
| DXF R2018/AC1032 candidate dimension export | Picture/entity gate trong app target | CAD-editable entity khác associative link; refs/style/Unicode kiểm riêng | D07a entity, D07b source edit/save/reopen; SolidWorks/MasterCAM chưa kiểm là Not assessed |
| PDF export candidate | Sau D08, font/profile/paint gate | Appearance output không native annotation editing | Không source-format editable round-trip; print/production cần profile riêng |
| PDF/AI authoring/AI PDF/CDR/EPS import | Unsupported hiện tại, native authoring không cần các adapters này | Per-format/version/representation adapter riêng khi có task; C8/C9 không tự implemented | Không CDR/AI/EPS writer/round-trip claim |

Tài nguyên online: không có downloader trong plan. File qua broker/worker giới hạn và normalized model theo master;
loss/provenance reporting phải đúng coverage thật, không giả C8 đầy đủ chỉ vì có metadata fields.

## 9. Nghiệm thu: Given / When / Then / Verify

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-D01 | A(0,0), B(30,40) mm và transform/reflection fixtures | Tạo Linear H/V và Aligned | 30/40/50 mm theo basis; preview=commit=raw; camera không đổi số | Independent analytic math, document+native tool tests |
| AC-D02 | Polygon edge có two stable nodes | Tạo Aligned, kéo Node một đầu, Undo/Redo/save-reopen | Annotation cập nhật current revision, IDs/refs/style/source nguyên vẹn, một Undo per source edit | Node→writer→resolver→renderer/storage vertical slice |
| AC-D03 | Edge dimension refs cả segment span/t, intersection pair | Split t=0.4, reverse, merge/delete target | Relink theo semantic mapping hoặc Broken rõ; không vô tình đo leading half, không đoán nearest | N01 interval vectors + D resolver tests, Undo restores refs |
| AC-D04 | Radius của circle R20, quarter arc; ellipse nonuniform scaled | Radius/Diameter/Arc Length, đổi transform | 20/40/10π theo fixtures; ellipse radius unsupported/current dim invalid, không fake value | Analytic source measure, affine/circle eligibility tests |
| AC-D05 | Two line rays với 90° và 270° sector, degenerate/parallel cases | Đặt Angular ở hai phía | Sector theo preview và raw radians, invalid có outcome; không bbox angle | Analytic atan2/sign fixture, native placement |
| AC-D06 | Datum O(10,20), basis rotated 30°, point P | Ordinate rồi pan/zoom/di chuyển datum, Undo | Signed basis projection đúng; datum edit transaction cập nhật users; không machine claim | Independent projection/storage/reference tests |
| AC-D07 | 25.4 mm, ±0.05 mm, 0/−0/rounding ties và Unicode note | mm/inch dual, decimals/separator/tolerance formats | Exact conversion 1 in trước rounding, lower/upper và signs đúng, raw không đổi | Independent formatting golden vectors; glyph/font metrics tests |
| AC-D08 | Annotation span nhỏ hơn text+arrows, text tiếng Việt/Ø/± | Best Fit, flip arrow, drag text/mask, Undo | Chữ không cắt, arrow extents/readable-angle đúng, không đổi geometry/số raw | Native text layout/renderer alpha/bounds/hit-test, screenshot + structure |
| AC-D09 | Style đang dùng bởi nhiều dims và overrides Mixed | Sửa style, clone/rename/delete-used | Một atomic update, overrides giữ, delete-used cần replace rõ; raw/source không đổi | Style commands/dependency invalidation/storage tests |
| AC-D10 | Multi-Leader Unicode với nhiều branches, max text/branch limit | Thêm/xóa branch rồi save/reopen hoặc vượt budget | Notes/landing/branches/IDs đúng; vượt cap reject không partial | Commands/layout/storage + resource negatives |
| AC-D11 | Properties đóng/bar hẹp; text/IME có focus | Mở Dimension properties/Styles, đổi dual/tolerance, nhấn D/Ctrl+Shift+D | Đúng section, mở không Undo; bar đồng bộ, text shortcuts giữ; không chiếm Step and Repeat | Native actions/session/focus tests + direct OS/DPI/theme review |
| AC-D12 | 1000 dims liên kết, layout async | Source edit/cancel/tab/reopen + stale layout result | Source+refs atomic, cache đúng revision, stale không hiện/commit | Dependency benchmark CPU/memory/cancel + strict lifecycle assertions |
| AC-D13 | Source+dim trong group/duplicate/automation commands | Group/transform, duplicate cả batch hoặc annotation riêng | Không double transform, remap IDs đúng policy, Undo khôi phục; cùng semantics mọi entry | Document consumer tests; automation/clipboard chỉ qua scoped owner integration |
| AC-D14 | Native annotation với style/refs; existing SVG đích | D06 export hoặc bị unsupported | Báo loss đúng trước explicit expand; reject không ghi đè destination | SVG writer/report + destination byte preservation tests |
| AC-D15 | DXF profile fixtures H/V/Aligned/Angular/R/Ø/Ordinate + Unicode | D07a export, mở exact AutoCAD build | Entity/style/units/definition/picture đúng, chưa tự claim association | Independent DXF inspection + AutoCAD Properties/measure + save/reopen |
| AC-D16 | D07a fixture có source geometry handles và association graph | D07b sửa source trong AutoCAD, regen/save/reopen | Values/refs thực sự update; type/version unsupported không được báo pass | Named source-app edit oracle, giữ evidence/hash; không nhận từ parser count |
| AC-D17 | Unknown dimension schema/type/invalid refs hoặc save giữa lúc có revision mới | Load/save/export | Fail atomic/no silent drop; save snapshot r không clear dirty r+1 | Native codec/fault tests, unsupported interop tests |

Các lệnh dưới là hướng dẫn cho agent triển khai, chưa được chạy trong task lập plan:

```powershell
# Target mới D01, phải đăng ký CMake trước:
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_document_dimensions,test_pikdraw_document_storage_fidelity -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_document_dimensions|test_pikdraw_document_storage_fidelity)$'
# Consumer UI hiện có, bổ sung test cases/target Dimension theo D03:
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_desktop_actions,test_pikdraw_vertical_slice_e2e -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_desktop_shell|test_pikdraw_desktop_actions|test_pikdraw_vertical_slice_e2e)$'
./tooling/native/build.ps1 -Configuration Debug
```

Schema/visitor change là rủi ro nhiều consumer: sau focused checks cần broaden đúng contract consumers
document/storage/rendering/interop/automation đã inventory; Debug/Release cho writer/layout mới.
Native manual theo master; không cần chạy full repo suite mỗi task nhỏ, nhưng không bỏ variant consumers khi D01 đổi schema.
D07 không có AutoCAD môi trường/source-app evidence thì giữ export gate **chưa nghiệm thu**, vẫn bàn giao native đã đạt.
D08 không có PDF substrate thì giữ Planned; không dùng Windows screenshot/print path làm PDF fidelity evidence.

## 10. Điều kiện dừng và bàn giao

Dừng mở capability nếu schema/consumer chưa review, font/glyph không thật, refs tách khỏi single-writer transaction,
source topology không có mapping duy nhất, formatter sai units hoặc writer âm thầm flatten/làm mất dữ liệu.
Không reset/stash/sửa shared file của task khác để unblock. Chờ lease hoặc bàn giao exact patch requirement qua protocol.

Handoff ghi mốc DIM-native-basic/full/SVG/DXF-entity/DXF-associative/PDF đạt gì, APIs/schema/version,
fixture/app-build hashes, commands/counts/results, source/exe hashes, manual UX/performance/production chưa kiểm.
Chỉ cập nhật project-state/capabilities theo các rows đã gate; không đưa cả gói vào implemented vì đã tạo được một đường đo.
