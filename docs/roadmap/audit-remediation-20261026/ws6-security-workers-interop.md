# WS6 — Bảo mật worker và importer

Owned paths: `platform/workers/**`, `modules/interop/src/adapters/**`, `modules/production/src/measurement/**`.

Đọc trước: [C6 Workers](../../../contracts/c6-workers.md), [C8 Import fidelity](../../../contracts/c8-import-fidelity.md),
[vector interoperability](../../architecture/vector-resource-interoperability.md).

Nguyên tắc: output importer là dữ liệu chưa tin cậy — kiểm schema, kích thước, số lượng trước khi áp dụng;
mọi quota phải là hằng số có tên, có test vượt quota.

Test chung: `test_pikdraw_workers`, `test_pikdraw_workers_policy`, `test_pikdraw_worker_sandbox`,
`test_pikdraw_workers_live_containment`, `test_pikdraw_worker_v2_integrity`, `test_pikdraw_desktop_import`,
`test_pikdraw_worker_svg_import`, `test_pikdraw_worker_dxf_import`, `test_pikdraw_worker_raster_import`.

---

## WS6-01 [Critical] [x] Quota CDR bridge bị nâng lên `UINT32_MAX`

- Bằng chứng (đã xác minh): `modules/interop/src/adapters/cdr_libcdr_bridge.cpp:25-29` năm hằng số là `UINT32_MAX`;
  thông điệp `:573` vẫn ghi "bounded 320 MiB".
- Cách sửa:
  1. Tra `git log -p -- cdr_libcdr_bridge.cpp` để biết giá trị cũ và lý do nâng (có thể do file CDR thật vượt 320 MiB).
  2. Đặt lại quota hữu hạn có căn cứ (ví dụ raw SVG 512 MiB, ảnh unique 1 GiB, instances 1.000.000) **và** kiểm
     theo memory headroom động (dùng waiter/pressure sẵn có). Nếu cần file lớn hơn, làm streaming chứ không bỏ quota.
  3. Sửa thông điệp lỗi theo hằng số thực (format từ giá trị).
- Test: fixture tổng hợp vượt từng quota → lỗi có mã rõ, không cấp phát lớn; CDR mẫu hợp lệ trong `cdr/` (chỉ đọc) vẫn import.
- Hỏi người dùng nếu giá trị cũ khiến file CDR thực tế bị từ chối.

## WS6-02 [Critical] [x] ZIP trong CDR không giới hạn tổng byte giải nén

- Bằng chứng (cần xác minh): `ZipInput::Extract` (`cdr_libcdr_bridge.cpp:204-246`) chỉ chặn tràn 64-bit;
  duplicate-name scan O(N²) (`:165-167`, tối đa 65535 entry).
- Cách sửa: tổng `expanded_bytes_` có trần (ví dụ 2 GiB hoặc theo headroom); tỷ lệ nén tối đa mỗi entry (chống ZIP bomb,
  ví dụ >200:1 với entry > 1 MiB bị từ chối); extract lặp cùng stream dùng cache, không cộng lặp vô hạn; duplicate check
  bằng `unordered_set` (chuẩn hóa case).
- Test: ZIP bomb tổng hợp (sinh trong test, ghi vào `PIKDRAW_TEST_OUTPUT_DIR`) bị từ chối nhanh; 65535 entry không O(N²).

## WS6-03 [High] [x] AppContainer để lại ACE mồ côi trên NTFS và WindowStation/Desktop

- Bằng chứng (đã xác minh một phần): `platform/workers/src/appcontainer.cpp:30-40,96` cấp quyền session;
  `Cleanup()` (`:105-111`) chỉ `DeleteAppContainerProfile`. Cần xác minh `process_launcher.cpp:146-162` cấp quyền
  từng file trong `bin_path` mỗi lần launch.
- Cách sửa:
  1. Dùng một AppContainer profile **ổn định** (tên cố định theo app/version) thay vì SID ngẫu nhiên mỗi lần → ACE chỉ thêm một lần, idempotent.
  2. Hoặc nếu giữ SID theo lần chạy: ghi danh sách đối tượng đã cấp quyền và gỡ ACE trong `Cleanup()` (đọc DACL, xóa ACE
     theo SID, `SetNamedSecurityInfoW`/`SetSecurityInfo`).
  3. Cấp quyền đọc/thực thi ở mức thư mục với inheritance thay vì từng file; không cấp quyền thư mục build rộng hơn cần.
  4. Thêm công cụ dọn một lần cho máy dev đã tích lũy ACE (script trong `tooling/` có dry-run, chỉ xóa ACE dạng
     `S-1-15-2-*` của profile PikDraw).
- Test: `test_pikdraw_worker_sandbox` chạy 20 lần → số ACE trên `bin_path` và desktop không tăng sau lần đầu.

## WS6-04 [High] [x] `LoadLibraryW` fallback mở lại search order không an toàn

- Bằng chứng (đã xác minh): `pdf_worker_adapter.cpp:165`; `raster_image_decoder.cpp:198` (`windowscodecs.dll`),
  `:224` (`ole32.dll`), `:233` (`propsys.dll`).
- Cách sửa: bỏ fallback trần; DLL hệ thống dùng `LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`;
  pdfium dùng đường dẫn tuyệt đối đã xác minh + `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`.
  Cân nhắc `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` khi khởi động process (desktop và worker).
- Test: smoke import PDF/TIFF; grep không còn `LoadLibraryW(` trong `modules/` và `platform/`.

## WS6-05 [High] [ ] Parser chạy in-process thay vì worker cô lập

- Bằng chứng (cần xác minh): `platform/workers/src/worker_main.cpp:425-440` trả "no parser adapter is implemented"
  cho `ImportRequest`; các adapter trong `modules/interop` chạy trong `pikdraw.exe`. Đối chiếu với tên test
  `test_pikdraw_worker_svg_import`/`dxf`/`raster` — có thể một số format đã đi qua worker; xác định chính xác format nào
  chạy ở đâu và ghi ma trận vào C6.
- Cách sửa (theo pha):
  1. Pha 1: lập ma trận format → process (in-process / worker / external converter) trong C6 và README workers.
  2. Pha 2: chuyển CDR (libcdr) và PDF (PDFium) — hai parser rủi ro nhất — sang worker V2 với quota/timeout/Job Object;
     host chỉ nhận proposal đã kiểm.
  3. Không tuyên bố containment filesystem/network khi chưa có test live containment pass.
- Test: `test_pikdraw_workers_live_containment`; crash giả lập trong worker không làm sập desktop, import báo lỗi.

## WS6-06 [Medium] [x] Tìm tên OCG PDF bằng quét chuỗi thô

- Bằng chứng (cần xác minh): `pdf_worker_adapter.cpp:597-655` quét `/Type /OCG /Name` trên toàn bytes.
- Cách sửa: dùng API PDFium (`FPDF_*` optional content nếu có trong bản pin) hoặc parser object có giới hạn; nếu không
  có API, đánh dấu tên layer là "best-effort" trong fidelity report và giới hạn thời gian/kích thước quét.
- Test: PDF có OCG trong object stream (`/ObjStm`) và tên UTF-16BE → tên đúng hoặc báo best-effort, không đoán sai.

## WS6-07 [Medium] [ ] Measurement parser cấp phát trước khi kiểm giới hạn

- Bằng chứng (cần xác minh): `measurement_import.cpp:369,473-496`; `kMaxMeasurementSamples = 4096`.
- Cách sửa: `parse_SpectralMeasurementText` kiểm độ dài input và đếm dòng dạng streaming trước khi tách/copy; từ chối
  sớm khi vượt số sample/byte.
- Test: `test_pikdraw_production_measurement` — input 64 MiB một dòng / 10⁶ dòng bị từ chối nhanh, RSS không tăng theo input.

## WS6-08 [Low] [ ] Rà soát hằng số quota toàn bộ importer

- Cách sửa: liệt kê mọi quota trong `modules/interop/src/adapters` và `platform/workers` vào một bảng trong C8/C6
  (tên, giá trị, lý do, test). Hằng số không có test → thêm test vượt quota.
