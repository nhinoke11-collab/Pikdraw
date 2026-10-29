# Hoàn thiện Cut & Shape Tools và xử lý ứng dụng tự đóng

Ngày 29/10/2026. Trạng thái: **plan triển khai, chưa xác nhận nguyên nhân ứng dụng tự đóng và chưa nghiệm thu runtime**. Người dùng báo nhóm trong ảnh hiện chỉ có Knife Tool và Vector Eraser, còn thiếu chức năng so với [plan gốc](cut-erase-smart-tools.md), và ứng dụng bị đóng khi dùng. Ảnh xác nhận menu có hai item; ảnh không cung cấp thao tác, file, thời điểm đóng hay crash dump. Không coi báo cáo build hoặc test cũ là bằng chứng đã sửa sự cố này.

Plan này bổ sung và cụ thể hóa plan gốc, giữ đầy đủ phạm vi Knife, Vector Eraser, Scissors, Smart Trim, Trim Objects, Smart Fill, Shape Builder và Raster Eraser theo các giai đoạn dưới đây. Tôn trọng `AGENTS.md`, `docs/START.md`, `registry/modules.json`, UI standard và workspace layout hiện hành. Làm trực tiếp tại source gốc, giữ toàn bộ local edits. Agent triển khai kiểm lại source và trạng thái build trước mỗi thay đổi; không xóa hoặc viết đè tài liệu/code đang sửa của người khác.

## 1. Bằng chứng source hiện có và các nghi vấn cần chứng minh

| Điểm | Quan sát source tại thời điểm viết | Việc cần xác minh |
| --- | --- | --- |
| Flyout | `native_window_paint_docks.cpp` chỉ vẽ Knife và Vector Eraser; `native_window_toolbox.cpp` giữ slot Knife, group 6, tool 5. | Truy dấu chuột, bàn phím, action registry và shortcut profile tới cùng tool; không coi nhãn trong ảnh là triển khai đầy đủ. |
| Pointer | `native_window_selection.cpp` dùng chung `is_knife_cutting_` cho tool 5/16; move chỉ lưu điểm cuối; release Knife gọi `split_objects_by_line`, Eraser truyền **hai điểm**, đường kính **2 mm**, nib tròn vào `erase_objects_by_stroke`. | Nét cong Eraser hiện không đại diện cho đường chuột thực; cần kiểm pointer coalescing, release, capture loss và tab/tool switch. Đây là hạn chế đã quan sát, không phải kết luận nguyên nhân crash. |
| Overlay | `native_window_canvas.cpp` vẽ cùng đường thẳng cho hai tool. | Preview cần thể hiện chính gesture/nib và kết quả hoặc trạng thái tính toán; không trình bày đường thẳng như footprint Eraser cong. |
| Workflow | `document_workflow_io_shaping.cpp::split_objects_by_line` gọi `cut_objects_by_line`; `document_workflow_cut.cpp` resolve candidate, tính hình học đồng bộ và gọi `ReplaceObjectWithPiecesCommand`. Chỉ path có fill được xử lý tại đây. | Kiểm ownership, snapshot/reference lifetime, thứ tự stack và khóa; công việc nặng đồng bộ trên UI là rủi ro treo, chưa chứng minh process crash. Selection rỗng duyệt ứng viên theo thứ tự ngược, ứng viên không hợp lệ đầu tiên có thể khiến workflow trả false. |
| Kernel/controller | `path_cut.cpp` từ chối đường mở trong các nhánh hiện tại; `modules/vector/src/tools/cut_tool_controller.cpp` có cấu hình và đường polyline. | Đối chiếu route desktop với controller: API tồn tại không chứng minh người dùng dùng được; không duy trì hai định nghĩa tính toán khác nhau. |
| Kiểm thử | Có `test_pikdraw_geometry`, `test_pikdraw_document_path_cut`, `test_pikdraw_vector_cut_tool`, `test_pikdraw_desktop_shell`; các test vector gọi trực tiếp API controller. | Kiểm CTest mapping và source hiện hành; test trực tiếp API không chứng minh native event, release, render, hay app không crash. |

Không dựa vào nguồn cũ ở mục 2 của plan gốc như snapshot hiện tại. Các test cũ và những sửa đổi trong working tree là tài sản cần giữ; xác nhận từng kết luận bằng source và lần chạy mới.

## 2. P0 — tái hiện và xử lý crash trước khi mở rộng

1. Kiểm Git status, tiến trình `pikdraw.exe`, `tooling/native/build.ps1 -Status`, canonical EXE và cấu hình build. Không giết ứng dụng giữ EXE, không xóa build lock hay cache. Chỉ build canonical target qua runner sau khi có thể relink.
2. Thu thập bằng chứng không tiết lộ tài liệu của người dùng: phiên bản/binary, backend hardware/WARP, Windows, DPI, tool/shortcut, chuỗi down/move/up, vị trí xảy ra (chọn tool, kéo, thả, Undo, đổi tab), đối tượng/selection/layer và thông báo/exit code nếu có. Ưu tiên tài liệu test tạo mới; không xóa hoặc tải lên file, dump hay recovery của người dùng. Nếu chưa tái hiện, tiếp tục xây ma trận tái hiện, ghi rõ chưa biết nguyên nhân và không khẳng định đã sửa.
3. Dựng fixture tối thiểu riêng cho mỗi tool: canvas trống; path đóng có fill; path mở chỉ stroke; nhiều path; group transform; layer khóa/ẩn; bitmap/text; không selection; selection hỗn hợp; kéo cong và kéo dài; click/tiny drag; thả ngoài canvas; Esc, mất capture, đổi tab và đóng tài liệu khi thao tác. Ghi event cuối, exception code, call stack/địa chỉ và snapshot revision trước/sau. Dùng debugger/sanitizer sẵn có phù hợp toolchain, exception logging hoặc dump do runner/fixture tạo trong output riêng; xác nhận bằng cùng binary và source digest.
4. Theo dấu `WM_*` → selection/tool session → resolver → geometry → command → snapshot/selection → render. Kiểm `optional`/variant lifetime, ID bị xóa trong selection, owner/transform, callback sau hủy/tab close, capture reentrancy, GDI lifetime, ngoại lệ tại boundary, tràn size hoặc allocation, và crash khi Undo/Redo. Với mỗi stack, nêu root cause kèm regression tái hiện trước/sau; các mục không khớp stack chỉ là giả thuyết. Không che lỗi bằng `catch (...)` báo thành công hoặc xóa artwork để né crash.
5. Sửa trong domain liên quan, bảo đảm thất bại/cancel không ghi một nửa document, không bỏ recovery và không làm đóng app. Sau đó chạy lại thao tác gốc nhiều lần với native pointer messages và canonical executable; log/fixture tự sinh dọn theo AGENTS. Nếu không thể bắt được stack, đánh dấu gate crash **chưa đạt** và vẫn làm các phần độc lập tiếp theo.

## 3. Hợp đồng tương tác và Properties cho hai công cụ hiện có

| Nội dung | Knife | Vector Eraser |
| --- | --- | --- |
| Gesture | Straight: down–drag–up tạo đoạn từ tọa độ down/up; Freehand: lấy và flush toàn bộ samples qua coalescer. Nhấp/đường không cắt hợp lệ là no-op có hint. | Down–drag–up lưu stroke world liên tục, gồm điểm down/up và các move đã coalesce; dùng footprint quét theo đường thật, không thay bằng chord hai đầu. Một click dùng policy nib được ghi rõ và kiểm thử. |
| Preview | Đường cắt + hiển thị mảnh/khoảng tác động và sai số khi có proposal. | Con trỏ/nib và vùng quét theo kích thước mm ở mọi zoom/DPI; preview và commit chung stroke/options/kernel. |
| Bar | Straight/Freehand, gap, scope, nút Properties. | Diameter, nib tròn/vuông, scope, nút Properties. |
| Properties | Mode, gap 0–1 mm, close pieces/outline và precision khi semantics thực có hỗ trợ, target policy. | Diameter 0.01–1000 mm mặc định 2 mm, nib, scope, xử lý open path và precision; pressure chỉ bật khi backend giữ đầy đủ samples và render/commit khớp. |

Các field chia sẻ một tool preference/draft binding, parser đơn vị trang, validation và hiển thị trạng thái disabled/lý do; sửa preference không tạo Undo. Giá trị sai giữ text và báo lỗi, không clamp âm thầm. Lưu identity/action `action.tool.knife`, `action.tool.vector_eraser`, K, Shift+E và settings hiện có; kiểm xung đột shortcut theo profile và text focus. Properties dùng dock hiện hữu, có đường mở qua bar/menu khi đóng; không tạo control rỗng. Paint/hit/focus dùng cùng rect, VI/EN, tooltip, SVG icon, status hint và Help. Scope mặc định: selection hợp lệ nếu có; nếu rỗng, topmost editable thật sự bị gesture tác động. Mặc định không sửa ẩn/khóa hay xuyên PowerClip; báo loại không hỗ trợ.

Một session giữ document/page/epoch/revision, IDs, world gesture, options và generation. Down bắt đầu draft/capture; move cập nhật overlay có giới hạn; up flush mẫu và chỉ tạo một proposal; Escape/right-click, capture loss bất thường, đổi tool/tab/page/đóng document hủy an toàn. Không dùng lại reference document/snapshot sau đóng tab. Resolve target và kiểm lại trạng thái khi commit; stale/busy trả lý do, không ghi document. Một thao tác thành công là một command/transaction và một Undo; selection chỉ đổi sau command thành công. Redo giữ ID và thứ tự theo quy tắc proposal. Save/reopen so geometry/metadata/IDs; chỉ yêu cầu Undo sau reopen nếu storage contract thật sự lưu history, không giả định history được persist.

## 4. Các công cụ còn thiếu: hành vi và phân kỳ giao hàng

| Giai đoạn | Kết quả phải triển khai qua UI thật → geometry → command | Điều kiện đóng giai đoạn |
| --- | --- | --- |
| P1 Knife/Eraser | Chia path kín theo biên thật; freehand và straight; Eraser swept round/square trên fill, hole, nhiều thành phần; xóa khoảng centerline trên path mở/stroke. Chuyển segment Bézier theo tham số và mapping nếu backend hỗ trợ, nếu phải flatten công bố bound và từ chối khi vượt tolerance. | Preview/commit đồng nhất, style/profile/effects/parent/stack bảo toàn, selected và topmost đúng, crash gate P0 và Undo/Redo qua. |
| P2 Scissors | Nhấp node hoặc segment thật; split tại tham số curve, mở loop/tách path mở, bảo toàn tangent và mapping IDs; nhấp ngoài là no-op. | Test node/cubic/arc, transform, endpoint, Undo/Redo và save/reopen. |
| P3 Smart Trim | Intersection graph và khoảng trước/sau giao; hover highlight đúng khoảng, click xóa một interval; coincident/tie mơ hồ báo lý do hoặc cho chọn. | Không xóa nhầm tại tiếp tuyến, tự giao, nhiều đường gần nhau; một Undo. |
| P4 Trim Objects | Chọn Source(s)/Target rõ trên UI, Keep Source; tính target trừ union source, giữ style và stack target; preview kết quả rỗng trước apply. | Nhiều nguồn, source/target khác parent, lỗ, khóa và Undo atomic; không dùng bitmap bounds làm vector trim. |
| P5 Smart Fill/Shape Builder | Arrangement các biên đủ điều kiện, fill-rule và holes; chọn face tạo path chỉnh node; Builder merge/erase nhiều face, policy style rõ; gap mặc định 0, bridge có preview. | Vùng kín/hở, self-intersection, ambiguity, gap, Undo và save/reopen có oracle độc lập. |
| P6 Raster Eraser | Edit node alpha riêng, source bất biến, mapping ảnh/transform, 8/16-bit, profile/alpha, cancellation và budget; audit mọi reader/writer/consumer và migration nếu đổi wire schema. | Test alpha/pixel, Undo/Redo, save/reopen, file legacy, source invariant và memory/cancel; không gọi mask adjustment là eraser. |

Mỗi item chỉ xuất hiện khi action, enable reason, command, help và acceptance của item đã hoạt động. Bổ sung catalog descriptor typed cho flyout thay các mảng/nhánh số rải rác, giữ slot và active/focus/last-used; kiểm click vùng trống, đóng flyout và Select không đổi tool ngoài ý muốn. Menu chính, action ID, icon SVG tự thiết kế, shortcut thực, Property bar/Properties, trạng thái và hướng dẫn cho từng tool phải được ghi rõ khi triển khai. Trim là lệnh chọn vai trò; kiểm sự phù hợp với menu Object/Tools theo UI standard trước khi đăng ký.

## 5. Kiến trúc, dữ liệu và hiệu năng

- Dùng `GeometryEditRequest` (snapshot immutable, epoch/page/revision, IDs, gesture/options, budget) → proposal (output, provenance/mapping, owner, bounds, error bound và status) → command validate/commit. Kernel thuộc geometry; transaction thuộc document; controller thuộc vector; Win32 chỉ route/paint. Không import desktop vào core.
- Trả các trạng thái Success, NoChange, Unsupported, Ambiguous, Invalid, Cancelled, Stale, BudgetExceeded; empty result có nghĩa xóa sạch hợp lệ riêng với lỗi. Giữ fill rule, lỗ, transform phản chiếu, color/ICC/spot, opacity, blend, stroke/effect, metadata và layer/parent/z-order. Nếu chưa giữ được thuộc tính, từ chối hoặc yêu cầu chuyển đổi có thông báo trước commit; không silently flatten.
- Không giữ pointer tới bản sao `optional` đã hết lifetime; tra từ snapshot còn sống và kiểm generation trước mọi publish. Không sửa đồng thời một phần batch khi có target không hợp lệ theo policy all-or-nothing.
- Geometry nặng không chạy chặn UI event: job dùng snapshot, cancellation, quota số mẫu/intersection/output và memory, progress; coalesce move, cache proposal theo revision/options, bỏ kết quả stale. Đặt budget cụ thể sau đo trên máy tham chiếu; khi hết budget giữ document và báo lý do. Không tái render toàn scene cho từng mẫu khi chỉ overlay thay đổi.

## 6. Ma trận nghiệm thu bắt buộc

| ID | Điều kiện và oracle |
| --- | --- |
| CS-CRASH-01 | Tái hiện thao tác người dùng và ma trận P0 trên canonical EXE; process còn sống, không exception, không mất document; ghi stack gốc và regression cho từng root cause. Chưa tái hiện thì chưa đạt. |
| CS-UI-01 | Toolbox/flyout/menu/shortcut thật cho từng tool, focus và hit rect trùng paint; light/dark, VI/EN, 1280×720 và 1920×1080, DPI có thể kiểm; mở Properties không dirty document. |
| CS-GESTURE-01 | Knife straight/freehand và Eraser đường cong zigzag: native down/move/up, capture, release ngoài, Esc và switch tab; kiểm samples và preview/pixel so với commit, không chỉ gọi controller. |
| CS-GEO-01 | L lõm, cubic, hole, winding, nhiều mảnh, near-contact, tiếp tuyến, self-intersection, origin lớn, parent transform/reflection, open stroke; so oracle hình học/pixel độc lập và sai số công bố. |
| CS-DOC-01 | Multi selection, hidden/locked, unsupported text/bitmap, PowerClip, empty result, chọn topmost; giữ IDs/style/color/profile/layer/z-order; một Undo/Redo, save/reopen và export đúng capability hoặc fail-closed. |
| CS-TOOLS-01 | Scissors, Smart Trim, Trim, Smart Fill, Shape Builder và Raster Eraser đạt gate riêng trong mục 4; action chỉ bật khi chức năng tương ứng hoàn thiện. |
| CS-ASYNC-01 | Stale revision, cancel, mất capture, tool/tab/page close, budget và device loss không commit cũ, rò job hoặc thay đổi một phần; test stress và số đo p50/p95 input-to-overlay/present, peak memory trên máy/backend ghi rõ. Không suy FPS từ build/offscreen. |

Mở rộng regression có giá trị trong `core/geometry/tests/operations/test_path_cut.cpp`, `core/document/tests/commands/test_path_cut_commands.cpp`, `modules/vector/tests/tools/test_cut_tool_controller.cpp` và desktop native-event test phù hợp; bổ sung storage/render/interop/raster tests khi phần tương ứng thay đổi. Xác minh target/CTest mapping trước khi chạy. Dự kiến lần focused: `./tooling/native/build.ps1 -Test -TestTarget @('test_pikdraw_geometry','test_pikdraw_document_path_cut','test_pikdraw_vector_cut_tool','test_pikdraw_desktop_shell')`; sau đó `./tooling/native/build.ps1 -Test -TestTarget pikdraw -BuildOnly`. Thêm target theo thay đổi và dùng `-AllTests` khi cần full suite. Theo dõi request queued bằng `-Wait` đúng ID; đọc log lỗi và dọn output riêng bằng `clean-test-artifacts.ps1`, giữ PCH/object/binary/cache.

Chỉ báo hoàn thành sau khi các gate thuộc mọi giai đoạn yêu cầu đều có bằng chứng, bao gồm thao tác native và root cause crash. Báo riêng phần chưa tái hiện/chưa chạy, lỗi và giới hạn; không dùng test API, build/link hoặc screenshot tĩnh để thay thế nghiệm thu UX.

## 7. Nhật ký thực thi ngày 29/10/2026

### Đã sửa trong lượt này

- Undo/Redo đồng bộ lại selection với snapshot hiện hành; Shape Builder không còn giữ ID của object vừa bị Undo xóa, và status của lỗi chọn mặt không bị ghi đè bởi thông báo chọn rỗng.
- Add Layer sinh ID ổn định, không trùng; clipboard page có ID layer hợp lệ. Native reader thay danh sách layer mặc định bằng layer đã lưu khi migrate file cũ, tránh nhân đôi ID ở schema v2/v3.
- Thêm catalog có kiểu cho đủ tám mục Cut & Shape và dùng chung catalog khi vẽ/hit-test flyout. Thêm Trim Objects và Smart Trim vào Tools menu.
- Reset vai trò Target/Source của Trim Objects không xóa selection người dùng khi đổi tool.

### Kiểm chứng source hiện tại

- Build request `124556876c5041f98593a27d76cc751a`: 10/10 CTest target liên quan passed, gồm geometry, document path cut/layer/storage, vector cut controller, raster eraser/evaluator/edit graph, desktop shell và native canvas view.
- Build request `0a3bda09e2f8480897cca45c494ba20f`: native canvas regression passed sau khi sửa việc Trim Objects xóa selection.
- `git diff --check` không báo lỗi whitespace; Git chỉ in cảnh báo line-ending CRLF cho các file đang sửa.
- Build `pikdraw` request `e9d82d65c91843f48c619e2611eabc13` passed trước các sửa đổi catalog/menu và selection mới nhất; không xác nhận binary hiện tại.

### Gate còn mở — không được ghi là hoàn tất

- **CS-CRASH-01:** chưa có chuỗi thao tác gốc, fixture/tài liệu, exit code hoặc stack. Ảnh cửa sổ CMake trước đó không xác nhận crash trong PikDraw. Hồi quy selection/storage đã được sửa, nhưng chưa thể gắn chúng với crash chưa tái hiện.
- **CS-UI-01:** process canonical khởi động và Windows báo Responding, nhưng Computer Use trả `FrameArrived timed out` rồi `window capture timed out`; chưa có bằng chứng trực quan cho hit-rect, DPI, light/dark, VI/EN hoặc preview. Không dùng AX tree sơ sài thay cho kiểm tra này.
- **CS-ASYNC-01:** chưa có `GeometryEditRequest`/proposal worker cho cut geometry; các workflow Knife/Eraser/Trim/Smart Trim/Shape Builder vẫn tính geometry đồng bộ. Chưa có số đo p50/p95 input-to-present hoặc peak memory, nên điều kiện hiệu năng và cancellation của plan chưa đạt.
- Canonical EXE cần relink lại sau các sửa đổi mới nhất. Tiến trình PikDraw được mở để smoke-check vẫn giữ EXE; thao tác đóng qua Computer Use thất bại, nên không kill process. Chờ đóng cửa sổ thủ công rồi build `pikdraw` lại theo runner.

Test failure run `12176-0a4130b44b364e3e93493a8248c29329` đã đọc log, sửa nguyên nhân và dọn bằng `clean-test-artifacts.ps1 -Apply -RunId`. Không lưu output test tạm.

### Cập nhật tiếp tục triển khai sau phản hồi ngày 29/10/2026

#### Đã hoàn thiện thêm trong source

- Knife và Vector Eraser giờ chuyển geometry sang worker dùng snapshot bất biến. Worker chỉ sinh proposal; UI thread kiểm lại generation, tab, revision, selection và tool trước khi tạo một command/transaction. Bỏ kết quả stale; Escape, right-click, mất capture, đổi tool/tab/page và đóng cửa sổ yêu cầu hủy. Có timer UI 50 ms dự phòng nếu thông điệp hoàn tất không tới; lỗi tạo timer/worker trả trạng thái thất bại và không commit.
- Sửa lỗi vòng đời vừa phát hiện bằng native regression: cleanup sau mouse-up trước đây gọi cùng hàm hủy job, nên mọi proposal Knife/Eraser mới tạo đều bị đánh dấu hủy. Mouse-up hiện chỉ dọn state pointer, giữ worker mới khởi chạy; các đường hủy độc lập vẫn hủy worker.
- Chặn input vượt quota trước khi chạy kernel: tối đa 4096 điểm gesture, 4096 target, 200000 phần tử cho mỗi path, 1000000 phần tử input và 1000000 phần tử output. Batch không áp dụng một phần khi bị hủy hoặc vượt budget.
- Bổ sung native regression cho Knife straight/freehand, Vector Eraser fill và stroke mở, Undo, Escape/right-click khi worker chạy, thay revision trước commit, fallback timer sau khi bỏ thông điệp worker, pre-cancel, quota gesture và selection hỗn hợp.
- Preview Knife/Vector Eraser dùng cached canvas cộng overlay-only repaint khi cache còn hợp lệ; cache mismatch quay lại repaint đầy đủ.

#### Bằng chứng kiểm thử mới

- Request `beecf9a45e124324a84f6d9f8620cd20`: `test_pikdraw_canvas_view` **1/1 passed** sau sửa trạng thái báo lỗi khi worker không khởi chạy.
- Request `774bc5fc57a540d9b6f8a467e51cd20c`: **11/11 CTest passed**: geometry (59 test nội bộ), path cut, node/Scissors, layer, vector cut controller, raster evaluator/vertical slice/edit graph, storage fidelity, desktop shell và native canvas.
- Request `3452bd5c3bbc48b5866d3a1935bb1b72`: **1/1 passed**, gồm fallback timer, pre-cancel và quota; synthetic Direct2D hardware trace báo input-to-present-submit p50 **2.97 ms**, p95 **3.52 ms**, 65 mẫu, overlay-only 64/64. Đây là thời gian tới GDI submit trong test HWND; không phải thời gian scanout/màn hình hoặc phép đo chuột vật lý.
- Request `7187cd37314f4dd78268d31c661c76a1`: build lại `test_pikdraw_canvas_view` qua **80 bước trong 408.9 giây** rồi test **1/1 passed**; chậm do runner báo 106 PCH thiếu và phải dựng lại nhiều object desktop. Không phải bằng chứng `pikdraw.exe` bị treo.
- Các lần failed compile/test trong lúc dựng worker đã đọc log, sửa nguyên nhân và dọn output thuộc request; không xóa build cache.

#### Cổng còn mở

- **CS-CRASH-01 chưa đạt:** Windows Application Error/WER có ba record `pikdraw.exe` từ ngày 26/10; cả ba ghi `ucrtbase.dll`, exception `0xc0000005`, offset `0x25f10`. Log không ghi thao tác Cut & Shape và không có call stack. Không có debugger/PDB phù hợp trong toolchain để quy địa chỉ này về source. Chưa thể gắn lỗi với Knife/Eraser hoặc tuyên bố root cause đã sửa.
- **CS-UI-01 chưa đạt:** cửa sổ canonical có title `PikDraw 2026 - Untitled Document [Vector Workspace]` và process Responding, nhưng Computer Use tiếp tục timeout khi chụp. Chưa xác nhận trực quan hit rect, preview, Properties, theme, VI/EN hoặc DPI.
- **CS-ASYNC-01 mới đạt một phần cho P1:** Knife và Vector Eraser đã chạy worker và có cancellation/stale/quota regression; Smart Trim, Trim Objects, Smart Fill, Shape Builder và Scissors vẫn có phép tính đồng bộ. Chưa có stress test peak memory/device-loss hoặc đo input-to-present bằng chuột thật.
- **CS-DOC-01 còn thiếu ca riêng:** focused storage fidelity/raster tests và Undo/Redo đã qua, nhưng chưa có một test save/reopen/export chuyên biệt cho kết quả mỗi phép cut/trim mới dưới parent transform, profile và stack khác nhau.
- Canonical `pikdraw.exe` vẫn được PID `15028` giữ mở và Windows báo Responding. Chưa relink lại app sau các sửa đổi hiện tại; không kill process hoặc ghi đè executable đang dùng.

Vì các gate trên cần call stack/thao tác crash, capture UI thành công và giải phóng EXE đang mở, plan **chưa được đánh dấu hoàn tất**. Các phần source/test có thể làm độc lập đã tiếp tục triển khai và kiểm chứng như trên.

### Cập nhật tiếp tục triển khai sau yêu cầu hoàn thành toàn plan

#### Đã sửa và thêm bằng chứng

- Job Cut Geometry giờ khóa việc gửi message UI theo vòng đời cửa sổ. Khi `NativeWindow` bị hủy, worker detached chỉ được phép hoàn thành trên snapshot/record của nó, không gửi progress/completion vào HWND cũ. Các exception chuẩn và không chuẩn đều thành proposal lỗi fail-closed; timer polling tiếp tục là đường dự phòng nếu PostMessage thất bại.
- Proposal Knife/Eraser giữ `approximation_error_bound_mm` từ Boolean kernel. Ngưỡng mặc định được giới hạn ở **0,05 mm**, caller chỉ có thể siết ngưỡng. Kết quả không hữu hạn/âm hoặc vượt ngưỡng bị từ chối trước khi tạo command; thông báo hoàn tất nêu bound đo được.
- Bổ sung regression song song notification với shutdown để xác nhận không phát callback sau khi đóng notification channel.
- Bổ sung save/reopen `.pr` cho kết quả Knife và Smart Trim trong group đã transform: kiểm tra thứ tự/ID đối tượng, local/world transform, paint ICC, opacity, stroke, effect, topology mở của Smart Trim và khả năng chỉnh sửa/Undo sau reopen.
- Bổ sung kiểm export SVG sau khi reload `.pr`: cùng fixture Knife + Smart Trim phải xuất ba path có transform/style, không có unsupported warning và importer nhận lại được nội dung.

#### Bằng chứng mới

- Request `2bce7c913358469cb3a0c52ec48170f1`: native canvas và document storage **2/2 passed** sau hardening vòng đời worker và save/reopen.
- Request `4804eb90d597468b9ad063c15da66734`: native canvas và document storage **2/2 passed** sau kiểm soát approximation bound, gồm từ chối bound lớn hơn 0,001 mm và giữ bound trong proposal thành công.
- Request `ae726e3ffe9048fcb9086eecfe9ca2db`: `test_pikdraw_interop_svg_exporter` **1/1 passed** cho luồng cut + Smart Trim → save/reopen → SVG export/import.
- Các request build desktop trên mất lần lượt khoảng **523 giây** và **508 giây** vì runner phát hiện **106 PCH** đã build trước đây nhưng không còn trong cache. Không dùng số bước Ninja để suy phần trăm hoặc ETA.

#### Cổng còn mở sau cập nhật

- **CS-CRASH-01 chưa đạt:** WER chỉ có ba crash lịch sử ngày 26/10 (`ucrtbase.dll`, `0xc0000005`, offset `0x25f10`), không có thao tác hoặc stack. Chưa có bằng chứng để gắn crash ban đầu với Cut & Shape; không tuyên bố root cause đã xác định.
- **CS-UI-01 chưa đạt:** Computer Use nhận đúng cửa sổ `PikDraw 2026 - Untitled Document [Vector Workspace]` nhưng hai lần capture đều timeout (`window capture timed out: timed out waiting on channel`). Không thể nghiệm thu trực quan theme, DPI, hit-test, preview hoặc Properties trong lượt này.
- **CS-ASYNC-01 mới đạt một phần:** Knife/Vector Eraser có worker, cancellation, stale guard, quotas, shutdown-safe notifications và test HWND. Scissors, Smart Trim, Trim Objects, Smart Fill, Shape Builder và Raster Eraser còn tính phần geometry/evaluation trên đường gọi hiện tại; chưa có stress/peak-memory/device-loss hoặc latency với mouse/present vật lý.
- **CS-DOC-01 đạt thêm cho Knife + Smart Trim** qua `.pr` save/reopen và SVG export sau reload. Chưa chạy ma trận persistence/export tương ứng cho mọi tool/format, nhiều lựa chọn, PowerClip và các trường hợp màu/profile khác.
- Canonical app vẫn đang mở và giữ `build/win-dev/apps/desktop/pikdraw.exe`; chưa relink binary sau source hiện tại. Không đóng hoặc kill tiến trình đang mở để tránh mất tài liệu người dùng.

Do crash chưa có stack tái hiện, UI capture không khả dụng, các tool ngoài Knife/Eraser còn đồng bộ và canonical executable chưa thể relink, plan vẫn **chưa hoàn tất**. Những gate này vẫn cần nghiệm thu trước khi phát hành.

### Cập nhật tiếp tục triển khai sau yêu cầu hoàn thành toàn plan

#### Sửa lỗi và bao phủ thêm trong source

- Smart Trim kiểm tra điểm bấm có nằm trên tiếp xúc với path khác trước khi tạo khoảng xóa. Kiểm tra tìm đường gần nhất có quota/cancellation và fail-closed với tiếp tuyến, tự giao hoặc trường hợp không phân biệt được; không dựa vào rectangle có chiều cao bằng 0 của line để bỏ qua ứng viên.
- Smart Trim hover dùng worker debounce, chỉ tô đỏ đúng khoảng đã tính và không đổi revision. Click chỉ dùng proposal còn khớp điểm hover, snapshot, selection, tool và generation; một click commit một transaction, Undo phục hồi topology.
- Scissors đã có regression trực tiếp cho cung ellipse: tách thành hai cung giữ nguyên hình học, giữ identity gốc, Undo trả lại source đóng và Redo mở lại path.
- Knife, Vector Eraser, Scissors, Smart Trim, Trim Objects, Smart Fill và Shape Builder đều gửi phép tính vector qua `CutGeometryRequest`/worker và chỉ áp dụng proposal sau khi kiểm ngữ cảnh. Raster Eraser thêm edit node alpha không phá hủy; pixel evaluation chạy qua raster evaluator/preview service, không tính lại ảnh đồng bộ trong pointer handler.
- Scissors có property bar mở dock hướng dẫn: click node/đoạn, hit area 6 px màn hình, kéo không cắt, thao tác hợp lệ tạo một bước Undo. Dock là thông tin đúng với tool không có tham số; mở dock không làm dirty tài liệu.
- Tách icon identity khỏi tool ID và cấp glyph vector riêng cho đủ tám tool, có GDI+ và GDI fallback; catalog có `static_assert` cấm trùng icon. Đây là glyphs vector của painter hiện có, chưa phải pipeline nạp asset SVG.
- `NodeTool::close_selected()` nối lại đầu/cuối trùng tọa độ sau khi Scissors tách loop, để người dùng đóng path bằng Node Tool mà không để hai endpoint trùng nhau. Thêm kiểm tra Undo/Redo và cung mở vào command test.

#### Bằng chứng mới cùng source digest

- Request `a9ebfb80887a4aa3bc2abdb43672cb20`: `test_pikdraw_canvas_view` **1/1 passed** sau bổ sung fail-closed cho Smart Trim; gồm tangent, coincident overlap và self-intersection.
- Request `7e403cdd531346a5b136359694c54d08`: `test_pikdraw_document_node_commands` **1/1 passed**, gồm regression Scissors trên elliptic arc.
- Request `bb49764978c04d458e1d4c49bfbf0fdf`: ma trận **14/14 CTest passed**: geometry, path cut, node commands, vector cut/node, raster evaluator/vertical slice/edit graph/preview, document storage/storage fidelity, desktop shell, native canvas và SVG exporter. Matrix có kiểm tra alpha U8/U16, raster source invariant, `.pr` roundtrip, Undo/Redo, worker/stale/cancel và native HWND gestures.
- Request `27880676db7d4557b92e715a5b67f754`: target `pikdraw` build-only **thành công**, đã relink `build/win-dev/apps/desktop/pikdraw.exe` với cùng source digest `8ec0dd75699f4e9362cec35e735fcb0b70b61ddcebac58b84888fecdbbce40c4` của ma trận 14/14. Đây là bằng chứng build, chưa phải smoke test nhìn thấy.
- Request `d82093b39a1345a0af1ec89ca651f616`: `test_pikdraw_desktop_shell` và `test_pikdraw_canvas_view` **2/2 passed** sau khi thêm Scissors Properties; kiểm tra click nút bar mở đúng hướng dẫn mà revision không đổi.
- Request `b54624a596d14119be393c57fbc2a18d`: target `pikdraw` build-only **thành công** sau UI patch, cùng source digest `66dfbc92e5cd2b739cebaf0f6015b1754de0e12fbccff2bf904e789cf4de26cb` của hai test trên.
- Request `5c3871ae60fa4e5d8690725b089b02f3`: `test_pikdraw_desktop_shell` và `test_pikdraw_canvas_view` **2/2 passed** sau khi cấp glyph riêng và kiểm tính duy nhất của catalog.
- Request `2c022178b9e3457db90a8e6f8200b505`: target `pikdraw` build-only **thành công** sau icon patch, cùng source digest `321a9e5d88fb67286efbbbb0a3ebfd9162f525fcf6e5c67a4e628c96b0387d11` của hai test trên.

#### Trạng thái gate còn lại

- **CS-CRASH-01 chưa đạt:** chưa có thao tác tái hiện, fixture tối thiểu hoặc stack của lần ứng dụng tự đóng. Rà soát WER Application log và các `AppCrash_pikdraw.exe` archive ngày 26/10 xác nhận `ucrtbase.dll`, `0xc0000005`, offset `0x25f10`; mỗi report archive chỉ còn `Report.wer`, không có `.dmp`/stack. Bằng chứng này không đủ xác định root cause hoặc chứng minh crash đã sửa.
- **CS-UI-01 chưa đạt:** cửa sổ native chưa được kiểm tra trực quan trong lượt này. Computer Use trả trạng thái bị dừng bởi phím Escape vật lý; theo hướng dẫn tool, không tiếp tục gửi input UI sau sự kiện đó. Test HWND xác nhận event/commit/Undo nhưng không thay thế kiểm tra menu, hit rect, Properties, theme, DPI và thao tác bằng chuột thật.
- Catalog đã có glyph khác nhau, nhưng chưa có asset SVG được nạp làm nguồn render; UI walkthrough và nguồn icon SVG của plan vẫn cần hoàn thiện cùng một lượt kiểm tra trực quan.
- **CS-ASYNC-01 đạt phần worker/cancel/stale ở phép tính vector, còn thiếu stress dài hạn, peak-memory, device-loss và đo p50/p95 bằng input-to-present vật lý. Số đo trace tổng hợp cũ chỉ kết thúc ở GDI submit, không phải scanout/màn hình.
- **CS-DOC-01 đạt các luồng đã có test riêng:** Knife + Smart Trim lưu/mở `.pr` và xuất/nhập SVG; Raster Eraser giữ nguyên source, alpha U8/U16, save/reopen và Undo/Redo; Scissors giữ curve và topology khi split/Undo/Redo. Chưa có ma trận save/reopen/export riêng cho mọi tool dưới PowerClip, nhiều selection và mọi trường hợp ICC/profile/parent stack.

Các sửa đổi và kiểm chứng nội bộ trong scope đã tiếp tục hoàn tất; plan vẫn không được ghi là nghiệm thu phát hành cho tới khi có repro/stack crash và lượt kiểm UI/runtime theo đúng ma trận. Tiến trình Computer Use báo bị dừng bởi Escape vật lý nên không có smoke walkthrough; app canonical đã relink xong từ source digest mới nhất và không có process giữ file sau build.

### Cập nhật cuối lượt: SVG runtime và kiểm tra build mới nhất

- Bổ sung đủ tám SVG tự thiết kế cho Knife, Vector Eraser, Scissors, Trim, Smart Trim, Shape Builder, Smart Fill và Raster Eraser. Catalog giữ mapping icon asset; CMake kiểm tra hợp đồng `24×24/currentColor`, chỉ nhận path tuyệt đối `M/L/Z`, sinh dữ liệu nhúng từ chính asset. Flyout painter vẽ cùng dữ liệu qua GDI+ và có fallback GDI; regression kiểm đủ mapping và parse mọi path.
- Request `58b6ec7a08e34dfca0029fa9ad9bf292` dựng xong; `test_pikdraw_desktop_shell` qua nhưng `test_pikdraw_canvas_view` timeout nội bộ 5 giây khi chờ Knife worker. Đã đọc JUnit/log, xác nhận revision không đổi, rồi dùng `-RetryFailed` cùng source digest thay vì coi lần chạy lỗi là pass.
- Retry request `e4efc161c20644e3985b71260cc262ef`: `test_pikdraw_canvas_view` và `test_pikdraw_desktop_shell` **2/2 passed**; source digest `36f3ebedf5e3f2ef9975c957ebfa13bf0a4a053961cb50c435f5f5fe2d8552a3`. Run lỗi đã dọn bằng `clean-test-artifacts.ps1 -Apply -RunId` đúng ID.
- Build request `53ec411dfc444fbc8d61a79928fe37d4`: canonical `pikdraw` build-only **succeeded** cùng source/source-after digest trên. Không có process giữ EXE và build lock trống sau khi chạy.

#### Trạng thái nghiệm thu còn mở

- **CS-CRASH-01:** chưa có chuỗi thao tác gốc hoặc dump/stack; WER cũ chỉ ghi `ucrtbase.dll`, `0xc0000005`, offset `0x25f10`. Các sửa lỗi và regression Cut & Shape không chứng minh chúng là nguyên nhân của crash lịch sử.
- **CS-UI-01:** chưa có walkthrough trực quan light/dark, VI/EN, DPI và kích thước cửa sổ. Computer Use đã báo bị dừng bởi Escape vật lý; HWND tests không thay thế nghiệm thu bằng chuột thật.
- **CS-ASYNC-01:** worker/cancel/stale/quota đã có cho phép tính hình học vector; còn thiếu stress dài hạn, peak-memory, device-loss và latency với mouse/present vật lý. Timeout 5 giây ở lần canvas đầu chưa tái diễn ở retry cùng digest nhưng vẫn được ghi nhận là một lần timeout test, không xóa khỏi bằng chứng.
- **CS-DOC-01:** Knife/Smart Trim có `.pr` roundtrip và SVG export/import; các ca save/reopen/Undo có cho Scissors, Trim Objects, Smart Fill/Shape Builder và Raster Eraser. Ma trận đầy đủ mọi tool dưới PowerClip, nhiều selection, ICC/profile và mọi định dạng export vẫn chưa chạy.

Vì các gate crash, UI trực quan, stress/device-loss và ma trận persistence/export đầy đủ chưa có bằng chứng, trạng thái là **source implementation và các kiểm tra đã chạy hoàn tất; nghiệm thu phát hành của toàn plan còn mở**.
