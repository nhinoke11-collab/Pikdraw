# Raster Local Exposure Mask — UI contract

**Trạng thái:** menu và dialog Apply/Cancel có; graph lưu rectangle, linear (có góc) và radial mask; evaluator áp coverage cho U8/U16. Property bar/Properties và mask painting/overlay chưa hoàn tất.

## 1. Mục tiêu và trạng thái

- Người dùng áp Exposure cục bộ lên một bitmap RGB, chỉnh lại được sau khi Apply, Undo/Redo và lưu/reopen `.pr`.
- Capability hiện có: bitmap/rectangle, linear gradient có góc (0° trái→phải, dương theo chiều kim đồng hồ), radial gradient tâm→biên, feather, density và invert. Brush/refine, overlay, vector mask và nhiều node target chưa có UI.
- Source tham chiếu: `modules/raster/README.md`, `RasterAdjustmentMask`, `RasterEditNode`, `SetRasterEditNodeCommand`, evaluator mask sampling và chuẩn `docs/design/ui-ux-standard.md`.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `Bitmaps > Local Exposure Mask…`; đây là chỉnh pixel raster, Apply cần tham số trước khi ghi node. |
| Action ID | `action.bitmap.adjust.local_exposure_mask`; enable khi một bitmap sRGB có nguồn được giữ lại và tài liệu cho phép sửa. |
| Loại thao tác | Action một lần tạo một node Exposure/mask; dialog có draft Apply/Cancel. |
| Standard toolbar | Không áp dụng hiện tại: action ít dùng và cần nhiều tham số; không chiếm chỗ toolbar. |
| Toolbox / flyout | Không áp dụng hiện tại: chưa có brush gesture; không giả lập tool tương tác bằng icon. |
| Property bar | **Chưa triển khai.** Đích: Exposure, Shape, Density và nút mở/focus Properties; mọi field dùng cùng giá trị/validation/command. |
| Properties | **Chưa triển khai; đây là gap bắt buộc để đóng UX.** Đích có Header, Transform/frame, Feature Settings và Apply/Reset action group; trực tiếp sửa toàn bộ mask đang chọn. |
| Mở bảng thuộc tính | Chưa có; đích mở/focus `Properties > Raster > Local Exposure Mask` từ nút bánh răng trên bar hoặc action menu. |
| Context menu / shortcut | Không có alias hoặc shortcut; chưa gán. |
| Dialog/panel mới | Dialog hiện có dùng cho draft Apply/Cancel; giữ vì Apply phải tạo một transaction duy nhất. Properties vẫn là bề mặt chỉnh sửa lại sau Apply. |

## 3. Trường thuộc tính

| ID / label EN + VI | Bề mặt | Kiểu, unit, range, default | Scope và source | Enable / Mixed | Preview, commit, validation |
| --- | --- | --- | --- | --- | --- |
| Exposure / Phơi sáng | Dialog hiện có; target bar + Properties | số EV, −10…10, mặc định +1 | draft của một bitmap được chọn; Apply lưu `RasterExposureParams` | Chỉ một bitmap hợp lệ; không có mixed | graph preview; Apply một `SetRasterEditNodeCommand`; finite/range bắt buộc |
| Shape / Hình dạng | Dialog hiện có; target bar + Properties | Rectangle / Linear gradient / Radial gradient; mặc định Rectangle | draft và `RasterAdjustmentMaskKind` | Một bitmap; không có mixed | Linear/radial xác định coverage; enum phải thuộc allowlist |
| Gradient angle / Góc gradient | Dialog; chỉ hiện với Linear | số độ −180…180°, mặc định 0° | draft linear mask; serialize trong graph schema v17 | Một bitmap; không có mixed | 0° trái→phải; dương xoay theo chiều kim đồng hồ; Apply dùng cùng transaction |
| Left, Top, Width, Height / Trái, Trên, Rộng, Cao | Dialog hiện có; target Properties | tọa độ chuẩn hóa 0…1; frame sau Crop/Rotate | geometry của mask trước node | Một bitmap; không có mixed | vùng phải hữu hạn, rộng/cao >0, nằm trong frame |
| Feather / Feather | Dialog hiện có; target Properties | pixel nguồn, 0…cạnh dài | hình học mask | Một bitmap; không có mixed | Rectangle/linear dùng edge falloff; radial điều khiển đường cong biên |
| Density / Mật độ | Dialog hiện có; target bar + Properties | số nguyên 0…100%, mặc định 100 | mask coverage | Một bitmap; không có mixed | trộn adjusted/original; density ngoài miền bị từ chối |
| Invert / Đảo vùng | Dialog hiện có; target Properties | toggle, mặc định off | mask coverage | Một bitmap; không có mixed | lấy phần bù coverage trước density |

- Bar và Properties phải lấy từ cùng một node/mask ID; không giữ một bản dialog-only khác sau khi Apply. Selection đổi thì panel đổi context, không sửa nhầm ảnh.
- Với code hiện tại, dialog là nguồn draft duy nhất. Bar/Properties sync, reset/reopen vào đúng node, và per-node mask editing còn là gap.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint / lý do |
| --- | --- | --- | --- | --- |
| Không tài liệu | disabled | ẩn | thông tin mặc định | Mở/tạo tài liệu trước |
| Có tài liệu, không selection | disabled | selection context | thông tin mặc định | Chọn một bitmap |
| Một bitmap sRGB editable | enabled | controls mục tiêu | section mục tiêu | Apply tạo một node, Ctrl+Z hoàn tác |
| Nhiều selection / mixed | disabled | mixed/ẩn | báo yêu cầu một bitmap | Mỗi mask gắn một graph target |
| Bitmap locked, màu/profile chưa resolved, nguồn vắng | disabled | disabled | lý do/repair path | Không diễn giải màu hoặc source ngầm định |
| Đang nhập/draft | modal draft | giữ selection context | đóng/focus ổn định | Enter/Apply commit; Esc/Cancel bỏ draft |
| Busy/error | disabled hoặc pending | trạng thái rõ | lỗi gần section | Không commit kết quả stale |

## 5. Icon, tên và hướng dẫn

| Nội dung | EN / VI hoặc tham chiếu |
| --- | --- |
| Icon SVG | Không áp dụng cho menu-only action hiện tại; chưa có asset riêng. Nếu chuyển thành tool/bar control, thêm SVG theo icon system hiện hành trước khi bật. |
| Label ngắn | `Local Exposure Mask…` / `Mặt nạ phơi sáng cục bộ…`; section target `Local Exposure Mask` / `Mặt nạ phơi sáng cục bộ`. |
| Accessible name | Dialog labels và combo/toggle có label tĩnh; combo shape cần giữ accessible name “Mask shape / Kiểu mặt nạ”. |
| Tooltip + focus help | `Local Exposure Mask` — `Apply a feathered local Exposure adjustment to the selected raster.`; menu action hiện dùng mô tả action; từng field cần tooltip/focus help khi đưa lên Properties. |
| Status hint | `Nhập vùng và chọn kiểu mask · Apply tạo một bước Undo · Esc hủy` / `Set mask bounds and shape · Apply creates one Undo step · Esc cancels`. |
| Error/disabled reason | Chọn một bitmap sRGB có nguồn được giữ lại; kiểm tra bounds, feather, density và ID trước Apply. |
| Help đầy đủ | Tài liệu này; 3 bước: chọn bitmap → chọn shape/bounds/Exposure → Apply; Ctrl+Z sửa lại; Cancel/Esc bỏ draft. Linear chạy trái→phải, radial giảm từ tâm ra biên. |

## 6. Hành vi và bằng chứng nhận

- Apply ghi một graph node và một command transaction; Cancel/Esc không sửa tài liệu; Undo/Redo phải khôi phục nguyên graph/mask.
- `.pr` schema hiện hành v17 lưu shape, geometry, góc linear và Color Balance; graph v1–v16 tiếp tục đọc, góc cũ mặc định 0°.
- Focus/tab order theo thứ tự Exposure, bounds, Feather, Density, Shape, Angle (khi Linear), Invert, Apply, Cancel; numeric input giữ locale parse hiện hành.
- Regression source bao phủ góc 0° và 90°, radial falloff, và `.pr` round-trip góc linear; lượt Release cuối chưa chạy. Chưa có native capture, DPI/layout, dialog keyboard/IME hoặc printer/proof evidence.
- Trước khi tuyên bố UX P5 hoàn tất, thêm tương tác trực tiếp trong Properties, bar đồng bộ, node reselect/edit, reset, tooltip từng field, test pointer/menu/Apply/Undo/reopen và kiểm native window.
