# Tổ chức mã nguồn

Tài liệu này mô tả cây mã hiện tại và một vài nguyên tắc review. Đây không phải bộ quy tắc số dòng hay yêu cầu tách file máy móc.

## Cấu trúc hiện tại

- apps/desktop: ứng dụng Win32/GDI. Phần chính nằm trong src/actions, development, presenters, runtime, shell, tools và workflows.
- core: document, geometry, rendering, color và typography có source; localization mới là chỗ ghi đặc tả.
- modules/vector và modules/interop có implementation. raster, production và scene3d hiện là module kế hoạch.
- platform/workers: launcher, IPC và kiểm tra proposal của worker.
- Test C++ thường nằm trong thư mục tests của module. tests/architecture, tests/harness và tests/integration chứa kiểm tra cấp repo.
- packages/plugin-sdk mới có tài liệu; chưa phải plugin host đang chạy.

Tên module và dependency chuẩn nằm trong registry/modules.json. README của từng module mô tả chi tiết hơn khi có thay đổi tại module đó.

## Nguyên tắc review

- Nhóm code theo hành vi và nơi nó được dùng. Helper riêng cho một feature có thể ở cạnh feature đó.
- File khoảng 1.000 dòng hoàn toàn có thể hợp lý. Kích thước tự nó không bắt buộc tách file hoặc xin exception; file dài hơn vẫn ổn nếu trách nhiệm rõ, dễ đọc và có thể kiểm chứng.
- Tách khi có ranh giới thật về trách nhiệm, dependency, lifecycle hoặc kiểm thử. Gộp lại khi việc tách chỉ tạo wrapper, file vài dòng hay nhiều lần chuyển tiếp.
- Giữ public API gọn khi có lợi cho consumer; đừng hy sinh abstraction hữu ích hoặc tính rõ ràng để đạt một số dòng.
- Không minify, giấu implementation trong file include hoặc thêm module chung chỉ để né kiểm tra.
- Các lớp lưu trữ, production và renderer giữ ranh giới riêng: dữ liệu nguồn không bị thay bằng preview hoặc approximation.

Review tập trung vào tính đúng, khả năng sửa tiếp và ranh giới module. Không có hard cap LOC hoặc checker tự động về kích thước file.