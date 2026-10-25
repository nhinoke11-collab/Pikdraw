# Nhập Measurement Run

Trạng thái: nhập/lưu và so sánh hai Measurement Run có báo cáo CSV. So sánh dùng ngưỡng do người vận hành nhập; không xác nhận contract proof.

## UI và hành vi

- Menu: File > Import Measurement Run… — action ID action.file.import_measurement_run.
- Menu: File > Compare Measurement Runs… — action ID action.file.compare_measurement_runs.
- Nhãn tiếng Việt: Nhập số đo màu…; tiếng Anh: Import Measurement Run….
- Luồng: chọn CGATS/CSV → kiểm tra schema và điều kiện → chọn đường dẫn .pdmr → ghi artifact có checksum → hiện số patch, route, instrument, mode, chart hash rút gọn và trạng thái Chưa đánh giá.
- Input nhận CGATS/CSV subset đã tài liệu hóa; giới hạn tệp 64 MiB, 250.000 patch. Invalid metadata, duplicate/missing ID, Lab ngoài miền, file bị truncate hoặc phát sinh thêm bytes trong khi đọc đều bị từ chối.
- .pdmr schema 1 lưu điều kiện đo, Lab/XYZ và patch IDs; checksum phát hiện hỏng. Ghi vào file tạm cùng thư mục rồi replace đích. Hủy dialog trước khi lưu không tạo artifact.
- So sánh chọn measured run và reference run; bắt buộc cùng chart/source/output-profile hash, route, instrument, illuminant, observer, mode và backing. Match bằng sample ID, báo patch thiếu/thừa và ΔE00 mean/P95/max. Nhập đủ ba giới hạn mean/P95/max để đánh giá; để trống cả ba chỉ lưu số liệu và hiện NOT_ASSESSED. CSV được ghi atomic, giới hạn 128 MiB, chống công thức bảng tính từ patch ID và chứa metadata, giới hạn cùng kết quả từng patch.
- Báo cáo so sánh run theo điều kiện khai báo; không phải chứng nhận contract proof hoặc xác nhận độ đúng của thiết bị đo.
- Không sửa document, selection, source pixels, lịch sử Undo/Redo hay color profile. Artifact run tách khỏi .pr.

## Các bề mặt khác

- Toolbox và standard toolbar: không áp dụng; nhập số đo là lệnh quản lý artifact, không phải công cụ vẽ.
- Property bar, Properties và field scopes: không áp dụng; chưa có run đang mở trong document hoặc đối tượng được chọn.
- SVG icon: không áp dụng cho menu File hiện tại; dùng biểu tượng menu chuẩn của shell.
- Tooltip/focus help: “Import CGATS/CSV measurements, validate chart and measurement conditions, then save a checksummed .pdmr run.” Compare help: “Compare two saved runs by patch ID, verify their declared print conditions, and export a CSV report; optional Delta E limits are supplied by the operator.”
- Status hint sau nhập: Đã nhập và lưu Measurement Run · chưa đánh giá dung sai. Sau so sánh: trạng thái đánh giá thực hoặc “pass/fail not assessed”.
- Help: run được chấm điểm chỉ khi có chart/reference patches, cùng chart/source/output-profile hash và trùng route/instrument/illuminant/observer/mode/backing. Ngưỡng mean/p95/max phải do người vận hành chọn theo job; không gắn nhãn chứng nhận tự động.

## Acceptance lát cắt

- Parse bounded CGATS/CSV, match patch theo ID, lưu/load .pdmr, giữ XYZ tùy chọn, checksum lỗi bị từ chối.
- So sánh dịch vụ có thể trả complete riêng với assessed/passed; không cung cấp tolerance thì không tạo kết quả pass/fail.
- UI menu → file pickers → loader → comparison/threshold dialog → CSV report cần xác nhận bằng native runtime. Đo màu vật lý, thư viện chart chuẩn, hard proof hai thiết bị, direct printer/RIP và qualification tại xưởng vẫn chưa được xác nhận.
