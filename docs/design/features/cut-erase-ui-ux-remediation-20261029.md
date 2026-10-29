# Kế hoạch cải tiến UX nhóm Cắt, tẩy và Trim

Ngày: 29/10/2026. Trạng thái: **kế hoạch, chưa triển khai và chưa nghiệm thu UI**.
Phạm vi: Knife, Vector Eraser, Scissors, Trim Objects, Smart Trim; kiểm tính nhất quán với
Shape Builder, Smart Fill và Raster Eraser trong cùng flyout. Mục tiêu là người dùng biết
công cụ đang nhận đối tượng nào, sắp sửa phần nào và vì sao thao tác không tạo thay đổi.

Đây là kế hoạch bổ sung về tương tác cho [đặc tả nhóm công cụ](cut-erase-smart-tools.md)
và [kế hoạch hoàn thiện](cut-shape-completion-crash-recovery-20261029.md), không thay hợp đồng hình học.
Tuân theo [chuẩn UX](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md).
Chỉ thay tài liệu trong lượt lập kế hoạch này. Các sửa selection chưa commit trong checkout được giữ nguyên.

## 1. Chẩn đoán từ source hiện hành

| Phát hiện | Bằng chứng source | Hệ quả / giới hạn kết luận |
| --- | --- | --- |
| Properties Tẩy vector vẽ và bắt click lệch hàng | `native_window_paint_docks.cpp`: Diameter tại `content_y+32`, nib tại `+70`; `handle_cut_tool_properties_click` trong `native_window_chrome_clicks.cpp`: numeric tại `+68`, nib tại `+106` | Lệch 36 DIP; bấm ô số có thể không vào edit, bấm nib có thể kích hoạt ô số. Cần tái hiện native và sửa layout chung trước tinh chỉnh mỹ thuật. |
| Trim dùng cùng hàm cho hai bước | `commit_trim_objects()` trong `native_window_selection.cpp`: chưa có proposal thì chạy job; có proposal thì apply | Cần phân biệt rõ nút Xem trước và Áp dụng; nhấn lần đầu chưa đổi tài liệu là hành vi tính preview, không được trông như bỏ qua click. |
| Vai trò Trim hiển thị bằng ID và số Source | Nhánh tool 20 trong `native_window_paint_docks.cpp`; pointer chọn Target trước, Source sau | Khó biết hình nào là đích/dao cắt khi chồng lấp; cần tên đối tượng, highlight và khả năng sửa vai trò trực tiếp. |
| Preview hiện tại chủ yếu là contour nét đứt | `native_window_cut_geometry_preview.cpp`: đỏ `removed`, xanh phần kết quả; màu hardcode | Chưa phân biệt đầy đủ miền bỏ, miền giữ, giao điểm và mặt được chọn; cần theme tokens, pattern và ký hiệu ngoài màu. |
| Shape Builder nhận điểm mặt trước khi tính hình học | Nhánh `shape_builder_click` ghi seed point; `commit_shape_builder` mới tính/preview | Click được ghi nhận nhưng chưa chắc là mặt hợp lệ. Không dùng marker điểm làm bằng chứng đã chọn được face. |
| Smart Trim có hover worker và nhận click khi job đang chạy | `update_smart_trim_hover_preview`, `start_smart_trim_hover_job`, nhánh `smart_trim_click` | Đã có async; cần làm rõ pending/ready/invalid và chứng minh click không mất, không apply nhầm interval khi rê nhanh. |
| Smart Trim hiện chỉ sửa path không fill; Trim chỉ nhận path có fill | Nhánh Properties Smart Trim, pointer Trim và workflow tương ứng | Chọn được đối tượng không đồng nghĩa sửa được bằng mọi tool. Fill-off phải có lý do tại hover/Properties, không để click rồi im lặng. |
| Đã có status enum, generation, snapshot, cancel và proposal | `cut_geometry_job.hpp`, `native_window_cut_geometry.cpp` | Tái dùng nền tảng hiện tại; cần hợp nhất trình bày trạng thái, tránh status bị pointer/selection ghi đè. |

Các phát hiện trên là review source, chưa xác nhận hình ảnh hay thao tác native. Không coi báo cáo
test cũ là bằng chứng cho phiên bản hiện tại. Chưa kết luận toàn bộ cảm giác không phản hồi do một lỗi duy nhất.

## 2. Quy tắc tương tác chung

1. Mỗi lần hover/down/click/up được phản hồi bằng cursor, highlight hoặc trạng thái nhận việc.
   Không có thay đổi thì nói rõ: không chạm hình, không giao nhau, sai loại, bị khóa, cần chọn thêm hay đang tính.
2. Phân biệt `Chọn được` và `Có thể xử lý`: dùng picker/scope chung, sau đó đánh giá capability theo tool.
   Path kín fill-off vẫn có thể pick theo bản sửa selection hiện tại; không tự bật fill để qua điều kiện Trim.
3. Scope mặc định giữ cách làm hiện hành: selection hợp lệ nếu có; khi rỗng dùng policy của từng tool.
   Hiện `Vùng chọn: N đối tượng` hoặc `Đối tượng trên cùng` rõ trên bar. Không âm thầm xử lý ngoài selection.
   Selection hỗn hợp phải nêu đối tượng bị chặn và cách tiếp tục; không thành công một phần không báo.
4. Hình học nặng chạy worker. Hover dùng latest-only; click được nhận một lần, gắn snapshot/interval/option
   đã xác định, không lấy vị trí chuột mới khi worker hoàn tất. Không xếp hàng vô hạn.
5. Preview kết quả dùng proposal thật; overlay đường quét trong lúc kéo phải được gọi là footprint/draft,
   không trình bày như kết quả hình học đã giải xong. Apply dùng đúng proposal còn hiệu lực.
6. Một thao tác commit là một Undo. Hover, chọn vai trò, đổi preference và mở Properties không dirty tài liệu.
   Undo/Redo đồng bộ selection, vai trò và preview; save/reopen giữ kết quả, không giả định history được lưu.
7. Duy trì chọn liên tiếp sau commit. Esc hủy draft/job/preview theo trạng thái; khi Ready thì bỏ vai trò/điểm
   chọn của session. Đổi tool/tab/page hủy an toàn; input invalid đang nhập phải được resolve trước khi đổi.

## 3. Trạng thái và phản hồi

| Trạng thái | Canvas / cursor | Property bar / Properties | Hành vi |
| --- | --- | --- | --- |
| Không có tài liệu | Cursor mặc định | Tool action disabled, lý do rõ | Không tạo session |
| Ready, chưa có ứng viên | Cursor tool và hint bước đầu | Scope + tham số; Trim ghi thiếu Target/Source | Không giả success |
| Hover hợp lệ | Highlight đúng hình/đoạn/mặt; nib đúng kích thước | Tên ứng viên, thao tác tiếp theo | Không ghi document |
| Hover không hợp lệ | Cursor cấm + lý do ngắn; không đổi selection | Lý do và cách sửa: mở khóa, vào scope, chọn path phù hợp | Không auto-convert text/bitmap |
| Đang kéo | Đường Knife / footprint Eraser và mục tiêu bị chạm | Tham số được chốt cho gesture | Up flush mẫu cuối, release ngoài vẫn kết thúc đúng |
| Đang tính | Giữ draft, ký hiệu đang xử lý | `Đang tính…`, Hủy; chỉ hiện tiến độ thật khi kernel cung cấp | Chặn submit trùng; canvas pan/zoom và Cancel vẫn phản hồi |
| PreviewReady | Miền giữ, miền bỏ, biên mới, legend nhỏ | `Áp dụng`, `Hủy xem trước`; ghi số hình bị tác động | Enter chỉ Apply khi focus canvas, không có IME/numeric draft |
| NoChange / Unsupported / Ambiguous | Giữ artwork, chỉ dấu vị trí nếu có | Lý do cụ thể và cách tiếp tục | Không tạo Undo |
| Cancelled / Stale / BudgetExceeded / ToleranceNotMet | Bỏ overlay không hợp lệ | Thông báo ngắn, cho thao tác lại | Không tự apply hoặc lặp job cũ |
| Applied | Kết quả chọn/highlight ngắn | `Đã cắt N hình thành M mảnh · Ctrl+Z hoàn tác` | Không đè lỗi bằng thông báo thành công chung |

`ToolInteractionState` và presenter chung sinh enable state, label, hint, accessible name và các action
cho menu/bar/Properties. Document command vẫn là writer duy nhất. Message ID đề xuất:
`cut.ready.*`, `cut.target.*`, `cut.busy`, `cut.preview_ready`, `cut.no_change.*`, `cut.rejected.*`, `cut.applied`.
Lỗi gắn với session được giữ đến thao tác có ý nghĩa tiếp theo; mouse move trống không xóa ngay thông báo.
Status chỉ ghi hướng dẫn ngắn; chi tiết kỹ thuật/tolerance nằm trong phần nâng cao, không che canvas bằng log.

## 4. Luồng theo công cụ

| Tool | Luồng đích | Preview / phản hồi cần thấy |
| --- | --- | --- |
| Knife | Chọn Straight/Freehand → kéo qua hình → nhả để tính và commit; tiếp tục cắt | Cursor dao, đường cắt và điểm giao khả dụng; sau up báo đang tính và số mảnh. Click ngắn/không xuyên biên báo không tạo mảnh, không bắt buộc thêm Apply mỗi nhát. |
| Vector Eraser | Chọn đường kính/nib → rê thấy footprint → kéo → nhả commit | Nib đúng mm qua zoom/transform, footprint quét liên tục. Phân biệt tẩy miền fill và xóa khoảng centerline stroke. Click đơn áp nib nếu kernel hỗ trợ, nếu chưa phải nêu rõ cần kéo. |
| Scissors | Rê gần node/segment → highlight điểm cắt → click một lần | Marker node hoặc điểm chiếu, không dùng bbox. Endpoint không cần cắt lại phải báo NoChange; nhiều đường sát nhau cần xác định ứng viên trước click. |
| Smart Trim | Rê vào khoảng giữa giao điểm → highlight đúng đoạn và hai đầu → click xóa đoạn đó | Pending khác Ready; invalid fill/coincident/no intersection có lý do ngay tại tool state. Click khi preview chậm vẫn có dấu đã nhận, một click chỉ một commit. |
| Trim Objects | Chọn Đích → chọn một/nhiều Dao cắt → Xem trước → Áp dụng | Đích nét liền kèm nhãn, dao cắt nét gạch kèm nhãn; vùng sẽ mất có hatch. Có danh sách tên + ID phụ, bỏ từng nguồn, đổi Đích, tùy chọn giữ nguồn. Hai đối tượng có thể đổi vai trò bằng action rõ; nhiều nguồn không tự hoán đổi. |
| Shape Builder | Rê face hợp lệ → click toggle face; kéo qua nhiều face ở giai đoạn sau → Hợp nhất/Xóa → preview → Apply | Highlight toàn face cùng biên/lỗ. Seed chưa kiểm chỉ là Pending; không tăng đếm face hợp lệ khi chưa resolve. Nút chuyển thành Xem trước hợp nhất/xóa rồi Áp dụng đúng mode. |
| Smart Fill | Rê mặt kín → highlight → click tạo path | Face hở/boundary/ambiguous có lý do, không sinh marker như đã tạo đối tượng. Style của path mới được thể hiện trước click. |
| Raster Eraser | Chọn bitmap → nib theo px nguồn → kéo → nhả | Tách rõ px nguồn với mm của Tẩy vector, footprint đúng transform ảnh; preview alpha/checkerboard, không đổi source ảnh gốc. |

Trim/Builder kết quả xóa sạch phải có preview minh bạch và semantics được kernel/command hỗ trợ;
nếu chưa hỗ trợ thì disable Apply kèm lý do. Không đổi empty result thành success giả hay Delete ngầm.
Path kín không fill cho Trim được xem là capability cần đánh giá riêng ở pha sau; ưu tiên trước mắt là
phản hồi đúng hạn chế hiện có, giữ màu/flags của nguồn, không mở rộng semantics chỉ để hết lỗi UI.

## 5. Bề mặt, field và truy cập

Giữ một flyout `Cắt & tạo hình` bên trái; tám tool dùng catalog hiện hữu. Giữ menu Tools hiện có cho lượt
sửa UX này, ghi nhãn theo catalog; chưa tái tổ chức menu cấp cao. Không thêm standard toolbar hoặc panel mới.
Action chọn tool hiện có, prefix `action.tool.`: `knife`, `vector_eraser`, `scissors`, `trim_objects`,
`smart_trim`, `shape_builder`, `smart_fill`, `raster_eraser`. Giữ K, Shift+E, X đã đăng ký; tool khác chưa gán phím.
Action session đề xuất, dùng chung mọi bề mặt: `action.cut.preview`, `action.cut.apply`, `action.cut.cancel`,
`action.cut.reset_roles`, `action.cut.set_target`, `action.cut.remove_source`, `action.cut.swap_roles`.
Không tự gán shortcut mới; Enter/Esc là binding theo session và phải kiểm focus/context.

| Field / control EN – VI | Bề mặt | Binding, scope và hành vi |
| --- | --- | --- |
| Mode – Kiểu cắt | Knife bar + Feature Settings | Straight/Freehand, tool preference; không sửa object. Chốt tại down. |
| Gap – Khoảng hở | Knife bar + Feature Settings | Length theo đơn vị trang, lưu mm; dùng range validator hiện có, số sai giữ text và báo lỗi. |
| Diameter – Đường kính | Eraser bar + Feature Settings | Vector length lưu mm; Raster px nguồn và chỉ bật khi có bitmap hợp lệ. Field nhập trực tiếp, preset là phụ. |
| Nib – Đầu tẩy | Vector Eraser bar + Feature Settings | Tròn/Vuông, cùng binding bar/dock, preview thay ngay khi idle. |
| Scope – Phạm vi | Bar + Feature Settings | Hiển thị selection/topmost hiện hành; chỉ thêm chọn scope khi resolver hỗ trợ, không dựng option rỗng. |
| Target / Sources – Đích / Dao cắt | Trim Properties | Tool-session IDs + tên, số lượng; chọn/focus/bỏ vai trò không sửa document. Đổi vai trò làm invalid preview. |
| Keep sources – Giữ dao cắt | Trim bar + Feature Settings | Boolean tool preference, mặc định giữ; thay giá trị invalid preview, không tạo Undo. |
| Merge / Erase – Hợp nhất / Xóa mặt | Builder bar + Feature Settings | Mode rõ; đổi mode bỏ proposal không tương thích; số face valid/pending tách riêng. |
| Preview / Apply / Cancel | Action Group + tối đa một action chính trên bar | Label và enable theo trạng thái; Preview đang tính thì disabled submit trùng, Cancel luôn có đường dùng. |
| Precision / limits – Độ chính xác / giới hạn | Properties Advanced | Hiển thị capability/error bound thực; chưa có setter thì chỉ đọc và ghi rõ. Không đưa quota kỹ thuật vào hướng dẫn thao tác chính. |

Property bar tối đa 3–5 control chính theo tool, có nút Properties. Dock dùng Header, Transform,
Feature Settings, Action Group: Transform không áp dụng cho preference/draft, chỉ dẫn tới thuộc tính selection
khi thích hợp, không tạo ô transform giả. Scissors/Smart Trim đơn giản có trạng thái ứng viên + lý do và Help;
không nhét slider chưa có semantics. Bar hẹp chuyển tùy chọn phụ vào overflow, giữ action chính và Properties.
Bar/dock/menu đọc cùng presenter; không dùng tọa độ paint và hit-test khai báo riêng.

Icon: tái dùng SVG trong `apps/desktop/resources/icons/tool-{knife,vector-eraser,scissors,trim,smart-trim,
shape-builder,smart-fill,raster-eraser}.svg`; giữ identity catalog, bổ sung state bằng tokens/pattern,
không vẽ chữ cái/emoji thay tool. Tooltip hai dòng: tên + phím thật; câu hành động ngắn.
Ví dụ `Tẩy vector (Shift+E)` / `Kéo để xóa miền hoặc đoạn nét trong phạm vi hiện tại.`
Accessible name đọc được, tab/focus rõ; flyout mở/chọn được bằng bàn phím, không phụ thuộc hover.
Help đề xuất `help.cut.<tool>` được nối Help menu/focus help khi triển khai; hướng dẫn 3–5 bước/tool,
nêu điều kiện, cách hoàn tất/hủy và một ví dụ không có thay đổi. Chưa tuyên bố F1 hoạt động.

## 6. Thứ tự triển khai và cổng hoàn thành

| Pha | Công việc và vùng source | Điều kiện đóng pha |
| --- | --- | --- |
| P0 — Bấm đúng và báo đúng | Sửa lệch hit rect Eraser; tạo layout descriptor DIP chung cho paint/hit/focus/tooltip. Rà top-level chrome route theo `inspector_width()` thay hardcode. Tách label Preview/Apply Trim/Builder; giữ lỗi có nguyên nhân. | Click đúng tâm/mép control nhận đúng action ở dock scroll/resize/DPI; click khoảng trống không đổi tham số. Không còn Apply hai lần cùng nhãn mơ hồ. |
| P1 — Trạng thái dùng chung | Presenter/session/eligibility chung, reason enum, action enable; giữ khả năng pick fill-off nhưng capability tool rõ. Tách domain cut UI khỏi các file shell quá dài theo code-organization. | Menu/toolbox/bar/Properties cùng state; Ready/Busy/Preview/Error/Cancel/Stale có đường ra; không mất click hoặc ghi đè lỗi. |
| P2 — Nhìn thấy tác động | Nib/đường cắt, hover node/interval/face; Target/Source labels; hatch removed/outline retained và theme tokens. Tái dùng proposal/generation, overlay dirty repaint. | Preview đúng phần commit; hủy/đổi tab/tool không còn overlay cũ; mơ hồ không highlight như hợp lệ. |
| P3 — Thao tác liên tục | Sửa chọn vai trò Trim, danh sách Source; Builder face thật/toggle và sau đó sweep nhiều face; đồng bộ field/numeric/IME/overflow/Help. | Luồng người dùng hoàn thành được từ menu/flyout và khi dock đóng; commit một Undo, Undo/Redo và save/reopen đúng. |
| P4 — Nghiệm thu native | Chạy matrix dưới đây trên canonical EXE mới, đo latency/memory, kiểm crash gốc nếu tái hiện được. | Có bằng chứng input → preview → command → Undo/Redo → save/reopen; ghi chính xác ca chưa chạy/thiết bị chưa có. |

Ưu tiên P0 → P1 → P2 cho Knife/Eraser/Trim/Smart Trim trước; hoàn thiện Scissors và nhóm face theo cùng nền tảng.
Không mở rộng engine để che lỗi điều khiển. Nếu khám phá thiếu capability thực, ghi thành item riêng và
chỉ bật UI khi end-to-end hỗ trợ. Không ước ETA từ số bước build hoặc số file.

## 7. Ma trận nghiệm thu khi triển khai

| ID | Given / When / Then và bằng chứng |
| --- | --- |
| UX-CUT-01 | Mở Eraser Properties, scroll/resize; click Diameter/Nib ở vị trí đang vẽ → đúng field/control thay đổi. Kiểm tâm, mép, gap; native pointer và giá trị model, không chỉ gọi handler trực tiếp. |
| UX-CUT-02 | Cùng tham số trên bar/dock, nhập số hợp lệ/sai và IME → hiển thị đồng bộ, lỗi không mất text; focus Properties không dirty document. |
| UX-CUT-03 | Không selection, một/nhiều hình, fill on/off/transparent, stroke-only, image/text, hidden/locked, PowerClip → hit đúng scope; tool không hỗ trợ báo lý do và cách tiếp tục. |
| UX-CUT-04 | Knife/Eraser down-move-up, zigzag, release ngoài, click ngắn, không chạm hình → footprint đúng; kết quả/no-op rõ; một Undo/Redo. |
| UX-CUT-05 | Trim chọn Đích/Dao, bỏ từng nguồn, đổi vai trò, Keep Sources, hai hình chồng hoàn toàn/không giao → phân biệt Preview/Apply; đối tượng và vùng sẽ mất đúng nhãn. |
| UX-CUT-06 | Smart Trim hover chậm/nhanh rồi click trước/sau preview → xóa đúng interval một lần; pending không nuốt click; ambiguous/filled path có lý do. |
| UX-CUT-07 | Scissors và Smart Fill/Builder trên curve, hole, near-contact → đúng node/face, không coi seed/bbox là geometry hợp lệ. |
| UX-CUT-08 | Job đang chạy, Esc/right-click, capture loss, đổi tool/tab/page, sửa revision/đóng tài liệu → không commit stale, không crash, không chờ worker trong UI event. Normal mouse-up không vô tình cancel job mới. |
| UX-CUT-09 | Commit → Undo/Redo → lưu/mở .pr → chọn/sửa kết quả → IDs, style, flags, hierarchy và geometry hợp lệ; không yêu cầu history tồn tại qua reopen. |
| UX-CUT-10 | Light/dark, VI/EN, cửa sổ 1366×768 và 1920×1080, scale 100/125/150/200% khả dụng → icon/hit/focus/help và control không cắt chữ; keyboard thao tác được. Ghi ca chưa có thiết bị. |
| UX-CUT-11 | Fixture nhỏ/vừa/nặng, ghi cấu hình máy/backend/số object và segment → đo p50/p95 input-to-overlay, job, cancel và peak RAM; mục tiêu ban đầu hover feedback p95 ≤50 ms, báo Busy ≤100 ms, phản hồi Cancel ≤100 ms. Đây là mục tiêu cần đo, chưa là kết quả hoặc cam kết kernel dừng trong 100 ms. |

Test mở rộng trong suite hiện có: vector selection/cut/node, geometry/document path cut,
desktop native canvas và storage fidelity theo code thực sự thay đổi. Target/CTest mapping phải đối chiếu
CMake trước khi chạy. Build/test chỉ qua `tooling/native/build.ps1`, dùng `-Status/-Submit/-Wait` đúng request,
giữ cấu hình Auto; cuối lượt build canonical `pikdraw` rồi kiểm bằng chuột native trên EXE mới.
Screenshot tĩnh/test API không thay thế kiểm tra gesture. Output tạm vào thư mục runner, đọc rồi dọn theo AGENTS.

**Kết quả của lượt này:** kế hoạch dựa trên source; chưa sửa code, chưa chạy test/build hoặc xác nhận UI runtime.
