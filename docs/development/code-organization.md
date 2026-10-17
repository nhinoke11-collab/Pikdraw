# Tổ chức mã nguồn & Quy chuẩn 800 dòng (Vibe Code Standard)

Tài liệu này quy định cấu trúc cây mã nguồn và **ngưỡng độ dài tối đa 800 dòng/file** nhằm tối ưu hóa khả năng đọc hiểu trọn vẹn trong 1 lần gọi tool (`view_file`), tránh lỗi sửa file và giảm xung đột khóa (lease) khi nhiều AI Agent cùng làm việc.

## Cấu trúc hiện tại

- `apps/desktop`: Ứng dụng Win32/GDI. Phần chính nằm trong `src/actions`, `development`, `presenters`, `runtime`, `shell`, `tools` và `workflows`.
- `core`: `document`, `geometry`, `rendering`, `color` và `typography` có source; `localization` là đặc tả.
- `modules/vector`, `modules/interop`, `modules/automation` có implementation. `raster`, `production` và `scene3d` hiện là module kế hoạch.
- `platform/workers`: Launcher, IPC và kiểm tra proposal của worker.
- Test C++ nằm trong thư mục `tests/` của từng module. `tests/architecture`, `tests/harness`, `tests/fixtures` và `tests/integration` chứa kiểm tra và dữ liệu mẫu cấp repo.

Tên module và dependency chuẩn nằm trong `registry/modules.json`.

## Quy chuẩn độ dài file (Ngưỡng 800 dòng)

1. **Ngưỡng chuẩn (300–700 dòng, tối đa 800 dòng/file)**:
   - Công cụ đọc file của AI Agent đọc tối đa **800 dòng/lần**. Vì vậy, mọi file `.cpp`/`.hpp` mới hoặc sau khi tách phải nằm trong ngưỡng **300–700 dòng** và **không vượt quá 800 dòng**.
2. **Cấm nối đuôi (No Appending) vào file đã trên 800 dòng**:
   - Khi thêm tính năng, công cụ hoặc nhóm lệnh mới vào một class lớn (như `NativeWindow`, `DocumentWorkflow`, `AutomationService`), **tuyệt đối không viết nối tiếp vào cuối file đã tiệm cận hoặc vượt 800 dòng**.
   - Bắt buộc tách nhóm hàm của tính năng đó sang một file `.cpp` cùng thư mục theo đúng tên nhóm tính năng (ví dụ: `native_window_text.cpp`, `document_workflow_appearance.cpp`) và đăng ký vào `CMakeLists.txt` của module.
3. **Nguyên tắc tách file theo nhóm tính năng (Feature Cohesion)**:
   - Tách theo **nhóm hành vi/tính năng đồng nhất** (ví dụ: nhóm Text/Table, nhóm Clipboard/Array/Group, nhóm Appearance/Effects, nhóm Tool Pointer Events), không cắt ngang giữa một hàm hoặc tách vụn thành các file chỉ vài chục dòng.
   - Với các hàm helper dùng chung giữa nhiều file `.cpp` của cùng một module, khai báo trong một header nội bộ (`*_internal.hpp` trong thư mục `src/...`) hoặc thành private helper method của class.
   - Chi tiết kế hoạch tách các file vượt ngưỡng hiện tại xem tại [docs/roadmap/code-modularization-800loc-plan.md](../roadmap/code-modularization-800loc-plan.md).