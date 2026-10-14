# Quality gates của PikDraw

Trạng thái: **mục tiêu và quy trình đề xuất; chưa có baseline đo từ ứng dụng**. Scaffold/harness checks không chứng minh vector engine, tương thích file hay khả năng chạy mượt. Gate chỉ được ghi “pass” khi có artifact và dữ liệu đo tương ứng.

## Mức kiểm tra

| Gate | Bằng chứng cần có |
| --- | --- |
| G0 — thay đổi review được | Task scope, owner, diff, dependency/contract impact, tài liệu mới đúng trạng thái |
| G1 — nền tảng build | Build trên cấu hình hỗ trợ, static checks, dependency graph không vòng, smoke test ứng dụng |
| G2 — đúng chức năng | Test có oracle độc lập/fixture; command undo/redo; document invariants; migration nếu đổi schema |
| G3 — tương thích và độ chính xác | Semantic report, ảnh so sánh, font/color/units checks và phạm vi format/version công bố |
| G4 — ổn định/hiệu năng | Benchmark fixture và máy tham chiếu cố định; cancellation/crash/recovery evidence |
| G5 — phát hành | Review độc lập, license inventory, bản build tái tạo, release notes/known limits, bộ đơn xưởng đã kiểm tra |

G0 có thể thực thi ở scaffold. G1–G5 đang chờ engine, fixture và môi trường chạy. Không tạo test chỉ lặp lại implementation để tăng số lượng. Chọn kiểm tra theo rủi ro thay đổi; broaden khi có thay đổi/failure hoặc vấn đề chưa giải quyết.

## Fidelity gate

Mỗi fixture có nguồn và quyền sử dụng, format/version, hệ đơn vị, profile màu, font cần thiết, nội dung kỳ vọng và ảnh chuẩn. Ma trận ghi riêng import, editability và export. Tài liệu có giới hạn được đánh dấu `partial`, `unsupported` hoặc `research`; không biến một file pass thành cam kết cho cả định dạng.

| Lớp kiểm tra | Oracle cần có |
| --- | --- |
| Hình học | Bounding box, vị trí, kích thước, winding, path closure, cung/spline và tolerance theo tác vụ |
| Đối tượng | Số lượng/loại node, hierarchy, layer, instance, transform, clips/masks |
| Chữ | Nội dung Unicode, shaping/line breaks, font mapping, trạng thái text sống/outline |
| Hình ảnh | Pixel size, orientation, crop/mask, alpha, liên kết/nhúng, profile |
| Màu/in | ICC pipeline, spot/CMYK theo phạm vi hỗ trợ, transparency và output trên RIP tham chiếu |
| Lưu/chỉnh sửa | Nhập → sửa đại diện → lưu native → mở lại; undo/redo và xuất đích |

Pixel diff dùng để tìm thay đổi, không là oracle duy nhất: antialias, font rasterizer và quản lý màu có thể làm ảnh khác dù hình học đúng. Tolerance khởi đầu cho đường sản xuất có thể khảo sát mức 0,01 mm; chỉ chốt sau khi đối chiếu đơn vị, khổ, máy và nhu cầu xưởng. Không dùng một tolerance cho cả CNC lẫn preview.

Lỗi chuyển dạng phải có report: nguồn, đối tượng, lý do, mức mất khả năng sửa và lựa chọn đã dùng. Trường hợp dữ liệu bị mất mà không có cảnh báo chặn release.

## Performance gate

Trước benchmark phải ghi OS/build, CPU, RAM, GPU/driver, màn hình/DPI, power mode, compiler flags và commit. Chọn một máy Windows tham chiếu cùng bộ fixture có checksum; sau đó mới chốt budget. Máy khác có report riêng, không gộp trung bình để che cấu hình chậm.

Các profile khởi đầu:

| Profile đề xuất | Fixture cố định cần xây |
| --- | --- |
| Small | Một trang, khoảng 1.000 đối tượng vector/chữ, ảnh nhỏ |
| Typical | Tám trang, khoảng 10.000 đối tượng, ảnh in lớn, clipping và gradient |
| Stress | Khoảng 100.000 đối tượng, nhiều tài nguyên/layer và tọa độ lớn |

Con số trên là gợi ý xây bộ đo, không phải khả năng sản phẩm đã đạt. Fixture phải ghi cả số segment, glyph, megapixel, asset bytes và loại hiệu ứng vì số đối tượng đơn thuần không phản ánh chi phí.

Mục tiêu khảo sát cho Small/Typical trên máy tham chiếu: pan/zoom gần 60 FPS; frame p95 khoảng 16,7 ms, input-to-visible p95 dưới 50 ms, không có tác vụ parse/encode làm chặn event loop lâu. Stress cần ngân sách riêng và chế độ preview giảm chất lượng công khai. Chưa đưa các con số này thành CI fail threshold trước baseline được duyệt.

Đo cold/warm open, save/export, pan/zoom, node drag, text edit, Boolean/offset, import cancellation, peak RAM, GPU memory và thời gian phục hồi. Báo p50/p95/p99, peak và workload; không chỉ báo FPS trung bình. Memory budget và regression threshold được đặt sau benchmark đầu tiên, qua ADR hoặc baseline artifact có review.

## Stability và phục hồi

- Inject crash vào importer/exporter/plugin: UI sống, tài liệu chưa commit không thay đổi, lỗi có hướng xử lý, worker có thể khởi động lại.
- Hủy ở các bước parse/convert/commit/export: không có output được ghi như thành công khi chỉ có file dở dang.
- Fault injection lúc lưu và autosave: sau mở lại phải có bản trước hoặc bản mới hợp lệ; không nhận file thiếu cấu trúc là tài liệu tốt.
- Kill ứng dụng khi sửa: mở được checkpoint đã commit; công bố cửa sổ thay đổi có thể mất dựa trên policy tự lưu đo được.
- Font/profile/linked asset thiếu: cảnh báo đúng và có lựa chọn sửa; không crash hoặc âm thầm thay đổi nội dung.
- Document lớn và phiên 8 giờ với chuỗi thao tác xác định: theo dõi leak, cache, handle, responsiveness và recovery. Đây là bài nghiệm thu đề xuất, chưa chạy.
- Fixture lỗi: payload vượt giới hạn, số không hữu hạn, nesting sâu, schema lạ, timeout và plugin version mismatch được từ chối có kiểm soát.

Process isolation không đủ chứng minh sandbox an toàn. Plugin untrusted chỉ được bật khi containment và quyền file/mạng đã được kiểm tra; trước đó phải công bố chế độ trusted-only.

## Review và quyết định phát hành

Agent triển khai không tự xác nhận mọi thay đổi của mình. Reviewer đọc diff, contract và bằng chứng đúng revision. Với geometry/format/storage, review cần người/agent có năng lực tương ứng và fixture lỗi; bài test xanh không thay thế đánh giá mất dữ liệu.

Phân loại chặn phát hành: mất dữ liệu, sai đơn vị/đường sản xuất không được báo, crash lặp lại trong luồng chính, migration hỏng hoặc quyền plugin vượt phạm vi. Giới hạn format đã công bố có thể phát hành như `partial` nếu đúng scope và có report; không đổi nhãn thành “full” để vượt gate.

Mỗi release lưu artifact: commit, toolchain/dependency versions, licenses, test/benchmark reports, ma trận format, known limits và recovery policy. Chỉ log metadata cần thiết; nội dung khách hàng, file gốc và đường dẫn nhạy cảm không đưa lên telemetry mặc định.
