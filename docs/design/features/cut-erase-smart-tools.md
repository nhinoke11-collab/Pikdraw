# Kế hoạch nhóm công cụ Cắt, tẩy và tạo vùng thông minh

Ngày lập: 25/10/2026. Cập nhật trạng thái: 29/10/2026. **Các luồng source Knife, Vector Eraser, Scissors, Smart Trim, Trim Objects, Smart Fill, Shape Builder và Raster Eraser hiện có; chưa đồng nghĩa toàn bộ plan đã nghiệm thu runtime.** Theo dõi triển khai, regression receipts và các gate còn mở trong [plan hoàn thiện và crash recovery](cut-shape-completion-crash-recovery-20261029.md). Các bảng đối chiếu source ở mục 2 và 2.1 là baseline lịch sử ngày 25/10/2026, không mô tả source hiện hành. Phạm vi chính: core/geometry, core/document, modules/vector và apps/desktop. Không coi tên menu, icon hay test cũ là bằng chứng tính năng đã đúng.

## 1. Mục tiêu và cách tham khảo

Kế hoạch xử lý thao tác không phản hồi, lệch vùng bấm, preview và vai trò Trim nằm trong
[cải tiến UX ngày 29/10/2026](cut-erase-ui-ux-remediation-20261029.md). Đây là kế hoạch triển khai,
chưa phải bằng chứng UI runtime đã đạt.

Người dùng cần cắt một hình theo đường thật, tẩy vùng bằng nét quét, mở đường tại một điểm, xóa đoạn nằm giữa các giao điểm, trim một đối tượng bằng đối tượng khác, và tạo/chỉnh vùng kín từ nhiều đường. Mỗi kết quả là hình học có thể chọn, sửa node, Undo/Redo, lưu/mở lại và xuất theo năng lực định dạng; preview phải diễn tả cùng phép tính sẽ được commit.

| Tham chiếu chính thức | Hành vi lấy làm cơ sở | Quyết định cho PikDraw |
| --- | --- | --- |
| [CorelDRAW Crop and erase](https://product.corel.com/help/CorelDRAW/540111148/CorelDRAW-en/CorelDRAW-Crop-and-erase-objects.html) | Eraser kéo qua vector/bitmap; Virtual segment delete xóa đoạn giữa giao điểm. | Tách Vector Eraser và Smart Trim theo kiểu dữ liệu, giữ preview đoạn/vùng bị xóa. |
| [CorelDRAW Trim](https://product.corel.com/help/CorelDRAW/Documentation-Mac/CorelDRAW-en/CorelDRAW-Trim-objects.html) | Source cắt Target; Target giữ fill/outline. | Ghi rõ source/target và chính sách giữ source, bảo toàn style/stack của Target. |
| [CorelDRAW Smart Fill](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Create-objects-from-enclosed-areas.html) | Nhấp vùng được các biên bao kín để tạo đối tượng mới. | Smart Fill tạo path mới từ biên hình học, không flood-fill pixel. |
| [Illustrator Knife/Scissors](https://helpx.adobe.com/illustrator/desktop/manage-objects/edit-objects/cut-objects.html) | Knife cắt thành mảnh; Scissors mở/cắt path ở node hoặc trên đoạn. | Hai công cụ có state và command khác nhau, dùng chung bộ truy vấn giao điểm. |
| [Illustrator Shape Builder](https://helpx.adobe.com/illustrator/desktop/manage-objects/reshape-transform-objects/create-shapes-using-the-shape-builder-tool.html) và [Pathfinder](https://helpx.adobe.com/illustrator/desktop/manage-objects/reshape-transform-objects/pathfinder-panel-overview.html) | Chọn vùng để gộp/xóa; Trim Pathfinder có chính sách bỏ Stroke riêng. | Shape Builder dùng vùng chọn; lệnh Trim của PikDraw giữ Stroke của Target, không dùng ngầm semantics Pathfinder Trim. |

Đây là tham khảo hành vi, không sao chép giao diện, icon, shortcut hay thuật toán độc quyền. UI tuân theo docs/design/ui-ux-standard.md và docs/architecture/workspace-layout.md.

## 2. Đối chiếu source ngày 25/10/2026

| Luồng hiện có | Source | Kết luận và việc phải cấu trúc lại |
| --- | --- | --- |
| Knife | native_window_selection.cpp bắt đầu/kết thúc drag khi active_tool_ == 5, gọi DocumentWorkflow::split_objects_by_line. | Có đường input → workflow → command; đây chưa phải cắt path thật. |
| Split hiện tại | document_workflow_io_shaping.cpp gọi split_rect_by_line trên bounds của PathObject/TextObject/ImageObject. Một mảnh path/text còn bị đổi sang màu cam cố định; bitmap bị chia theo cột pixel. | Thay thuật toán theo biên hình học và asset thực. Bỏ màu chứng minh, giữ style/profile/opacity và báo rõ loại chưa hỗ trợ. |
| Boolean hiện tại | boolean_operation_selected và compute_selected_boolean dùng đúng hai PathObject có fill; kết quả Clipper2 có flattening và lưới lượng tử. | Có thể tái dùng kernel Difference, nhưng cần source/target rõ, hỗ trợ hierarchy world transform, kết quả rỗng, ID, vị trí lớp/stack, style/effect và sai số. |
| Bitmap trim hiện tại | trim_bitmap_with_vector chỉ lấy giao của hai bounds, cắt hình chữ nhật rồi AddObject vào active layer. | Không gọi đây là trim theo vector. Tách thành phép crop chữ nhật hiện hữu và thiết kế mask/clip bitmap thực ở giai đoạn riêng. |
| Node topology | SplitPathSegmentCommand, BreakPathAtNodeCommand và PathTopologyMapping đã có. | Scissors/Smart Trim dùng lại authoring path và mapping này; bổ sung command cho xóa khoảng và thay nhiều mảnh nếu cần. |
| Toolbox | ToolboxSlotId::Knife = 6, action.tool.knife, phím K; kích thước/nhãn/item flyout nằm trong nhiều nhánh số nguyên ở native_window_paint_docks.cpp và native_window_chrome_clicks.cpp. | Giữ ID/shortcut Knife để tương thích settings; chuyển mô tả nhóm, hình học hit, enable, focus và chọn item sang catalog chung. Không thêm một nhánh hardcode cho từng tool. |
| Raster | RasterEditKind chưa có Eraser/AlphaErase; adjustment mask hiện điều khiển các phép chỉnh màu. | Raster Eraser cần node alpha riêng, codec/version và nguồn bất biến; không gắn nghĩa tẩy ảnh vào mask của adjustment. |

Đây là bảng baseline trước khi sửa. Snapshot audit độc lập hiện tại ở mục 2.1; kết quả source không thay cho nghiệm thu bằng chuột.

## 2.1. Kết quả audit độc lập ngày 25/10/2026

| Giai đoạn | Trạng thái source | Phần còn thiếu để qua gate |
| --- | --- | --- |
| P0 | Một phần. Toolbox group hiện chỉ bày Knife và Vector Eraser; action/mode giả cho Scissors, Smart Trim, Trim Tool, Smart Fill và Shape Builder đã được gỡ/ẩn. | Chưa có catalog descriptor chung, đầy đủ focus/hit/enable và Properties đồng bộ cho các tool. |
| P1 | Một phần. Knife UI cũ đi qua workflow mới, cắt path đóng có fill, giữ style/appearance và parent stack; command thay nhiều mảnh có một Undo. Bitmap/Text không còn bị chia theo bounds. | Scissors chưa được triển khai. Chưa có save/reopen/export, pixel oracle, Bézier/source-node mapping hoặc nghiệm thu UI runtime cho AC-CUT-01/02. |
| P2 | Một phần. Vector Eraser trừ footprint của nét quét trên path đóng có fill; topology lỗ và các vùng rời được giữ trong geometry. | Path mở/chỉ có stroke hiện trả Unsupported; đường kính 2 mm đang cố định ở pointer route, chưa có Property bar/Properties hay preview. Smart Trim chưa có hit/interval graph, highlight hoặc command. |
| P3 | Một phần. Có geometry/workflow Target trừ nhiều Source, giữ style/parent/stack và hỗ trợ Keep Source trong workflow. | Chưa có hộp chọn vai trò Source/Target; action hiện dùng phần tử chọn đầu làm Target và ép Keep Source. Smart Fill và Shape Builder chưa được triển khai. |
| P4 | Chưa triển khai. | Raster Eraser và vector clip/mask cho bitmap chưa có. |

Kiểm thử source đã chạy bằng `tooling/native/build.ps1`:

- `test_pikdraw_geometry`, `test_pikdraw_document_path_cut`, `test_pikdraw_vector_cut_tool`, `test_pikdraw_desktop_shell`, `test_pikdraw_document_annotation_model`, `test_pikdraw_annotation_tool_controller`, `test_pikdraw_rendering_culling`, `test_pikdraw_rendering_rasterizer`, `test_pikdraw_rendering_effect_compositor`, `test_pikdraw_effect_draft`, `test_pikdraw_effect_render_warning` và `test_pikdraw_interop_svg_exporter`: **12/12 CTest qua**. Geometry report 58/58; rasterizer có pixel parity và benchmark offscreen riêng, không phải FPS màn hình.
- Build app Release `.\tooling\native\build.ps1 -Test -TestTarget @('pikdraw') -BuildOnly`: **thành công**, liên kết `build/win-dev/apps/desktop/pikdraw.exe` (11,801,600 bytes, 25/10/2026). Đây chỉ xác nhận build/link, chưa phải nghiệm thu chuột/UI runtime.
- Lần chạy regression đầu ghi receipt CTest `succeeded`, nhưng wrapper in lỗi code page khi log có ký tự thay thế Unicode. Đã sửa build broker để thay ký tự console không mã hóa được; `tests/harness/test_build_broker.py`: **20/20 qua**. Chạy lại cùng batch sau sửa runner: **12/12 CTest qua, wrapper trả thành công**.

Các trạng thái trên là một phần tiến độ, không phải chấp nhận hoàn thành AC. Chỉ chuyển từng hàng sang “đạt” sau khi chạy gate đầy đủ của mục 7 bằng đúng bản executable mới. Các constant/action ID cũ nếu còn trong source chỉ là khai báo; không xem đó là đăng ký action hay tool đã dùng được.

Audit UI còn phát hiện Eraser dùng nhầm glyph Dimension (cùng số icon 16 với tool ID); nguồn hiện tách glyph Eraser riêng. Hình vẽ glyph chưa được xác nhận bằng screenshot native.

## 3. Tên gọi và kết quả của từng thao tác

| Công cụ/lệnh | Gesture và kết quả | Đối tượng giai đoạn đầu |
| --- | --- | --- |
| Knife / Dao cắt | Kéo đường thẳng hoặc freehand qua hình đóng; tạo các mảnh theo đường cắt thật, màu/biên ban đầu được giữ. | Path đóng, có fill; path cong được chia theo giao điểm có kiểm sai số. |
| Vector Eraser / Tẩy vector | Kéo đầu tẩy đường kính theo mm; trừ miền quét khỏi fill, tạo lỗ hoặc nhiều mảnh. Path chỉ có stroke mất đoạn tại nơi quét. | Path vector được phép sửa. Không đổi ImageObject. |
| Scissors / Kéo cắt | Nhấp node hoặc vị trí trên segment; mở path đóng hoặc tách path mở thành các subpath với endpoint mới. | EditablePath; hit vào node/segment thực. |
| Smart Trim / Xóa đoạn giao | Rê chuột tô sáng đúng đoạn từ giao điểm trước tới giao điểm sau; nhấp để bỏ đoạn đó. | Đường mở/stroke và self-intersection đã phân giải; ca trùng khít mơ hồ từ chối có lý do. |
| Trim Objects / Cắt bằng đối tượng | Chọn hoặc chỉ định Source và Target; tính Target trừ Source. Target giữ style, nguồn theo Keep Source. | Hai hoặc nhiều path đóng hợp lệ; nhiều source được union có kiểm budget trước khi trừ. |
| Smart Fill / Tạo vùng kín | Nhấp vào một mặt khép kín do nhiều đường tạo ra; sinh PathObject mới ngay trên nhóm đóng góp. | Path vector; gap tolerance mặc định 0 mm. |
| Shape Builder / Ghép-xóa vùng | Kéo qua các mặt đã chọn để gộp; chế độ xóa bỏ mặt được highlight. | Path vector được chọn, dùng chung arrangement với Smart Fill. |
| Raster Eraser / Tẩy ảnh | Quét trên ImageObject để giảm alpha qua edit node bất biến; source ảnh vẫn còn. | Giai đoạn sau, riêng với bitmap/U8/U16. |

Crop ảnh, xóa nguyên object bằng Delete, và bitmap mask/PowerClip là thao tác khác; giữ đường truy cập hiện tại. Text giữ Unicode và tính sửa được: yêu cầu Convert to Curves một cách tường minh trước thao tác vector nếu chưa có phép chia glyph/text thật. Group/PowerClip chỉ sửa nội dung khi đã vào đúng scope; object/layer ẩn, khóa hoặc read-only không bị sửa.

## 4. Cấu trúc logic chung

### 4.1. Một đường dữ liệu từ pointer đến Undo

Pointer/menu/action → ToolSession → TargetResolver → GeometryEditRequest → GeometryEditProposal → DocumentCommand/Transaction → snapshot mới → render/Properties/export.

- ToolSession giữ mode, preference, draft pointer và focus; không giữ bản sao PathObject làm document writer thứ hai. Mở flyout, di chuyển hover hoặc focus Properties không tạo Undo.
- TargetResolver dùng scene hierarchy, z-order, selection, visibility/lock, parent PowerClip, artboard và camera world transform. Hit tolerance ở màn hình chỉ chọn ứng viên; kích thước và sai số hình học luôn ở mm.
- GeometryEditRequest chứa operation, source/target IDs, immutable snapshot, document epoch/revision, world-space gesture, option và budget. Không đọc lại selection khi commit.
- GeometryEditProposal chứa topology output, source-ID provenance, ID mapping cho node/segment, owner layer/parent/stack slot, style/metadata, sai số tối đa và lý do từ chối. Preview và commit dùng cùng proposal; không preview bằng bounds rồi commit path khác.
- DocumentCommand kiểm lại epoch/revision/quyền, apply một lần atomic, publish snapshot mới và ghi một Undo unit. Undo/Redo khôi phục geometry, IDs, layer/stack, selection hợp lý và style. Kết quả đã chuẩn bị trên worker chỉ commit khi còn đúng document/generation.
- Không làm tính toán nặng trong UI event loop: pointer move có preview gọn/cancellable; job thật có quota bộ nhớ/work, progress và cancellation. Hủy, chuyển tab, đổi source hoặc mất capture bỏ draft/proposal cũ.

### 4.2. Hình học và bảo toàn dữ liệu

- core/geometry sở hữu giao line/curve/arc, nearest segment, phân rã mặt, swept nib và Boolean có sai số. Tái dùng find_curve_intersections, split_curve và backend Boolean hiện có. Với giao trùng/tiếp tuyến, trả status rõ; không chọn một nhánh tùy ý.
- Đường thẳng/arc/Bézier chưa bị cắt giữ source segment gốc. Điểm cắt dùng tham số trên curve và PathTopologyMapping. Nếu thuật toán phải flatten, trả approximation_error_bound_mm và provenance; trường hợp vượt tolerance phải từ chối thay vì âm thầm đổi curve thành polygon.
- Lưới clipping hiện có và max_output_points là work budget của phép tính, không phải trần của file. Near-contact, lỗ, self-intersection, fill rule, transform phản chiếu, origin lớn và nhiều trang cần oracle độc lập.
- PathObject chính giữ ObjectId khi còn một mảnh kế thừa xác định; các mảnh khác nhận ID mới và mapping công khai. Với nhiều mảnh, quy tắc chọn mảnh giữ ID phải ổn định (chứa anchor gốc, sau đó diện tích lớn nhất, sau đó thứ tự hình học). Nếu target bị xóa sạch, command báo Removed và rà các reference liên quan.
- Kết quả thay tại đúng parent/layer và vị trí z-order của target. Giữ fill/stroke, gradient, ICC/spot, opacity/blend, overprint, effect, tên/metadata và đơn vị khi semantics cho phép. Nếu một thuộc tính không thể giữ, hiện rõ trước Apply và chỉ chuyển đổi sau thao tác của người dùng.
- Multi-object Trim/Builder ghi một transaction; không Remove rồi Add vào cuối layer hoặc trả success khi execute_command thất bại. Lưu/mở .pr phải giữ đường hình, màu, ID và Undo sau reopen theo contract hiện hành.

### 4.3. Thuật toán theo tool

1. Knife: giao cut polyline với đường bao PathObject thật; sort tham số, chia segment, dựng các loop/mảnh bằng graph nửa cạnh và fill rule. Đường cắt không xuyên vùng không tạo mảnh. Gap/overlap là offset tùy chọn có preview và kiểm tự giao. Giai đoạn đầu chỉ path vector; xử lý bitmap bằng asset/mask riêng.
2. Scissors: nearest node/segment trong selected path, split_curve ở t được kiểm; mở loop hoặc chia subpath, bảo toàn handle và tangent. Nhấp ngoài path không ghi command.
3. Eraser: tạo swept region từ samples pointer và nib round/square (không tự đổi cỡ theo zoom); filled path dùng Difference, open path dùng các khoảng giao dọc centerline rồi split/loại khoảng. Một drag là một Undo. Pressure/tilt chỉ có hiệu lực sau khi backend lưu đầy đủ mẫu và preview khớp.
4. Smart Trim: xây intersection graph trên tập path hợp lệ trong scope, chia tham số mỗi segment thành interval. Hover chọn interval gần pointer, highlight endpoint và object ID; click xóa đúng interval. Trùng đoạn/coincident hoặc nhiều interval đồng hạng phải yêu cầu người dùng chọn rõ hoặc báo lỗi, không xóa bừa.
5. Trim Objects: Source(s) và Target là vai trò hiển thị riêng; Difference dùng kernel Boolean. Kết quả rỗng là thao tác có cảnh báo/preview riêng; Preserve Source mặc định bật cho workflow mới. Existing Difference giữ action và semantics riêng cho đến khi migration UI được xác nhận.
6. Smart Fill/Shape Builder: arrangement từ những đường biên được chọn/hiển thị hợp lệ; point-in-face với fill rule và hole; không lấy biên từ pixel/stroke thickness. Default gap = 0, gap khác 0 chỉ nối khi có đường bridge hiển thị rõ và sai số được chấp nhận. New face tạo path chỉnh được; Builder ghi nguồn style từ mặt bắt đầu.
7. Raster Eraser: node alpha erase chứa đường quét trong tọa độ ảnh và policy bit-depth/profile; evaluate theo tile, không sửa encoded source. Bổ sung RasterEditKind có operation ID cố định, codec, schema/version migration, reader legacy và kiểm save/reopen.

### 4.4. Chuyển luồng cũ sang luồng chung

| Điểm gọi hiện tại | Thay đổi cụ thể | Điều kiện tương thích |
| --- | --- | --- |
| native_window_selection.cpp: drag Knife → split_objects_by_line | Route gesture qua ToolSession; resolve target một lần theo selection hoặc topmost editable dưới nét cắt, không quét toàn bộ document khi selection rỗng. | Giữ action.tool.knife, K và slot 6; Esc/capture loss hủy draft như trước. |
| DocumentWorkflow::split_objects_by_line | Trong P1, đổi thành adapter mỏng tạo GeometryEditRequest Knife, lấy proposal rồi gọi một transaction. Xóa nhánh split_rect_by_line cho Path/Text/Image sau khi test cũ chuyển sang phép cắt thật. | Đường gọi cũ chỉ tồn tại trong giai đoạn migration; không nhận kết quả bounds như thể là path cut. |
| DocumentWorkflow::boolean_operation_selected | Tách khâu nhận vai trò Source/Target và build proposal khỏi khâu commit; lệnh Difference hiện hữu giữ ID/semantics action cũ, Trim Objects dùng policy riêng. | Không tái dùng keep_source/keep_target ngầm cho menu mới; kết quả Empty phải hiển thị trước khi có lệnh xóa. |
| trim_bitmap_with_vector | Giữ tên/entry cũ chỉ nếu nó được gắn nhãn crop theo bounds; P4 thay bằng clip/mask ảnh có vùng cắt vector thật. | Không để action Trim mới gọi hàm bbox này. |
| CompositeCommand RemoveObject + AddObject | Thay bằng command thay hình học/nhiều mảnh có validation và rollback atomic, nhận owner parent/layer/stack và mapping từ proposal. | Kiểm `execute_command(...).success` trước khi đổi selection hoặc trả true; Undo là một đơn vị. |

Thứ tự migration: thêm contract và adapter cùng regression cho đường cũ → chuyển input/action sang adapter → thay kernel theo từng tool → bỏ nhánh bounds/chọn toàn document/màu minh họa → kiểm save/reopen và UX. Ở mọi bước, build phải dùng đúng một đường commit; không để UI mới và UI cũ cùng ghi document theo hai phép tính khác nhau.

## 5. UI cụ thể và đồng bộ mã

### 5.1. Toolbox, menu, action

Giữ số ToolboxSlotId::Knife = 6 và phím K. Slot này trở thành nhóm **Cut & Shape / Cắt, tẩy & tạo vùng**; nhấn mặt chính gọi tool dùng gần nhất, nhấn mũi tên mở flyout. Các item được đăng ký theo descriptor chung, hiện theo giai đoạn đã triển khai: Knife, Vector Eraser, Scissors, Smart Trim, Trim Tool, Smart Fill, Shape Builder. Không dựng item disabled giả trước khi có action, model và command. Flyout có phân đoạn Cắt/Tẩy và Vùng/Trim; chiều cao dựa trên item thật và được neo/clamp theo cửa sổ.

| Bề mặt | Đường dùng và ID đề xuất |
| --- | --- |
| Tools > Cut & Shape | action.tool.knife (giữ), action.tool.vector_eraser, action.tool.scissors, action.tool.smart_trim, action.tool.trim, action.tool.smart_fill, action.tool.shape_builder. Mỗi tool có tên EN/VI, icon và enable reason. |
| Object > Shaping > Trim Objects… | action.object.trim_source_target, thao tác một lần trên vai trò source/target rõ; khác action.object.boolean_difference hiện hữu. |
| Toolbox slot 6 | Catalog khai group ID typed, item/action/tool kind, icon, last-used, enabled, tooltip, pointer/keyboard hit rect và paint từ một danh sách. Migrate settings mask mà không đổi bit của Knife hoặc thứ tự slot cũ. |
| Standard toolbar | Không thêm; toolbox là bề mặt chính. |
| Context menu | Trim trên selection hợp lệ là alias của cùng action; không tạo command thứ hai. |
| Shortcut | K vẫn gọi Knife; shortcut mới để “chưa gán” đến khi kiểm xung đột và đăng ký ở shortcut catalog. Alt+Down/mũi tên/Enter/Esc mở, duyệt, chọn, đóng flyout theo chuẩn toolbox. |

Thay các nhánh lặp toolbox_open_group_, flyout_tools[], max_items, chiều cao/vùng bấm ở paint/click/focus bằng ToolboxGroupDescriptor dùng chung. Dùng ToolKind typed cho các tool mới; lớp chuyển đổi giữ active_tool_ == 5 của Knife trong lúc migration, sau đó gom dispatch pointer/cursor/status. Việc mở nhóm không kết thúc draft của tool khác; rời tool khi đang drag dùng chính sách commit/cancel hiện có và thông báo rõ.

### Điều chỉnh Smart Fill: path không có màu tô

- Menu/action `action.tool.smart_fill`, toolbox Cắt & Tạo vùng, icon SVG và nhãn/tooltip hiện có được giữ; không thêm toolbar hay phím tắt.
- Property bar/Properties không thêm trường: phạm vi là các path kín đang hiện, mở khóa, có hoặc không có fill; selection giới hạn nguồn, nếu không chọn thì xét path hợp lệ đang hiện.
- Nhấp bên trong vùng kín, tránh đường biên; tạo path mới có thể sửa và giữ nguồn. Nếu nguồn style không có fill, kết quả dùng màu tô mặc định; nếu có fill, tiếp tục kế thừa style. Undo một bước xóa kết quả.
- Không hỗ trợ path hở, bitmap hay nối khe hở trong thay đổi này. Status hint và focus help hiện có tiếp tục hướng dẫn nhấp vào mặt kín; không bổ sung field hoặc trạng thái mixed.

### Shape Builder: thao tác vùng theo Illustrator

- Dùng action `action.tool.shape_builder` trong Tools > Cut & Shape và toolbox slot hiện hữu, icon SVG và tên Shape Builder; giữ Properties/toolbar và các lệnh preview hiện có. Không thêm thanh, field, shortcut hay focus state mới trong giai đoạn sửa logic.
- Ghép mặt: trừ các mặt đã chọn khỏi nguồn, giữ các phần còn lại và tạo mặt hợp nhất trong một transaction/Undo; xóa mặt trừ khỏi nguồn. Phong cách đầu ra dự kiến lấy từ nguồn tại điểm bắt đầu, giữ profile và đường viền. Path ẩn/khóa và clip không được sửa.
- Kéo qua các mặt của path kín thẳng để ghép; Alt+nhấp/kéo để xóa. Worker chia nét kéo tại giao điểm đường biên, không phụ thuộc mật độ sự kiện chuột. Nhấp từng mặt và xem trước vẫn dùng được, kể cả path cong kín. Chưa hỗ trợ kéo trên path cong, gap, nhánh cong hở hay shortcut Illustrator.
- Hủy preview, thay tab/selection/revision hoặc Escape giữ tài liệu nguyên trạng. Kiểm tra ghép hai vùng, phần nguồn còn lại, style, erase, Undo/Redo và trường hợp toàn bộ nguồn bị hấp thụ.

### Smart Fill: nhấp liên tiếp nhiều vùng

- Giữ action `action.tool.smart_fill`, menu, toolbox, icon SVG, nhãn, property bar và Properties hiện có; không thêm field, trạng thái mixed hay shortcut. Status sau mỗi lần tô hướng dẫn nhấp vùng tiếp hoặc Undo.
- Selection ban đầu giới hạn tập nguồn; khi không chọn, dùng các path hợp lệ đang hiện. Sau khi tạo mặt, Smart Fill giữ tập path nguồn làm selection cho các lần nhấp tiếp, không thay bằng mặt vừa tạo. Mỗi mặt là một Undo; chuyển Pick để chọn và chỉnh kết quả.
- Worker tiếp tục kiểm snapshot, tab, tool và selection trước commit. Gap, đường cong hở và các giới hạn hình học đã nêu bên dưới không thay đổi.

### Smart Fill: vùng từ đường hở thẳng

- Corel Smart Fill tạo path mới từ các biên bao vùng; Illustrator Live Paint gắn màu với nhóm mặt nên là mô hình khác. Thay đổi này tiếp tục dùng action `action.tool.smart_fill`, toolbox, icon SVG, property bar/Properties và status hiện có; không thêm field, shortcut hay menu.
- Chọn các đường hoặc bỏ chọn để xét các path hiện và mở khóa, nhấp trong mặt kín. Bốn đường thẳng hở nối chính xác có thể tạo mặt, nguồn được giữ và Undo xóa một kết quả. Fill kết quả theo quy tắc style hiện có.
- Gap mặc định 0; chưa có điều khiển gap hoặc bridge preview. Nhánh đường hở chỉ hỗ trợ đoạn thẳng, tối đa 512 segment và từ chối giao trùng, mặt lồng nhau mơ hồ; đường cong hở, bitmap và nhóm clip chưa hỗ trợ. Giới hạn này chưa tương đương đầy đủ Corel/Illustrator.

### Pick các đường line hairline

- Giữ action Pick, toolbox, icon, nhãn, tooltip, property bar, Properties và phím tắt hiện có; không thêm field, menu hay toolbar. Focus/status help hiện hành tiếp tục hướng dẫn nhấp và Shift thêm vào selection.
- Đường line có stroke hairline (độ rộng 0) được bắt theo tâm tuyến và dung sai nhấp hiện tại; Window/Crossing cũng nhận đường này. Bounds nhiều selection tính đủ cả đường ngang/dọc. Path khóa/ẩn vẫn bị loại.
- Cách dùng: chọn Pick, nhấp đường hoặc quét khung; Shift thêm đường rồi chọn Smart Fill và nhấp vào mặt kín. Không thay đổi kiểu nét hoặc màu của artwork.

### Điều chỉnh công cụ hình học cho path không tô

- Menu và action, toolbox, toolbar, icon SVG, nhãn và phím tắt giữ nguyên; không thêm control. Property bar và Properties giữ chế độ hiện có, status/help nêu path kín thay vì yêu cầu fill. Không có field scope hoặc mixed mới.
- Knife, Trim Objects, Shape Builder và Boolean xét hình học path kín, độc lập trạng thái fill; kết quả giữ style nguồn/target. Smart Trim sửa đoạn biên và giữ thuộc tính fill: khi path kín bị mở, vùng tô hiển thị có thể thay đổi. Tẩy vector tiếp tục xóa miền tô hoặc đoạn nét tương ứng.
- Người dùng chọn công cụ trong Cắt & Tạo vùng, chọn đối tượng nếu cần, thao tác trên biên hoặc vùng rồi dùng Undo để hoàn tác. Path hở chỉ được nhận ở công cụ có thuật toán tương ứng; bitmap và path nằm trong clip không thuộc thay đổi này.

### 5.2. Property bar và Properties

| Tool | Property bar, tối đa 3–5 control + nút Thuộc tính | Properties tương tác đầy đủ |
| --- | --- | --- |
| Knife | Cut mode: thẳng/freehand; gap 0 mm; chọn phạm vi; nút mở Cut Settings. | Chế độ, gap/overlap, đóng mảnh, giữ outline, snapping, sai số/budget, Apply/Cancel. |
| Vector Eraser | Diameter 2 mm; nib tròn/vuông; scope; nút mở Eraser Settings. | Diameter, angle/roundness, pressure khi có pen input, smooth/reduce nodes có preview, chọn cách xử lý path mở. |
| Scissors | Snap node/segment; nút mở Scissors Settings. | Hit tolerance hiển thị theo DIP, xử lý closed path, chọn endpoint sau cắt. |
| Smart Trim | Scope selected/all editable; highlight; nút mở Smart Trim Settings. | Policy giao trùng, chọn interval, marquee xóa nhiều đoạn, tolerance mm và danh sách lỗi. |
| Trim Tool | Chọn Source/Target; Keep Source; nút mở Trim Settings. | Vai trò object ID rõ, Keep Source/Target, output placement, precision và Apply/Cancel. |
| Smart Fill | Fill swatch, outline, gap 0 mm; nút mở Smart Region Settings. | Màu/profile, outline, gap bridge preview, fill rule, owner layer và Add Region. |
| Shape Builder | Merge/Erase, lấy style từ mặt bắt đầu; nút mở Smart Region Settings. | Mặt tham gia, policy style, gap/precision và Apply/Cancel. |

Diameter/gap/precision là mm thật qua NumericFieldDescriptor và parser đơn vị trang; không lưu pixel màn hình làm độ rộng hình học. Diameter hợp lệ 0.01–1000 mm (default 2 mm); gap 0–1 mm (default 0); tolerance hình học default 0.01 mm nhưng phải báo bound thực. Giá trị bất hợp lệ không cập nhật draft/document. Đây là tool preference cho lần thao tác sau, không tạo Undo; chỉ commit geometry mới tạo Undo. Bar và Properties dùng cùng state/validation, selection/mixed/disabled reason. Khi dock đóng hoặc cửa sổ hẹp, nút Thuộc tính trên bar và đường Tools > Cut & Shape > Tool Settings mở/focus đúng section; mở panel không sửa artwork.

Icon dự kiến dưới apps/desktop/resources/icons: tool-vector-eraser.svg, tool-scissors.svg, tool-smart-trim.svg, tool-trim.svg, tool-smart-fill.svg, tool-shape-builder.svg; Knife dùng/chuẩn hóa icon hiện có. Icon tự thiết kế theo hệ PikDraw, không chép artwork Corel/Adobe. Tooltip hai dòng có tên + shortcut thật và một câu; status hint ngắn theo state, ví dụ “Rê lên đoạn giữa hai giao điểm · Nhấp để xóa · Esc hủy”. Help mỗi tool có 3–5 bước, một ca Undo và giới hạn kiểu đối tượng; English mặc định, Vietnamese đầy đủ.

### 5.3. Ma trận ngữ cảnh

| Tình huống | Hành vi bắt buộc |
| --- | --- |
| Chưa có tài liệu / document content-locked | Tool/menu không commit; hiện disabled reason. |
| Không selection | Knife/Eraser mặc định chỉ nhắm topmost editable dưới gesture; Smart Fill chỉ dùng biên trong scope khả dụng; chế độ all editable phải chọn tường minh. |
| Có selection | Chỉ đối tượng được chọn hợp lệ tham gia, không tự sửa object khác nằm dưới. Trim hiển thị Source/Target; mixed option có nhãn Mixed. |
| Hidden/locked/read-only/khác loại | Bỏ khỏi target và báo số lượng, hoặc từ chối transaction nếu thao tác yêu cầu tất cả. Không tự convert text/bitmap/PowerClip. |
| Draft, IME, nhập số | Phím công cụ không cướp text input; Esc trước tiên hủy draft/field theo thứ tự ưu tiên. |
| Compute bận/stale | Preview cũ bị bỏ; Apply chỉ enable khi proposal đúng epoch/revision/generation; Cancel có tác dụng trước commit. |
| Light/dark, 1366×768, 1920×1080, DPI 100–200% | Flyout luôn trong viewport; hit/paint cùng rect DIP, focus thấy được, icon/label không chồng lên canvas hay Property bar. |

## 6. Chia file và thứ tự triển khai

| Giai đoạn | Thay đổi source dự kiến | Gate trước giai đoạn kế |
| --- | --- | --- |
| P0: chuẩn hóa đường UI/command | Catalog flyout/tool kind trong apps/desktop/src/shell/native_window_toolbox.* và file group nội bộ mới; chuẩn hóa action dispatch, enable, focus, pointer state; giữ Knife ID/shortcut/mask. Thêm geometry request/proposal contract nội bộ và command ReplaceObjectWithPieces. | Menu/toolbox/shortcut đi tới cùng action; mở/đóng nhóm không đổi document; preexisting Knife demo không được báo là cắt path thật. |
| P1: Knife thật + Scissors | core/geometry/src/operations/path_cut.cpp, core/document/src/commands/path_cut_commands.cpp, modules/vector/src/tools/cut_tool_controller.cpp, apps/desktop/src/workflows/document_workflow_cut.cpp và shell tool events/paint tách theo domain. Thay split_objects_by_line bounds bằng phép cắt path; giữ old demo/test chỉ nếu được cập nhật oracle. | Cắt path lõm/cong, transform/group, preserve style/profile/IDs/stack, Undo/Redo, save/reopen; text/bitmap unsupported có hint. |
| P2: Vector Eraser + Smart Trim | core/geometry path_sweep/segment_graph, modules/vector tool controller, command topology interval, Property bar/Properties. | Hole/open subpath và giao/tiếp tuyến đúng; drag một Undo; ambiguous no-op có lý do; preview trùng commit. |
| P3: Trim + vùng thông minh | Dùng Boolean bounded hiện tại cho Trim có policy riêng; arrangement/face picker cho Smart Fill và Shape Builder; thêm menu/action/Properties theo capability. | Source/Target, nhiều source, lỗ/self-intersection, fill/spot/ICC, z-order/parent và ID mapping đúng; no silent flatten. |
| P4: ảnh | modules/raster alpha-erase evaluator/tile, core/document RasterEditKind/codec/migration, apps/desktop raster tool; bitmap trim bằng vector là clip/mask thực, không bbox patch. | U8/U16, alpha, color profile, Undo, save/reopen và reader cũ; cancel/retention/asset quota. |

Không nối tiếp vào file code quá 2000 dòng. native_window_selection.cpp, paint_docks.cpp và menu_actions.cpp đã có nhiều trách nhiệm; phần mới vào file tool/canvas/workflow theo domain, chỉ thêm route ngắn vào file cũ. registry/modules.json vẫn giữ chiều phụ thuộc geometry → document → vector → desktop; core không include apps/desktop.

## 7. Nghiệm thu bắt buộc khi triển khai

| ID | Given / When / Then và cách xác nhận |
| --- | --- |
| AC-CUT-01 | Given path chữ L lõm, Bézier và rotated group; When Knife cắt; Then mảnh theo hình thật, không là hai bounds hình chữ nhật, không mảnh cam, source style/profile vẫn đúng. So với oracle hình học và pixel độc lập. |
| AC-CUT-02 | Given path đóng/mở có node IDs; When Scissors ở anchor/giữa cubic; Then endpoint, tangent và mapping hợp lệ; Undo/Redo và lưu/mở giữ topology. |
| AC-ERASE-01 | Given path có lỗ và stroke mở; When quét Eraser 2 mm; Then đúng vùng quét trong mm, lỗ/winding và phần còn lại vẫn sửa node được; một drag một Undo. |
| AC-TRIM-01 | Given hai hoặc nhiều path chồng nhau trên layer/group khác; When chọn Source/Target rồi Trim; Then chỉ Target bị trừ, Source theo Keep Source, đúng parent/z-order, fill/stroke/gradient/ICC/spot của Target, không đổi object khóa. |
| AC-SMART-01 | Given hai đường giao và self-intersection; When hover/click Smart Trim; Then highlight đúng interval bị xóa; coincident mơ hồ không làm thay đổi tài liệu. |
| AC-REGION-01 | Given vùng kín có hole và một vùng hở; When Smart Fill/Builder; Then chỉ face hợp lệ tạo/sửa path, gap chỉ được nối khi bật và preview xác nhận. |
| AC-UI-01 | Given toolbox một/hai cột, dock mở/đóng và hai ngôn ngữ; When mở flyout bằng chuột/bàn phím, sửa field từ bar/Properties; Then paint/hit/focus/action khớp, giá trị đồng bộ, mở panel không tạo Undo. |
| AC-STALE-01 | Given preview nặng; When sửa source, chuyển tab, Esc hoặc cancel; Then không commit proposal cũ, không giữ worker/output tạm. |
| AC-RASTER-01 | Given bitmap 8/16-bit có alpha/profile; When quét Raster Eraser rồi save/reopen/Undo/Redo; Then alpha thay đúng vùng, encoded source bất biến và reader file cũ vẫn mở được. Chỉ áp dụng P4. |

Test ở core/geometry cho giao/arrangement và sai số; core/document cho ID mapping, transaction, Undo và storage; modules/vector cho tool proposal; apps/desktop cho pointer → action → command → snapshot và hit/paint flyout. Bộ fixture cần near-contact, lỗ, self-intersection, large origin, reflection, stacked transforms, spot/CMYK, locked layer và PowerClip scope. Chạy target qua tooling/native/build.ps1, giữ regression, dọn output theo AGENTS.md. Build/test tự động chưa chứng minh UX nhìn thấy: nghiệm thu native bằng chuột và screenshot ở light/dark/DPI; so khớp preview/commit, không suy FPS từ CPU render test.

## 8. Điều kiện hoàn tất

Từng công cụ chỉ được ghi “đã hỗ trợ” khi đường UI thật → tool session → hình học → command → snapshot → render → save/reopen/export phù hợp → Undo/Redo đi xuyên được với ca chính và ca từ chối. P4 tách trạng thái riêng với vector. Nếu tolerance hoặc fidelity không đạt, giữ source không đổi và báo lý do; không tạo nút đã bấm được nhưng thực hiện phép cắt theo bounds hoặc màu minh họa.
