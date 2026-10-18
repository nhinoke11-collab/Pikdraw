# Sửa nhập CDR, AI và ảnh lớn

Phạm vi: sửa adapter hiện có, giữ chuẩn [UI](../ui-ux-standard.md) và
[workspace](../../architecture/workspace-layout.md), đối chiếu C8/C9.

## Thao tác và UI

- File > Open (`action.file.open`, Ctrl+O) và File > Import (`action.file.import`)
  dùng chung worker/proposal hiện có. Không thêm menu, toolbar, toolbox, panel,
  icon SVG, property bar hoặc field Properties vì không có tham số mới.
- Giữ dialog chọn file, dialog xác nhận chuyển đổi và status tiến trình/Esc hiện có.
  Tooltip, focus, enable state và icon dùng lại action cũ.
- Open dùng tab riêng khi cần; Import áp vào tài liệu đích bằng một Undo unit.
  Busy không mở tác vụ thứ hai; cancel/stale không commit; lỗi không tạo đối tượng.
- Help: Ctrl+O → chọn CDR/AI → xem báo cáo → chấp nhận ảnh xem trước hoặc hủy.
  Đây là ảnh tham khảo, không phải các đường vector/chữ chỉnh sửa được.

## Adapter và ma trận

| Nguồn | Biểu diễn | Kết quả và giới hạn |
| --- | --- | --- |
| CDR ZIP hiện có | PNG preview do Corel lưu | Dùng adapter CDR riêng; chỉ xem trước, không xác nhận mọi phiên bản CDR |
| AI PDF-compatible có XMP thumbnail | JPEG/PNG thumbnail nguồn | Giữ ảnh thật; không thay bằng ảnh trắng; không đọc AI authoring |
| PDF/AI không có thumbnail hợp lệ | Chưa có renderer PDF tích hợp | Từ chối rõ ràng; ảnh XObject rời không đại diện toàn trang |
| JPEG/PNG/TIFF | RGBA8, tối đa 8 Mi pixels/32 MiB | Mở ảnh mẫu 5–6 MP không giảm kích thước; giữ byte gốc với raster |
| SVG/DXF | Parser/profile hiện tại | Không thay semantics hoặc capability |
| EPS/AI PostScript | Ngoài scope | Không thêm interpreter |

Sau normalization dùng chung ImageObject, command, Undo/Redo và renderer.
Coverage/appearance partial, explicit conversion, production not_assessed;
không công bố vector editability, fidelity in ấn hoặc export round-trip.
CDR/AI preview không được xem là đã giữ nguồn authoring trong native save.
Không thêm dependency, network hoặc quyền sandbox. Giới hạn decode, record,
stream và host được đổi đồng bộ; nguồn vẫn tối đa 64 MiB và worker 256 MiB.

## Kiểm chứng

Mở rộng test hiện có: tìm corpus từ CTest working directory, kiểm pixel thumbnail
AI thật, CDR/AI qua worker và commit/Undo, raster lớn hơn 4 Mi pixels, và từ chối
PDF thiếu thumbnail thay vì báo thành công với ảnh trắng. Corpus local không
đóng gói lại hoặc thêm vào Git. Build/test qua tooling/native/build.ps1.
Kết quả runtime và giới hạn sẽ ghi theo đúng bằng chứng sau khi chạy.

Kết quả đã có: desktop workflow pass với 4 CDR, 1 AI và 4 JPEG local, gồm hai
ảnh hơn 4 Mi pixels; xác nhận commit/Undo và pixel thumbnail AI 256×180.
Sửa stream digest vượt 1 MiB và staging worker để build tăng dần không dùng
worker cũ. Kiểm tra cửa sổ trực tiếp chưa hoàn tất vì helper capture timeout;
không suy ra chất lượng hiển thị hay vector fidelity từ kết quả workflow.
