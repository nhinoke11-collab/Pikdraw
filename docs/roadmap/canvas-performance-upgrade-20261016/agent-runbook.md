# Runbook triển khai kế hoạch canvas

Đây là gợi ý áp dụng khi một task nâng cấp được giao; task board và AGENTS.md hiện hành là nguồn cho scope và quy trình.

1. Kiểm tra trạng thái task, phụ thuộc, module README/AGENTS và source snapshot.
2. Chọn paths đủ để giao kết quả hoàn chỉnh; chỉ tách khi giúp review, giảm xung đột hoặc cho phép tích hợp độc lập. Đường dẫn mới/shared cần thống nhất scope trước khi sửa.
3. Giữ nguyên thay đổi có sẵn. Dùng source gốc, không tạo checkout riêng.
4. Agent tự chọn chi tiết triển khai trong scope. Shared contract, storage, scheduling và tài nguyên cần review consumer khi thay đổi ảnh hưởng đến chúng.
5. Chọn build, test, benchmark hoặc kiểm tra UI theo rủi ro thực tế. Native build dùng tooling/native/build.ps1 và output chuẩn.
6. Bàn giao paths, source snapshot, lệnh/kết quả đã chạy, giới hạn và bước tiếp theo.

Catalog trong tasks.json là kế hoạch, không cấp quyền sửa file hay tự xác nhận task đã chạy. Ngưỡng hiệu năng chỉ được công bố sau khi đo trên workload và máy đã nêu. Một lần build hoặc ảnh chụp không thay cho số đo present/input-to-visible.
