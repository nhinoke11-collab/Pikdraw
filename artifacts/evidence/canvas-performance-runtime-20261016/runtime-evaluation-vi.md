# Đánh giá runtime PikDraw trên build hiện có — 2026-10-16

## Phạm vi và máy thử

Đã chạy trực tiếp `build/win-dev/apps/desktop/pikdraw.exe`; không rebuild. SHA-256: `DEA40828BAEA04F587E190E705EDF2D6482AAB4AD8B129BDEA05DE8A3216D416`. Executable 46,116,352 byte, ghi lúc 2026-10-16 21:03:45 +07:00. CMake cache của `build/win-dev` ghi `Debug`.

Máy thử: Windows 10 Pro 22H2 (build 19045), Dell Inspiron 3521, Intel Core i7-3537U (2 nhân/4 luồng), RAM 8.45 GiB, Intel HD Graphics 4000 và AMD Radeon HD 8500M/8700M. Cảnh chụp 1440 × 900 px.

## Đo đường dựng canvas và thao tác demo

Chạy 5 lần mỗi cảnh, tổng 20 lượt. Mỗi tiến trình gọi chính executable với `--screenshot`; các cảnh gồm canvas trống, hai shape, Weld và demo vector/bitmap. Lệnh chụp gọi `NativeWindow::paint()` vào memory DC, sau đó ghi BMP 3,888,054 byte. Thời gian dưới đây là từ tạo tiến trình đến lúc kết thúc, gồm khởi tạo app, dựng cảnh/thao tác demo, một lượt paint và ghi ảnh; không phải thời gian riêng của hàm render hay thời gian input-to-visible.

| Cảnh | Median tổng lượt | P95 nearest-rank (n=5; bằng max của 5 lượt) | CPU median | Peak working set | Peak private bytes | Exit lỗi |
|---|---:|---:|---:|---:|---:|---:|
| Trống | 337.93 ms | 375.09 ms | 312.50 ms | 18.96 MiB | 12.07 MiB | 0/5 |
| Hai shape | 381.70 ms | 412.46 ms | 359.38 ms | 19.09 MiB | 12.14 MiB | 0/5 |
| Boolean Weld | 352.11 ms | 365.22 ms | 328.12 ms | 19.16 MiB | 12.45 MiB | 0/5 |
| Vector/bitmap demo | 348.85 ms | 381.40 ms | 343.75 ms | 19.44 MiB | 12.33 MiB | 0/5 |

P95 ở đây chỉ là giá trị lớn nhất trong 5 lượt, chưa đủ số mẫu để gọi là percentile ổn định. Bộ đếm process I/O cho mỗi ảnh ghi 3,888,425 byte và đọc 60 byte; đây là file I/O, không phải lưu lượng đọc/ghi bus RAM. Peak working set ở trên được lấy khi tiến trình còn chạy; số đo sau khi process thoát không được dùng.

## Cửa sổ native và tài nguyên lúc nghỉ

Chạy cửa sổ thật 18 giây với `--demo-objects --timeout 18`. Windows handle của cửa sổ được thấy sau khoảng 2.86 giây kể từ lúc tạo process; process thoát bình thường (exit 0). Trong các mẫu sau khởi động, RAM ổn định quanh 17.31 MiB working set và 7.26 MiB private; CPU về 0% trong 7 mẫu cuối. Không có chuột/bàn phím hoặc hoạt ảnh được tạo trong lượt này.

PerfMon `GPU Engine(*)` không trả engine instance khớp PID PikDraw trong lượt cửa sổ. Điều này phù hợp với renderer hiện tại dùng software CPU/GDI; không chứng minh GPU toàn hệ thống đã dùng 0%. Chưa có số đo memory bandwidth CPU/GPU.

Ảnh `canvas-vector-bitmap.bmp` cho thấy cảnh demo có dòng cảnh báo “Some objects could not be displayed. Source data is preserved.” Vì vậy cảnh đó không chứng minh bitmap đã render đầy đủ.

## Kết luận và giới hạn

- Build hiện tại chạy được các cảnh kiểm tra và thoát sạch. Peak working set khoảng 19.4 MiB trên các cảnh chụp này; cửa sổ idle dùng khoảng 17.3 MiB.
- Mỗi lượt chụp đầy đủ mất median khoảng 0.34–0.38 giây và tiêu thụ 0.30–0.36 giây CPU. Do phần lớn thời gian ngắn này nằm trên CPU, đường dựng/chụp hiện không có bằng chứng GPU acceleration.
- **FPS/present thực tế và độ trễ từ thao tác chuột đến pixel hiển thị chưa đo được.** Chế độ screenshot gọi `paint()` trực tiếp, không chạy HWND/message-present loop. Cửa sổ native chỉ được lấy mẫu lúc nghỉ; môi trường điều khiển native không khả dụng trong phiên này. Không quy đổi thời gian process/screenshot thành FPS.
- **Độ trễ riêng của Weld, drag, pan/zoom hoặc gọi lệnh chưa tách được.** Thời gian tổng có cả khởi động, dựng cảnh, paint và ghi BMP. Đây là Debug build nên các số đo không đại diện Release.
- Số liệu đọc/ghi ở đây là file I/O từ process counter; muốn đánh giá throughput RAM cần hardware performance counters. Muốn nghiệm thu FPS/input latency cần chạy cửa sổ với cảnh nhiều đối tượng thực tế, tạo tương tác pan/zoom/drag và ghi present/input timestamps.

## Tệp bằng chứng

- `environment.json`: định danh binary, cấu hình build và phần cứng.
- `screenshot-runs.csv`, `screenshot-summary.csv`: 20 lượt chụp và tổng hợp.
- `visible-idle-samples.csv`, `visible-idle-summary.json`: mẫu tài nguyên cửa sổ native.
- `canvas-*.bmp`: ảnh được executable tạo ở từng cảnh.
