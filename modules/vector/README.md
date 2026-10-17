# PikDraw vector tools

Trạng thái: Đã tích hợp pick/style/shape, Line/Polyline/Pen, Node Tool, Contour/Offset, Gradient Tool và OSnap/Inference engine vào source chính.

## D09 — Line, Polyline và snap trong desktop

Win32 toolbar có icon Line/Polyline với tooltip và tên hành động; `L`, `Shift+L` và `F3`
lần lượt chọn Line, Polyline và mở bảng Snap. `F8`, `F10` và `Shift+F2` không bị chiếm.
Line đặt điểm đầu/cuối bằng chuột rồi commit thành một path nguồn có thể sửa. Polyline đặt
nhiều node; Enter commit và bắt đầu bản nháp kế tiếp, Backspace bỏ candidate/node cuối,
Escape commit đường đã có đoạn hoặc hủy bản nháp rỗng.

Khi công cụ đang hoạt động, nhập tọa độ bằng cú pháp D06 như `#x;y` cho điểm tuyệt đối đầu
tiên và `@distance<angle` cho điểm tiếp theo. HUD hiển thị tọa độ mm, chiều dài, góc, trạng
thái snap và nội dung đang nhập. Shift khóa góc theo profile (45° PikDraw/Illustrator,
15° Corel; Custom dùng bước đã đặt); Ctrl khóa Ortho.

Bảng F3 bật/tắt snap tổng, Endpoint, Source node, Line midpoint, Grid, Free/Ortho/Polar,
bước cực và profile. Candidate được xây từ snapshot revision chính xác trên một
worker latest-only; khi index chưa sẵn sàng hoặc lỗi, con trỏ thô vẫn dùng được. Thiết lập lưu riêng ngoài file bản vẽ, giới
hạn 4096 byte và thay file nguyên tử.

Kiểm thử consumer: `apps/desktop/tests/test_drawing_tool_controller.cpp` và
`apps/desktop/tests/test_drawing_tool_ui.cpp`.

## Bổ sung pick/style — 2026-10-15

`SelectionModel::query_region` phân biệt Window (chứa cả bounds và viền) với Crossing
(giao source polyline/fill winding/stroke). `set_selection` gắn selection với lifecycle
document. Point picking dùng tolerance từ số pixel của desktop; stroke biến đổi không đều
được kiểm trong source và world, tránh chọn nhầm xa đường viền.

`selection_resize_transform` trả transform binary64 cho tám tay nắm, giữ anchor đối diện;
khóa tỉ lệ/Shift áp dụng cho cả handle cạnh và góc, còn cạnh khi mở khóa chỉ scale một trục.
Shell Pick có tám handle scale cùng handle xoay riêng; preview không sửa source, thả chuột commit
qua `TransformGesture` thành một undo unit và Escape hủy. Commit kiểm document/epoch/revision.
`apply_object_styles` áp patch cho nhiều
đối tượng nguyên tử, giữ overprint riêng của từng đối tượng.

Test mới: `test_pikdraw_editor_selection`; consumer ở `apps/desktop/tests/test_editor_tools.cpp`.
Group hierarchy chưa đầy đủ; text hit bounds gần đúng. Region query vẫn quét các root,
chưa có spatial index. Chưa chứng nhận 100.000 object riêng lẻ.

| Nhóm | Public API | Implementation | Tests |
|---|---|---|---|
| Line/Polyline authoring | `tools/line/path_drawing_tool.hpp`, `precision/numeric/numeric_input.hpp`, `precision/snap/snap_index.hpp` | `src/tools/line/`; native consumer under `apps/desktop/src/tools/` | `tests/tools/line/`; desktop consumer tests |
| Selection/painter order | `selection/selection_model.hpp` | `src/selection/hit_test.cpp`, `hit_geometry.*` | `tests/selection/` |
| Drag move/lifecycle | `gestures/transform_gesture.hpp`, `edit_target.hpp` | Header nhỏ | `tests/gestures/` |
| Shapes/pointer lifecycle | `tools/shape_tools.hpp`, `vector_controller.hpp` | `src/vector_controller.cpp` | `tests/tools/` |
| ToolSession/draft lifecycle | `interaction/tool_session.hpp`, `interaction/input_router.hpp` | `src/interaction/` | `tests/interaction/` |
| Fill/stroke/flags | `gestures/style_tool.hpp` | `src/gestures/style_tool.cpp` | `tests/gestures/` |
| Editable text/font | `gestures/text_tool.hpp` | `src/gestures/text_tool.cpp` | `tests/gestures/` |

Selection xét layer/object theo painter order từ trên xuống; bỏ hidden/locked, không sắp theo ID. Hit filled path dùng winding NonZero/EvenOdd và geometry nguồn qua transform; không dùng bounds rectangle làm quyết định cho path. Stroke lấy khoảng cách source path; tolerance qua inverse transform là bảo thủ khi scale không đều. Join/cap stroked outline đầy đủ chưa có trong model, không coi đây là oracle sản xuất/CNC.

`hit_test_detail` trả `HitQuality`: SourceLines, CurveApproximation hoặc TextApproximation. Curves Q/C dùng subdivision de Casteljau riêng cho interaction, target <=0.01mm world; dừng và báo diagnostic nếu không đạt trong depth20/budget. Dùng distance tới segment để không bỏ qua Bézier collinear overshoot. Private boundary này cần thay bằng API geometry có error/budget rõ ràng khi API đó được nghiệm thu; không thay đường nguồn bằng polyline. Budget cho source lên đến 1M elements cộng 16K subdivision points; regression có path hơn 100K line nodes. Query hiện duyệt layer/object tuyến tính, chưa có spatial index cho tool selection.

Text hiện chỉ có logical bounds xấp xỉ (chưa phải glyph outline/hit grapheme); người gọi có thể tắt bằng `allow_approximate_text=false`. Arc, group hierarchy và approximation vượt budget không được đoán bằng bbox; `HitDiagnostics` báo unsupported/budget. Controller giữ `last_hit_diagnostics()` để UI hiển thị giới hạn. Không claim exact text/curve/CAD hit.

`TransformGesture::begin(start, ids, snapshot)` bắt buộc snapshot có document identity/epoch/revision. Preview không tạo revision; commit từ chối document khác, reopen, revision stale sau edit/undo, cancel, target thiếu/hidden/locked. Cancel/mode change không ghi source. Pointer up luôn dùng tọa độ release cuối ngay cả thiếu event move. Shape bắt đầu từ pointer down, rectangle/ellipse lấy drag bounds thực, có `shape_preview()`; zero-size/cancel không tạo object. Ellipse source hiện là 4 cubic Bézier của geometry factory, không phải analytic CAD ellipse.

Selection thay sau create/delete/text command thành công; failed delete giữ selection. Delete kiểm tra document lifecycle, target/lock/layer và commit cả nhóm trong một transaction. Controller có active layer rõ ràng; layer hidden/locked hoặc index sai bị từ chối. Text insertion phải truyền nội dung/font qua `insert_text`; pointer-up Text không bịa nội dung.

`SetObjectStyleCommand(id, fill, stroke, width, has_fill?, has_stroke?)` cập nhật source màu/alpha và enable flags, undo giữ cả flags lẫn giá trị cũ. Không có giá trị truyền vào bị báo lỗi; text stroke/flag, group style không giả success. `UpdateTextCommand` cập nhật Unicode và/hoặc FontIdentity; `SetTextFontCommand` chỉnh family/PostScript/weight/italic/point size mà không substitute font nguồn. Source validation UTF-8, finite/color/font do document publication thực hiện atomically. Commands đọc staged `doc.objects()/page()` để phối hợp Add+Style hoặc Text+Font trong một transaction; không đọc snapshot cũ rồi đè mất thay đổi trước.

`interaction::ToolSession` giữ snapshot base, draft byte-patches, undo/redo cục bộ và preview tạm; pointer move không tạo document revision. Finish chỉ trả proposal kèm document ID/epoch/revision, còn consumer phải revalidate và commit qua writer. Capture/focus loss rollback gesture, pan có thể tạm dừng cả gesture kéo, profile generation chỉ đổi ở ranh giới gesture, stale live-session base bị hủy. Recovery checkpoint dùng record riêng bounded; sau reopen chỉ được rebind bằng document ID, revision và checksum nguồn khớp, rồi người dùng phải chấp nhận thủ công. Epoch trong checkpoint là nguồn gốc runtime; rebind này không áp dụng cho job/proposal đang chạy. R1 phục hồi payload đã xác nhận và trạng thái finish, chưa phục hồi undo/redo history; tài liệu gốc không tự đổi. `InputRouter` ưu tiên text/numeric/IME/AltGr, giới hạn phím tạm vào canvas và có API nhả Space/S khi mất focus. Win32 shell chưa gọi các API này; đây chưa phải tính năng UI nghiệm thu.

Các giới hạn `max_*_payload_bytes` chỉ chặn tổng kích thước payload logic còn giữ. Chúng không tính vector capacity, overhead của allocator hay bản sao tạm; cần đo peak RAM trong benchmark tích hợp trước khi đặt giới hạn bộ nhớ tiến trình.

Thay đổi API cần consumer review: mọi gọi TransformGesture::begin phải thêm snapshot; tạo shape phải dùng down/move/up; `on_pointer_up` không còn tự tạo hình 100x60 ở vị trí click. Selection manual qua `selection()` không tự xác nhận document identity; controller chỉ cho delete sau bind lifecycle qua pointer interaction hoặc create/insert thành công.

Build/test từ root qua runner chuẩn `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_vector_selection,test_pikdraw_vector_shapes,test_pikdraw_vector_gestures -TestRegex '^test_pikdraw_vector'` (dùng chung `build/win-dev`, không tạo `build/vector-release`). Checks trong tests dùng lỗi runtime, vẫn hoạt động với `NDEBUG`. Các suite này không chứng minh UI render/frame timing hoặc shaping/font fallback.
