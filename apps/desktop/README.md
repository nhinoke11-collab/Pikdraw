# PikDraw Desktop

Adapter đang chạy là **Win32/GDI**, dùng ViewportRenderer software và document C++20.
Canvas hiển thị snapshot thật; chỉnh sửa đi qua command của document. Qt/QSS và bốn workspace
là kế hoạch kiến trúc; không suy ra các workflow đó đã chạy từ bản vẽ UI tham chiếu.

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
| Canvas & input | native_window_canvas.cpp, native_window_selection.cpp, native_window_shortcuts.cpp | test_canvas_view.cpp, test_editor_tools.cpp |
| Khung chọn & preview | native_window_selection_geometry.hpp, native_window_selection_paint.cpp | input native và ảnh từ test_editor_tools |
| Frame cache | native_canvas_frame.hpp; key root/camera/size/profile/theme, giữ một frame | native CPU paint benchmark và ảnh |
| Fill/stroke | presenters/object_style.hpp, object_style.cpp, native_window_style*.cpp, native_style_dialog.* | palette, hộp nhập và Undo tests |
| ICC preview | native_window_color_profiles.cpp → color::IccPreview → render backend | ICC test và pixel consumer |
| Document I/O | DocumentWorkflow, native_window_files.cpp | save/reopen và source-channel assertions |

## Build và xác minh

Chạy trong terminal đã nạp MSVC x64/Windows SDK. Các literal nguồn và test tiếng Việt
cần UTF-8; giữ cả các cờ MSVC mặc định khi đặt CMAKE_CXX_FLAGS:

    cmake --preset win-msvc-debug "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /utf-8"
    cmake --build --preset win-msvc-debug --parallel 2
    ctest --test-dir build/win-msvc-debug --output-on-failure

test_pikdraw_editor_tools.exe [evidence-directory] gửi mouse/keyboard messages tới cửa sổ
native, mở và thao tác hộp màu thật, kiểm tra source/Undo/ICC pixels/native reopen rồi
chụp canvas bằng chính code paint của app. Cửa sổ test ẩn; đây không phải đo input-to-present.

Build Release theo win-msvc-release với cùng cờ UTF-8. Các test mới dùng điều kiện có hiệu lực
khi NDEBUG bật. benchmark_pikdraw_editor_tools.exe xuất JSON cho corpus 1.000 path /
101.000 path elements: redraw CPU, point pick, Window/Crossing và cached native CPU paint.
Không lấy số đo cửa sổ ẩn làm FPS/present hoặc chứng nhận GPU/min-spec.

## Giới hạn

Pick/transform/style hỗ trợ root path/text có thể sửa; group hierarchy chưa đầy đủ.
Hit-test chữ dùng logical bounds gần đúng; build này báo unsupported nếu chưa có backend
outline chữ thật. Resize biến đổi cả stroke theo transform; inspector ghi **viền nguồn**.
Chưa có node editing, rotation handles, constraint CAD hoặc CAM.

ICC hiện là preview của máy. Chưa gán/nhúng ICC vào document, soft proof giấy/mực, gamut
warning, BPC hoặc PDF/X. Native v2 giữ RGB/CMYK/spot/overprint; native v1 lossy bị từ chối
theo ADR 0005. Bản này không tự công bố native AI/CDR round-trip.
