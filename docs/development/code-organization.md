# Tổ chức mã nguồn & Quy chuẩn 2000 dòng (Vibe Code Standard)

Tài liệu này quy định cấu trúc cây mã nguồn và **ngưỡng độ dài tối đa 2000 dòng/file** nhằm tối ưu hóa khả năng đọc hiểu, tránh lỗi sửa file và giảm sửa chồng cùng file khi nhiều AI Agent cùng làm việc.

## Cấu trúc hiện tại

- `apps/desktop`: C++20 / Win32 Native Shell kết hợp Direct2D 1.1 / D3D11 hardware hoặc WARP qua `ViewportRenderer`; staging/readback về BGRA rồi GDI trình bày. Phần chính nằm trong `src/actions`, `development`, `presenters`, `runtime`, `shell`, `tools` và `workflows`. Qt 6/Skia là định hướng tương lai.
- `core`: `document`, `geometry`, `rendering`, `color` và `typography` có source; `localization` là đặc tả.
- `modules/vector`, `modules/interop`, `modules/automation` có implementation. `raster` đang triển khai asset store/tile/edit graph; `production` và `scene3d` là module kế hoạch.
- `platform/workers`: Launcher, IPC và kiểm tra proposal của worker.
- Test C++ nằm trong thư mục `tests/` của từng module. `tests/architecture`, `tests/harness`, `tests/fixtures` và `tests/integration` chứa kiểm tra và dữ liệu mẫu cấp repo.

Tên module và dependency chuẩn nằm trong `registry/modules.json`.

## Quy chuẩn độ dài file (Ngưỡng 2000 dòng)

1. **Ngưỡng chuẩn (khuyến nghị 300–1800 dòng, tối đa 2000 dòng/file)**:
   - Mọi file `.cpp`/`.hpp` mới hoặc sau khi tách/chỉnh sửa phải nằm trong ngưỡng khuyến nghị **300–1800 dòng** và **tuyệt đối không vượt quá 2000 dòng**.
2. **Cấm nối đuôi (No Appending) vào file đã trên 2000 dòng**:
   - Khi thêm tính năng, công cụ hoặc nhóm lệnh mới vào một class lớn (như `NativeWindow`, `DocumentWorkflow`, `AutomationService`), **tuyệt đối không viết nối tiếp vào cuối file đã tiệm cận hoặc vượt 2000 dòng**.
   - Bắt buộc tách nhóm hàm của tính năng đó sang một file `.cpp` cùng thư mục theo đúng tên nhóm tính năng (ví dụ: `native_window_text.cpp`, `document_workflow_appearance.cpp`) và đăng ký vào `CMakeLists.txt` của module.
3. **Nguyên tắc tách file theo nhóm tính năng (Feature Cohesion)**:
   - Tách theo **nhóm hành vi/tính năng đồng nhất** (ví dụ: nhóm Text/Table, nhóm Clipboard/Array/Group, nhóm Appearance/Effects, nhóm Tool Pointer Events), không cắt ngang giữa một hàm hoặc tách vụn thành các file chỉ vài chục dòng.
   - Với các hàm helper dùng chung giữa nhiều file `.cpp` của cùng một module, khai báo trong một header nội bộ (`*_internal.hpp` trong thư mục `src/...`) hoặc thành private helper method của class.
   - Shell tách menu/pointer/window events, isolation và numeric edit/array; workflow tách appearance/effect draft và clipboard/group transform. Test vẫn giữ target cũ, chia translation unit theo miền; helper dùng chung trong header nội bộ.
   - Gate `test_source_files_respect_2000_loc_limit` kiểm `.h/.hpp/.cpp` trong core/modules/apps/platform/packages. Gate `test_cross_module_includes_match_registry` kiểm public include theo module registry, kể cả test với cạnh test riêng.
4. **Nguyên tắc Include Hygiene & Forward Declaration (Ngăn ngừa Rebuild Cascade)**:
   - Trong file header (`.hpp`), luôn ưu tiên dùng **Forward Declaration** (ví dụ: `class Document;`, `struct Point;`, `class ShellGeometry;`) thay vì `#include` trực tiếp khi chỉ sử dụng con trỏ (`*`), tham chiếu (`&`) hoặc `std::unique_ptr` trong pImpl. Chỉ đặt `#include` đầy đủ trong file `.cpp`.
   - Việc này ngăn chặn hiệu ứng tuyết lở (khi 1 header thay đổi khiến hàng chục file `.cpp` khác bị biên dịch lại ngoài ý muốn).
   - Giữ Precompiled Headers (PCH) thuần túy cho thư viện chuẩn C++ và API hệ điều hành bất biến; không đưa header nghiệp vụ thay đổi thường xuyên vào PCH.
