# Kiểm chứng pick, fill/stroke và màu nguồn — 2026-10-15

Code được kiểm: `e977cc2` trên nhánh `agent/codex/editor-pick-style-delivery-20261015`.
Base UI: `ed5bd9c4`; đã tích hợp các sửa document/vector/storage/rendering cần thiết.
Các commit tài liệu/evidence sau đó không đổi source C++ đã đo; hash source ở
[source-sha256.json](source-sha256.json).

## Kết quả chức năng

- Pick theo hình học, tolerance theo pixel; không chọn nhầm phần bbox trống của triangle,
  vùng lỗ EvenOdd hoặc stroke bị scale không đều.
- Window trái → phải chứa trọn đối tượng/viền; Crossing phải → trái giao với fill/stroke.
  Shift thêm, Ctrl đảo chọn. Selection không tăng revision.
- Tám tay nắm resize, anchor đối diện, Shift giữ tỉ lệ; move/resize preview không ghi source.
  Thả chuột commit một undo unit; Escape, modal, capture và stale revision được hủy.
- Palette và hộp nhập native sửa source RGB/CMYK/opacity, fill/stroke flags và viền mm.
  Áp nhiều đối tượng nguyên tử; Undo/Redo và overprint riêng của mỗi object được kiểm.
- Native v2 save/reopen giữ kênh CMYK và thuộc tính; tạo shape mới giữ kiểu nguồn mặc định.
- ICC thật: Windows sRGB và SWOP cài sẵn được nạp; file sai/slot sai bị từ chối.
  CMYK→sRGB khác công thức gần đúng tổng RGB `0.475456` trong fixture, giữ source và alpha.
  Consumer kiểm pixel renderer, không chỉ kiểm hàm trả success.
- Dark/light, palette và trạng thái selection đã xem trên ảnh từ code paint của app.

## Lệnh và evidence

MSVC 19.44.35229, C++20, Windows SDK 10.0.19041.0, Debug/Release.
Chạy từ VS Developer Shell x64; CMAKE_CXX_FLAGS là
`/DWIN32 /D_WINDOWS /EHsc /utf-8`. Không bỏ các cờ MSVC mặc định khi thêm UTF-8.

| Kiểm tra | Kết quả | Log |
| --- | --- | --- |
| doctor | ok=true, 15 modules | chạy trên task worktree |
| harness unittest | 27/27 | cũng được chạy trong CTest |
| architecture unittest | 8/8 | [native-gates.txt](native-gates.txt) |
| contract/reference | 7/7 contracts, fixtures/oracles pass | native-gates.txt |
| Debug build + CTest | **33/33 pass** | [ctest-debug.txt](ctest-debug.txt) |
| Release consumer CTest | **5/5 pass** | [ctest-release.txt](ctest-release.txt) |
| Native mouse/dialog/source/reopen | pass; kiểm tra vẫn bật ở Release | [native-input.txt](native-input.txt) |
| ICC thực tế | pass, gồm SWOP→sRGB | [icc-preview.txt](icc-preview.txt) |
| LOC manual | 92 source/CMake files, không vượt cap | [source-size.json](source-size.json) |

Release chọn đúng 5 suites: ICC, storage fidelity, rendering pixels, editor selection và
editor tools. Không tính các test legacy chỉ dùng assert bị tắt bởi NDEBUG là evidence Release.

Runner `check.ps1 -Suite all -Preset win-msvc-debug` pass khi cwd là
`build/win-msvc-debug/platform/workers`: worker fixture tìm executable bằng cwd.
Lần chạy từ root đã fail vì chỉ tìm `build/win-dev`; giữ log lỗi ở
`build/editor-tools-native-gates.log`, không sửa gate để bỏ lỗi. CTest dùng đúng cwd và pass.
Log worker ghi containment **NOT_MEASURED**, không biến lifecycle test thành OS sandbox claim.
Các log tracked chỉ thay prefix worktree bằng `<worktree>`; raw logs nằm trong build.

## Benchmark CPU trên máy hiện tại

i7-3537U 2 core/4 logical, RAM thực 8.452.857.856 byte (máy danh nghĩa 8 GB).
Renderer software, không dùng Intel HD 4000/AMD GPU. Mỗi run mới có 3 warmup;
corpus 1.000 path, 101.000 path elements. Raster source 800×600; native paint dùng cửa sổ
1280×800 được yêu cầu, lấy client size thật. Ba run nối tiếp, không chạy build đồng thời.

| CPU p95, ms | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: |
| Source redraw | 41.640 | 37.743 | 36.894 |
| Point pick | 6.945 | 5.484 | 5.270 |
| Window query | 8.941 | 5.610 | 5.621 |
| Crossing query | 11.420 | 6.631 | 6.577 |
| Cached native CPU paint | 12.156 | 14.329 | 12.291 |

Raw samples tổng hợp tại [benchmark-release.json](benchmark-release.json).
Đây là wall time của CPU, native window ẩn. Chưa đo present intervals, input-to-visible,
p99 theo trace 60 giây, peak RAM, Intel/AMD/RTX backend hoặc power/driver matrix.
**Source redraw không đạt budget 16,7 ms hoặc 33,3 ms p95 trong trace này.**
Không gọi cached paint là FPS hiển thị hoặc chứng nhận 60 FPS pan/zoom.

## Tổ chức code và giới hạn

File map ở README desktop: selection input/overlay, style presenter/command routing,
numeric dialog và ICC configuration được tách theo feature; keyboard có file riêng.
Các file mới nhỏ hơn 300 dòng không rỗng. File legacy lớn nhất trong diff dưới 500.
Đếm LOC ở đây là manual report, chưa có protected checker được thực thi tự động.
Paint inspector giữ layout liền mạch để đối chiếu vị trí nút; integration test giữ trace
input→source→undo→save liền mạch. Đây là lý do review giữ các hàm UI/test dài hơn 60 dòng.

Group hierarchy và text bounds/render backend chưa đầy đủ. ICC chỉ là preview của máy;
chưa nhúng/gán ICC vào document, soft proof, gamut warning, BPC hay PDF/X. Native v1 lossy
bị từ chối theo ADR 0005. Region/style chưa được chứng nhận cho 100.000 object riêng lẻ.

## Ảnh, build và tích hợp

Ảnh native cuối nằm trong `build/editor-evidence-final/`: pick-style-dark.png,
pick-style-light.png, window-selection.png, crossing-selection.png.
Executable: `build/win-msvc-release/apps/desktop/pikdraw.exe`.
[artifact-sha256.json](artifact-sha256.json) ghi hash ảnh/binary; chúng không được đưa vào Git.

Main vẫn ở `ed5bd9c4` và có thay đổi UI chưa commit từ phiên khác, trùng nhiều file.
Không reset, stash, ghi đè hoặc tự commit phần đó; không đọc/đẩy thư mục cdr chưa tracked.
Delivery là nhánh riêng đã kiểm thử; chưa đánh dấu task integrated/done hoặc M1 complete.
Bước tiếp: coordinator kiểm kê/commit phần main sau khi phiên đó dừng, hợp nhất có review,
rồi chạy lại consumer input/ICC/native reopen trên revision kết hợp.
