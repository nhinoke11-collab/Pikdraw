# Kế hoạch import ảnh lớn, tiết kiệm RAM và nền tảng sửa ảnh

Ngày rà soát: 21/10/2026. Trạng thái: **plan để agent khác triển khai; chưa phải capability đã nghiệm thu**.

## 1. Quyết định sản phẩm

Cho phép đặt và chỉnh sửa ảnh lớn theo khả năng thực của codec, hệ thống lưu trữ và thiết bị. **Không dùng dung lượng file nguồn làm ngưỡng loại bỏ tùy ý.** Quota 128 MiB nguồn, 192 MiB toàn ảnh, worker 640 MiB và yêu cầu trống 768 MiB xuất hiện trong code thử nghiệm của phiên trước **không phải yêu cầu sản phẩm được chấp nhận**. Cập nhật 22/10: quota byte cố định 128/192/512/640/768 MiB đã được gỡ khỏi import raster/CDR và host staging; import worker/host tạm chờ ở ngưỡng sử dụng RAM vật lý 95%, resume khi headroom đủ và timeout cộng dồn sau 5 phút. Pipeline vẫn giữ full-frame, chưa có lifetime budget cho model/Undo hoặc asset/tile path nên AC ảnh lớn chưa đóng. Không xóa resource accounting hoặc gọi việc bỏ một con số là tối ưu.

Thiết kế đích gồm: nguồn ảnh bất biến nằm trên đĩa hoặc liên kết ngoài, descriptor nhẹ trong document, metadata/preview xuất hiện sớm, decode theo vùng/mức chi tiết, cache tile có ngân sách chung, sửa ảnh bằng graph tham số, lưu/xuất theo stream. File 1 GiB không kéo theo một hay nhiều buffer nguồn 1 GiB trong host/worker. Ảnh giải nén nhiều GiB vẫn có thể làm việc bằng các tile đang cần.

Illustrator và CorelDRAW là tham chiếu cho **workflow đã được tài liệu hóa**: link/embed, quản lý nguồn, cập nhật nguồn và sửa không phá hủy. Không có bằng chứng để khẳng định thuật toán cache/decode nội bộ của hai hãng giống kiến trúc đề xuất ở đây. Nguồn chính thức và ranh giới suy luận ở [audit](raster-import-large-assets-20261021/01-audit-and-references.md).

## 2. Đọc và thực thi theo thứ tự

| Tài liệu | Nội dung và người tiêu thụ |
| --- | --- |
| [01 — Audit và tham chiếu](raster-import-large-assets-20261021/01-audit-and-references.md) | Điểm tốn RAM hiện tại; phần thử nghiệm cần thay; nguồn Adobe/Corel/Microsoft/codec; ma trận định dạng |
| [02 — Kiến trúc và hợp đồng](raster-import-large-assets-20261021/02-asset-storage-decode-edit.md) | Asset, nguồn 64 bit, persistence, codec, tile, edit graph, cache, scheduler và dependency |
| [03 — Các giai đoạn và UI](raster-import-large-assets-20261021/03-implementation-and-ui.md) | Phạm vi file, dependency giữa bước, thao tác UI và deliverable để agent triển khai |
| [04 — Nghiệm thu](raster-import-large-assets-20261021/04-acceptance-and-performance.md) | Corpus lớn, AC-RI-01…AC-RI-24, cách đo RAM/thời gian, lệnh test và điều kiện kết thúc |

Đọc thêm `AGENTS.md`, `registry/modules.json`, scope AGENTS của module chạm tới, `docs/START.md`, C3/C5/C6/C8/C9, `docs/architecture/vector-resource-interoperability.md`, `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md` và `docs/quality/feature-acceptance.md`.

## 3. Phạm vi bắt buộc

1. Import trực tiếp JPG/JPEG, PNG, TIFF/BigTIFF; giữ BMP hoạt động qua adapter. TIFF một trang đi thẳng; nhiều trang có chọn trang/lazy thumbnails, không áp trần 256 trang lên toàn file.
2. Hai cách giữ nguồn: **Embedded** được quản lý trên đĩa, tự chứa khi lưu; **Linked** tham chiếu ngoài, có Missing/Modified/Relink/Update. Mặc định Embedded để giữ kỳ vọng hiện tại; lựa chọn được ghi nhớ và hiển thị rõ. Cả hai đều dùng descriptor/tile, không giữ toàn nguồn trong vector RAM.
3. Import là một bước Undo. Asset có stable ID và phiên bản bất biến; transform/duplicate/crop không sao chép toàn ảnh. Có save/reopen/recovery thật và nguồn 64 bit vượt 4 GiB.
4. D2D hardware/WARP và CPU fallback đều đọc tile/LOD qua cùng giao diện. Không upload toàn ảnh lớn thành một GPU texture, không bắt CPU dựng toàn ảnh để fallback.
5. Lát cắt sửa ảnh thật: crop không phá hủy, flip/rotate và một điều chỉnh Exposure có schema/ngữ nghĩa đã kiểm chứng. Properties và property bar dùng chung command; sửa lại node cũ giữ ID; Undo/Redo, save/reopen và export thấy cùng kết quả.
6. Export ảnh đã sửa tại độ phân giải yêu cầu từ nguồn/graph bằng strip/tile. Đường xuất hiện tại nhận toàn bộ `span` pixel phải được nối sang provider streaming. Không dùng ảnh xem trước làm bản xuất mặc định.
7. Raster nhúng trong CDR/PDF/AI và tài nguyên ảnh SVG được nối vào cùng asset model bằng adapter riêng theo coverage hiện hành; giữ geometry/layer/effect của vector. Không rasterize toàn file vector để đạt tốc độ import.

Ngoài đợt bắt buộc: bộ brush/heal/clone hoàn chỉnh, mọi filter ảnh, AI background removal, PSD/PSB layers, RAW camera và print separations. Kiến trúc phải có điểm mở rộng cho chúng; không công bố capability hoặc nút hoạt động giả trước khi có implementation.

## 4. Nguyên tắc xuyên suốt

- Giữ original encoded bytes hoặc liên kết/version nguồn; proxy là cache dựng lại được. Preview xuống mẫu không thay kích thước pixel, DPI, bit depth, ICC hoặc dữ liệu authoritative của nguồn.
- Tách thời gian metadata/đặt được ảnh, thời gian có preview hữu ích, thời gian sao chép/hash nguồn và thời gian chuẩn bị chất lượng đầy đủ. Nhúng file nhiều GiB vẫn cần I/O tương ứng; không hứa thao tác nhúng hoàn tất tức thì.
- Giới hạn tài nguyên áp vào **công việc đang thực hiện**: RAM cache, scratch, số job, tile đang bay, metadata window, thời gian/tiến độ và dung lượng đĩa. Khi thiếu RAM: hạ LOD/concurrency, evict, spill và pause/resume. Import pause ở ngưỡng RAM vật lý 95%; không kết luận file sai chỉ vì nguồn lớn hoặc bộ nhớ hệ thống đang chờ ứng dụng khác nhả RAM.
- Không phải mọi codec đều random access. PNG/strip TIFF có thể cần quét tuần tự; progressive JPEG có bộ nhớ coefficient riêng. Những trường hợp này có đường xử lý ngoài RAM và gate riêng; chia `CopyPixels` thành dải không tự chứng minh RAM có giới hạn.
- Document commands và kết quả async giữ stable IDs, epoch/revision, asset revision và generation. Kết quả cũ không được thay nguồn, graph hoặc canvas hiện tại.
- Tách xem/render, chỉnh sửa, metadata, giữ nguồn, export và production theo C8/C9. Native tự chứa không đồng nghĩa round-trip TIFF/AI/CDR hay in đúng màu tuyệt đối.

## 5. Điểm kết thúc và quy tắc làm việc

Thực hiện P0→P7 trong tài liệu 03; P8 là phần mở rộng sau foundation. Không kết thúc ở việc xóa quota, linked-only, proxy-only, unit test hoặc build pass. Kết thúc đợt bắt buộc khi AC-RI-01…24 đạt hoặc có giới hạn codec/đầu ra được chứng minh, ghi đúng phạm vi và không thay bằng file dễ hơn để né yêu cầu 1 GiB/BigTIFF.

Làm tại `C:/Users/CK ART STAGE/Downloads/PikDraw`, một source gốc và executable `build/win-dev/apps/desktop/pikdraw.exe`. Đường Administrator trong hướng dẫn cũ không phải checkout dùng ở phiên này. Không tạo worktree/clone/branch hay task lease/harness hành chính. Giữ toàn bộ local edits; trong phiên audit đã có nhiều thay đổi ngoài scope và một số file liên quan cũng dirty. Trước sửa phải đọc diff từng file; không reset/stash hoặc ghi đè hàng loạt. Không append chức năng vào file quá 2.000 dòng.

Plan này là tài liệu chính thức cho phương án mới. Cập nhật 22/10: đã gỡ cap byte cố định raster 128/192 MiB, worker 640 MiB, preflight 768 MiB, host staging 512 MiB và CDR decode 512 MiB. Host/raster import admission chờ dưới guard RAM 95%, resume khi có headroom; allowance chờ cộng dồn tối đa 5 phút. Raster record còn trần biểu diễn 32-bit. Full-frame assembly, storage/tile, persistence và renderer vẫn chưa đáp ứng AC-RI-01…24. Chỉ cập nhật `docs/project-state.json` sau gate thực tế, giữ dưới 8.000 byte và không append lịch sử.

## 6. Prompt giao cho agent triển khai

> Triển khai trọn phần bắt buộc trong `docs/roadmap/raster-import-large-assets-20261021.md` và bốn tài liệu con. Đọc audit, hợp đồng, scope AGENTS và diff hiện tại trước khi sửa. Thay pipeline full-file/full-frame bằng disk-backed assets, stream codec, tile/LOD và edit graph có save/reopen/export. Không dùng quota file nguồn hay preview-only để đạt mục tiêu. Làm tuần tự P0–P7, giữ AC-RI-01…24, mở rộng test hiện có theo rủi ro và chạy build native đúng target. Báo kết quả thực tế, benchmark file ≥1 GiB và BigTIFF offsets >4 GiB; ghi rõ phần chưa xác minh. Không tạo worktree, không reset local edits, không cần chờ duyệt lựa chọn triển khai thường lệ trong scope.
