# Workers — bản nháp spike C

Host/worker C++20 đã build và chạy kiểm thử tiến trình con trên Windows. Parent dùng overlapped named pipes với deadline chung cho cả write/read; timeout, partial response, sai version/sequence/type hoặc frame quá lớn kết thúc worker scope. HANDLE_LIST chỉ cấp stdin, stdout và stderr NUL; log không trộn vào binary stream. Bản canary AppContainer ngày 15/10 có positive control cho cả năm probe; kết quả chi tiết ở artifact bên dưới.

Child được tạo suspended, chỉ resume sau khi assign Job Object thành công. AppContainer được yêu cầu thì lỗi tạo profile/attribute phải từ chối launch, không rơi về tiến trình không cách ly. Profile tồn tại trước bị từ chối; cleanup chỉ xóa profile instance đã tạo. Canary đã tạo profile riêng và cấp quyền RX trong thư mục bin tạm của chính lần chạy; chưa có fault-injection đầy đủ cho cleanup profile.

Chạy không AppContainer chỉ cho repository-owned smoke-test worker với opt-in trusted_test_worker; không dùng cho parser/plugin không tin cậy. Cấu hình Job Object đã được kiểm tra tạo thành công, chưa chứng minh enforcement của toàn bộ quota.

`pikdraw_spike_c_tests --protocol-only` qua 8 nhóm kiểm thử, gồm child ping/pong/import unsupported, stalled write timeout và truncated-response exit. Đây là protocol/process subset, không thay cho full containment gate. Full test đã chạy năm canary có đối chứng nhưng hiện chỉ ghi nhận bốn denial tường minh; full gate trả lỗi đúng thiết kế.

Chưa có corpus parser thực, kiểm tra fidelity, CPU/memory quota đầy đủ hoặc sandbox acceptance. Untrusted plugins/converters vẫn không được bật. Native artifacts ở out/spikes/c-direct trong lane checkout.

## Lượt canary thực tế 15/10/2026

Đã thêm `tests/live_containment.cpp`; test mặc định thực sự tạo AppContainer riêng và chạy đủ năm canaries, với listener localhost và positive controls. Chỉ grant RX cho thư mục bin mới thuộc scratch do test tạo; không đổi ACL cha, không loopback exemption hoặc firewall rule toàn máy. Worker đối chứng được whitelist thêm đúng event handle test; sandbox không được nhận handle này. Các probe dùng dữ liệu tự sinh và binary worker của repo.

[Artifact theo từng case](reports/live-containment-20261015.json): read/write bị từ chối lỗi 5, handle bị từ chối lỗi 6 (OS strict handle check `0xC0000008` được bắt riêng bằng shim SEH Windows), child bị quota từ chối lỗi 1816. Probe child chạy sau cùng vì quota có thể kết thúc process vi phạm. Lỗi mạng 10060 là timeout; **không được xem là explicit permission denial**. Full test exit 1, containment gate fail; protocol/process subset 8/8 pass. Không bật untrusted plugins/converters.

Host nay cleanup ngay khi Launch thất bại, đóng Job handle, enforce prevent_child_processes kể cả configured count lớn, chặn response quá budget và xử lý lỗi cấp phát response. Thêm diagnostic phase/worker exit khi IPC lỗi. SEH shim chỉ bắt STATUS_INVALID_HANDLE dự kiến của probe; không bắt hoặc đổi nhãn crash tùy ý thành pass.

Review Gemini độc lập đã có. Rủi ro còn mở: cancellation drain `GetOverlappedResult(..., TRUE)` bảo vệ lifetime OVERLAPPED nhưng có thể chờ vô hạn nếu kernel/driver không hoàn tất hủy. Không thay bằng return sớm gây dangling memory. Cần thiết kế broker/deferred ownership có budget trước production; timeout tests hiện chỉ chứng minh named-pipe hoạt động bình thường, không chứng minh sống sót qua kernel hang. Profile cleanup chưa có fault-injection đầy đủ.

Microsoft mô tả [giới hạn Job Objects](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information) và [loopback isolation](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/troubleshooting-uwp-firewall). Timeout quan sát được chưa đủ phân biệt nguyên nhân; không đổi policy OS để làm test xanh.
