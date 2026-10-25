# Chuyển profile ICC ảnh raster sang sRGB

Ngày 24/10/2026. Capability P3 đang triển khai; pixel/oracle, command, action dispatcher,
Undo/Redo và `.pr` round-trip có regression. UI cửa sổ native chưa được kiểm trực quan.

## 1. Mục tiêu và trạng thái

Một ảnh raster có ICC RGB nhúng nhưng chưa được áp vào pixel có thể được chuyển rõ ràng sang
sRGB để dùng cùng các adjustment hiện có. Lệnh tạo buffer sRGB mới cho placement đang chọn,
giữ byte nguồn, ICC gốc và edit graph để provenance, Undo và xuất lại nguồn còn khả dụng.
Hiện chỉ hỗ trợ một ảnh, profile nguồn ICC RGB hợp lệ và profile sRGB hệ thống; không nhận
CMYK/Gray, không gán profile, không chọn profile đích và không chuyển kênh CMYK.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định |
| --- | --- |
| Menu chính | `Bitmaps > Convert Bitmap Profile to sRGB` / `Bitmaps > Chuyển profile ảnh sang sRGB`; thuộc bitmap vì chỉ chuyển pixel của ảnh chọn |
| Action/command ID | `action.bitmap.convert_profile_to_srgb`; dispatcher gọi worker raster, command ghi thay đổi vào document |
| Loại thao tác | Tác vụ một lần, bất đồng bộ; tiến độ ở status bar, `Esc` hủy trước commit |
| Standard toolbar | Không áp dụng; thao tác ít dùng, giữ menu đầy đủ |
| Toolbox / flyout | Không áp dụng; đây không phải công cụ vẽ trên canvas |
| Property bar | Không áp dụng; không có tham số do người dùng sửa trong phiên bản này |
| Properties | Không có editor riêng; đây là một chuyển đổi cố định. Profile nguồn/đích và cấu hình được ghi trong Action help, status và tài liệu; các profile chi tiết của selection chưa có control trong dock |
| Mở bảng thuộc tính | Không áp dụng; mở/focus panel không tạo dữ liệu hay Undo |
| Context menu / shortcut | Chưa có alias hoặc shortcut riêng; menu dispatch và Action ID duy nhất |
| Dialog/panel mới | Không áp dụng; destination, intent và BPC là cấu hình cố định, không có draft để xác nhận |

## 3. Trường và phạm vi

| ID / label EN + VI | Surface | Kiểu, giá trị | Scope và validation |
| --- | --- | --- | --- |
| `color.source_profile` / Source profile / Profile nguồn | Action help/status | ICC nhúng của ảnh, chỉ RGB; chỉ nhận diện sau validation CMM | Một ảnh chọn; ICC bytes được giữ nguyên |
| `color.destination_profile` / Destination / Profile đích | Action help/status | sRGB hệ thống; readonly | Dùng profile đã qua `ProfileResource` validation |
| `color.intent` / Rendering intent / Mục đích chuyển màu | Action help/status | Relative Colorimetric; cố định | CMM transform chạy trên worker |
| `color.bpc` / Black point compensation / Bù điểm đen | Action help/status | Off; cố định | CMM flag được truyền explicit, không bỏ qua ngầm |
| `color.scope` / Scope / Phạm vi | Enable state | Một `ImageObject` được chọn, không khóa | Snapshot/revision/object/selection phải còn nguyên trước commit |
| `color.sample_type` / Precision / Độ chính xác | Readout help | RGBA8; giữ RGBA16LE nếu ảnh có master 16-bit | U16 chuyển trước; RGBA8 preview làm tròn từ mẫu U16 sau chuyển |

Không có field draft, bar/Properties không dùng chung giá trị chỉnh sửa. Action disable nếu không
có đúng một ảnh, ICC RGB chưa áp dụng, nguồn gốc không còn, document bị khóa, pixel buffer sai
kích thước hoặc một job bitmap xung đột. ICC hỏng, CMM không khả dụng, profile sRGB thiếu,
hết bộ nhớ hoặc timeout được báo riêng; không đổi document.

## 4. Hành vi và bằng chứng

- Worker đọc snapshot bất biến; không chờ CMM hoặc RAM trong UI event loop. Memory waiter dự trù output,
  metadata có thể copy và scratch 16 MiB; chờ tối đa 5 phút dưới áp lực RAM.
- `Esc` gửi cancellation; converter kiểm tra theo block 16.384 pixel. Hủy/lỗi không commit buffer một phần.
- Commit qua `ConvertRasterImageToSrgbCommand`; một Undo khôi phục buffer và color basis. Redo áp lại.
- `.pr` lưu buffer sRGB, basis `ConvertedToSRgb`, source bytes, ICC nguồn và graph. Export dùng pixel
  hiện hành; export master nguồn vẫn đi từ encoded source theo workflow importer.
- Job hoàn tất sau khi document snapshot hoặc selection đổi sẽ bị bỏ. Profile conversion không thay graph,
  geometry, alpha, DPI hoặc pixel source 16-bit ngoài việc đổi không gian ICC.
- Đây là chuyển đổi raster số; không chứng minh màn hình đã hiệu chuẩn, profile máy in, soft proof,
  separations, TAC, driver color owner hoặc màu vật lý.

## 5. Icon, tên và hướng dẫn

| Nội dung | EN / VI |
| --- | --- |
| Icon SVG | Không áp dụng; menu item chữ, không tạo toolbox/toolbar icon |
| Label | `Convert Bitmap Profile to sRGB` / `Chuyển profile ảnh sang sRGB` |
| Accessible name | Cùng label menu/action; action ID ổn định |
| Tooltip + focus help | Action help: “Convert the selected raster's embedded RGB ICC profile to system sRGB using relative colorimetric intent; black point compensation is off.” Không gán shortcut chưa tồn tại |
| Status hint | Đang chuyển profile · Relative · BPC off · `Esc` hủy; phần trăm tiến độ; thành công nêu giữ source/ICC và Undo |
| Error/disabled reason | Chỉ enable với một ảnh RGB ICC chưa áp dụng và source còn giữ; CMM/profile/bộ nhớ lỗi hiển thị báo lỗi |
| Help đầy đủ | Menu `Bitmaps`; chọn ảnh có ICC RGB → Convert → chờ tiến độ hoặc `Esc` → Undo/Redo. Hiện chưa hỗ trợ Assign, CMYK source, lựa chọn intent/BPC hoặc proof |

## 6. Kiểm thử giao diện và giới hạn

`test_pikdraw_raster_profile_conversion` so RGB8 với scalar CMM oracle, so RGBA16LE với transform
trực tiếp, kiểm alpha và cancel. `test_pikdraw_document_raster_profile_commands` kiểm lệnh, Undo/Redo,
source/ICC/graph và serialize/deserialize. `test_pikdraw_raster_profile_workflow` dispatch action thật,
chờ worker, áp command, Undo/Redo và mở `.pr`. Các test này không thay cho thao tác menu bằng chuột,
tooltip, focus, DPI hoặc xem màu trên thiết bị; các bước đó còn phải nghiệm thu native UI.

Tuân thủ [chuẩn UX/UI](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md)
và [kế hoạch sửa ảnh/màu/in](../../roadmap/image-editing-color-print-20261024.md).
