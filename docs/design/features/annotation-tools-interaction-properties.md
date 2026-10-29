# Triển khai lại thao tác và thuộc tính của nhóm Chú thích

Ngày: 25/10/2026. Trạng thái: **đang triển khai một phần; chưa nghiệm thu đầy đủ**. Tiến độ và giới hạn hiện tại ghi ở mục 14.

Đây là đặc tả chi tiết cho Detail View, Leader Note, Plain Note và việc sửa chúng bằng Select. Nó cụ thể hóa yêu cầu UX mới của người dùng và thay phần gesture/Properties A1 tương ứng trong [plan tổng](drawing-annotations-and-detail-views.md). Các mốc Balloon, Revision Cloud và ký hiệu kỹ thuật khác vẫn theo plan tổng. Workflow theo [AGENTS](../../../AGENTS.md); giao diện theo [UI standard](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md) và [phiếu UI](../feature-ui-template.md).

## 1. Kết quả phải đạt

Detail View phải thao tác được bằng một lần kéo và một lần click: **nhấn giữ trái tại điểm gốc → kéo chọn vùng nguồn → thả trái để khóa vùng → di chuột đặt preview → click trái một lần để đặt view**. Ở bước đặt, con trỏ đổi thành dấu góc vuông `┌`; hotspot tại góc trên trái frame, cùng tọa độ với preview và dữ liệu commit. Không phải giữ chuột kéo frame lần thứ hai.

Người dùng chọn vùng tròn/chữ nhật, tỉ lệ và nhãn ngay trên Property bar; có Properties đầy đủ để nhập chính xác. Leader Note và Plain Note có nội dung text, font, cỡ chữ, màu và các thiết lập phù hợp. Chọn lại bằng Select phải xem/sửa đúng đối tượng đã tạo, di chuyển hoặc kéo handle mà không tạo một annotation mới.

Phạm vi triển khai gồm gesture, cursor, draft, text editor, các bề mặt thuộc tính, thao tác Select, liên kết leader cần cho các thao tác này, persistence và regression liên quan. Giữ kiến trúc Win32/Direct2D hiện có. Chưa chuyển toàn bộ annotations sang `ObjectVariant` mới trong task này; adapter metadata phải có semantics rõ và không làm mất dữ liệu.

## 2. Baseline source đã đối chiếu

| Vị trí/symbol | Hiện trạng và lỗi cần giải quyết |
| --- | --- |
| `apps/desktop/src/tools/annotation/annotation_tool_controller.{hpp,cpp}` | Có `DetailSourceDragging → DetailFramePlacing`; frame đang được căn tâm theo chuột và commit ở pointer press. Circle quá nhỏ tự đổi thành R10 mm; rectangle cạnh nhỏ tự đổi thành 20 mm; release còn thêm offset 10 mm. Đây là nguồn nhảy vị trí và vùng chọn ngoài ý muốn. |
| Controller: `set_detail_shape`, `set_scale_ratio`, `set_detail_label` | Có setter nhưng chưa tạo thành luồng field đầy đủ trên cả bar/Properties. Note text bị clear ở các bước bắt đầu editor; không được làm mất nội dung đã nhập qua UI/template. |
| `native_window_selection.cpp`: các nhánh `active_tool_ == 17` | Gọi controller trực tiếp; cần contract rõ về capture, button sequence, click cuối, thành công/thất bại, đổi Select và selection. Không để mouse-up sau commit tiếp tục chạy Pick. |
| `native_window_events.cpp`: `WM_SETCURSOR`; `NativeWindow::canvas_cursor_at` | API trả `WORD` cho system cursor và gọi `LoadCursorW(nullptr, id)`. Cần bổ sung cursor semantic/custom có hotspot thật, không giả lập bằng một icon không đổi con trỏ. |
| `core/rendering/include/pikdraw/rendering/camera.hpp` | World Y hướng lên, screen Y hướng xuống. Với frame không xoay, góc trên trái world là `(rect.x, rect.y + rect.height)`. Không gán trực tiếp `rect.y = mouse.y` khi đặt từ góc trên trái. |
| `native_window_canvas_overlays.cpp`: `paint_annotation_canvas_overlay` | Đã có marker/frame/leader preview. Cần preview nội dung phóng to, vị trí đặt, handles và invalidation đồng bộ với controller. |
| `core/document/.../annotation_object.hpp`, `annotation_layout.cpp`, `annotation_commands.cpp` | Có Circle/Rectangle, uniform scale, style, Add/Update commands và adapter `get_annotation/get_detail_view`. Runtime vẫn là `PathObject` + `contour_spec`; wrapper transform là dữ liệu có hiệu lực, cần xử lý đúng khi sửa selection. |
| `evaluate_detail_view_layout` | Label scale đang format số lẻ với một chữ số thập phân. Connector rectangle hiện lấy bán kính từ cạnh nhỏ thay vì giao ray với biên rect. Cần sửa cùng semantics shape/ratio. |
| `action_id.hpp`, `composition_root.cpp`, `desktop_window.cpp`, shell menu/toolbox | Annotation chưa có cùng mức tích hợp action như các tool chuẩn; flyout đang gọi activate trực tiếp. Phải trace cả registration, enable, menu, keyboard và pointer. |
| `test_annotation_tool_controller.cpp` | Có test chỉ khẳng định click cuối thành **tâm** frame. Phải đổi oracle theo yêu cầu góc trên trái, kiểm tọa độ thực và native pointer route; không chỉ đổi tên test. |

Các kết quả build/test cũ không chứng minh UX mới này. Ảnh người dùng cung cấp thể hiện nhóm ba tool hiện có; nó không phải bằng chứng các thuộc tính hoặc thao tác đã chạy đúng.

## 3. Detail View: gesture, hình học và con trỏ

### 3.1. Máy trạng thái bắt buộc

| State | Sự kiện | Xử lý và state tiếp theo |
| --- | --- | --- |
| `Ready` | Left down trong canvas | Lưu điểm gốc world, document/page/session; capture chuột; vào `SourceArmed`. Không ghi document. |
| `SourceArmed` | Move vượt ngưỡng kéo 3 DIP | Vào `SourceDragging`; circle lấy điểm down làm tâm, rectangle lấy làm góc thứ nhất. |
| `SourceArmed` | Left up trước ngưỡng | Trở lại `Ready`, bỏ capture/draft hình học; hint yêu cầu kéo vùng. Không tự tạo R10 hoặc rect 20 mm. |
| `SourceDragging` | Move | Cập nhật vùng từ tọa độ pointer hiện tại, cùng hàm hình học dùng khi release; preview và field đọc cùng draft. |
| `SourceDragging` | Left up hợp lệ | Flush mẫu move đang coalesce; lấy chính tọa độ up; khóa source region; bỏ capture; vào `FramePlacing`, đổi cursor `┌`. Sự kiện này chỉ kết thúc vùng nguồn. |
| `FramePlacing` | Move không giữ nút | Di chuyển ghost frame bằng góc trên trái; source region giữ nguyên. |
| `FramePlacing` | Left down trong canvas | Ghi nhận sequence đặt, capture ngắn; vào `PlacementPressed`. Cập nhật preview ngay cả khi không có move trước down. |
| `PlacementPressed` | Left up hợp lệ | Lấy điểm up làm điểm đặt cuối; validate và commit đúng một Add command; chọn owner mới và chuyển Select sau khi đã consume cả sequence. Không đòi kéo thêm. |
| `PlacementPressed` | Release ngoài canvas | Không commit; bỏ capture, trở lại `FramePlacing`; giữ source và thiết lập. |
| Mọi state có draft | Esc/right click | Hủy toàn bộ draft, về `Ready`, giữ defaults hợp lệ; không Undo, không layer/object rác. Right click không mở context menu trong cùng sự kiện hủy. |
| Mọi state | Document/page đóng hoặc thay, đổi mode có chủ đích, mất capture bất thường | Hủy session đang thao tác; invalidate job/cursor/overlay; không dùng `Document&` của tab cũ. Mất capture chủ động khi chuyển `FramePlacing` không được hiểu nhầm là hủy. |

Release ngoài canvas khi đang quét nguồn hủy lượt quét; không dùng tọa độ của dock làm vùng nguồn. Canvas gồm trang và pasteboard hiện có. Pan/zoom tạm dùng cơ chế chung; giữ điểm nguồn trong world, không commit vì camera đổi. Mọi event trước click/release phải flush coalescer để không commit vị trí cũ.

Một click đặt là down/up bình thường; down không tự tạo object. Double-click sinh thêm message không được tạo view thứ hai hoặc kích hoạt ngay thao tác sửa trên object vừa tạo.

### 3.2. Quy tắc hình học và điểm đặt

- **Rectangle:** hai góc đối diện; normalize min/max để kéo được bốn hướng. Shift giữ hình vuông, có hint; Ctrl không bổ sung nghĩa mới trong đợt này.
- **Circle:** down là tâm, bán kính là khoảng cách world tới pointer. Không đổi circle thành ellipse khi kéo hoặc resize.
- Ngưỡng DIP chỉ quyết định gesture có phải kéo. Validator dùng số hữu hạn và tolerance hình học; không áp trần dưới 2 mm. Vùng chính xác nhỏ hơn có thể nhập trong Properties khi hợp lệ.
- `frame_width = source_width × scale_ratio`, `frame_height = source_height × scale_ratio`. Không có stretch độc lập X/Y.
- Điểm đặt `P` là **góc trên trái của frame nhìn trên trang**, không phải tâm, tâm vòng tròn hay bounds của label/connector. Với camera Y-up hiện tại và frame identity: `frame_rect = {P.x, P.y - frame_height, frame_width, frame_height}`. Circle dùng cùng góc trên trái bounding square.
- Ngay sau thả vùng nguồn, ghost dùng điểm release làm P đầu tiên; không cộng offset 10 mm. Lần move/click kế tiếp cập nhật cùng anchor, không nhảy cách căn.
- Source marker, frame, connector và label cùng một logical owner; label có thể nằm ngoài frame nhưng không làm đổi placement anchor.
- Connector Circle dùng giao ray với đường tròn; Rectangle dùng giao ray với cạnh rect. Frame/source giao nhau hoặc cùng tâm phải cho kết quả hữu hạn, không vẽ đoạn đảo hoặc cắt xuyên nội dung vì giả định bán kính.
- Dùng một `parse/format_scale_ratio` chung cho field, label và help. Nhận `2:1`, `1:2`, `2.5`; giữ range hiện tại `[0.1, 50]`, hai vế dương, mẫu khác 0. Không format `1.25` thành `1.2:1`; nhãn biểu diễn đúng scale đã lưu với precision đủ dùng.

Ví dụ oracle bắt buộc, không phụ thuộc zoom:

| Source | Scale | Click đặt góc trên trái | Frame được lưu |
| --- | --- | --- | --- |
| Rect từ `(10,10)` tới `(40,30)` → `{10,10,30,20}` | 2:1 | `(130,120)` | `{130,80,60,40}` |
| Circle tâm `(20,20)`, R10 → `{10,10,20,20}` | 3:1 | `(100,100)` | `{100,40,60,60}` |

### 3.3. Cursor và phản hồi từng bước

| Ngữ cảnh | Cursor | Status EN / VI |
| --- | --- | --- |
| Ready/SourceDragging | Crosshair + badge shape nhỏ | `Drag to select the source region · Esc cancels` / `Giữ và kéo để chọn vùng nguồn · Esc hủy` |
| FramePlacing/PlacementPressed | Góc vuông `┌`, hotspot đúng đỉnh góc | `Click to place the top-left corner · Esc cancels` / `Nhấp đặt góc trên trái ô chi tiết · Esc hủy` |
| Note text editor | I-beam | `Type note text · Enter new line · Ctrl+Enter finishes` / `Nhập ghi chú · Enter xuống dòng · Ctrl+Enter hoàn tất` |
| Select trên body/handle | Move/resize/anchor cursor tương ứng | Hint đúng part và phép sửa, không hiện hint tạo mới. |

Tạo cursor thật qua resource/cache Win32 có lifetime rõ, tương phản light/dark, theo DPI; hotspot không dịch theo độ dày nét hoặc zoom. `WM_SETCURSOR` phải chọn được standard/custom descriptor; giữ adapter cho test/caller dùng system IDs hiện hữu. Ngoài canvas trả cursor của control đang hover. Không chỉ vẽ `┌` trong overlay trong khi Windows vẫn dùng cursor khác.

Dùng snap solver chung cho điểm nguồn/điểm đặt và chỉ áp snap một lần mỗi event. Khi bật snap, P hiệu lực là candidate đã snap; HUD/Properties/frame cùng dùng P và hiển thị snap indicator. Không warp OS pointer; khoảng lệch raw pointer tới snap candidate phải nhìn thấy qua phản hồi snap, không trở thành offset ẩn. Editing loại owner đang sửa khỏi các candidate tự snap.

## 4. Leader Note và Plain Note

**Leader Note:** chọn `Straight / Elbow` trước khi vẽ. Straight: click anchor → click vị trí text → nhập. Elbow: click anchor → click elbow → click vị trí text → nhập. Move chỉ thay điểm preview đang chờ, không sửa anchor đã chọn. Nguồn hợp lệ dùng source reference/snap hiện có; điểm không gắn nguồn là Fixed, hiển thị đúng trạng thái.

**Plain Note:** click vị trí text → hiện caret/editor ngay → nhập. `landing_point` và leader không tham gia bounds/hit-test của Plain Note. Nếu đã nhập nội dung ở scope Tạo mới trước đó, dùng nội dung này; không clear âm thầm khi click canvas.

Hai tool dùng chung text editor binding với typography hiện hữu: caret, chọn text, Home/End, Delete/Backspace theo grapheme, clipboard, undo nội bộ khi đang draft, Unicode/IME composition và font fallback. Bản nháp text là dữ liệu của annotation, không thêm một TextObject phụ hoặc path chữ vào document để làm editor.

Enter xuống dòng; Ctrl+Enter hoàn tất; Esc hủy draft/sửa text. Với object đã tồn tại, hủy khôi phục nội dung trước khi edit. Click canvas ngoài editor hoàn tất nội dung hợp lệ rồi **consume click đó**; không đồng thời tạo note thứ hai hay bắt đầu drag Select. Click Property bar/Properties chuyển focus vào cùng session và không kết thúc text editing. `V`, Space, Delete khi editor/IME đang giữ focus là input của editor theo keymap text, không vô tình chuyển tool/xóa object.

Click một tool khác bằng chuột khi đang nhập: hoàn tất nội dung hợp lệ rồi chuyển tool; note mới chỉ có whitespace thì hủy note; lỗi dữ liệu giữ editor và focus field lỗi, không hiện hộp hỏi lặp. Close/switch document hủy draft theo lifecycle chung. Chỉ coi composition đã commit là text hoàn tất; không commit giữa chừng một IME composition.

Sau tạo thành công, chuyển `action.tool.select`, chọn logical owner mới, giữ mặc định style của mode để lần kích hoạt sau dùng lại. Nội dung ghi chú không tự trở thành mẫu cho note tiếp theo; font/cỡ/style được nhớ theo mode. Template text chỉ thay khi người dùng sửa scope Tạo mới rõ ràng.

## 5. Menu, flyout và Property bar

| Bề mặt | Quyết định triển khai |
| --- | --- |
| Menu chính | `Tools > Annotation > Detail View`, `Leader Note`, `Plain Note`; chỉ ba entry có capability thật. |
| Action IDs | Đăng ký `action.tool.annotation.detail_view`, `.leader_note`, `.plain_note`; đây là IDs đề xuất trong plan tổng, cần thêm registration thật ở các composition entrypoint đang dùng. |
| Properties | `action.annotation.properties.focus` mở/focus section đúng loại trong dock hiện có. `Object > Properties` / `action.object.properties.focus` là alias theo selection. Chỉ mở/focus không ghi document. |
| Sửa đặc biệt | `action.annotation.text.edit`, `.source_region.edit`, `.frame.place`, `.navigate_source`, `.relink`, `.detach`; enable theo owner/kind/state, không handler rỗng. |
| Toolbox | Giữ `ToolboxSlotId::Annotation = 14` và flyout group 7; giữ slot/mask cũ. Main click gọi mode dùng gần nhất. Arrow mở flyout; Alt+Down/arrow/Enter cùng action với chuột. Chọn lại đúng mode đang dùng chỉ focus options, không reset draft. Hover/focus/gap không đổi active tool hoặc hủy draft. |
| Standard toolbar | Không thêm nhóm toolbar mới. |
| Shortcut | Select giữ `V`; shortcut global mới chưa gán. Ctrl+Enter/Esc là local editor/session bindings được ghi help. |
| Context menu | Select annotation: Properties; Edit Text với notes; Edit Source Region/Reposition Frame/Go to Source với view; Relink/Detach với leader khi hợp lệ. |

Bar tối đa 3–5 control nhanh, ngoài nút mở Properties gọn. Không cộng thêm cả dãy X/Y/W/H vào cùng bar feature; tọa độ đầy đủ ở Properties. Khi Select một annotation, bar dùng context của loại đó và badge `Đối tượng`; khi tool tạo mới chưa có draft, badge `Tạo mới`; khi đang thao tác, badge `Bản nháp`.

| Context | Controls nhanh bắt buộc | Properties link |
| --- | --- | --- |
| Detail View | Shape Circle/Rectangle, Scale, Label, Connector | `Thuộc tính ô chi tiết` |
| Leader Note | `Nội dung…`, Font, Text height, Straight/Elbow | `Thuộc tính ghi chú` |
| Plain Note | `Nội dung…`, Font, Text height | `Thuộc tính ghi chú` |

`Nội dung…` là control chỉnh sửa thật: click mở/focus cùng inline editor hoặc ô nội dung đầy đủ trong Properties, hiển thị preview dòng đầu và số dòng. Không dùng preview rút gọn để overwrite nội dung nhiều dòng. Text field dài không nới rộng bar; Properties là nơi nhập đầy đủ khi cần.

Ở 1280×720/1366×768, thu field phụ theo priority nhưng luôn truy cập được nút Properties. Không giảm font để nhét, không tràn lên canvas. Paint/hit/focus dùng cùng layout descriptor; không viết hai bộ rect khác nhau cho bar và click.

## 6. Properties đầy đủ và field bindings

Properties chia Header → Geometry → Feature Settings → Text/Appearance → Actions. Mọi field hỗ trợ đều là control tương tác; field read-only ghi rõ vì sao. Dùng semantic field keys + `NumericFieldDescriptor`/unit parser hiện hữu, không parse số riêng ở paint/click.

| Field key / EN · VI | Surface và kiểu/default | Binding, quy tắc |
| --- | --- | --- |
| `annotation.detail.shape` / Shape · Vùng quét | Bar + Properties, Circle/Rectangle; default Circle | Cùng shape cho source và clip frame. Khi đang giữ nút quét nguồn thì disabled; đổi sau khi release/đã chọn phải preview trước commit. |
| `annotation.detail.scale` / Scale · Tỉ lệ | Bar + Properties; 2:1 | Ratio parser `[0.1,50]`; đổi scale giữ source region và góc trên trái frame, tính lại W/H. |
| `annotation.detail.label` / Label · Nhãn | Bar + Properties, text; auto next unused `DETAIL A` | Nhãn không chứa scale đã format. Kiểm UTF-8/byte limit bằng validator chung; trùng nhãn cùng trang báo tại field, giữ draft. |
| `annotation.detail.connector` / Connector · Đường nối | Bar + Properties, toggle; on | Dùng `show_connector`; toggle không đổi source/frame. |
| `annotation.detail.source.*` / Source region · Vùng nguồn | Properties; Rect X/Y/W/H hoặc Circle Cx/Cy/R, theo đơn vị trang | Base mm hữu hạn. Source page hiển thị tên/ID read-only ở A1 cùng trang. Sửa region giữ scale và frame top-left, cập nhật frame size. |
| `annotation.detail.frame.*` / Frame · Ô chi tiết | Properties; Top-left X/Y, W/H readout, uniform size input | X/Y là góc đặt đã định nghĩa, không phải min-Y của Rect2D. Sửa W hoặc H suy scale duy nhất; nếu hai field paste mâu thuẫn thì từ chối nguyên thay đổi. |
| `annotation.note.text` / Content · Nội dung | Bar launcher + Properties multiline + inline editor | Một text buffer binding; UTF-8, multiline, IME; giữ giới hạn 64 KiB hiện có và kiểm trước append/paste; không cắt giữa code point/grapheme. Commit yêu cầu nội dung không chỉ whitespace. |
| `annotation.note.font` / Font · Phông | Bar + Properties; Segoe UI | Font identity gốc; fallback chỉ là render, có báo trong Properties. |
| `annotation.note.text_height` / Text height · Cao chữ | Bar + Properties; 2.5 mm trang | Quantity Length, không lẫn FontPoint. Có thể hiển thị giá trị pt phụ đã quy đổi; canonical vẫn mm trang, độc lập zoom và scale Detail View. |
| `annotation.note.alignment` / Alignment · Căn chữ | Properties; Left/Center/Right, default Left | Dùng semantics paragraph typography; thêm field codec v2 nếu model hiện thiếu. |
| `annotation.note.line_spacing` / Line spacing · Giãn dòng | Properties; positive scalar/percent, default 100% | Cùng paragraph layout và đo glyph thật; cần lưu codec v2. |
| `annotation.leader.mode` / Leader · Đường dẫn | Bar + Properties; Straight/Elbow, default Elbow | Chuyển mode giữ anchor và vị trí text; tạo/bỏ elbow có preview; derive từ geometry đã lưu, không giữ UI enum lệch geometry. |
| `annotation.leader.anchor` / Anchor · Điểm neo | Properties; state + coordinates/reference | Fixed/Valid/Partial/Broken có nhãn; source ID/element/parameter read-only, Relink/Detach là actions thật. |
| `annotation.leader.arrow` / Arrowhead · Mũi tên | Properties, enum + size 2 mm | Dùng ArrowType đã hỗ trợ, không hiện enum chưa render được. |
| `annotation.leader.landing` / Landing · Đoạn đáp | Properties; toggle on, length 4 mm, gap 1 mm | Disabled với Plain Note. Layout và bounds dùng cùng style/draft. |
| `annotation.appearance.line_width` / Line width · Nét | Properties, 0.25 mm | Core validator hiện hành, không phóng theo zoom; Plain Note không có control nét leader. |
| `annotation.appearance.color` / Color · Màu | Properties, ColorValue hiện có | Giữ RGB/CMYK/spot/profile và alpha; một control source màu dùng chung, không đổi RGB chỉ do UI preview. |
| `annotation.detail.label_height` / Label height · Cao nhãn | Properties, 2.5 mm | Dùng `DetailViewObject::text_size_mm`; không tự thêm font field khi model/codec chưa giữ được. |
| `annotation.layer` / Layer · Lớp | Properties; tên + effective flags + Find in Layers | Destination theo LayerId/role; source snap scope và annotation destination là hai dữ liệu riêng. Không tự bật/mở khóa group khi chọn tool. |

Đổi shape Rectangle → Circle: giữ tâm source, lấy đường tròn nội tiếp với đường kính `min(W,H)`; preview phần vùng bị bỏ, giữ scale và frame top-left. Circle → Rectangle giữ bounding square. Đổi shape trong selection là một sửa document có Undo; mở control chưa ghi gì.

Action Group: `Hoàn tất/Áp dụng`, `Hủy`, các action sửa text/source/frame theo loại, `Tìm trong Layers`. Không đưa chuỗi nút này lên bar. Reset chỉ reset draft/default theo scope đang hiện; không tự sửa selected object khi người dùng reset defaults.

## 7. Một nguồn dữ liệu cho canvas, bar, Properties và Select

Đề xuất `AnnotationContext` + `AnnotationEditSession` trong desktop, không thêm lớp UI vào core:

1. `ToolDefaults`: giá trị riêng từng mode, không ghi document/Undo. Content template tách khỏi text của object vừa tạo.
2. `CreationDraft`: source/frame/text/style của lượt tạo, base document epoch/page/session generation. Bar, Properties và overlay cùng bind vào một draft.
3. `Selection`: resolve owner IDs từ snapshot hiện hành qua `get_annotation/get_detail_view`, không giữ con trỏ vào object mutable. Chọn lại phải đọc giá trị mới, không lấy defaults của tool.
4. `SelectionEditDraft`: một field/text/handle session có before-value và base revision. Preview không publish document; kết thúc tạo một command. Invalid/cancel khôi phục before-value.

Mỗi delta đi qua một controller/binding: parse → validate → update draft → invalidate đúng view. Không gọi setter trực tiếp từ paint; không đồng bộ hai chiều bằng hai callback tự gọi nhau. Một change token ngăn vòng lặp UI; focus/IME buffer đang sửa được giữ khi selection không đổi.

Numeric Enter/Tab hoặc mất focus hợp lệ commit một field; wheel/slider/handle gộp từ begin tới end thành một Undo. Giá trị lỗi giữ input và thông báo tại field, không kẹp im lặng. Nhiều annotation cùng loại: field chung hiện Mixed khi khác; chỉ sửa field người dùng chọn, giữ text/style riêng còn lại. Mixed notes không cho nhập một text chung nếu chưa bấm hành động thay nội dung hàng loạt rõ ràng.

Source picking/snap và destination layer độc lập. Source scope dùng artwork đủ điều kiện; nếu bật Current Layer Only, dùng Source LayerId đã chọn rõ ràng, không vô tình chuyển thành child `Ghi chú/Ô chi tiết` vì vừa tạo annotation. Layer không còn hợp lệ phải báo lý do. Annotation destination theo role/LayerId, không theo tên và không thay source scope. Mixed selection chứa artwork chỉ hiện field chung thật sự, không áp field annotation vào path thông thường.

No-document, read-only, object/layer/group locked hoặc hidden, wrong-kind và busy có enable reason riêng. Nếu multi-selection có target không sửa được, batch từ chối toàn bộ và chỉ rõ target; không áp một nửa. Mở panel, đổi section, hover toolbox và camera pan/zoom không tạo history.

## 8. Select: hit-test, handles và sửa lại

Sau commit thành công, trả kết quả owner ID từ controller/command về shell; dùng selection service hiện có và `action.tool.select`. Không tìm object vừa tạo bằng `layer.object_ids.back()` trong runtime. Consume mouse-up trước khi đổi tool để không bắt đầu kéo ngay object mới.

| Thành phần khi Select | Hành vi |
| --- | --- |
| Nội dung/border frame Detail View, label hoặc connector | Chọn **một** owner Detail View. Kéo frame/label area thông thường chỉ di chuyển frame + label; source region giữ nguyên. |
| Source marker | Hit trên biên marker chọn owner; nội thất marker vẫn cho chọn artwork nguồn. Khi owner được chọn, handle tâm/biên chỉnh source region; frame top-left giữ nguyên, W/H tính theo scale. |
| Góc frame | Resize uniform, đối góc là điểm cố định; suy scale, giữ source region và circularity. Không có side handle kéo méo. |
| Double-click frame | Go to Source theo plan tổng; không tạo view mới. `Edit Source Region`/`Reposition Frame` gọi edit session trên owner đã chọn. |
| Leader text/body | Chọn một annotation; kéo text/landing/elbow, giữ arrow anchor. Tip handle dùng để đổi neo. Plain Note kéo vị trí text. |
| Double-click nội dung note | Mở cùng editor với nội dung hiện tại, không activate tạo mới hoặc clear text. |
| Label/text/line derived | Không thành selection ID phụ, không orphan khi Delete/Undo. |
| Empty canvas, toolbox gap/hover | Giữ Select sau deselect. Không resume tool cũ hoặc mở nhóm tool chỉ vì click trống. |

Thứ tự hit: chrome/editor → handles của selection → annotation visual parts theo painter order → artwork bình thường → marquee. DIP hit tolerance dùng chung cho paint/hit/focus; đổi DPI không đổi kích thước vùng bấm theo cảm giác.

`Reposition Frame` dùng lại bước FramePlacing nhưng kết thúc bằng UpdateDetailViewCommand trên owner hiện có. `Edit Source Region`/source handles cho kéo vùng mới, release cập nhật region và frame size với frame top-left cố định; không quay vào luồng Add hoặc yêu cầu đặt frame lần nữa. Cancel mọi edit giữ document trước đó. Paragraph note tự fit theo block chữ đã shape; chưa có text-frame width persisted thì không dựng handle kéo width giả.

World/local transforms phải được resolve qua hierarchy hiện có. Không dùng generic path scale để phóng luôn source region, font, stroke và frame mà ratio vẫn giữ số cũ. Phiên này hỗ trợ move, handles semantic và đổi cỡ chữ qua field; rotation/shear của annotations chưa có semantics riêng nên field/handle đó disabled có lý do. Object legacy đã có transform vẫn render/lưu được, không normalize khi chỉ mở file; edit adapter inverse-map tọa độ được hỗ trợ, hoặc từ chối phép sửa không biểu diễn được và giữ dữ liệu. Không biến chữ thành outline để né hợp đồng transform.

Leader anchor phải theo source world transform/path element khi reference còn hợp lệ. Lưu point local của source cho anchor theo object và element/parameter cho anchor theo curve. Khi topology không map được, giữ last-known point và báo Partial/Broken; không tự snap sang source khác. Source xóa → Broken; Undo phục hồi association. Relink cập nhật cùng owner trong một command, Detach cố định tại vị trí world đang hiển thị. Việc resolve anchor khi render không tạo document write mỗi frame.

## 9. Commands, metadata và persistence

- Tạo mới dùng AddAnnotation/AddDetailView một transaction, gồm layer/group mới nếu cần. Command trả owner ID; selection là UI state sau thành công.
- Sửa dùng delta hoặc read-modify-write trên snapshot hiện hành qua UpdateAnnotation/UpdateDetailView. Giữ ObjectId, layer/parent/stack, wrapper visibility/lock/color/profile, unrelated appearance và source IDs. Stale revision/object generation phải bị từ chối hoặc mở lại session; không overwrite từ form cũ.
- Multi-object style update phải là một command/transaction, validate tất cả trước publish. Không lặp `execute_command` để tạo N Undo hoặc partial commit.
- Các metadata field mới cho paragraph/anchor-local dùng `annotation:v2` với reader v1+v2, validator/schema rõ. V1 còn mở được; thiếu local anchor không được đoán một attachment mới. Không ghi field mới vào v1 nếu reader v1 coi đó là unknown.
- Detail fields hiện có đủ cho gesture/shape/scale. Chỉ bump `detail_view` metadata khi có field mới thực sự cần lưu. Không tự bump container `.pr` cho thay đổi UI; nếu wire fields native đổi, audit theo [PR format](../../architecture/native-pr-format.md).
- Audit mọi parser/version consumer và export preflight; SVG phải tiếp tục từ chối annotation/detail không biểu diễn được, kể cả metadata v2. Không mất chữ/link sau generic copy/paste, Delete/Undo hoặc save/reopen; clipboard remap ID nội bộ, external source bị thiếu phải Broken rõ ràng.
- Không ghi preview bitmap vào `.pr` như nguồn Detail View. Nguồn vẫn là scene snapshot + source region; marker/frame/label là derived data của owner.

## 10. Preview và hiệu năng

Ghost phải có nội dung vùng nguồn phóng to, clip đúng Circle/Rectangle, kèm marker/frame/label. Khi chưa render xong, khung phản hồi ngay với trạng thái `Đang cập nhật`; không hiển thị hình vùng cũ như thể là kết quả mới.

Content policy A1 loại annotations và Detail Views khỏi nguồn theo metadata/role, gồm chính owner đang sửa, để không render đệ quy. Không thay cờ visible của artwork/document để lọc preview. Source render phải dùng đúng page/hierarchy, color và clipping của cùng immutable snapshot.

Pure frame translation dùng lại source preview và chỉ đổi placement/connector; không render lại toàn scene trong mỗi pointer move. Key cache gồm source scene revision/dependencies, region/shape, ratio/output resolution, backend và color/profile context. Camera/điểm frame không làm thay source geometry. Source/style/scale đổi mới invalidated phần tương ứng.

Tái dùng coalescer pointer hiện có, flush trước press/release. Công việc render nặng chạy qua scheduler/renderer phù hợp, không block UI event handler hoặc gọi GDI ngoài UI thread. Mỗi job giữ immutable snapshot và token `{document, epoch, page, session, generation, source_revision}`; Esc/tool switch/tab close/field change invalidate generation, kết quả stale bị bỏ.

Preview khi đang di chuyển có cạnh dài tối đa 1024 px và budget riêng tối đa 64 MiB; thu chất lượng preview nếu cần, không giảm độ chính xác geometry commit. Khi dừng/commit dùng render chất lượng hiện hành, không thay bằng thumbnail persisted. Dọn surface/job khi session kết thúc và device loss; không giữ snapshot của tab đã đóng vô hạn.

Mục tiêu để đo trên máy tham chiếu ghi cấu hình: event handler p95 dưới 8 ms, pointer-to-overlay p95 dưới 33 ms với scene 10.000 artwork/20 views; source preview có số đo riêng. Đây là **gate cần đo**, chưa phải số đã đạt. Dùng trace present của native app; offscreen benchmark/readback và số test pass không chứng minh input-to-display.

## 11. Chia file và thứ tự triển khai

| Mốc | Công việc và file dự kiến | Gate |
| --- | --- | --- |
| U0: contract | Hoàn thiện `AnnotationContext/EditSession`, delta fields, cursor descriptor và policy transforms theo spec này. Tách domain helper thay vì thêm toàn bộ logic vào shell chung. | Một nguồn giá trị và state machine có kết quả consumed/commit/cancel rõ. |
| U1: Detail gesture/cursor | Sửa `tools/annotation/annotation_tool_controller.*`; thêm `shell/native_window_annotation_interaction.cpp` và `..._cursor.cpp` nếu cần; mỏng hóa routing trong `native_window_selection.cpp`, `native_window_events.cpp`, `native_window_pointer_dispatch.cpp`; wire CMake. | Gesture source release không commit; click đặt đúng top-left; native cursor thật. |
| U2: Properties | Thêm `presenters/annotation_properties.*`, `shell/native_window_annotation_properties.cpp`, `..._property_bar.cpp`; nối layout/paint/hit/focus/numeric input hiện có. Thêm field keys/descriptor, actions, catalog/menus và shortcut customization ở entrypoints runtime. | Bar/Properties/canvas đồng bộ defaults/draft/selection, đầy đủ editable/mixed/disabled. |
| U3: Text và Select | Thêm binding editor note, semantic hit/handles/selection edit; sửa hooks text_input/shortcuts/selection mà không copy một text engine mới. Resolve anchor trong core document/geometry; cập nhật model/metadata reader nếu cần. | Tạo/sửa Unicode, reselect, frame move, source edit, anchor lifecycle, một Undo mỗi thao tác. |
| U4: Layout/render/cache | Sửa annotation layout cho bounds typography, connector rect, label ratio; tích hợp `native_window_annotation_overlay.cpp` hoặc tách từ overlays hiện có; CPU/D2D preview và cancellation/cache. | Preview=commit về geometry, nguồn live, no stale frame, measured work/latency. |
| U5: Verification/help | Mở rộng regression hiện có; thêm test domain nếu test file quá lớn; icons/help/catalog; cập nhật trạng thái plan tổng bằng bằng chứng thực. | Qua bảng AC dưới, build app canonical, cleanup output theo AGENTS. |

Giữ dependency `desktop → document/geometry/rendering/typography`; core không import desktop. File code tối đa 2000 dòng; ưu tiên 300–1800. Các shell file hiện khoảng 950–1580 dòng nên thêm feature files theo domain, không phát triển một switch annotation hàng trăm dòng trong mỗi file paint/click.

## 12. Regression và nghiệm thu có thể kiểm chứng

| ID | Given / When / Then bắt buộc |
| --- | --- |
| ANN-UX-01 | Chọn tool, down-drag-up vùng nguồn: state sang FramePlacing, object count/revision/history chưa đổi, chuột đã bỏ capture drag. Up đầu không đặt view. |
| ANN-UX-02 | Hai oracle tọa độ ở mục 3.2; click đặt không có move cuối: frame lưu đúng top-left và source không đổi; một Undo xóa cả owner/layer mới liên quan, Redo giữ ID. |
| ANN-UX-03 | Click/tiny drag, rect bốn hướng, circle, zoom 25–400% và DPI 100–200%: không sinh vùng R10/20 mm giả, không NaN, hit/preview đúng. |
| ANN-UX-04 | Native WM_SETCURSOR sau source release: custom corner cursor có hotspot thật; outside canvas/control trả đúng cursor; DPI/theme không đổi điểm đặt. Query GetCursorInfo/ICONINFO và capture có composited cursor khi cần; ảnh không có cursor không đủ chứng minh. |
| ANN-UX-05 | Chỉnh shape/scale/label/connector trên cả bar và Properties trong draft: cùng giá trị, top-left không nhảy, không thêm document writes; rectangle connector tới đúng cạnh; scale 1.25 không ghi nhãn 1.2. |
| ANN-UX-06 | Leader Straight/Elbow và Plain Note: mọi bước click đúng, anchor đã chọn không trôi theo hover; text preset không bị clear; Plain không có leader bounds ẩn. |
| ANN-UX-07 | Gõ/dán tiếng Việt, combining marks, emoji/IME, multiline; Enter newline, Ctrl+Enter commit, Esc cancel; focus qua Properties giữ composition/text; Backspace không cắt UTF-8 hoặc grapheme. |
| ANN-UX-08 | Commit rồi clear selection/reselect/double-click note: sửa đúng owner cũ, font/content/size khớp, không tạo bản sao hoặc quay về defaults. |
| ANN-UX-09 | Sau commit chuyển Select và chọn owner đúng; mouse-up/double-click không leak thành drag/create; click trống và hover/gap toolbox giữ Select. |
| ANN-UX-10 | Kéo body frame: source region bất biến; kéo source marker: frame top-left bất biến, size theo scale; corner resize uniform; Circle vẫn circle. Test world/local và parent transform. |
| ANN-UX-11 | Kéo text/elbow/tip leader, move/edit/delete source và Undo: anchor theo ref hợp lệ hoặc Broken/Partial có last-known point, không tự đổi nguồn; relink/detach một Undo. |
| ANN-UX-12 | Scope defaults/draft/selection khác nhau; multi-selection Mixed; sửa một field giữ các field khác; target locked/hidden làm batch từ chối toàn bộ; mở dock không làm dirty file. |
| ANN-UX-13 | Esc/mất capture/tool switch/tab change/page close, source edit khi job chạy: no half-commit, no dangling Document reference, no stale overlay/job, no orphan layer. |
| ANN-UX-14 | `.pr` metadata v1/v2 save/reopen sau text/shape/scale/move: so sánh nội dung, IDs, links, colors/fonts/layer flags; SVG fail-closed không xuất annotation chỉ còn frame. |
| ANN-UX-15 | Copy/paste/delete/Undo nhiều notes/views: đúng logical owner/remap, link ngoài clipboard có trạng thái rõ; không chọn marker/label ID giả. |
| ANN-UX-16 | Light/dark, EN/VI, 1280×720/1366×768/1920×1080 và DPI có thể kiểm: paint/hit/focus cùng rect, keyboard flyout tới cùng action, Properties luôn mở lại được, không tràn bar. |
| ANN-UX-17 | Native pointer messages qua chrome → canvas → controller → command; kiểm revision/geometry ở output, capture preview/committed CPU+D2D. Direct controller/API tests chỉ là một lớp bằng chứng. |
| ANN-UX-18 | Scene thường và 10k/20 views: trace p50/p95 event/overlay/present, render count khi chỉ move frame, peak preview memory và cleanup sau Esc/tab close; ghi rõ backend/reference machine và gate chưa đạt. |

Mở rộng `test_pikdraw_annotation_tool_controller`, `test_pikdraw_document_annotation_model`, desktop-shell tests và rasterizer/culling tests đang có. Test binding riêng có thể thêm vào target domain; không thay native pointer test bằng gọi setter trực tiếp rồi báo pass UI. Với source/pixel oracle, kiểm sai số và clip thực chứ không chỉ `has_preview == true` hoặc số object.

Các lệnh dự kiến cho agent triển khai, chỉ chạy sau khi đã có source changes tương ứng:

~~~powershell
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_annotation_tool_controller','test_pikdraw_document_annotation_model','test_pikdraw_desktop_shell','test_pikdraw_drawing_tool_ui') -VerboseTests
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_rendering_culling','test_pikdraw_rendering_rasterizer','test_pikdraw_interop_svg_exporter') -VerboseTests
.\tooling\native\build.ps1 -Test -TestTarget @('pikdraw') -BuildOnly
~~~

Kiểm target/CTest mapping hiện hành trước chạy; dùng queue/status/retry/cleanup theo [Native build](../../development/native-build.md). Chỉ chọn thêm storage/clipboard targets khi thay codec/commands tương ứng. Gate UI runtime/cursor/performance phải báo riêng, không thay bằng build hoặc focused CTest.

## 13. Icon, help và điều kiện hoàn tất

Tái dùng hoặc bổ sung SVG theo hệ icon hiện tại: `annotation-detail-view.svg`, `annotation-leader-note.svg`, `annotation-plain-note.svg`, `annotation-region-circle.svg`, `annotation-region-rectangle.svg`, `annotation-properties.svg`; tên cuối kiểm inventory để tránh trùng. Cursor góc vuông có nguồn/lifetime/cache riêng, không dùng emoji hoặc glyph font làm resource chuẩn.

Label EN/VI: `Detail View / Ô chi tiết`, `Leader Note / Ghi chú mũi tên`, `Plain Note / Ghi chú`, `Source region / Vùng nguồn`, `Place top-left corner / Đặt góc trên trái`, `Content / Nội dung`. Tooltip hai dòng tên + shortcut thật, rồi một câu tác dụng; keyboard focus có cùng help. Status dùng mục 3.3, không chép câu dài lên bar.

`Help > Annotation` có ba hướng dẫn 3–5 bước: tạo view tròn/rect và đổi tỉ lệ; tạo Leader/Plain và nhập Unicode; Select để sửa text/frame/source và Undo. Ghi rõ góc đặt, Enter/Ctrl+Enter/Esc, đơn vị trang, giới hạn transform và export hiện hành.

Chỉ ghi hoàn tất khi ba tool thao tác được qua menu/toolbox/keyboard, fields hai bề mặt đồng bộ, Select sửa owner cũ đúng, file mở lại đúng và các gate đã chạy có bằng chứng. Phần chưa đo hoặc chưa chạy phải giữ trạng thái chưa nghiệm thu; không bật control/handler giả để đủ giao diện.

## 14. Tiến độ triển khai hiện tại

Đã nối và kiểm tra ở source:

- Detail View gesture dùng ngưỡng 3 DIP, bỏ capture sau khi khóa vùng, không tự tạo R10/20 mm cho click/tiny drag; frame đặt bằng góc trên trái theo trục Y-up và commit ở click down/up.
- Cursor `┌` theo bước đặt; sau commit shell chọn đúng owner rồi chuyển Select. Đổi tab hủy draft và bind controller sang document mới.
- Shape Circle/Rectangle, tỉ lệ, connector, nhãn tuần tự A/B/C; note giữ template, hỗ trợ Straight/Elbow, Unicode, Enter xuống dòng, Ctrl+Enter nhận và Esc hủy/sửa note đã chọn qua Update command.
- Leader Note đang được chọn hiển thị đúng mode từ geometry lưu; đổi Straight/Elbow cập nhật geometry của owner qua Update command.
- Properties hiển thị controls đang hỗ trợ cho draft/selection; đã thêm nhập source X/Y/W/H hoặc Cx/Cy/R và frame top-left X/Y, tôn trọng đơn vị trang, validation và giữ frame top-left khi đổi source size. Dock content height cho các hàng mới đã được tăng.
- Layout formatter hỗ trợ nhãn tỉ lệ chính xác; connector hình chữ nhật chạm cạnh theo hướng connector. Regression controller/model được chạy và target app link thành công.

Chưa hoàn tất nên chưa đạt điều kiện hoàn tất ở mục 13:

- Select chưa có semantic handles/body drag cho source marker, frame, leader tip/elbow/text; generic selection transform chưa được thay bằng edit session chuyên biệt.
- Detail View chưa render nội dung scene nguồn phóng to trong frame/preview; hiện overlay chỉ vẽ marker, frame, connector và label.
- Nhãn Detail View mới đổi tuần tự A/B/C thay vì text field; font/color cho note và các thiết lập arrow/landing/alignment/line spacing chưa có controls đầy đủ.
- Chưa có kiểm thử Win32 message route tự động, native pointer/cursor/hotspot, DPI/light-dark, save/reopen, clipboard/anchor lifecycle, hay hiệu năng 10k/20 views. Cửa sổ chạy được nhưng capture desktop trong lượt này timeout nên chưa thể xác nhận trực quan.

Không được đánh dấu plan hoàn tất cho tới khi các mục còn lại có implementation và evidence theo bảng ANN-UX-01…18.
