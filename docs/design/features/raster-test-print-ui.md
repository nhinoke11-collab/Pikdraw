# Raster Test Sheet Print — UI contract

**Trạng thái:** action/menu và direct-print worker ghi nhận destination-profile SHA-256 từ Windows WCS; chưa build/test hoặc chạy với printer thật. Physical color, paper/ink and ruler qualification remain open.

## 1. Mục tiêu và capability

In bảng test gồm crop chẩn đoán, control patches, variants và thước lên máy in đã chọn. Artwork nguồn và graph không bị thay đổi; kết quả là một job riêng, không được gọi là proof xác nhận màu.

Route hiện tại dùng Windows WCS trên printer DC khi DEVMODE xác nhận `DMICMMETHOD_NONE` hoặc `DMICMMETHOD_SYSTEM`. Driver/device ICM riêng bị từ chối để tránh double conversion. Native UI chưa xác minh điều kiện thực tế theo model/driver; giấy, mực, ICC đầu ra, thước vật lý và ΔE cần acceptance thiết bị.
Worker đọc đường dẫn profile đầu ra hiện hành bằng `GetICMProfileW`, băm nội dung SHA-256 có giới hạn kích thước,
đưa hash vào condition ID in trên chart và yêu cầu hash hợp lệ trước `StartDoc`. Đường dẫn hệ thống không được
lưu vào tài liệu; hash chỉ nhận diện file profile tại thời điểm job. Điều này không chứng minh driver/RIP đã
áp dụng đúng profile đó hoặc giấy/mực đã đúng condition.

## 2. Vị trí và hành vi

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu | `File > Print Test Sheet…`; đây là lệnh xuất trực tiếp tới print device. File test TIFF vẫn là `File > Export Test Sheet…`. |
| Action ID | `action.file.print_test_sheet`; enabled cùng điều kiện chọn một bitmap có source/profile basis resolved và không có export/profile job khác. |
| Loại thao tác | Action một lần; PrintDlg chọn printer/copies/paper mode; render và spool bất đồng bộ. |
| Toolbar | Không áp dụng: tác vụ ít dùng và cần printer selection trước khi chạy. |
| Toolbox | Không áp dụng: không phải tool canvas hay gesture. |
| Property bar / Properties | Không áp dụng: output condition là metadata riêng của job; không sửa ảnh/selection. Printer options được giao cho PrintDlg/driver UI. |
| Shortcut/context menu | Chưa gán; menu File là đường truy cập duy nhất. |
| SVG icon | Không áp dụng cho menu action hiện tại; nếu thêm toolbar icon thì phải có asset thuộc icon system trước. |
| Dialog | Windows PrintDlg là dialog chuẩn để chọn thiết bị và DEVMODE; Cancel đóng dialog mà chưa tạo job. Print to File bị ẩn và disable. |

## 3. Input, output condition và lifecycle

| Field | Source / value | Validation and behavior |
| --- | --- | --- |
| Bitmap | Một selection có source RGBA sRGB resolved | Graph frozen trong snapshot; không chấp nhận source basis unknown/unapplied hoặc locked |
| Printer | DEVNAMES device string | Bắt buộc; ghi vào `device_id` |
| ICM method | DEVMODE `dmICMMethod` | Chỉ NONE/SYSTEM; driver/device-owned mode bị chặn |
| Paper | DEVMODE paper code hoặc width/length | Ghi media id; thiếu dữ liệu thì `printer-default-paper` |
| Print mode | Orientation, color, quality, Y-resolution, media type, ICM intent and dither khi được DEVMODE khai báo | Không đoán trường không có trong `dmFields`; ghi driver-default |
| Printer PPI/page | `GetDeviceCaps` | 1…9600 PPI; physical dimensions và printable bounds phải dương, offsets nằm trong giấy |
| Test crop | Center crop, tối đa 25 mm trên mỗi axis | Crop/source bounds do planner validate; layout dùng page mm/PPI thực từ DC |
| Color owner | Windows WCS enabled; driver ICM disabled | `SetICMMode(ICM_ON)` trước job và lại sau `StartPage`; nguồn DIB báo `LCS_sRGB` |
| Destination profile identity | `GetICMProfileW` trên printer DC, đọc trong worker | Nội dung file profile tối đa 16 MiB được SHA-256; thiếu/không đọc được thì chặn trước `StartDoc`; chỉ lưu hash, không lưu path |

Render, source decode, DIB allocation, StartDoc/StartPage/StretchDIBits/EndPage/EndDoc/AbortDoc và DeleteDC chạy trong worker. Hủy được kiểm tra trước spool và sau các lệnh GDI; trang đang render bị AbortDoc từ cùng worker sở hữu DC.

## 4. Trạng thái và trợ giúp

| Nội dung | EN / VI |
| --- | --- |
| Label | `Print Test Sheet…` / `In bảng thử trực tiếp…` |
| Tooltip/focus help | `Print Test Sheet` — `Send a frozen raster test sheet through Windows WCS; physical color is not verified.` |
| Status khi chạy | `Rendering and sending the test sheet through Windows WCS · Esc cancels · physical color is not verified` / `Đang dựng và gửi bảng thử qua Windows WCS · Esc để hủy · chưa xác nhận màu bản in` |
| Error | Chọn lại máy in/ICM method được hỗ trợ, kiểm tra paper/PPI, profile màu hoặc RAM theo thông báo |
| Success | Printer spooler nhận job; status/chart mang hash profile đích dạng đầy đủ/prefix và nói rõ màu vật lý chưa được đo. Không coi ruler tính theo PPI là xác nhận thước trên giấy. |
| Help | Chọn bitmap → File > Print Test Sheet → chọn printer/setting WCS → kiểm tra hàng đợi driver. Đo thước 100 mm và màu với thiết bị/điều kiện ghi riêng. |

## 5. Ngữ cảnh và acceptance

| Ngữ cảnh | Kết quả |
| --- | --- |
| Không có tài liệu/selection | Action disabled; hướng dẫn chọn một bitmap |
| Một bitmap hợp lệ | Action enabled; PrintDlg mở |
| Nhiều selection, locked, profile unresolved | Không in; không flatten/convert ngầm |
| DEVMODE thiếu hoặc chỉ driver/device ICM | Hủy trước StartDoc với hướng dẫn dùng Windows WCS và tắt ICM riêng của driver |
| WCS không bật được sau `StartPage` | AbortDoc; không báo job thành công |
| Cancel/error/memory pressure | Job được hủy hoặc báo lỗi; snapshot và source không đổi; retry là lệnh mới |
| Print to File | Disabled; TIFF test export có đường File riêng |

Acceptance chưa chạy: production route validation tests, desktop action/menu, worker cancellation/error paths, Release link, dialog native, printer spool capture, physical ruler, paper/ink/profile association và ΔE. Hash file được truy vấn chưa chứng minh profile association của driver hoặc output proof. Không suy ra proof/production readiness từ code hoặc spool success.
