# Xuất ảnh master TIFF CMYK16

Trạng thái triển khai: đang tích hợp. Tuyến này tạo một tệp đầu ra từ ảnh RGB đã chọn;
nó không đổi mẫu ảnh hoặc graph trong tài liệu.

## Vị trí và hành vi

| Bề mặt | Quyết định |
| --- | --- |
| Menu / action | `Bitmaps > Export Bitmap Master to CMYK16 TIFF…` / `Bitmaps > Xuất ảnh gốc thành TIFF CMYK16…`; ID `action.bitmap.export_cmyk_master` |
| Toolbox / flyout | Không áp dụng; đây là lệnh xuất tệp, không phải công cụ trên canvas |
| Standard toolbar | Không áp dụng; giữ thanh công cụ hiện tại gọn |
| Property bar | Không áp dụng; các lựa chọn chỉ thuộc một lượt xuất |
| Properties | Không áp dụng; profile và rendering intent chỉ áp dụng cho tệp đang xuất, không ghi vào ảnh nguồn |
| Icon SVG | Không áp dụng; menu native hiện dùng nhãn chữ |
| Tooltip / focus help | “Export the selected RGB image master as a 16-bit CMYK TIFF with an explicit ICC profile.” / “Xuất ảnh RGB đang chọn thành TIFF CMYK 16-bit với profile ICC được chỉ định.” |
| Status | Hiện trạng thái chuyển RGB → CMYK16 và rendering intent; lỗi báo rõ profile, CMM, bộ nhớ hoặc encoder |

## Trường và scope

| Trường | Scope | Trạng thái |
| --- | --- | --- |
| ICC đầu ra CMYK | Lượt xuất | Chọn file ICC hợp lệ, đúng hệ CMYK, tối đa 1 MiB; profile được nhúng trong TIFF |
| Rendering intent | Lượt xuất | Perceptual, Relative, Saturation hoặc Absolute; mặc định Relative; tạo transform trước khi mở Save dialog |
| Black point compensation | Lượt xuất | Disable kèm giải thích vì Windows CMM hiện tại không cung cấp capability này |
| Đường dẫn TIFF | Lượt xuất | Save dialog TIFF; thay thế file chỉ khi người dùng chấp nhận prompt của Windows |
| DPI | Từ ảnh nguồn | Giữ DPI ngang/dọc của ảnh; giá trị không hợp lệ dùng 96 DPI như export master RGB |
| Alpha | Pixel đầu ra | Flatten trên nền trắng trong RGB linear-light trước khi chuyển; TIFF CMYK16 đầu ra opaque |

## Luồng sử dụng

Chọn đúng một ảnh RGB có buffer đầy đủ và color basis đã resolve. Mở lệnh trong menu Bitmaps,
chọn ICC CMYK đích, chọn rendering intent, rồi chọn đường dẫn TIFF. Worker đánh giá graph đã lưu,
đổi RGBA8 lên miền U16 nếu cần, resolve source ICC về RGB working profile, flatten transparency
trên nền trắng trong linear light, chuyển batch qua Windows CMM, ghi TIFF CMYK16 cùng profile,
rồi publish tệp hoàn chỉnh nguyên tử. Cancel hoặc lỗi trước publish không để lại tệp một phần;
tài liệu và Undo history không đổi.

Ảnh nguồn có ICC chưa áp dụng chỉ được nhận khi ICC nguồn là RGB và transform khả dụng. Model ảnh
hiện chưa giữ native CMYK samples, nên ảnh CMYK nguồn phải được decode/nối channel native trước khi
có thể hứa giữ separation hoặc chuyển từ CMYK. CMM hiện chỉ chạy trên Windows; BPC chưa hỗ trợ.

## Acceptance

- Menu/action chỉ enable với một ảnh hợp lệ có pixel đủ kích thước; mixed/vector/multiple selection bị từ chối.
- RGB U16 → CMYK TIFF giữ 16 bit, kích thước sau crop/graph, compositing nền trắng, DPI và đúng profile ICC đích.
- RGB U8 → CMYK TIFF mở được qua ICC; đầu vào được mở rộng lên U16 trước compositing/CMM.
- TIFF writer từ chối profile không phải CMYK, profile quá lớn, stride/sample-size sai, extension không khớp và không ghi tệp khi xác thực thất bại.
- Pixel CMYK được so với transform ICC cùng intent; Save/Open TIFF kiểm profile, chiều, DPI và alpha.
- CMM/encoder lỗi hoặc cancellation không thay ảnh trong document; transform/export không được tính là soft proof, hard proof, separations hoặc bản in đã xác nhận.

Build, regression và thao tác menu native là các cổng riêng. Chưa có bằng chứng thao tác cửa sổ native
hoặc xác nhận bằng RIP/máy in; không công bố route này là proof đủ điều kiện.
