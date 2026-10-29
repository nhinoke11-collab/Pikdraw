# Đại tu chỉnh màu ảnh: tính đúng, đồng bộ và xem trước realtime

Ngày rà soát: **25/10/2026**, kế hoạch hoàn tất **26/10/2026**. Trạng thái:
**đang triển khai từng phần; các mục còn lại và gate runtime được ghi ở cuối tài liệu**.

Yêu cầu: sửa trải nghiệm và logic của Exposure, Light, Color Balance, Levels, HSL,
Curves, White Balance, Vibrance, Selective HSL và Channel Mixer; giữ kết quả khi chuyển
chức năng, đồng bộ slider/ô số/Properties/property bar/canvas và kết quả Apply.

Đọc cùng [chuẩn UI](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md),
[document runtime](../../architecture/document-runtime.md) và
[roadmap sửa ảnh/màu/in](../../roadmap/image-editing-color-print-20261024.md).
Tài liệu này cụ thể hóa việc sửa các adjustment đang có; không thay workflow trong AGENTS.md.

## 1. Kết luận và giới hạn bằng chứng

Đây là lỗi xuyên suốt **binding UI → trạng thái chỉnh sửa → graph → preview → trình bày**,
không chỉ là thiếu thanh kéo hoặc giao diện chưa đẹp. Source hiện tại có đường làm thay thế
hiệu chỉnh kênh cũ, nhân đôi Exposure, điều khiển cùng tên nhưng sửa khác phạm vi,
histogram không nhận đúng kết quả worker, và preview có thể trở về pixel nguồn khi chờ.
Chỉ thay layout sẽ giữ nguyên các lỗi này.

Không tìm thấy bằng chứng rằng thao tác đổi tab thông thường tự xóa toàn graph:
`native_window_chrome_clicks.cpp:955–960` chỉ đổi tab UI. Cần phân biệt **mất hiệu chỉnh
đã ghi thật** do retarget kênh ở R02 với **hình trông như bị reset tạm thời** do fallback
preview ở R07. Chưa có runtime capture để quy mọi lần reset người dùng gặp về cùng một nguyên nhân.

Baseline kiểm tra là checkout gốc ở HEAD `1d5f28d`, có nhiều local edits từ trước lượt audit.
Đã đọc Git status trước khi làm; không sửa các local edits đó. `apps/desktop/tests` hiện có
source test, nhưng hai file workflow/export nêu ở mục 8 không tồn tại. Không suy trạng thái
test hiện tại từ ghi chép cũ hoặc lời mô tả acceptance trong feature docs.

Ảnh người dùng cho thấy menu Bitmaps dài, chứa lẫn chỉnh màu, mask, hình học và export;
ảnh không chứng minh thao tác realtime, sai số màu hoặc nguyên nhân một lần reset cụ thể.
Source đã được lần theo qua handler, command, evaluator, cache, renderer và storage/export.
Các ca số học bên dưới là suy ra trực tiếp từ công thức source, **chưa phải đo cửa sổ native**.

Đã thử quan sát app canonical đang mở bằng computer-use. Sau activate/refresh, capture lỗi
`FrameArrived timed out` rồi `window capture timed out`; đã dừng thao tác UI. Không có
bằng chứng native mới về kéo slider, DPI hoặc độ trễ input-to-present trong lượt này.

Đã gửi build/test qua runner chính thức, request `175900cbed5f4e2cbb80bd3b3c0bd911`:

```powershell
.\tooling\native\build.ps1 -Submit -Test -TestTarget @(
  'test_pikdraw_raster_evaluator',
  'test_pikdraw_raster_preview_service',
  'test_pikdraw_raster_histogram_service',
  'test_pikdraw_raster_vertical_slice'
)
```

Runner chọn Release theo cấu hình sẵn có. **Build thất bại trước CTest** tại local edit
`core/geometry/src/operations/boolean_polygon.cpp:5`: include `re ctangle_subset.hpp`
không tồn tại. Không có test nào trong request này được chạy. Không sửa lỗi ngoài scope,
không dùng binary cũ để báo pass. Output của riêng request được đọc rồi dọn theo runner.

## 2. Nguyên nhân cụ thể

Mức P0: sửa trước khi nghiệm thu tính đúng; P1: bắt buộc trong đợt đại tu; P2: hoàn thiện
khả năng sử dụng/duy trì. “Xác nhận source” không đồng nghĩa đã tái hiện bằng cửa sổ native.

### R01 — Histogram yêu cầu Edited nhưng đọc Source (P0, xác nhận source)

- `native_window_paint_docks.cpp:570–581` gọi `request(..., RasterHistogramMode::Edited)`.
- `native_window_raster_dock_paint.cpp:403–405` và `:200` gọi `find` không truyền mode.
- `histogram_service.hpp:35–42` mặc định `find` là **Source**; key của service có mode.
- Hệ quả: chart và Auto Levels không lấy được kết quả Edited vừa hoàn tất. Bản thân worker
  có chạy cũng không làm UI có histogram tương ứng. Auto rơi vào mức cố định 4%/96%.
- Sửa: mode phải là một phần của state/binding chung; request/find/is_processing và label
  cùng dùng một analysis key. Không vá riêng callback repaint.

### R02 — Đổi kênh Levels/Curves đang sửa artwork (P0, xác nhận source)

- `native_window_raster_properties.cpp:210–246` lấy Levels cuối cùng, đổi `channel`, rồi
  ghi lại cùng node ID. `:326–388` làm tương tự cho Curves; tìm node chỉ theo **loại**.
- `native_window_chrome_clicks.cpp:1015–1035,1057–1077` gọi các setter này khi bấm RGB/R/G/B.
- Chuỗi tái hiện: sửa Levels Red, bấm Green. Node Red trở thành Green với cùng tham số;
  hiệu chỉnh Red trước đó mất. Curves có cùng vấn đề. Ngay cả chỉ xem kênh cũng tạo Undo.
- Sửa: chọn kênh là state của editor. Load đúng node theo loại + kênh + NodeId đang sửa;
  không đổi channel của node cũ, không copy giá trị R sang G khi chỉ điều hướng.

### R03 — Exposure bị áp dụng hai lần khi bắt đầu chỉnh Light (P0, xác nhận source)

- `native_window_raster_properties.cpp:12–42` fallback Exposure vào `RasterLightParams`.
- `:58–112` dùng toàn bộ params đó để tạo node Light mới, trong khi node Exposure vẫn còn.
- `native_window_raster_edits.cpp:1037–1096` đọc/ghi Exposure hoặc Light theo thứ tự node;
  giới hạn EV thay đổi giữa ±10 và ±5. Numeric commit còn dựa vào việc có *bất kỳ* Light node.
- Ca phân biệt: ảnh xám code 64, Exposure +1 EV cho khoảng 90. Sau đó đặt Contrast = 0 khi
  chưa có Light; node Light mới mang thêm +1 EV, cho khoảng 125. Chỉ thao tác một giá trị
  trung tính cũng đổi kết quả. Đây là tính toán scalar từ evaluator, chưa là runtime capture.
- Nếu Exposure cũ là +6 EV, việc tạo Light từ fallback còn có thể bị validator Light ±5 từ chối.
- Sửa: EV có target rõ ràng. Không sao chép EV giữa hai node; không cộng gộp/migrate các
  node có mask, enabled state hoặc thứ tự khác nhau một cách ngầm định.

### R04 — HSL slider và ô số sửa hai phạm vi khác nhau (P0, xác nhận source)

- `native_window_raster_dock_paint.cpp:632–652` luôn vẽ ô số bằng Master HSL/Vibrance/WB.
- `:701–710,1342–1357` dùng `raster_hsl_active_channel_` để slider sửa Selective HSL khi
  chọn R/Y/G/C/B/M. Numeric commit vẫn đi qua `set_selected_bitmap_color_adjustment` cho Master.
- Chuỗi tái hiện: chọn Red, kéo Hue; nhập Hue bằng số để tinh chỉnh. Hai thao tác không sửa
  cùng node, phạm vi Hue còn khác nhau (Master ±180°, Selective ±30°).
- Sửa: slider, số, nhãn, range và reset lấy từ cùng `FieldBinding` có component target;
  WB/Vibrance là mục toàn ảnh, không giả là tham số của một selective band.

### R05 — Gamma paint và phép kéo không nghịch đảo (P0, xác nhận source)

- `native_window_raster_dock_paint.cpp:451,859,1176,1245` vẽ vị trí bằng
  `clamp(1 - 0.5*log10(gamma), .1, .9)`.
- `:1369–1374` suy gamma từ vị trí bằng `10^((1-rel)*2 - 1)`.
- Gamma 1 được vẽ ở khoảng 90% của dải input; công thức drag ở vị trí đó trả khoảng
  **0.15849**, không phải 1. Ngoài ra histogram dùng left+4 trong khi drag dùng left+8.
- Sửa: một cặp value↔position duy nhất, cùng rect DIP cho paint/hit/drag; giữ grab offset.
  Tại gamma 1, thumb ở giữa black/white và nhấn ngay thumb không làm thay đổi gamma.

### R06 — Clipping và Auto Levels dùng sai đơn vị thống kê (P0, xác nhận source)

- `histogram.hpp` quy định bins/clipping/coverage theo **đơn vị 1/255 pixel**;
  `histogram.cpp:151–160` cộng `coverage` vào các bộ đếm đó, còn `covered_pixels` tăng 1.
- `native_window_raster_dock_paint.cpp:460–469` chia `clipped_*_units` cho `covered_pixels`:
  ảnh đen toàn bộ có thể tính thành **25500%** thay vì 100% khi có histogram.
- Auto ở `:200–231` so `display_bins` có trọng số với target tính bằng số pixel. Ramp 256
  sample, mỗi bin 255 units: low target=2, high target=254, cả hai bị vượt ngay ở bin đầu.
- Auto luôn dùng channel[0] (Red), kể cả khi target là RGB/G/B; không phải luminance.
  Không có histogram vẫn dùng 4/96; bỏ qua kết quả ba setter rồi báo thành công.
- Sửa: percentile và clipping cùng đơn vị; mẫu rỗng/đồng màu có policy rõ. Auto dùng phân bố
  đúng channel hoặc luminance RGB đã khai báo, trên **input của node đang sửa**, không lấy
  output của chính Levels rồi lặp Auto làm trôi điểm. Chưa có analysis thì pending/disabled,
  không dùng một cặp số cố định như kết quả phân tích. Một Apply Auto là một command nguyên tử.

### R07 — Frame chờ preview trở về ảnh nguồn (P1, xác nhận đường source)

- `edit_preview_service.cpp:599–610` chỉ tìm cache khớp chính xác graph hiện tại; chưa có trả null.
- `d2d_render_backend.cpp:734–736` và `software_rasterizer.cpp:522–525` dùng nguyên ImageObject
  nguồn khi resolver trả null. Pixel nguồn không chứa các adjustment của graph.
- Đổi tham số tạo graph key mới → frame trước completion có thể vẽ ảnh chưa chỉnh. Khi kết quả
  có cache thì lại nhanh, tạo cảm giác lúc realtime/lúc reset. Chưa đo tần suất/độ dài nháy native.
- Sửa: trạng thái Pending/Ready/Error rõ ràng; giữ frame đã chỉnh gần nhất của **cùng source và
  ngữ cảnh geometry**, có nhãn đang cập nhật. Không coi fallback nguồn là preview thành công.

### R08 — Cache pixel giữ cả geometry/clip cũ nhưng key không có chúng (P1, xác nhận source)

- Key `edit_preview_service.cpp:159–170,246–266` có source storage, graph, profile, dimensions;
  không có transform/bounds/clip. Đây có thể là key tốt cho **pixel result**.
- Nhưng `:491–507` cache cả ImageObject với transform/bounds/clip; renderer dùng trọn object đó.
- Di chuyển/resize/đổi clip ảnh đã chỉnh nhưng pixel/graph không đổi có thể reuse presentation
  cũ. Đây là lỗi đồng bộ ngoài field màu, ảnh hưởng trực tiếp độ tin cậy của adjustment preview.
- Sửa: cache immutable pixel result + kích thước pixel; ghép transform/bounds/clip/appearance
  từ object của snapshot hiện tại. Kiểm toán resolver D2D và CPU cùng semantics.

### R09 — Scheduler chưa bảo đảm kết quả mới nhất luôn đến (P1, cần regression saturation)

- `edit_preview_service.cpp:556–596` quét toàn bộ object, thêm các graph key vào FIFO; không
  thay job cũ đang chờ của cùng object. Khi queue đầy, latest key được cập nhật nhưng không
  được enqueue. Stale completion không notify (`:296–325`), nên có đường chờ lần paint kế tiếp
  để request lại; cần test chứng minh trạng thái cuối cùng không bị bỏ rơi.
- `finish_job` chỉ cache result khi `bytes <= cache_capacity`; result lớn hơn cache nhưng
  không có error không publish và không notify. Native mặc định working budget 512 MiB,
  cache 192 MiB (`native_window.cpp:855–857`). Frame RGBA8 60 MP khoảng 240 MB có thể tính
  xong trong working budget nhưng không được đưa ra UI.
- Vòng repack output `:473–489` chưa poll cancellation, bổ sung nguy cơ phản hồi cancel chậm.
- Sửa: latest-request-wins theo object/session, bounded pending slot; mọi request cuối có
  Ready/Error/Cancelled/Superseded rõ. Result vượt cache không được biến mất im lặng.

### R10 — Một lần kéo tạo nhiều document revisions và Undo (P1, xác nhận source)

- `native_window_raster_dock_paint.cpp:1316–1411` gọi setter/document command và
  `UpdateWindow` ở mỗi move. `mouse_up` chỉ xóa drag flag, không kết thúc một batch.
- `Document::execute_command` (`document.cpp:160–177`) push history cho mỗi command đổi dữ liệu.
- Reset (`:242–296`) gọi nhiều setter; Auto gọi ba setter. Có thể tạo nhiều Undo, trạng thái
  trung gian và thông báo thành công khi chỉ một phần setter thành công.
- Sửa: gesture chỉ cập nhật draft, coalesce preview; Apply ghi một transaction với graph hoàn chỉnh.
  Không giữ transaction document mở suốt gesture và không undo hàng loạt để giả làm Cancel.

### R11 — Realtime đang phụ thuộc cách nhập và đường vào (P1, xác nhận source)

- Ô số raster commit ở `native_window_numeric_edit.cpp`; đường nhập ký tự chưa có raster
  draft preview thống nhất. Slider lại ghi ngay document. Local mask có dialog riêng.
- Nhiều action gọi `focus_raster_adjustment_in_dock(...)` rồi return khi có HWND; phía dưới
  còn modal code với Apply/Cancel. Test không có HWND có thể đi đường khác app thực.
- `focus_raster_adjustment_in_dock` chưa thiết lập đầy đủ visibility/scroll/focus field của dock.
  Menu WB/Vibrance/HSL đều focus cùng tab Color, chưa dẫn đến field cụ thể.
- Sửa: tất cả entry point đi qua cùng presenter/session. Numeric input hợp lệ preview như slider;
  giá trị đang nhập dở không được reset về mặc định hoặc đẩy sang document.

### R12 — More có nút không khớp handler, Channel Mixer không mở editor (P1, xác nhận source)

- Paint `native_window_raster_dock_paint.cpp:1103–1136` là lưới 2×2:
  Selective HSL / Channel Mixer; Clone Stamp / Local Mask.
- Hit `native_window_chrome_clicks.cpp:1099–1115` chỉ phân biệt hai hàng theo Y, không theo X:
  hàng trên gọi Selective HSL; hàng dưới gọi Channel Mixer. Nút vẽ không khớp hành động.
- `edit_selected_bitmap_channel_mixer` (`native_window_raster_edits.cpp:1462–1463`)
  chỉ focus lại More và return trong cửa sổ thật; tab More không chứa chín coefficient.
- Sửa: paint/hit cùng control descriptor; Channel Mixer có field matrix thực trong Properties.
  Không làm mất entry Clone Stamp/Mask; không mở rộng thuật toán retouch trong task này.

### R13 — Gesture không đóng băng target và chưa có lifecycle đầy đủ (P1)

- Drag chỉ giữ số `raster_dock_drag_control_`, đọc lại selection/range đang active ở từng move;
  state không gắn object/node/base revision/source generation.
- Raster interactive hit được gọi trước khối commit numeric thông thường
  (`native_window_chrome_clicks.cpp:57–64` so với `:941–945`); có thể bỏ qua xử lý field đang nhập.
- Guard raster hit không kiểm `show_snap_properties_` trong khi snap hit ở phía sau. Cần regression
  chồng panel, field chưa commit, selection/lock/source đổi giữa gesture và mất capture.
- Đây là rủi ro source cần kiểm qua dispatch thật, không khẳng định mọi tình huống đã xảy ra native.

### R14 — Thuật toán cần oracle; không đồng nhất “khác editor khác” với “sai” (P1/P2)

Evaluator đã có nền tảng hữu ích: graph thứ tự, version, U8/U16, alpha, mask và cancellation.
Không có bằng chứng để kết luận tất cả phép màu đều sai hoặc cần thay toàn bộ evaluator.
Các điểm cần đo/khóa contract:

- Light v1 tính tonal weights theo từng RGB channel; positive Highlights **giảm** vùng sáng
  (`evaluator.cpp:383–408`). Có thể đổi hue và khác kỳ vọng “kéo phải tăng”; spec hiện có đã
  mô tả dấu này. UI phải gọi rõ “Highlight recovery / Khôi phục vùng sáng” cho semantics v1.
- Color Balance tính vùng theo linear luminance của đầu vào **tại từng node**, cộng trục rồi
  clamp, sau đó rescale luminosity (`:416–488`). Rescale có clipping không bảo toàn luminance
  tuyệt đối. Ba range-node áp tuần tự nên không mặc định giao hoán.
- HSL là encoded sRGB; White Balance là relative gains, không phải Kelvin/RAW; Vibrance
  không có skin detection; Curves v1 là năm anchor nội suy tuyến tính. Không quảng bá thêm
  semantics chỉ vì UI vẽ dải màu đẹp hoặc dùng tên quen thuộc.
- Thứ tự graph theo vị trí node, lần đầu dùng adjustment append node; sửa node giữ vị trí
  (`raster_edit_commands.cpp:242–255`). UI chưa giải thích đầy đủ stack/order.
- Đổi thuật toán, dấu, working domain hoặc interpolation phải có version mới và legacy oracle;
  không đổi nghĩa file .pr cũ dưới algorithm version 1.

### R15 — Spec/test hiện có chưa chặn được lỗi tích hợp (P1)

Các feature docs còn mô tả modal, preview/Properties chưa làm hoặc coverage workflow chưa
được đối chiếu checkout. Có test evaluator/preview/histogram service, nhưng tìm kiếm source
test hiện tại không thấy regression gọi các setter đổi kênh, giao tiếp slider–số Selective HSL,
gesture raster hoặc Auto/Reset. `test_desktop_shell.cpp` có test tọa độ tab; nó không chứng minh
tính đúng graph hoặc pixel. CMake có guard EXISTS cho một số file workflow/export đang thiếu.

## 3. Đích kiến trúc và invariant bắt buộc

### 3.1. Một session chỉnh màu dùng chung

Tạo `RasterAdjustmentSession`/presenter ở desktop với key document ID + epoch + object ID.
Session giữ immutable base snapshot, base revision, source fingerprint, original graph,
working graph, active NodeId/component, generation và history bản nháp. Node IDs cấp trong
draft phải được validate lại tại Apply. Không dùng giá trị mặc định của UI làm dữ liệu thật.
Trong phạm vi này mỗi document có tối đa một session dirty, gắn với một ảnh; document khác
có thể có session riêng. Chọn ảnh B khi ảnh A còn draft chỉ suspend A, không tạo draft thứ hai
trong cùng document. Properties hiển thị target A và các nút Quay lại A / Apply A / Cancel A;
thông tin ảnh B vẫn xem được. Như vậy không cần đoán rebase giữa nhiều draft có base revision khác nhau.
Batch nhiều ảnh không thuộc scope lượt đại tu này.

Luồng đích:

```text
Menu / Properties / property bar / slider / ô số
    → cùng FieldBinding và RasterAdjustmentSession
    → working graph đầy đủ, giữ mọi adjustment trước đó
    → request preview bất đồng bộ có generation
    → pixel result → ghép presentation hiện tại → canvas
Apply → validate base revision/epoch/source → một document command nguyên tử
Save/reopen/export → graph đã commit + cùng evaluator, không dùng cache màn hình
```

Binding tối thiểu: semantic FieldId, ObjectId, NodeId/slot, channel/range, unit, min/max,
default, precision, enabled/error state, read/write draft và value↔position. Dùng shared
numeric descriptor hiện có; không tạo bộ parse/range riêng cho mỗi bề mặt.

### 3.2. Quyết định tương tác

| Sự kiện | Hành vi đích |
| --- | --- |
| Mở menu/Properties, đổi tab/kênh/range | Chỉ điều hướng; không đổi graph/revision/Undo, không reset các draft khác |
| Slider drag | Đóng băng target/source/base/generation; preview bật mặc định; mỗi move chỉ sửa draft |
| Nhập số hợp lệ | Cùng target/params với slider, cập nhật preview sau coalesce ngắn |
| Đang gõ `-`, `1,` hoặc chuỗi chưa hợp lệ | Giữ text và preview hợp lệ gần nhất; lỗi inline, không ép 0 hoặc silently clamp |
| Enter trong field, Tab/blur | Chấp nhận field vào draft; không Apply cả session; Tab theo đúng thứ tự controls |
| Mouse up | Flush sample cuối, kết thúc một gesture trong draft history; không tạo nhiều document Undo |
| Esc | Hủy gesture/field đang sửa trước; khi không còn edit cục bộ thì hủy session đang active |
| Apply | Ghi toàn bộ thay đổi của session bằng một command; chỉ báo thành công khi command thành công |
| Cancel | Bỏ draft; document/revision/history không bị sửa; preview quay về graph đã commit |
| Reset | Reset node/range đang ghi rõ trên nút, chỉ trong draft; không reset WB khi chọn selective range |
| Chọn ảnh khác/đổi tab document | Giữ draft theo đúng document/object, chuyển sang suspended; UI ghi rõ target; resolve draft ảnh A trước khi bắt đầu chỉnh B trong cùng document |
| Undo/Redo | Khi draft dirty, undo/redo draft theo gesture; khi draft sạch, dùng document history; không Undo artwork phía sau ngoài ý muốn |
| Close/Save/Open/Export khi dirty draft | Hiện lựa chọn Apply và tiếp tục / Bỏ bản nháp và tiếp tục / Quay lại; không âm thầm bỏ draft |
| Lock/delete/replace source/reopen/stale revision | Bỏ publish job cũ; session stale giữ giá trị để xem, chặn Apply; không đoán rebase |

Một document chỉ có một logical writer. Snapshot overlay cho preview không publish vào Document.
Việc chọn ảnh/tab đơn thuần không tăng revision, nên có thể giữ session. Nếu có document write làm
base revision stale, Apply phải thất bại an toàn theo contract hiện hành; không giảm kiểm revision
để làm test xanh. Suspend/resume/close phải có lifetime hữu hạn, không giữ nguồn/draft đã đóng.

### 3.3. Node và channel không ghi đè nhau

- Phân biệt **view target** với **operation target**. Chọn R/G/B chỉ load slot, không retarget node.
- Giữ graph v17 khi có thể: model hiện đã lưu nhiều node Levels/Curves với channel riêng,
  Color Balance theo range và Selective HSL theo hue band. Không cần tăng schema chỉ để sửa binding.
- Với tài liệu đơn giản, load node cuối cùng khớp kind + channel/range. Khi có nhiều instance cùng
  slot, cho chọn node theo ID trong danh sách adjustment; không âm thầm xóa/gộp các node trước.
- Slot chưa có node hiện identity trong draft; chỉ tạo node khi người dùng thật sự đổi giá trị.
  Sửa lại giữ ID, enabled, mask, algorithm version và vị trí trong stack.
- Giữ nguyên thứ tự mọi node cũ. Node mới append theo thao tác và hiển thị thứ tự trong danh sách;
  không sắp xếp graph theo tab UI hay hứa các phép màu giao hoán. Reorder graph không thuộc lượt này.
- EV quick field sửa đúng Exposure hoặc Light được chọn; nếu chưa có thì tạo Exposure. Khi tạo
  Light, EV mặc định 0; không lấy Exposure làm fallback của Light. Hai node cùng tồn tại được
  hiển thị rõ trong stack. Node disabled không tự enabled khi sửa số.
- Giá trị EV tối đa do **node target** quyết định; không dựa vào “ảnh có Light hay không”.
- Apply/Auto/Reset đa field dùng một graph patch/transaction có kiểm expected base; validation
  thất bại phải giữ toàn bộ graph cũ. Không sửa trực tiếp ImageObject bằng UI.

### 3.4. Preview scheduler và presentation

- Tách `PixelResult` khỏi ImageObject presentation. Key pixel gồm source identity/lifetime,
  encoding, color basis/working profile, full graph/mask/version, LOD/output size; nếu có ROI
  phải đưa coordinate domain vào key. Không dùng mỗi document revision để invalidate toàn cache.
- Publication token có document epoch + object/source + session ID + generation + graph fingerprint.
  Cùng key pixel có thể reuse, nhưng completion chỉ được gắn vào session còn hợp lệ.
- Một object có tối đa một job chạy và một pending state mới nhất; pending cũ bị thay thế có
  terminal status. Fair scheduling giữa ảnh/tài liệu; không giữ FIFO dài theo từng mouse move.
- Pending giữ last-good frame cùng source, có indicator đang cập nhật; Error giữ frame có nhãn lỗi,
  không lặng lẽ coi original pixels là kết quả chỉnh mới. Source/geometry không tương thích phải
  clear/đổi placeholder có thông báo, không mượn frame từ ảnh khác.
- Request theo thay đổi draft/nguồn/view resolution; không copy toàn object store mỗi lần paint
  chỉ để đổi một slider. Callback về UI không đợi worker và không render đồng bộ trong input handler.
- Preview tương tác dùng LOD vừa viewport, có quota; khi dừng input refine. Pointwise RGB operations
  dùng cùng evaluator/params/version. Geometry/mask cần mapping từ full-source normalized coordinates;
  không chạy Clone Stamp trực tiếp trên LOD rồi coi đó là cùng pixel semantics của full resolution.
  Giữ prefix retouch/geometry đúng hoặc invalidate/refine theo dependency tương ứng.
- Frame lớn hơn cache phải downsample/giữ uncached result theo budget hoặc trả diagnostic, không
  drop im lặng. Poll cancel cả copy/repack/analysis; completion khi lỗi cũng cập nhật UI.
- CPU và D2D ghép transform/bounds/clip/appearance hiện tại với PixelResult. Đổi vị trí/clip không
  dùng presentation cũ và không buộc chạy lại phép màu không thay đổi.

## 4. Phiếu UI đích

Tái dùng Properties phải, không mở một cửa sổ riêng cho mỗi adjustment. Cải tổ riêng nhóm
Bitmaps/adjustment, không thay framework hoặc toàn bộ khung workspace.

| Mục | Quyết định |
| --- | --- |
| Menu chính | Gom adjustment dưới `Bitmaps > Adjust > …`; Crop/Flip, profile và export giữ nhóm riêng trong Bitmaps |
| Action ID hiện có | Giữ `action.bitmap.adjust.exposure`, `.light`, `.color_balance`, `.channel_mixer`, `.levels`, `.hsl`, `.curves`, `.white_balance`, `.vibrance`, `.selective_hsl`; kiểm đối chiếu constants trước khi nối |
| Điều hướng menu | Mở/focus đúng section + field chính, scroll field vào view; không tạo command. Local Mask/Clone Stamp vẫn có đường mở cũ |
| Action editor mới | `action.bitmap.adjust.apply`, `.cancel`, `.reset_node`, `.preview`, `.before_after`; đăng ký enabled state/help/catalog, tách khỏi menu navigation |
| Standard toolbar/toolbox | Không áp dụng thêm tool mới; đây là adjustment theo ảnh đang chọn |
| Property bar | Giữ EV nhanh và tối đa vài field trọng yếu; nút SVG Thuộc tính mở đúng editor; không nhồi Apply/Cancel thành chuỗi ở bar |
| Properties | Header target + trạng thái draft; danh sách adjustment/node; histogram; editor active; nhóm Apply/Cancel/Reset phía dưới |
| Nhóm editor | Light; White Balance; HSL/Vibrance; Levels; Color Balance; Curves; Channel Mixer. Chọn mục không reset mục trước |
| Preview/so sánh | Preview on mặc định; trước/sau chỉ đổi view, không Undo. Phân biệt ảnh gốc với bản đã commit trước session bằng nhãn rõ |
| SVG | Tái dùng bộ icon hiện có khi đúng nghĩa; bổ sung icon expand/apply/cancel/reset/compare cùng viewBox/token nếu thiếu; không dùng chữ/emoji thay icon-only control |
| Nhãn/help | EN/VI qua catalog; mỗi field có accessible name, unit/range, tooltip/focus help, scope và lỗi; status hint ngắn |

Mỗi continuous field có slider + numeric field **cạnh nhau**, cùng binding và giá trị.
Bỏ cách đặt một cụm số Master phía trên và một bộ reference sliders scope khác ở dưới.
WB Temp/Tint và Vibrance vẫn luôn được ghi scope toàn ảnh. Selective HSL có Hue/Sat/Light/Feather
đủ ô số; Color Balance có range + 3 trục + Preserve luminosity. Channel Mixer hiển thị đủ 3×3
với nhãn Output/Input, không dùng More như nút gọi lại chính tab.

### Field contract

Tất cả params là draft của một bitmap hợp lệ; mọi field lấy enabled/reason từ cùng presenter.

| Editor | Fields / range / identity | Phạm vi |
| --- | --- | --- |
| Exposure | EV −10…10, 0; Light EV v1 −5…5 | Node đang chọn; quick EV hiển thị tên target |
| Light v1 | Contrast/Highlight recovery/Shadows/Whites/Blacks −100…100%, 0 | Node Light, giữ dấu/algorithm v1; không copy EV từ Exposure |
| Color Balance | Shadows/Midtones/Highlights; 3 trục −100…100%, 0; preserve true | Node đúng tonal range, giữ mask/enabled |
| Levels | RGB/R/G/B; In black/white 0/100%; gamma 0.1…10,1; Out black/white 0/100% | Slot/kênh độc lập; In black < white, Out black ≤ white như evaluator |
| HSL Master | Hue ±180°,0; Sat/Light ±100%,0 | Toàn ảnh |
| Selective HSL | R/Y/G/C/B/M; Hue ±30°,0; Sat/Light ±100%,0; Feather 0…100%, theo model default | Đúng hue band, không sửa Master |
| Curves v1 | RGB/R/G/B; output 0…100% tại X=0/25/50/75/100 | Slot kênh; polyline đúng nội suy tuyến tính, ô số cho mọi anchor |
| White Balance | Relative Temp/Tint ±100%,0 | Toàn ảnh, không ghi đơn vị Kelvin |
| Vibrance | Amount ±100%,0 | Toàn ảnh, không quảng bá bảo vệ màu da |
| Channel Mixer | 9 gains −2…2, identity diagonal=1 | Linear sRGB matrix, label clipping rõ |

Không selection/multiple/mixed/locked/read-only/profile chưa giải nghĩa: view info được,
edit disabled với lý do. Không tự unlock, convert profile hoặc sửa vector. Mask/enabled
phải nhìn thấy khi sửa node để tránh hiểu nhầm slider không có tác dụng.

Paint, hit, focus rect và scrollbar dùng cùng layout/control tree DIP. Tính content height từ
controls thực, không bảng offset đoán riêng. Hit target tối thiểu 24×24 DIP; kéo ra ngoài track
vẫn tiếp tục trong range; mất capture/Alt-Tab/resize có policy hủy gesture, không để drag flag treo.
Histogram header không được vô tình điều khiển Levels khi context/control đang bị panel khác che.

Tooltip VI mẫu: “Cân bằng màu — Chỉnh dải sắc độ đang chọn; giữ các hiệu chỉnh khác.”
Status: “Đang xem trước · Chưa áp dụng · Enter xác nhận ô số · Esc hủy thao tác”. Khi pending
analysis: “Đang tính phân bố màu”; khi lỗi: lý do cụ thể và Retry, không thông báo đã tối ưu.

Help cần ví dụ: chọn ảnh → Light → chỉnh Exposure → Levels R rồi G → Color Balance → HSL
→ so trước/sau → Apply → Undo/Redo → lưu/mở .pr. Giải thích Reset node khác Cancel session
và Khôi phục ảnh gốc; không dùng các tên này thay thế lẫn nhau.

## 5. Tính đúng màu và kết quả Apply

Sửa R01–R13 trước khi cân chỉnh cảm giác thuật toán. Giữ semantics v1 cho file cũ; không đổi
gamma, dấu Highlights, curve interpolation hoặc xử lý profile trong một refactor UI.
Nếu sau oracle cần thuật toán v2, đặc tả riêng công thức/domain/clamp/mask và version storage;
đưa ra preview nâng cấp có chủ đích thay vì âm thầm thay kết quả .pr cũ.

Oracle phải độc lập với evaluator production, không copy nguyên hàm rồi so hai bản giống nhau:

- Exposure: chuẩn tuyến tính `decode_sRGB → ×2^EV → encode_sRGB`; neutral và alpha.
- Levels: `outBlack + (outWhite-outBlack) * clamp((x-inBlack)/(inWhite-inBlack),0,1)^(1/gamma)`;
  đúng encoded domain và đúng kênh. Test Out black=white theo validator chung.
- Curves v1: kiểm tại anchor và midpoint từng đoạn bằng giá trị giải tích, không spline ngầm.
- HSL: màu primary/secondary với góc quay có đáp án; hue wrap ±180; hue-only giữ gray; alpha exact.
- Selective HSL: band center/boundary/overlap/feather, gray, tác động riêng band; phối hợp Master.
- Light: identity, dấu v1 đã khai báo, neutral/chromatic ramps và hue shift; công bố chỗ không
  có bảo toàn hue thay vì giả định đây là thuật toán luminance chuyên nghiệp.
- Color Balance: chiều trục, ba dải, preserve on/off, luminance sai số khi không clipping,
  extreme clipping được báo/đo; mask 0/partial/full, alpha giữ nguyên. Không hứa preserve tuyệt đối.
- WB/Channel Mixer: ma trận/gains giải tích, identity, clamp, đúng channel order, U16 và alpha.
- Với mọi node: disabled/identity bit-exact; U16 có mẫu phân biệt dưới một code U8;
  mask/density/invert không làm rò ra vùng coverage=0; nguồn bất biến.

Chạy cùng graph qua preview, final evaluator, native .pr reopen và TIFF master export.
So raw samples trước display ICC; display/proof là bước trình bày riêng. U8 sai số tối đa
1 code chỉ ở bước lượng tử đã khai báo; identity/alpha phải exact. U16 dùng oracle full precision,
không làm tròn qua U8. LOD không đòi bit-exact với full frame đã downsample sau phi tuyến;
phải định nghĩa resampling/oracle riêng và giữ full-quality path khi Apply/export.

## 6. Các bước triển khai và điều kiện ra khỏi bước

### P0 — Chặn sai kết quả và viết regression theo lỗi hiện tại

1. Thêm regression R01–R06 ở presenter/desktop + evaluator khi cần; chứng minh ca lỗi trước sửa.
2. Sửa mode histogram, đơn vị count/percentile, Auto không dùng fallback số cố định và không
   dùng Red thay RGB. Analysis input-node/version phải đóng băng; repeated Auto không trôi.
3. Tách chọn channel khỏi document mutation; ánh xạ slot theo node/kênh; sửa EV/Light duplication.
4. Thống nhất HSL number/slider và gamma mapping. Shared validator nhận cùng range ở mọi cửa vào.

Ra khỏi P0 khi các ca số học và ca đổi kênh/đổi chức năng qua UI handler đúng; không chỉ direct
command API. Source/profile/mask/enabled/ID/Undo và file cũ không đổi ngoài thao tác được yêu cầu.

### P1 — Session và graph commit nguyên tử

1. Tạo session/presenter/FieldBinding dùng chung; bỏ đường setter UI ghi ngay ở từng move.
2. Overlay immutable working graph cho canvas; draft history theo gesture và numeric edit.
3. Một Apply command patch graph có expected epoch/revision/source; Cancel/Reset/Auto nguyên tử.
4. Đóng băng target, xử lý capture loss, active numeric field, suspend/resume và stale writes.
5. Kết nối toàn bộ entry point, giữ mask/editor cũ khi cần nhưng đọc/ghi qua cùng session.

Ra khỏi P1 khi một chuỗi Light→Levels→Color Balance→HSL giữ mọi params, Apply một Undo;
Cancel không đổi revision; save/reopen và Undo/Redo giữ đúng toàn graph.

### P2 — Preview realtime đáng tin cậy

1. Pixel cache tách presentation; latest pending slot; generation/terminal completion đầy đủ.
2. Last-good-frame có trạng thái; LOD + refine có budget; loại silent drop và fallback nguồn giả.
3. Coalesce input theo nhịp UI, flush final sample; không `UpdateWindow` đồng bộ mỗi move.
4. Histogram có key/mode/input-stage đúng; tính async, chậm hơn preview được nhưng không
   hiển thị dữ liệu cũ như thuộc graph mới. Poll cancel ở repack/analysis và shutdown.
5. Trace input event→draft generation→worker begin/end→upload→present, không chỉ thời gian evaluator.

Ra khỏi P2 khi queue saturation không bỏ rơi request cuối; source/selection/tab đổi không có frame
lạc đối tượng; geometry/clip cập nhật ngay; không nháy về original trong pending.

### P3 — Đại tu Properties, menu và accessibility trong scope

1. Layout/control descriptors dùng chung paint/hit/focus/scroll/DPI; loại offset trùng lặp.
2. Hợp nhất slider+numeric; sửa More/grid dispatch và làm Channel Mixer matrix thật.
3. Menu Adjust có nhóm gọn, open/focus đúng mục; property bar tối đa controls chính + Properties.
4. Apply/Cancel/Reset/Preview/Compare, pending/error/disabled, EN/VI/help/icon và keyboard đủ.
5. Cập nhật feature docs cũ để không còn mô tả modal/coverage trái source; cập nhật snapshot
   capability chỉ sau khi có bằng chứng, không biến plan thành trạng thái “đã hoàn thành”.

Ra khỏi P3 khi thao tác native được ở cửa sổ hẹp, scroll và DPI kiểm được, không control chỉ vẽ,
không dùng test mode không HWND thay cho đường app thật.

### P4 — Nghiệm thu tích hợp

Đánh giá oracle màu, chuỗi workflow, persistence/export và latency theo mục 7–8.
Chỉ báo hoàn thành khi các lỗi P0/P1 của audit đã có regression và native walkthrough tương ứng.
Nếu môi trường chặn build/capture, báo rõ gate còn mở; không thay bằng screenshot mockup.

## 7. Ma trận acceptance bắt buộc

| ID | Given / When | Then / bằng chứng |
| --- | --- | --- |
| AC01 | Một ảnh chưa chỉnh; chỉ mở mọi mục, đổi tab/channel/range | Revision/history/graph/pixels không đổi; không tạo identity node |
| AC02 | Exposure +1 trên gray64; bắt đầu Light Contrast=0 | Không thành +2 EV; output khoảng 90, graph không copy EV |
| AC03 | Levels R khác identity; chuyển G, chỉnh rồi quay R | Mỗi kênh giữ params/NodeId và đóng góp pixel; view switch không Undo |
| AC04 | Curves RGB/R/G/B khác nhau; chuyển qua lại | Giữ cả bốn slot; anchor/ô số/polyline và output đúng |
| AC05 | Selective Red; sửa Hue bằng slider rồi số | Cùng node/value/range; Master và band khác không đổi |
| AC06 | Gamma 0.1/0.5/1/2/10; click thumb và drag có scroll | Round-trip mapping đúng tolerance; không jump lúc bắt đầu; field và thumb khớp |
| AC07 | Histogram Edited request; worker xong | Find đúng mode/generation, chart có data; Source/Edited phân biệt rõ |
| AC08 | Toàn đen/toàn trắng/ramp256/U16; histogram clipping | 0…100%, denominator đúng units; channel label đúng, không gọi Red là luminance |
| AC09 | Auto trên R/G/B/RGB, uniform/empty, chưa có hist; bấm lặp | Đúng phân bố input-node, không 4/96 giả, không trôi; lỗi/pending không success; một Undo khi Apply |
| AC10 | Light→Levels→Balance→HSL→WB→Curves, đổi tab nhiều lần | Kết quả cộng dồn theo stack; trở lại load đúng params; không reset/chỉ hiện effect cuối |
| AC11 | 200 move + numeric edit; Apply/Cancel/Reset | Một logical draft gesture; Apply một document Undo; Cancel không đổi graph/revision; Reset đúng scope |
| AC12 | Preview bị trì hoãn và cache miss/eviction | Giữ last-good có nhãn Pending; không frame nguồn giả; final đúng generation |
| AC13 | Queue đầy, completion đảo thứ tự, đóng/mở doc, A→B→A | Request cuối có terminal result; stale frame không publish; không treo hoặc giữ asset sau close |
| AC14 | Ảnh đã chỉnh; move/resize/rotate object, sửa clip | D2D/CPU dùng presentation hiện tại, không dùng transform/clip từ cache cũ |
| AC15 | Kéo rồi mất capture, đổi selection/source/lock, field chưa commit | Target không rò sang ảnh khác, không flag treo, không apply stale; focus/cancel đúng |
| AC16 | Menu và 4 nút More bằng chuột, thật có HWND | Mỗi label mở đúng control; Channel Mixer sửa đủ 9 hệ số; Mask/Clone vẫn truy cập được |
| AC17 | Node disabled/masked, image RGBA8/RGBA16, profile unresolved | Giữ ID/mask/enabled; unsupported giải thích rõ; không tự convert nguồn |
| AC18 | Apply→Undo→Redo→Save .pr→Reopen→TIFF master | Params/order/source/ICC/alpha/pixels theo oracle; export không lấy display cache |
| AC19 | Dirty draft, Save/Open/Export/Close hoặc external write | Không mất/auto Apply draft; boundary decision rõ; reject stale base, không half-commit |
| AC20 | 1366×768 và 1920×1080; light/dark; 100/125/150/200% | Field/action reachable, focus/hit/scroll không lệch; ghi riêng cấu hình thiếu thiết bị |

Bộ ảnh: neutral/chromatic ramps, primary/secondary patches, skin-tone/photo có quyền sử dụng,
ảnh saturated/near-clipping, alpha gradient, U16 near-code pairs, và ảnh 2/12/24/60 MP.
Generated fixture để trong output riêng của runner, tự dọn khi thoát/exception; các fixture
regression tái dùng là source test có quản lý, không xóa theo quy tắc output tạm.

Mục tiêu hiệu năng **đề xuất, chưa đo ở baseline**: UI input-handler p95 ≤8 ms; preview đầu
p95 ≤120 ms cho 2 MP và ≤200 ms cho 24 MP với LOD; refine cuối p95 ≤1 s/2 MP và ≤2 s/24 MP.
Đo riêng D2D hardware/WARP/CPU trong môi trường kiểm được, ghi CPU/RAM/DPI/graph/mask/source,
cold/warm cache và tải nền. Ít nhất 100 input samples sau warmup, báo p50/p95/max, dropped/superseded,
final generation và peak memory. 60 MP kiểm bounded memory/diagnostic/final delivery, không mặc định
đòi full-resolution 60 FPS. Nếu chưa đạt target thì báo số đo và xử lý bottleneck, không giảm target
âm thầm. Trace chỉ tới render/readback không được gọi là input-to-present; cần native presentation evidence.

## 8. Bản đồ code, test và phạm vi

| Domain | Điểm sửa/kiểm chính |
| --- | --- |
| Desktop editor | `native_window_raster_edits.cpp`, `native_window_raster_properties.cpp`, `native_window_raster_dock_paint.cpp`, `native_window_raster_dock_metrics.hpp`; tách session/presenter/control layout theo domain |
| Input/lifecycle | `native_window_chrome_clicks.cpp`, `native_window_pointer_dispatch.cpp`, `native_window_numeric_input.cpp`, `native_window_numeric_edit.cpp`, `native_window_events.cpp` |
| Entry points | `native_window_menu_actions.cpp`, `native_window_menus.hpp`, action constants/registry, property bar/Properties focus |
| Preview/analysis | `modules/raster/src/edit_preview_service.cpp`, `analysis/histogram_service.cpp`, `analysis/histogram.cpp`; public result/key API |
| Renderer | Native canvas resolver, D2D và SoftwareRasterizer; tách pixel result và presentation, cache invalidation theo dependency |
| Document | `raster_edit_graph.hpp`, `raster_edit_commands.cpp`, transaction API; expected-base atomic graph patch, preserve IDs/version/masks |
| Persistence/export | `core/document/src/storage/native_format.cpp`, `apps/desktop/src/shell/raster_bitmap_export.cpp`, page export worker/evaluator integration |

## 9. Tiến độ triển khai

Ngày **26/10/2026**, đã sửa một phần source theo scope P0/P1/P2:

- Histogram chart truy vấn `Edited` đúng mode; Auto Levels có service riêng cho `Source`,
  dùng trọng số cùng đơn vị coverage, chọn R/G/B đúng enum hoặc gộp ba histogram RGB cho
  Composite, không dùng mặc định 4/96 và không báo thành công khi dữ liệu chưa sẵn sàng/
  histogram gần đồng màu. Thay đổi Auto được ghi bằng một `SetRasterEditNodeCommand`.
- Chuyển kênh Levels/Curves chỉ đổi state inspector; getter/setter tìm node theo channel,
  giữ node và giá trị của kênh khác. Exposure quick field chỉ đọc/ghi node Exposure; nó không
  tự chuyển Exposure EV sang node Light.
- Ô số HSL dùng cùng selective band và range với slider khi chọn R/Y/G/C/B/M; Master vẫn
  dùng HSL toàn ảnh. Gamma Levels dùng một cặp ánh xạ nghịch đảo trên khoảng 0.1…10.
- Preview service có `find_last_good` cho cùng source/encoding/profile/dimensions khi key graph
  hiện tại đang chờ; presentation geometry/clip lấy từ ImageObject hiện tại. Đây là fallback
  hình ảnh gần nhất, chưa có trạng thái Pending/Error hoặc native trace input-to-present.
- Reset theo tab giờ thay graph bằng một `SetRasterEditGraphCommand`: một Undo khôi phục cả
  nhóm; command kiểm tra epoch/revision snapshot để từ chối ghi graph cũ lên document mới.
- Test `test_pikdraw_raster_histogram`, `test_pikdraw_raster_histogram_service` và
  `test_pikdraw_raster_preview_service` **pass** qua runner Release. Preview regression xác
  nhận last-good frame ở cache miss graph mới. `test_pikdraw_raster_edit_graph_command`
  **pass**: graph replacement, một bước Undo/Redo, duplicate ID rejection và stale-revision
  rejection. `test_pikdraw_raster_vertical_slice`
  build được nhưng fail trước phần test mới tại fixture Clone Stamp (`AddObjectCommand`, dòng
  329); source fixture đó nằm trước phần thêm trong lượt này và đang dùng các thay đổi local
  khác. Target `pikdraw` **build Release pass** trên checkout với các thay đổi raster này.
  `test_pikdraw_desktop_shell` chưa có kết quả xác nhận: một lượt CTest chạy xong nhưng runner
  đánh dấu **stale** vì source đổi trong lúc chạy; lượt sau build dừng ở file ngoài scope
  `apps/desktop/src/shell/native_window_annotation_properties.cpp:339` vì `detail_shape`
  chưa khai báo. Vì vậy chưa có desktop-shell regression pass hay native walkthrough.

Chưa hoàn tất: draft/session chung và Apply/Cancel/Reset nguyên tử cho mọi điều khiển; coalesce
gesture; sửa queue saturation/cancel/quota preview; trạng thái Pending/Error có thể truy cập;
Channel Mixer và menu More; full Properties/pointer/key dispatch; regression HWND, Save/Reopen/
export và walkthrough/DPI/latency. Chưa được xem P0/P1/P2/P3 hoặc đại tu tổng thể đã nghiệm thu.

Ưu tiên mở rộng tests hiện có: `modules/raster/tests/test_raster_evaluator.cpp`,
`test_raster_edit_preview.cpp`, `test_raster_histogram.cpp`, `test_raster_histogram_service.cpp`,
`test_raster_vertical_slice.cpp`; core document storage/commands và renderer image-resolver tests.
Thêm target desktop adjustment regression chuyên biệt cho shared presenter **và pointer/menu/key
dispatch có HWND** để không làm file test shell đã lớn phình thêm. Không khôi phục hay xóa cả test tree.
`apps/desktop/tests/test_raster_profile_workflow.cpp` và `test_raster_bitmap_export.cpp` không tồn tại
ở baseline audit dù CMake có nhánh EXISTS; không giả định hai target này đang chạy trong suite.

Sau khi blocker build được xử lý trong scope sở hữu của nó, dùng runner với target liên quan
như lệnh mục 1; bổ sung test histogram, document storage, desktop target mới và rasterizer/resolver
theo thay đổi thực. Gom Release gates theo hướng dẫn build. Không cần full suite cho mỗi sửa nhỏ;
khi chạy full suite phải chọn `-AllTests`. Không dùng focused pass để suy full suite/UX/physical proof.

File code mới/sửa phải tuân thủ giới hạn 2000 dòng. `native_window_raster_edits.cpp` đã sát ngưỡng;
tách legacy dialog khỏi session/binding thay vì thêm một khối editor mới vào cùng file. Core/raster
không phụ thuộc desktop. Bảo toàn local edits hiện có, đặc biệt menu/canvas/renderer/storage đang dirty.

Ngoài scope: engine RAW/HDR/Lab/CMYK channel mới, AI, retouch brush mới, đổi framework, thay renderer
toàn cục, tile storage toàn sản phẩm, proof máy in vật lý hoặc mở rộng format. Giữ các chức năng đang có;
task này sửa độ tin cậy của màu RGB sRGB U8/U16 và sự nhất quán giữa các bề mặt.

Agent triển khai bắt đầu từ R01–R06 và acceptance tương ứng, đi tiếp session→preview→UI→integration.
Báo kết quả theo từng gate: source đã sửa, test nào thật chạy, native thao tác nào đã kiểm, file/version
nào đã round-trip, latency nào đã đo và giới hạn còn mở. Không đánh dấu hoàn thành bằng số control,
action ID, ảnh thiết kế hoặc một lượt build đơn thuần.
