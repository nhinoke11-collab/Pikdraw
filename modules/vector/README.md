# PikDraw vector tools

Trạng thái: pick/style/shape cơ bản có consumer Win32 trên nhánh editor-pick-style-delivery-20261015.
ToolSession/input routing dưới đây là core API; chưa nối vào cửa sổ desktop cho đến task D09.

## Bổ sung pick/style — 2026-10-15

`SelectionModel::query_region` phân biệt Window (chứa cả bounds và viền) với Crossing
(giao source polyline/fill winding/stroke). `set_selection` gắn selection với lifecycle
document. Point picking dùng tolerance từ số pixel của desktop; stroke biến đổi không đều
được kiểm trong source và world, tránh chọn nhầm xa đường viền.

`selection_resize_transform` trả delta binary64 cho tám tay nắm, giữ anchor đối diện;
Shift giữ tỉ lệ ở góc. `TransformGesture::update_transform` chỉ giữ preview, commit kiểm
document/epoch/revision và tạo một undo unit. `apply_object_styles` áp patch cho nhiều
đối tượng nguyên tử, giữ overprint riêng của từng đối tượng.

Test mới: `test_pikdraw_editor_selection`; consumer ở `apps/desktop/tests/test_editor_tools.cpp`.
Group hierarchy chưa đầy đủ; text hit bounds gần đúng. Region query vẫn quét các root,
chưa có spatial index. Chưa chứng nhận 100.000 object riêng lẻ.

| Nhóm | Public API | Implementation | Tests |
|---|---|---|---|
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

Thay đổi API cần consumer review: mọi gọi TransformGesture::begin phải thêm snapshot; tạo shape phải dùng down/move/up; `on_pointer_up` không còn tự tạo hình 100x60 ở vị trí click. Selection manual qua `selection()` không tự xác nhận document identity; controller chỉ cho delete sau bind lifecycle qua pointer interaction hoặc create/insert thành công.

Build từ root Ninja/Clang C++20 Release, `-DPIKDRAW_REQUIRE_M1_TARGETS=OFF` khi thiếu desktop ở base. Targets: `test_pikdraw_vector_selection`, `test_pikdraw_vector_shapes`, `test_pikdraw_vector_gestures`. `ctest --test-dir build/vector-release -R test_pikdraw_vector --output-on-failure`. Checks trong tests dùng lỗi runtime, vẫn hoạt động với `NDEBUG`. Các suite này không chứng minh UI render/frame timing hoặc shaping/font fallback.
