# Công cụ phát triển PikDraw

Quy tắc chung duy nhất: [AGENTS](../AGENTS.md).
Cú pháp, hàng đợi giữa các task, cấu hình Auto, test theo target, retry và cleanup:
[Native build](../docs/development/native-build.md).
Không lặp workflow ở README module hoặc ADR.

Runner chính là tooling/native/build.ps1. tooling/pikdraw.ps1 cung cấp alias:

| Alias | Tác vụ |
| --- | --- |
| doctor | Kiểm tra toolchain và registry |
| build | Chuyển option sang native/build.ps1, gồm -Status/-Submit/-Wait |
| test -TestTarget tên | Build và chạy test đã chọn; -AllTests mới chạy toàn bộ |
| check -Suite architecture/contract | Kiểm tra Python, không build C++ |
| check -Suite unit/all | Kiểm tra full suite tường minh |
| run | Mở app canonical; build trước nếu chưa có |
| package | Build và kiểm ZIP Release |

Ví dụ:

~~~powershell
.\tooling\pikdraw.ps1 build -Status
.\tooling\pikdraw.ps1 test -Submit -TestTarget test_pikdraw_document_storage
.\tooling\pikdraw.ps1 check -Suite architecture
~~~

Các lệnh task/context cũ đã bỏ, không trả kết quả thành công giả.
Không cần task create/claim/lease hoặc context packet để sửa code.

Trong native/: env.ps1 tìm Python/CMake/Ninja/compiler; build_broker.py và build_state.py
là implementation nội bộ của runner, không là entrypoint thay thế.
dev.ps1 -Build giữ cấu hình hiện có. package.ps1 dùng cùng runner và khóa build.
clean-test-artifacts.ps1 chỉ dọn output đã xác minh, xem hướng dẫn chính trước khi Apply.

Regression của runner ở tests/harness/test_native_workflow.py và test_build_broker.py.
Suite dùng workspace tạm riêng và CMake/CTest giả để kiểm tra tranh chấp, scope, lỗi/retry;
nó không xác nhận build hay UX của ứng dụng C++.
