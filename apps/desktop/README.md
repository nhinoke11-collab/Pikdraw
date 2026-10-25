# PikDraw Desktop

Adapter đang chạy là **Win32 Native Shell** kết hợp **Direct2D 1.1 / D3D11 hardware hoặc WARP** qua `ViewportRenderer`.
Canvas dựng snapshot bằng GPU rồi đọc frame về pixel buffer; GDI chép ảnh vào backbuffer và vẽ lớp phủ. Qt/QSS và bốn workspace
là kế hoạch kiến trúc; không suy ra các workflow đó đã chạy từ bản vẽ UI tham chiếu.

## Quy tắc thiết kế cho thay đổi tiếp theo

Đọc [chuẩn UX/UI](../../docs/design/ui-ux-standard.md), [workspace](../../docs/architecture/workspace-layout.md)
và hoàn thiện [phiếu UI](../../docs/design/feature-ui-template.md) trước khi code tính năng.
Chốt menu chính, các thuộc tính trên property bar/Properties, icon SVG, label ngắn và tooltip/status/help.
Agent triển khai tự phân tích yêu cầu và UI hiện có, tự điền phiếu, tự quyết định các lựa chọn thiết kế
trong scope, ghi lý do ngắn và tự kiểm rồi tiếp tục code. Không giao mẫu trống cho người dùng hoặc
chờ người dùng/coordinator duyệt từng lựa chọn thường lệ. Review chất lượng khi bàn giao và quyền scope/contract vẫn giữ.
Tái dùng các bề mặt và action/value hiện có; không thêm bar/panel hoặc button dài theo từng feature.
Đây là quy tắc đích, không tuyên bố bản native hiện tại đã đúng toàn bộ thiết kế.

## Thao tác hiện có

- Pick V: hover, khung tương phản, tám tay nắm resize và dấu tâm. Kéo thân để di chuyển;
  kéo tay nắm để đổi kích thước; Shift giữ tỉ lệ ở góc. Preview chỉ đổi interaction state;
  thả chuột commit một lệnh; Escape, mất capture hoặc mở menu/settings hủy an toàn.
- Kéo từ vùng trống trái → phải: Window, chọn đối tượng nằm trọn trong khung, gồm viền.
  Phải → trái: Crossing, giao với hình học/fill/stroke, kể cả vùng rỗng.
  Shift thêm, Ctrl đảo chọn; Delete xóa, Ctrl+A chọn các root có thể sửa.
- Inspector: sửa fill/stroke, bật/tắt, nhập RGB/CMYK/độ đục, preset hoặc nhập viền nguồn
  bằng mm. Mỗi lần áp dụng cho nhiều đối tượng là một bước Undo. Màu hỗn hợp được báo rõ;
  chọn tab palette không chuyển màu nguồn đã có. Trái áp mục hiện tại, phải áp stroke.
- Hai palette cơ bản RGB/CMYK, mỗi palette 18 swatch có source channels thật. Chưa phải
  palette thương hiệu vật liệu hoặc thư viện màu đã đo bằng thiết bị.
- Profile preview: nạp ICC/ICM RGB hoặc CMYK; Windows Color Management chuyển về sRGB
  với Relative Colorimetric. Có thể chọn SWOP đã cài trên Windows nếu file tồn tại.
  Chưa nạp CMYK ICC thì preview gần đúng. Thiết lập lưu riêng tại
  %LOCALAPPDATA%/PikDraw/preview-profiles.txt; không đổi source channels.
- Tạo rectangle/ellipse dùng kiểu nguồn mặc định khi chưa chọn gì. Trang, pan, zoom,
  native save/reopen và SVG export gọi workflow thật.

## Bản đồ tính năng

| Nhóm | API / triển khai | Kiểm thử |
| --- | --- | --- |
| Canvas & input | native_window_canvas.cpp, native_window_canvas_snap.cpp, native_window_selection.cpp, native_window_shortcuts.cpp | test_pikdraw_drawing_tool_ui, test_pikdraw_pen_native_shell, test_pikdraw_vertical_slice_e2e |
| Khung chọn & preview | native_window_selection_geometry.hpp, native_window_selection_paint.cpp | input native và ảnh từ test_editor_tools |
| Render cache | native_window_canvas.cpp: snapshot/camera/size/profile/theme, dọn khi DPI đổi; nhận BGRA trực tiếp, `DamageTracker` chép vùng dirty nhỏ vào GDI bitmap, D2D vẫn render/readback toàn frame | `test_pikdraw_desktop_shell`; không phải GPU partial-render |
| Shell paint/status | native_window_paint.cpp, native_window_paint_status.cpp, native_window_canvas_overlays.cpp | desktop shell screenshot and interaction tests |
| Fill/stroke | presenters/object_style.hpp, object_style.cpp, native_window_style*.cpp, native_style_dialog.* | palette, hộp nhập và Undo tests |
| ICC preview | native_window_color_profiles.cpp → color::IccPreview → render backend | ICC test và pixel consumer |
| Document I/O | DocumentWorkflow, native_window_files.cpp; UI `.pr` save/open dùng một worker ưu tiên thấp, immutable save snapshot và revision check khi open commit | save/open, dirty snapshot và stale-open regression |
| Menu, pointer, window events | native_window_menu_actions.cpp, native_window_pointer_dispatch.cpp, native_window_events.cpp | drawing_tool_ui và desktop_shell |
| Isolation & numeric | native_window_isolation_menus.cpp, native_window_numeric_input.cpp, native_window_numeric_edit.cpp, native_window_array_quickdim.cpp | desktop_shell và drawing_tool_ui |
| Workflow | document_workflow_appearance.cpp, document_workflow_effect_draft.cpp, document_workflow_clipboard_transform.cpp, document_workflow_group_transform.cpp | effect_draft, drawing_tool_controller và desktop_shell |
| DPI | DIP chung cho paint/hit-test; WM_DPICHANGED dựng lại font/cache; canvas buffer theo pixel vật lý | drawing_tool_ui: thông điệp 120/144/192/96 DPI; chưa nghiệm thu chuyển màn hình thật |
| Phục hồi | native_window_files.cpp + native_window_recovery.cpp; timer 60 giây, snapshot immutable ghi checkpoint nền (tối đa 8 job); startup scan dùng một worker ưu tiên thấp sau frame đầu, hủy giữa các checkpoint | test_pikdraw_save_recovery |

`test_pikdraw_desktop_shell` dùng bốn translation unit (`test_desktop_shell`,
`_align_boolean`, `_appearance`, `_text_settings`) và `desktop_shell_test_helpers.hpp`.
Drawing controller giữ cùng target, thêm `test_drawing_tool_snap_input.cpp`;
storage fidelity thêm `test_storage_appearance_migration.cpp` trong core/document.

## Build và xác minh

Mọi phiên dùng source gốc và output duy nhất theo [native build](../../docs/development/native-build.md).
Không dùng preset/build folder cũ theo cấu hình riêng:

```powershell
.\tooling\native\build.ps1
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell -TestRegex '^test_pikdraw_desktop_shell$'
.\tooling\native\build.ps1 -Configuration Release
```

Giữ môi trường/toolchain và encoding UTF-8 nhất quán; thêm/sửa compiler flags cần task build có scope,
không thay đường output để né lỗi.

`test_pikdraw_editor_tools.exe` gửi mouse/keyboard messages tới cửa sổ
native, mở và thao tác hộp màu thật, kiểm tra source/Undo/ICC pixels/native reopen. Cửa sổ test ẩn; đây không phải đo input-to-present.

Chẩn đoán timing canvas chỉ ghi khi người vận hành chủ động đặt biến môi trường
`PIKDRAW_PERF_TRACE_PATH` thành đường dẫn CSV. Trace dùng QPC và ring buffer bounded;
worker nền ghi input/message, render, paint, first frame, recovery scan và cặp begin/end save/open .pr, không ghi tọa độ
hoặc nội dung bản vẽ. `PaintEnd` không chứng minh OS đã present pixel.

## Giới hạn hiện tại

Pick/transform/style hỗ trợ root path/text có thể sửa; đã có tay nắm xoay (rotation handle), lật đối xứng (flip), Pen/Quick Shapes/Gradient, cùng các bộ điều khiển `NodeToolController`, `ContourToolController`, `DimensionToolController`. `GroupObject` và `ImageObject` có đường vẽ trong SoftwareRasterizer và D2D; điều đó không khẳng định mọi thao tác chỉnh sửa nhóm/ảnh đã có trên shell.
File thiết kế chỉ dùng `.pr`. Payload schema v23 được lưu trong container v23 khi payload dưới 1 MiB, hoặc container chunk v24; schema v24 raster source/sample dedup được ghi trong container v25. Reader nhận v2–v25. Không còn trần file 256 MiB hoặc quota cứng 100.000 object/1.000.000 path element; wire widths và memory quota vẫn áp dụng. UI save/open `.pr` chạy trên một worker tuần tự; save ghi immutable snapshot và stream payload theo chunk tối đa 1 MiB vào file tạm, open chỉ áp dụng snapshot nếu tab/revision/UI context còn nguyên. Reader vẫn đọc toàn file và giải nén payload trước khi parse; streaming load, lazy assets và peak-RAM gate còn mở. Shutdown có thể chờ I/O/parse hiện tại. Source ICC, raster descriptors/edit graph, fidelity report, text properties, layer properties và artboards có codec tương ứng. Auto-recovery checkpoint được ghi nền từ immutable snapshot; startup recovery list chạy nền sau frame đầu, hủy tại ranh giới checkpoint và kiểm tra trạng thái UI trước khi bật prompt. Mỗi candidate vẫn được đọc/giải mã full-buffer; shutdown có thể chờ file đang đọc xong. Xem [PR format](../../docs/architecture/native-pr-format.md).
