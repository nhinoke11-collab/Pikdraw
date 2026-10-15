# Workers — bản nháp spike C

Host/worker C++20 đã build và chạy kiểm thử tiến trình con trên Windows. Parent dùng overlapped named pipes với deadline chung cho cả write/read; timeout, partial response, sai version/sequence/type hoặc frame quá lớn kết thúc worker scope. HANDLE_LIST chỉ cấp stdin, stdout và stderr NUL; log không trộn vào binary stream.

Child được tạo suspended, chỉ resume sau khi assign Job Object thành công. AppContainer được yêu cầu thì lỗi tạo profile/attribute phải từ chối launch, không rơi về tiến trình không cách ly. Profile tồn tại trước bị từ chối; cleanup chỉ xóa profile instance đã tạo. Chưa chạy profile/ACL mutations trong quyền workspace hiện tại.

Chạy không AppContainer chỉ cho repository-owned smoke-test worker với opt-in trusted_test_worker; không dùng cho parser/plugin không tin cậy. Cấu hình Job Object đã được kiểm tra tạo thành công, chưa chứng minh enforcement của toàn bộ quota.

`pikdraw_spike_c_tests --protocol-only` đã qua 8 nhóm kiểm thử, gồm child ping/pong/import unsupported, stalled write timeout và truncated-response exit. Đây không phải containment evidence. Test mặc định trả lỗi khi năm denied canaries chưa chạy; CTest full gate cố ý không xanh. Không biến profile creation hoặc skip thành pass.

Chưa có live canary read/write/network/child/ungranted-handle orchestration với control probes; chưa có CPU quota proof, parser crash corpus hoặc sandbox acceptance. Untrusted plugins/converters vẫn không được bật. Native artifacts ở out/spikes/c-direct trong lane checkout.
