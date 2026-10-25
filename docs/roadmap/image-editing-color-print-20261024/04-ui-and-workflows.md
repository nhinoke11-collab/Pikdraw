# 04 — Thiết kế UI, trường dữ liệu và luồng sử dụng

[Kế hoạch chính](../image-editing-color-print-20261024.md). Phiếu UI theo
[chuẩn UI](../../design/ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md)
và [feature template](../../design/feature-ui-template.md). Đây là spec, chưa là ảnh native/runtime đã nghiệm thu.
English là UI mặc định; nhãn tiếng Việt đi qua catalog, tài liệu này viết tiếng Việt.
Phiếu riêng của lệnh xuất master: [Xuất bản gốc ảnh raster đã chọn](../../design/features/raster-bitmap-master-export.md).
Phiếu riêng của histogram: [Histogram ảnh sau chỉnh sửa trong Properties](../../design/features/raster-source-histogram-properties-20261024.md).
Phiếu riêng của chuyển profile raster: [Chuyển profile ICC ảnh raster sang sRGB](../../design/features/raster-profile-conversion.md).
Phiếu riêng của gán profile raster: [Gán ICC RGB cho ảnh raster](../../design/features/raster-profile-assignment.md).

Triển khai hiện tại phủ Crop, Exposure, Levels, HSL, Curves, White Balance, Flip, histogram RGB sau graph kèm alpha coverage trong Properties, xuất master TIFF ảnh chọn, Assign ICC RGB và Convert ICC ảnh sang sRGB trong menu Bitmaps.
Crop/Exposure/Levels mở modal nhập số; Flip ghi node command; export TIFF chạy nền và dùng pixel
đầy đủ của ảnh. Histogram là chart đọc-only sau graph; chưa có mask hoặc chế độ xem channel riêng.
Crop handles và kiểm trực quan native vẫn chưa đạt.

## 1. Placement và cách tìm công cụ

| Bề mặt | Quyết định |
| --- | --- |
| Bitmaps | Các nhóm Image, Adjust, Channels, Mask, Retouch, Filters, Color; chỉ hiện nhóm có capability. Đường menu cụ thể ở mục 2 |
| File | Export, Print, Print Preview, Test Print; thiết lập/output tài liệu đặt trong workflow file hiện có |
| View | Before/After, channel view, Proof Colors, Gamut Warning, Overprint Preview; chỉ đổi view |
| Settings | Color Preferences/monitor, cache/resource preferences toàn app; không sửa profile ảnh đang chọn |
| Toolbox | Tái dùng crop nếu có; thêm flyout Image Selection/Mask và Retouch khi đủ handler; không tạo một icon cố định cho mỗi filter |
| Property bar | Target → 2–3 tham số hay dùng → icon mở Properties; tổng 3–5 controls. Không đưa chuỗi Apply/Cancel/Delete lên bar |
| Properties | Một dock theo context; Header, Geometry/Transform, Feature Settings, Action Group; mọi trường được hỗ trợ đều sửa được |
| Layers | Expand image để thấy adjustment/mask/retouch children có stable ID; không trộn chúng thành layer trang khác nghĩa |
| Channels | Tab trong hệ dock bên phải, mở khi cần; danh sách channel/alpha/spot và view/edit target cần không gian riêng |
| Output/Print | Tab thuộc hệ dock/Properties cho condition, proof, preflight, test sheet và run. Dialog Print chỉ chứa thiết lập job/driver cần thiết |
| Histogram | Section trong Properties của ảnh/adjustment, không mở thêm panel mặc định |
| Status | Hint thao tác/focus/tiến độ; lỗi cần xử lý có thông báo riêng. Profile/diagnostic chi tiết nằm trong Properties |
| Standard toolbar | Giữ Export/Print hiện có nếu thực sự có handler. Không thêm hàng icon adjust/AI mới vào standard toolbar |

Channels và Output là tab chuyên dụng vì cần list/table theo pixel hoặc print job; cùng hệ dock,
không tạo cửa sổ editor riêng. Properties đủ tham số đã hỗ trợ; không dùng dialog rời làm nơi duy nhất
để tìm một tham số nâng cao. Metadata readonly có label rõ; các tham số chỉnh được phải là control thật.

## 2. Menu và action IDs

`existing` dưới đây chỉ xác nhận ID đã khai báo; chưa xác nhận đầy đủ handler/runtime.
Các ID khác là **proposed** và chỉ đăng ký sau vertical slice tương ứng. Action mở editor chỉ
tạo draft/focus. Apply hoặc hoàn tất gesture ghi một command; lệnh một lần như Flip ghi trực tiếp
một command. Thay giá trị trong draft chưa commit không tự tạo document Undo.

| Menu chính EN / nhãn VI | Action ID | Hành vi |
| Bitmaps > Color Balance… / Cân bằng màu | action.bitmap.adjust.color_balance | Modal chọn Shadows/Midtones/Highlights; mỗi dải có bộ trục RGB riêng và một graph node stable ID; giữ mask khi sửa lại |
| --- | --- | --- |
| Bitmaps > White Balance… / Cân bằng trắng | action.bitmap.adjust.white_balance | Hai trường nhiệt độ/tint tương đối ±100% trên linear-light sRGB; không biểu diễn Kelvin/RAW; Apply cập nhật một node và Undo |
| Bitmaps > Image Info… / Thông tin ảnh | `action.bitmap.info` | Focus Image Info |
| Bitmaps > Export Bitmap Master (TIFF)… / Xuất bản gốc ảnh (TIFF)… | `action.bitmap.export_master` | Tác vụ tệp một lần; ghi ảnh chọn ở kích thước pixel hiện tại, áp graph đã lưu, giữ 16-bit TIFF khi có master 16-bit và nhúng profile khớp mẫu; không sửa tài liệu |
| Bitmaps > Export Bitmap Master to CMYK16 TIFF… / Xuất ảnh gốc thành TIFF CMYK16… | `action.bitmap.export_cmyk_master` | Một ảnh RGB → chọn ICC CMYK + intent → Save TIFF; worker chuyển U16, flatten alpha lên trắng, giữ DPI, nhúng profile; không sửa artwork |
| Bitmaps > Assign Bitmap Profile… / Gán profile ảnh… | `action.bitmap.assign_profile` | Một ảnh raster → chọn ICC RGB; giữ pixel/source/graph, Undo một command; preview worker chuyển sang system sRGB |
| Bitmaps > Convert Bitmap Profile to sRGB / Chuyển profile ảnh sang sRGB | `action.bitmap.convert_profile_to_srgb` | Một ảnh có embedded RGB ICC chưa áp dụng → system sRGB; Relative Colorimetric, BPC off; worker + progress/Esc, một command Undo; giữ byte/profile gốc và graph |
| Bitmaps > Crop… / Cắt ảnh | `action.bitmap.crop` | Modal X/Y/W/H chuẩn hóa; cập nhật crop node hiện có, Apply tạo command/preview; handles còn ở backlog |
| Bitmaps > Exposure… / Phơi sáng | `action.bitmap.adjust.exposure` | Modal -10…+10 EV trên sRGB đã xử lý; cùng node ID khi sửa lại |
| Bitmaps > Levels… / Levels | `action.bitmap.adjust.levels` | Channel selector + input/output black/white/gamma; Apply cập nhật một node và Undo |
| Bitmaps > Hue/Saturation/Lightness… / Sắc màu/Bão hòa/Độ sáng HSL | `action.bitmap.adjust.hsl` | Modal master HSL đã mã hóa; node không phá hủy, Apply/Cancel và Undo |
| Bitmaps > Curves… / Đường cong | `action.bitmap.adjust.curves` | Channel selector RGB tổng hợp/R/G/B và năm điểm output 0/25/50/75/100%; node không phá hủy |
| Bitmaps > Flip Horizontal / Lật ngang | `action.bitmap.flip_horizontal` | Thêm node flip; Undo/Redo và preview |
| Bitmaps > Flip Vertical / Lật dọc | `action.bitmap.flip_vertical` | Thêm node flip; Undo/Redo và preview |
| Bitmaps > Resize / Resample… / Đổi kích thước ảnh | `action.bitmap.resample` | Focus Resize |
| Bitmaps > Transform > Straighten… / Cân thẳng | `action.bitmap.straighten` | Tool/draft |
| Bitmaps > Transform > Perspective… / Phối cảnh | `action.bitmap.perspective` | Tool/draft P5 |
| Bitmaps > Transform > Flip Horizontal / Lật ngang | `action.bitmap.flip_horizontal` | Một command |
| Bitmaps > Transform > Flip Vertical / Lật dọc | `action.bitmap.flip_vertical` | Một command |
| Bitmaps > Transform > Rotate… / Xoay ảnh | `action.bitmap.rotate` | Focus angle draft |
| Bitmaps > Restore Crop / Khôi phục vùng cắt | `action.bitmap.restore_crop` | Sửa node crop, không xóa các node khác |
| Bitmaps > Color > Convert Profile… / Chuyển profile ảnh | `action.bitmap.convert_profile` | Conversion node/draft |
| Bitmaps > Color > Image Color Settings… / Thiết lập màu ảnh | `action.bitmap.color_settings` | Focus image color section |
| File > Export… / Xuất | `action.file.export_raster` (existing alias theo format) | Reuse workflow xuất ảnh, không tạo encoder thứ hai |
| File > Print… / In | `action.file.print` (existing) | Print job theo capability |
| File > Print Preview… / Xem trước bản in | `action.file.print_preview` (existing) | Preview layout/output condition |
| File > Test Print… / In test | `action.file.test_print` | Focus Output/Test Print |
| File > Print Settings… / Thiết lập bản in | `action.file.print_settings` | Output condition của tài liệu/job |
| View > Proof Setup… / Thiết lập mô phỏng in | `action.view.proof_setup` | Focus proof tab, không conversion artwork |
| View > Proof Colors / Mô phỏng màu in | `action.view.proof_colors` (existing) | Toggle với capability chính xác |
| View > Gamut Warning / Cảnh báo ngoài gamut | `action.view.gamut_warning` (existing) | View overlay; diagnostic cũ không nhận là gamut chuẩn |
| View > Overprint Preview / Xem in đè | `action.view.overprint_preview` (existing) | Chỉ enable khi output compositor đạt gate |
| View > Image Before/After / So sánh trước sau | `action.view.bitmap_before_after` | View original/input-node/final theo selector |
| Window > Channels / Kênh màu | `action.window.channels` | Mở/đóng tab; không đổi channel |
| Window > Output / Đầu ra và in test | `action.window.output` | Mở/đóng tab; không tạo print job |

Các action có nhiều lựa chọn dùng quy tắc mở rộng **xác định**, không tạo ID theo tên bản địa hóa:

| Đường menu chính | ID prefix và toàn bộ suffix dự kiến | Mở section |
| --- | --- | --- |
| Bitmaps > Adjust > tên chức năng… | `action.bitmap.adjust.` + `light`, `exposure`, `levels`, `curves`, `auto_tone`, `white_balance`, `hsl`, `vibrance`, `color_balance`, `replace_color`, `selective_color`, `sample_target`, `black_white`, `gradient_map`, `color_grading`, `lut` | Adjustment tương ứng |
| Bitmaps > Channels > tên chức năng… | `action.bitmap.channels.` + `mixer`, `edit_component`, `to_mask`, `split`, `combine`, `calculations`, `spot_channel` | Channels + Properties của phép toán |
| Bitmaps > Mask > tên chức năng | `action.bitmap.mask.` + `rectangle`, `ellipse`, `lasso`, `brush`, `color_range`, `luminance_range`, `refine`, `invert`, `disable`, `delete`, `save`, `from_path`, `from_subject` | Mask/selection tool hoặc command |
| Bitmaps > Retouch > tên công cụ | `action.bitmap.retouch.` + `clone`, `heal`, `spot_heal`, `patch`, `dodge`, `burn`, `sponge`, `paint`, `erase`, `red_eye`, `dust` | Retouch tool + Properties |
| Bitmaps > Filters > tên chức năng… | `action.bitmap.filter.` + `sharpen`, `denoise`, `clarity`, `texture`, `dehaze`, `gaussian_blur`, `motion_blur`, `grain`, `threshold`, `posterize`, `invert` | Filter Properties |
| Bitmaps > Adjustments > thao tác | `action.bitmap.stack.` + `add`, `apply`, `cancel`, `reset`, `remove`, `move_up`, `move_down`, `toggle`, `copy`, `paste`, `save_preset`, `apply_preset` | Command/draft theo ngữ cảnh |
| Bitmaps > Batch Adjust… / Chỉnh ảnh hàng loạt | `action.bitmap.batch_adjust` | Batch selection/recipe/output |

Các nhóm Adjust/Channels/Mask/Retouch/Filters nâng cao ở bảng trên vẫn là lộ trình. Chỉ đăng ký
suffix khi có handler end-to-end; không tạo menu “đủ danh sách” bằng IDs chưa hỗ trợ. P8 bổ sung
RAW/AI bằng phiếu capability riêng, không tự xuất hiện trong các nhóm cơ bản.

Context menu, toolbox, bar và Properties gọi lại cùng action/command. Existing
`action.color.assign_profile`/`action.color.convert_profile` tiếp tục scope paint/page đang có;
không đổi handler chúng sang pixel ảnh một cách âm thầm. Menu ảnh dùng IDs riêng phía trên.

**Shortcut:** giữ binding hiện có sau audit; các action mới chưa gán shortcut toàn cục.
Enter/Escape, brush modifiers và sample-anchor gesture là binding dự kiến phải được làm và test
trước khi quảng bá trong tooltip. Không chép Ctrl+Y của Photoshop vì PikDraw có Redo.

## 3. Property bar và Properties cùng nguồn dữ liệu

| Context | Bar nhanh, tối đa 5 controls | Properties đầy đủ |
| Color Balance | Menu `Bitmaps > Color Balance…` mở draft nhiều trường | Inspector chọn Shadows/Midtones/Highlights; ba trục màu và Preserve Luminosity cập nhật node stable ID qua command/Undo; mỗi dải giữ giá trị riêng |
| --- | --- | --- |
| Image selected | Target; W/H theo nhóm hiện có; Resample shortcut; mở Properties | Info, Transform, resolution, source/link, color, stack, export |
| Crop | Aspect; W; H; unit; mở Properties | Rect X/Y/W/H, anchor, ratio, resample policy, overlay, Apply/Cancel/Restore |
| Light | EV field; Contrast; mở Properties | Tab Light trong inspector với EV/Contrast/Highlights/Shadows/Whites/Blacks; dialog vẫn hỗ trợ Apply/Cancel |
| HSL/Vibrance | Không thêm hàng controls; mở Properties | Tab HSL có Hue, Saturation, Lightness và Vibrance; node ổn định, giới hạn theo field, Undo/Redo |
| Levels/Curves | Channel; preset; selected point/value; mở Properties | Graph và input số, endpoints/gamma/output, histogram, samplers, mask |
| Mask brush | Size; Hardness; Opacity; mode; mở Properties | Flow, spacing, pressure, smoothing, density/feather, overlay và mask operations |
| Retouch | Size; Opacity; source/sample scope; mở Properties | Source anchor/alignment/flow/pressure/solver/patch blend và actions |
| Proof/Test Print | Không giành bar của active tool; nếu print-preview context thì condition/scale/mở Properties | Output tab với toàn bộ fields ở tài liệu 05 |

Bar và Properties đọc một presenter/model, validation/enable/mixed state và draft chung. Sửa
slider/field ở một nơi cập nhật nơi kia; thumbnail/histogram/proof cùng generation. Đóng dock không
đổi dữ liệu, mở lại focus đúng node/section. Cửa sổ hẹp đưa action phụ vào overflow và luôn giữ
đường mở Properties bằng menu/focus; không thu font để nhét form.

## 4. Field registry — hình học, sắc độ và màu

Các field ở bảng sau có prefix `raster.`; label ghi EN / VI. Scope `draft(node)` chỉ ghi khi
Apply/gesture hoàn tất; `image` là ảnh được chọn; `view` không vào Undo artwork. Default là
identity khi thêm node; mở node cũ luôn lấy giá trị đã lưu.

| Field ID / label EN + VI | Surface | Kiểu, unit, range, default | Scope, enable/mixed và validation |
| --- | --- | --- | --- |
| `target` Target / Ảnh đích | Header/bar | Object name + count; selector khi nhiều ảnh | Selection; không tự thay selection khi hover |
| `geometry.x/y/w/h` Position/Size / Vị trí/Kích thước | Bar/Properties | mm hoặc page unit; W/H >0, finite | Existing object transform commands; giữ lock ratio; mixed cho multi |
| `crop.x/y/w/h` Crop rectangle / Vùng cắt | Bar W/H + Properties | Source px hoặc normalized; nằm trong bounds, W/H >0 | draft(crop); đổi đơn vị không resample |
| `crop.aspect` Aspect / Tỉ lệ | Bar/Properties | Free/original/presets/custom positive ratio; Free | draft(crop); hai nguồn dùng cùng validation |
| `rotation.angle` Angle / Góc xoay | Properties | degrees, normalized [-180,180); 0 | draft(rotate), finite |
| `resize.width_px/height_px` Pixel dimensions / Kích thước pixel | Properties | Positive integer, giới hạn biểu diễn/codec; current | draft(resample); checked arithmetic/memory admission |
| `resize.ppi_x/ppi_y` Resolution / Mật độ pixel | Properties | >0 finite PPI; current metadata | Thay PPI chỉ đổi metadata/kích thước theo mode được chọn |
| `resize.resample` Resample / Nội suy lại | Properties | Bool, mặc định off | Khi off số pixel readonly; không tự tái tạo ảnh |
| `resize.kernel` Method / Phương pháp | Properties | Nearest/Bilinear/Bicubic/Lanczos đã được bật | Enable khi resample; version theo node |
| `light.exposure` Exposure / Phơi sáng | Bar/Properties | Light: -5..+5 EV; Exposure: -10..+10 EV; 0 | Cùng field `raster.exposure_ev` đọc/ghi node gần nhất; RGB linear, CMYK-only unsupported rõ |
| `light.contrast` Contrast / Tương phản | Properties | -100..100%, 0 | Sửa trường đơn trên stable Light node, giữ mask/enabled/ID |
| `light.highlights/shadows/whites/blacks` Tone controls / Vùng sáng/tối/trắng/đen | Properties | Từng field -100..100, 0 | Sửa một trường trên Light node; không ghi draft khác hoặc tạo node trùng |
| `tone.width` Tonal width / Độ rộng vùng sắc độ | Properties Advanced | 0..100%, default theo version | Node có hỗ trợ; không hiện control không được evaluator dùng |
| `tone.radius` Local radius / Bán kính cục bộ | Properties Advanced | Source px, 0..1024, default theo version | Chỉ node local tone; quota/scratch admission rõ |
| `levels.channel` Channel / Kênh | Bar/Properties | Composite hoặc channels có thật | RGB/CMYK/Gray; view channel khác edit channel |
| `levels.black_in/white_in` Input black/white / Đen/trắng đầu vào | Properties | [0,1], default 0/1; hiển thị theo bit/ink | black<white; CMYK ink hướng 0%→100% có label rõ |
| `levels.gamma` Gamma / Trung độ | Properties | 0,1..10, default 1 | Positive finite; không bị shortcut canvas chiếm số nhập |
| `levels.black_out/white_out` Output black/white / Đen/trắng đầu ra | Properties | [0,1], 0/1 | Bản cơ bản black≤white; creative inversion dùng node Invert |
| `curves.points` Curve / Đường cong | Properties | 2..64 point v1, x/y [0,1], identity endpoints | x strictly increasing; thêm/xóa có focus/numeric alternative |
| `curves.point_x/point_y` Input/Output / Đầu vào/ra | Bar selected value + Properties | Theo normalized/source coding; không ép 16-bit về integer 8-bit | draft(curves), selected point; endpoint policy rõ |
| `curves.interpolation` Interpolation / Nội suy | Properties Advanced | PCHIP v1 ban đầu | Readonly nếu chỉ một algorithm; không giả selector |
| `auto.black_percentile/white_percentile` Clip percentiles / Phần trăm cắt | Properties | Từng 0..5%, default 0,1% và 0,1% | Histogram full-res hoặc sampled ghi rõ; Auto tạo params xem được |
| `wb.temperature/tint` Temperature/Tint / Nhiệt độ/Tint | Properties | Từng -100..100 relative, 0 | RGB rendered; Kelvin không hiện nếu không có RAW support |
| `wb.neutral_sample` Neutral sample / Mẫu trung tính | Properties/canvas | Source-space sampler + average radius | draft(WB); transparent/clipped sample báo invalid |
| `hsl.range` Color range / Dải màu | Properties | Master hoặc color band | draft(HSL); giữ dải hue wrap |
| `hsl.hue` Hue / Sắc màu | Bar nếu active/Properties | -180..180°, 0 | Không đổi color mode |
| `hsl.saturation/lightness` Saturation/Lightness / Bão hòa/Độ sáng HSL | Properties | Từng -100..100, 0 | draft(HSL); cảnh báo clipping theo mask |
| `hsl.center/width/feather` Hue range / Tâm/Rộng/Mềm dải | Properties Advanced | Center 0..360°, width 0..360°, feather 0..180° | Smooth cyclic weights, valid range relation |
| `vibrance.amount/saturation` Vibrance/Saturation / Sống động/Bão hòa | Properties | Từng -100..100, 0 | draft(Vibrance), skin protection capability riêng |
| `balance.range` Tonal range / Vùng sắc độ | Properties | Shadows/Midtones/Highlights | draft(Color Balance); mỗi vùng giữ params riêng |
| `balance.cyan_red/magenta_green/yellow_blue` Color pairs / Cặp màu | Properties | Từng -100..100, 0 | RGB correction, không phải plate numbers |
| `selective.family/method` Family/Method / Họ màu/Cách chỉnh | Properties | 9 families; Relative/Absolute | RGB creative node; không đổi image model |
| `selective.c/m/y/k` Components / Thành phần chỉnh | Properties | Từng -100..100%, 0 | Không dùng các giá trị này làm TAC |
| `mixer.output/source_weights/constant` Mixer / Kênh ra/Trọng số/Hằng số | Properties | Output enum; weights -200..200%, identity; constant -100..100%, 0 | Channels thật, sum warning; normalize là action rõ |
| `mixer.monochrome` Monochrome / Đơn sắc | Properties | Bool off | Một output Gray mix; không tự gán ICC Gray khác |
| `sample.space/size` Readout space/Area / Hệ đọc/Vùng mẫu | Properties | Source/Working/Proof/Output; 1/3/5/11 px | view; proof/output unavailable nếu thiếu profile/route |

## 5. Field registry — mask, retouch, detail và profile

| Field ID / label EN + VI | Surface | Kiểu, unit, range, default | Scope và validation |
| --- | --- | --- | --- |
| `node.opacity/enabled` Opacity/Enable / Độ áp dụng/Bật | Properties/Layers | 0..100%, 100; bool true | draft hoặc committed node; toggle một Undo, view bypass riêng |
| `node.mask_ref` Mask / Mặt nạ | Properties | None/new/existing MaskId | Node scope; shared-mask edits có hành động riêng |
| `mask.mode` Selection mode / Chế độ vùng chọn | Bar/Properties | Replace/Add/Subtract/Intersect; Replace | Session draft; gắn mask mới vào node mới là command |
| `mask.density/feather` Density/Feather / Mật độ/Mềm biên | Properties | 0..100%,100; source px 0..1024,0 | Mask revision; không đổi original alpha |
| `mask.overlay/overlay_opacity` Overlay / Lớp xem mask | Properties | Bool, false; 0..100%,50 | View; không đi vào export pixels |
| `brush.size/hardness/opacity` Size/Hardness/Opacity / Cỡ/Cứng/Độ phủ | Bar/Properties | 0,1..4096 source px,32; 0..100%,80; 0..100%,100 | Tool preference khi không stroke; stroke chốt params |
| `brush.flow/spacing/smoothing` Flow/Spacing/Smoothing / Lưu lượng/Khoảng cách/Làm mượt | Properties | 0..100%,100; 1..200% diameter,25; 0..100%,0 | Stroke params, interpolation/time policy có version |
| `brush.pressure` Pressure / Áp lực bút | Properties | Size/opacity/flow mapping; off | Enable khi device/handler hỗ trợ, keyboard fallback có |
| `retouch.sample_scope` Sample / Nguồn lấy mẫu | Bar/Properties | Current/Current+below/Visible composite | Chốt sample prefix/revision; reject circular source |
| `retouch.source_x/y/aligned` Source/Aligned / Điểm nguồn/Bám nguồn | Properties/canvas | Source px + bool true | Source anchor valid, sample snapshot khi bắt đầu gesture |
| `retouch.heal_radius/iterations` Heal / Bán kính/Số vòng | Properties Advanced | Radius 1..256 px,16; iterations 1..200,40 | Solver v1 budget/cancel; không phải slider không tác dụng |
| `detail.amount/radius/threshold` Sharpen / Mức/Bán kính/Ngưỡng | Properties | 0..500%,100; 0,1..100 px,1; [0,1],0 | draft(sharpen), source pixel scale, luminance option |
| `denoise.luma/chroma/detail` Denoise / Độ chói/Màu/Chi tiết | Properties | Từng 0..100, defaults 0/0/50 | Semantic version; zero amount identity |
| `clarity/texture/dehaze.amount` Amount / Mức áp dụng | Properties | Từng -100..100, 0 | Các node riêng, radius defaults theo descriptor |
| `blur.sigma/length/angle` Blur parameters / Tham số nhòe | Properties | Sigma 0..512 px,0; motion 0..4096 px,0; angle degrees | Theo kind; alpha/halo/memory validation |
| `grain.amount/size/roughness/seed` Grain / Mức/Cỡ/Độ nhám/Seed | Properties | 0..100,0; 0,1..100 px,1;0..100,50; U64 seed lưu | Deterministic theo source coordinates |
| `lut.resource/domain/interpolation` LUT / Tệp/Miền/Nội suy | Properties | ResourceId; declared domain; trilinear v1 | Validate .cube subset, size/bounds, input/output color spaces |
| `color.source_profile` Source profile / Profile nguồn | Properties | Actual resource hoặc Untagged/Unresolved | image; readonly label + Assign action, không dropdown đổi nghĩa ngầm |
| `color.working_profile` Working space / Không gian làm việc | Properties | Valid RGB/CMYK/Gray profile theo mode | Conversion boundary/draft; không thay số khi chỉ mở panel |
| `color.sample_type` Precision / Độ chính xác | Properties | U8/U16; F32 khi có capability | image/variant; giảm bit có report, không đổi bằng view selector |
| `color.intent` Rendering intent / Mục đích chuyển màu | Properties | 4 intents; Relative ban đầu | Conversion request; unsupported bị disable kèm lý do |
| `color.bpc` Black point compensation / Bù điểm đen | Properties | Bool; mặc định on với Relative nếu backend hỗ trợ | Unsupported/off khi chưa có; không im lặng bỏ qua true |
| `color.scope` Scope / Phạm vi | Properties | Selected image/eligible selection/output variant | Count và preview ảnh hưởng; không tự chuyển toàn document |
| `export.*`, `proof.*`, `print.*`, `test.*`, `measure.*` | Output tab/dialog | Field registry duy nhất ở tài liệu 05 | Scope view/job/variant, không lặp logic trong image presenter |

Range kernel/brush/solver là v1 safety/UX choices, không là tuyên bố giới hạn intrinsic của format.
Preset vượt range/không đúng version phải report và từ chối hoặc explicit migrate, không tự clamp.

### Các field bổ sung bắt buộc khi bật chức năng nâng cao

Không bật các action P5 chỉ với bộ controls cơ bản phía trên. Các nhóm sau có field descriptors
riêng và cùng presenter/command; field không áp dụng bị ẩn hoặc disable có lý do.

| Field ID / label EN + VI | Kiểu, range, default và scope |
| --- | --- |
| `replace.sample/target` Sample/Target / Mẫu/Màu đích | Source-space sample và target color/profile resource; draft(Replace), giữ original sample revision |
| `replace.metric/tolerance/feather` Range / Metric/Dung sai/Mềm biên | Lab ΔE00 0..100 khi CMM hỗ trợ hoặc RGB-distance normalized 0..1 có nhãn; defaults 10 ΔE00/0,1 RGB; feather 0..100%,20 |
| `replace.contiguous/preserve_shading` Contiguous/Preserve shading / Liền vùng/Giữ sắc độ | Bool false/true; draft(Replace), mask preview rõ; không dùng distance RGB làm ΔE |
| `match.source_region/target_reference` Source/Reference / Vùng nguồn/Mẫu đích | Valid ROI + source revision, target patch/reference ID và profile; minimum valid sample count theo estimator |
| `match.strength/luminance/chroma` Strength/Match components / Mức/Các thành phần khớp | 0..100%,100; luminance/chroma bool true; draft(Match), unsupported metric báo rõ |
| `bw.weights/tint` Mix/Tint / Trọng số/Tô màu | RGB-family weights -200..200%, preset identity theo version; tint off + color/profile resource; draft(B&W) |
| `map.stops/interpolation` Gradient / Điểm màu/Nội suy | 2..64 stops, positions [0,1], typed colors/profile/alpha; interpolation semantic ID, no duplicate invalid positions; draft(Gradient Map) |
| `grade.range/hue/saturation/luminance/balance` Grading / Vùng/Hue/Bão hòa/Sáng/Cân bằng | Shadows/Midtones/Highlights; hue 0..360°,0; saturation 0..100%,0; luminance -100..100,0; balance -100..100,0; mỗi vùng có params riêng |
| `mask.refine_grow/smooth/contrast/shift` Refine / Nở/Làm mượt/Tương phản/Dịch biên | Source px -1024..1024,0; smooth 0..100,0; contrast 0..100%,0; shift -100..100%,0; draft(mask), halo/quota validate |
| `mask.range_metric/tolerance/softness` Color/Luminance range / Dải màu/Độ chói | Metric enum, tolerance theo metric như Replace; luminance min/max [0,1],0/1; softness 0..100%,0; min≤max |
| `mask.vector_ref/link_transform` Vector mask / Path/Nối biến đổi | Valid Path/Mask ID, link bool true; stable revision, không đổi source path khác scope |
| `channel.view/component/tinted` Channel view / Cách xem/Kênh/Tô màu | Composite/solo; actual channel ID; tinted bool false; view scope, không đổi data target |
| `channel.edit_target` Edit channel / Kênh sửa | Explicit native component/alpha/mask ID; draft scope, khác `channel.view` |
| `channel.combine_inputs` Combine / Nguồn từng kênh | Ordered image/channel refs với equal dimensions/sample layout, profile/output layout; tạo ảnh đích bằng command riêng |
| `channel.calculation_method/opacity/destination` Calculations / Phép toán/Mức/Đích | Add/Subtract/Multiply/Screen subset; 0..100%,100; New mask/New image/Explicit component; input refs và revision frozen |
| `spot.ink_id/name/tint/alternate` Spot / Định danh/Tên/Tint/Màu thay thế | Stable ink ID, Unicode name, tint/coverage 0..100%,100, alternate Lab/profile resource; duplicate-name conflict report; không đọc từ display RGB |
| `retouch.patch_mode/blend` Patch / Chế độ/Hòa biên | Source/Target; blend 0..100%,50 theo solver version; draft(patch), valid source/target ROI |
| `retouch.tonal_range/protect_hue` Dodge/Burn / Vùng sắc độ/Giữ hue | Shadows/Midtones/Highlights/All, All; bool true; stroke params, RGB-only behavior khai báo rõ |
| `retouch.paint_color/blend` Paint / Màu/Chế độ hòa | Typed color/profile, Normal hoặc blend subset C9 được hỗ trợ; lớp sửa scope, erase chỉ coverage lớp sửa |
| `sharpen.luminance_only` Luminance only / Chỉ độ chói | Bool true; draft(sharpen), metric/working domain theo descriptor |
| `blur.edge_mode` Edge mode / Xử lý mép | Clamp/Mirror/Transparent theo kernel đã kiểm; default Clamp cho ảnh opaque, explicit alpha policy khi transparent |
| `preset.name/category/resources` Preset / Tên/Nhóm/Tài nguyên | Unicode labels + resource references + schema/semantic versions; app library scope, không âm thầm đổi artwork |
| `batch.targets/parameter_mode/mask_policy/output` Batch / Đích/Cách áp/Mask/Đầu ra | Eligible object IDs; Absolute/Relative theo field; None/Normalized remap/Per-image; output pattern và destination; validation trước chạy |

Lab component editing, RAW, HDR, AI và DeviceLink P8 cần phiếu field riêng trước code vì
camera/model/channel layout/SDK chưa được chốt. Không có control hoạt động giả trong P1–P7.

## 6. Ma trận trạng thái và lifecycle

| Ngữ cảnh | Hành vi |
| --- | --- |
| Không document | Tool/edit/export disabled có reason; app Color Preferences vẫn mở được |
| Có document, không selection | Page/output settings mở được; Adjust ảnh disabled, không sửa default fill/stroke |
| Một ảnh hợp lệ | Đúng info/graph/node; nhập số/gesture/Apply tạo transaction trong scope ảnh |
| Nhiều ảnh | Batch eligible count; mixed fields; chỉ apply field đã sửa, không chép mask tọa độ sang mọi ảnh khi chưa chọn policy |
| Mixed ảnh/vector | Không sửa vector vì đã chọn Adjust; cho explicit eligible selection và báo số không hợp lệ trước thao tác |
| Locked/read-only/reference-only | Xem info/proof được; edits disabled lý do; không tự unlock/flatten |
| CMYK với RGB-only filter | Báo cần conversion node/variant; không ép convert rồi giấu mất K |
| Graph/node unsupported | Hiện node và diagnostic; không pretend disabled là bản render đúng; chặn production theo policy |
| Đang nhập số/text/IME | Canvas shortcut không giành phím; Tab/Enter theo field, focus rời không mất draft |
| Có draft khi đổi tool/selection/tab | Giữ draft theo document/object; không auto Apply. Hiện draft state, có Apply/Discard khi hành động phụ thuộc cần chốt |
| Có stroke | Pan/Zoom tạm thời không phá source anchor; kết thúc stroke một Undo; Escape bỏ stroke hiện tại |
| Busy/error/cancel | Progress/cancel reachable; completion stale bị bỏ; lỗi còn source/graph cũ, retry theo generation mới |
| Link missing/modified | Xem cached preview có nhãn nếu còn; final export yêu cầu source đúng version hoặc lựa chọn Update/Relink |

Mở/focus panel, chọn node, eye view, before/after và proof toggle không tạo Undo artwork.
Thay params bằng draft rồi Apply gom một Undo; drag liên tục coalesce một logical edit.
Undo/Redo khôi phục IDs/source/ICC/mask/revision và cập nhật cả bar/Properties/canvas.
Reset node khác Remove node; “khôi phục ảnh gốc” phải nêu rõ xóa/bypass cả stack và undo được.

Vùng chọn tạm có history trong phiên draft và nhãn “Hoàn tác vùng chọn”; Undo khi focus đang ở
editor draft ưu tiên history đó, không xóa artwork phía sau. Apply mask gắn vào node ghi một
document command; Cancel trả selection về đầu draft. Stroke retouch đã hoàn tất thuộc document
Undo, không bị lẫn vào selection history. Routing này cần native regression trước khi bật shortcut.

## 7. Icon, catalog, tooltip, status và accessibility

- SVG đề xuất nằm trong hệ icon hiện có, family `bitmap-*`, `mask-*`, `retouch-*`, `proof-*`;
  dùng tên suffix action để tra asset. Ưu tiên icon tự vẽ theo token/viewBox chung, lưu provenance/license;
  không sao chép icon thương hiệu hoặc dùng chữ/emoji thay tool icon.
- Mỗi action có `label`, `accessible_name`, `tooltip_title`, `tooltip_body`, `status_hint`,
  `disabled_reason`, `help_id` trong namespace `ui.bitmap.*` / `ui.output.*`; field có accessible
  label/unit/current/mixed/error. EN/VI đủ cho mọi control được bật; không sinh tooltip từ tên class.
- Body tooltip lấy purpose theo IE-ID ở tài liệu 02, thêm scope khi cần. Hai dòng: tiêu đề +
  shortcut thật nếu đã gán; một câu tác động. Catalog test liệt kê action/field đang enabled và kiểm thiếu resource.

| Action/control | Tooltip VI mẫu hai dòng | Status/focus hint |
| --- | --- | --- |
| Exposure | `Phơi sáng`<br>`Tăng hoặc giảm ánh sáng của ảnh đang chọn mà vẫn giữ ảnh gốc.` | `Chỉnh EV · Xem trước trên ảnh · Áp dụng hoặc Hủy` |
| Curves | `Đường cong sắc độ`<br>`Chỉnh độ sáng hoặc từng kênh bằng các điểm đầu vào và đầu ra.` | `Chọn kênh · Kéo điểm hoặc nhập số · Áp dụng hoặc Hủy` |
| Mask brush | `Cọ mặt nạ`<br>`Tô vùng được nhận điều chỉnh mà không xóa pixel nguồn.` | `Tô vùng áp dụng · Chuyển Thêm/Trừ trong Properties · Hủy nét hiện tại` |
| Assign ICC | `Gán profile ảnh`<br>`Đổi cách diễn giải màu và giữ nguyên số của các kênh ảnh.` | `Chọn profile đúng nguồn · Kiểm tra ảnh hưởng · Áp dụng hoặc Hủy` |
| Convert ICC | `Chuyển profile ảnh`<br>`Tính lại số màu theo profile đích và giữ ảnh gốc để sửa lại.` | `Chọn đích và mục đích chuyển · Kiểm tra gamut · Áp dụng hoặc Hủy` |
| Test Print | `In test`<br>`Tạo trang mẫu để so màu, sắc độ và chi tiết theo điều kiện in đã chọn.` | `Chọn loại mẫu · Xem trước bố cục · Xuất hoặc In` |

Ví dụ không có phím tắt vì binding mới chưa được triển khai. EN dịch tương đương, không chứa
workflow khác. Thông tin trước/sau/profile missing không chỉ dùng màu; icon có tên và focus rõ.

Keyboard có tab order Header→Transform→Settings→Actions, numeric caret/selection/copy/paste,
graph point input bằng số và list alternatives cho drag. Light/dark, 1366×768/1920×1080,
scale 100/125/150/200% cần native acceptance theo thiết bị; không suy ra mọi DPI từ mockup.

## 8. Help và ví dụ thao tác

Help IDs: `help.bitmap.basics`, `help.bitmap.channels`, `help.bitmap.mask_retouch`,
`help.bitmap.color_profiles`, `help.output.soft_proof`, `help.output.test_print`,
`help.output.measurements`. Menu Help mở đúng mục, Properties có link theo context.

### Chỉnh một ảnh tối và ám màu

1. Chọn ảnh → Bitmaps > Image Info, kiểm pixel/profile và nguồn đầy đủ.
2. Bitmaps > Adjust > Light; chỉnh Exposure/Highlights/Shadows; xem histogram và clipping.
3. White Balance chọn vùng xám trung tính rồi HSL/Vibrance nếu cần; thêm mask để chỉ sửa vùng ảnh mong muốn.
4. So trước/sau; Apply; mở lại node từ Layers/Properties để sửa, Undo/Redo rồi save .pr.
5. Reopen và Export Edited Image đúng profile/bit/PPI; Help nhắc preview không phải file xuất.

### Sửa màu từng kênh mà giữ separation CMYK

1. Chọn ảnh CMYK, kiểm profile và bật Channels; solo K chỉ đổi view.
2. Chọn edit target K, mở Curves; nhập một điểm và kiểm composite/source sample readout.
3. Xem TAC/preflight theo condition; giữ các channel chưa sửa và spot identity.
4. Export cùng condition preserve-numbers nếu đủ capability; Undo trả đúng source channels.
5. Filter RGB-only cần lựa chọn conversion/variant rõ, không có bước chuyển ngầm.

### In test, chọn biến thể và dùng lại

1. File > Test Print chọn output condition khớp máy/mực/vật liệu; chọn owner chuyển màu.
2. Chọn crop chi tiết 1:1 hoặc bảng biến thể, nhập bước thay đổi và xem nhãn từng ô.
3. Xuất/in bản test; so dưới điều kiện xem đã ghi, nhập số đo nếu có thiết bị.
4. Chọn biến thể đạt → Save Output Preset hoặc Apply to Output Variant; master không bị bake.
5. Chạy lại khi đổi vật liệu/profile/chế độ in; xem [quy trình chi tiết](05-print-proof-and-test.md).
