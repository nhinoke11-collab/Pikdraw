# Gán ICC RGB cho ảnh raster

Ngày 24/10/2026. Đây là lát cắt P3 cho một ảnh RGB: giữ nguyên mẫu kênh, gán cách diễn giải ICC,
dựng preview canvas bất đồng bộ về system sRGB và lưu thao tác vào lịch sử Undo. UI native chưa được
kiểm bằng chuột; monitor profile, CMYK/Gray, proof/gamut và in vẫn là gate riêng.

## 1. Vị trí và thao tác

| Mục | Quyết định |
| --- | --- |
| Menu chính | `Bitmaps > Assign Bitmap Profile…` / `Bitmaps > Gán profile ảnh…` |
| Action/command | `action.bitmap.assign_profile`; `AssignRasterImageProfileCommand` |
| Chọn tệp | Windows ICC file picker, chỉ nhận resource RGB hợp lệ; Cancel không đổi tài liệu |
| Standard toolbar | Không áp dụng; lệnh ít dùng và nằm trong menu Bitmaps |
| Toolbox / flyout | Không áp dụng; đây không phải công cụ vẽ trên canvas |
| Property bar | Không áp dụng; không có tham số thường dùng để sửa liên tục |
| Properties | Chưa có editor profile ảnh. Status sau khi gán nêu tên profile và số kênh được giữ; Image Info/Properties là phần mở rộng sau |
| Dialog/panel bổ sung | Không áp dụng; native file picker chọn ICC, chọn xong áp một command Undo |
| Shortcut/context menu/icon | Chưa có alias riêng; dùng cùng Action ID qua menu Bitmaps |

Chỉ bật khi chọn đúng một ảnh raster có mẫu RGBA hợp lệ, source embedded/linked còn tham chiếu và tài liệu không khóa.
Chặn profile không phải RGB, ICC lỗi, CMM không tạo được preview sang sRGB, ảnh locked hoặc selection đổi trong lúc chọn tệp.

## 2. Ngữ nghĩa màu

- `Assign` thay ICC interpretation của buffer hiện hành nhưng giữ nguyên RGBA8/RGBA16LE, alpha,
  kích thước, graph, source encoded bytes và metadata gốc trong encoded source.
- Profile ICC đã gán được lưu trong `RasterSourceMetadata::icc_profile`; pixel basis ghi nhận rằng
  kênh hiện hành chưa được đổi thành sRGB. `.pr` hiện đã lưu cả profile bytes và basis, không cần schema mới.
- Canvas resolver đưa ảnh có ICC chưa áp dụng qua worker/CMM sang cache RGBA sRGB. Bản xem tạm không
  thay đổi master trong document. Lệnh chuyển profile riêng vẫn tạo mẫu sRGB mới và có Undo độc lập.
- Preview cố định Relative Colorimetric, BPC off và system sRGB. Nếu source và output có cùng identity,
  CMM transform không cần chạy và mẫu được sao chép nguyên vẹn.
- System sRGB là không gian đích preview hiện tại; đây chưa phải transform theo monitor profile hoặc soft proof.
  Profile không được CMM hỗ trợ bị từ chối trước command. Không hỗ trợ gán CMYK/Gray trong lát cắt này.

## 3. Bề mặt và help

| Nội dung | EN / VI |
| --- | --- |
| Label | `Assign Bitmap Profile…` / `Gán profile ảnh…` |
| Accessible name | Cùng label menu; ID ổn định `action.bitmap.assign_profile` |
| Action help | Assign an RGB ICC interpretation to the selected raster without changing channel values; the canvas preview updates through the ICC worker. |
| Status sau áp dụng | `Assigned RGB ICC: <name> · channel values preserved · preview updating · Ctrl+Z to undo` / nhãn Việt tương đương |
| Hướng dẫn | Chọn một ảnh → Bitmaps > Assign Bitmap Profile… → chọn ICC RGB đúng với số kênh → chờ preview hoặc Ctrl+Z để hoàn tác |
| Lỗi | Profile/CMM không hợp lệ thì document không đổi; app báo nội dung lỗi và không dùng preview gần đúng như màu đã quản lý |

Không có button/icon SVG vì chỉ có menu action và file picker. Menu không thêm cấp cao mới; action chuyển màu của
paint/page hiện có giữ nguyên scope vector/document.

## 4. Bằng chứng và giới hạn nghiệm thu

- `test_pikdraw_document_raster_profile_commands`: command giữ mẫu U8/U16, gán ICC, Undo/Redo và `.pr` save/reopen.
- `test_pikdraw_raster_preview_service`: worker cache đưa ICC RGB sang system sRGB, alpha và source samples giữ nguyên;
  so pixel với scalar `ProfileColorTransform` oracle.
- `test_pikdraw_raster_profile_workflow`: menu action → file-picker adapter → command → Undo/Redo; sau đó chạy Convert,
  serialize/reopen `.pr` và kiểm source/profile/pixel.
- `test_pikdraw_desktop_actions`: menu labels và Action ID.

Các test kiểm dispatch/model/cache/file nhưng không thay thao tác native menu bằng chuột, focus/file-picker thực,
DPI, monitor-profile switching hoặc đánh giá màu vật lý. Mốc P3 và B chưa đạt.

Tuân thủ [chuẩn UX/UI](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md),
[kế hoạch ảnh/màu/in](../../roadmap/image-editing-color-print-20261024.md) và
[phiếu chuyển profile](raster-profile-conversion.md).
