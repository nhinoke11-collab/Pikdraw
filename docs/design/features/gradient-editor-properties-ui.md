# Phiếu UI: Gradient Properties

## 1. Mục tiêu và giới hạn

- **Task:** `gradient-properties-ui-20261018` · module `desktop`.
- **Mục tiêu:** chỉnh stop và hình học Gradient qua Inspector mà vẫn dùng draft/Apply/Cancel và command transaction hiện có.
- **Logic có sẵn:** `GradientPropertiesPresenter` quản lý stop, màu, opacity, offset, midpoint, kind/spread và geometry; `GradientToolController::apply()` ghi một `SetFillPaintCommand` cho selection.
- **Giới hạn:** chỉ Path có fill Gradient. Import/export fidelity và color-management proof nằm ngoài task. Kéo tay nắm trên canvas giữ hành vi commit khi nhả; thao tác Properties phải Apply/Cancel.
- **Tham chiếu:** `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`, audit B2 trong `docs/reviews/ui-logic-gap-audit-20261018.md`.

## 2. Đường vào và bề mặt

| Mục | Quyết định |
| --- | --- |
| Menu chính | Dùng `Object > Gradient Fill…` hoặc `Tools > Interactive Gradient Tool` hiện có; cả hai mở cùng Gradient tool và Inspector. Không tạo action/menu mới. |
| Action ID | Dùng `MenuActionId::ObjectFillGradient` / `MenuActionId::ToolGradient` hiện có. |
| Toolbox / flyout | Gradient tiếp tục ở nhóm Effects/Appearance; icon và tên hiện có. |
| Property bar | Giữ thao tác nhanh: Linear/Radial, Pad/Repeat/Reflect, Reverse, Fit Bounds, Angle +45°, Add Stop, Apply, Cancel. Add Stop chèn giữa stop kề, sao chép source color và opacity của stop đang chọn. |
| Properties | `Inspector > Gradient`: danh sách 3 stop nhìn thấy; mũi tên chọn stop trước/sau và cửa sổ danh sách tự theo selection. Có màu, offset, opacity, midpoint, angle, length và tâm X/Y. Dùng đúng dock hiện có. |
| Standard toolbar / dialog | Không áp dụng. Nút Color mở source-color dialog hiện có; Apply/Cancel của Gradient vẫn điều khiển draft ngoài dialog màu. |
| Tab Style/Align | Khi Gradient tool active, Inspector tự chuyển Objects/Gradient context; dùng một panel duy nhất, không đồng thời vẽ Objects/Style/Align body. |

- **Icon SVG:** giữ `apps/desktop/resources/icons/tool-gradient.svg`; không thêm glyph hoặc hệ icon mới.
- **Tooltip:** `Interactive Gradient Tool` / `Công cụ chuyển sắc tương tác`.
- **Cách dùng:** chọn Path, mở Gradient từ menu/flyout, sửa stop hoặc geometry trong Properties, nhấn Enter để ghi vào bản nháp, rồi Apply để lưu hoặc Cancel để bỏ. Khi bản nháp bẩn, Apply/Cancel trước khi kéo tay nắm trên canvas.

## 3. Trường và hành vi

| Trường / nhãn EN · VI | Kiểu / miền | Scope và trạng thái | Commit |
| --- | --- | --- | --- |
| Stops · Điểm dừng | Danh sách 3 hàng nhìn thấy, tới giới hạn model; swatch, thứ tự và offset | Chọn stop theo stable ID; mũi tên chọn stop trước/sau và danh sách theo selection; Mixed stops khóa phần sửa stop khi nhiều object có dải màu khác nhau | Chỉ đổi stop đang chọn trong presenter draft |
| Add / Thêm; Delete / Xóa | Nút; model giữ tối thiểu 2 stop | Add chèn ở giữa stop kề, sao chép source color/opacity của stop đang chọn; Delete disable ở 2 stop hoặc Mixed | Cập nhật draft; Apply ghi command |
| Color… / Màu… | Source color dialog hiện có; RGB/CMYK và source alpha | Stop đang chọn; stop opacity riêng biệt với alpha màu | Apply của dialog màu cập nhật draft; Gradient Apply ghi document |
| Offset (%) / Vị trí (%) | Số thập phân 0–100 | Stop đang chọn; disabled với Mixed stops | Enter/Tab chỉ xác nhận field vào draft; Esc bỏ field |
| Opacity (%) / Độ mờ (%) | Số thập phân 0–100 | Stop đang chọn; tách biệt source alpha | Như Offset |
| Midpoint (%) / Trung điểm (%) | Số thập phân 5–95 | Thuộc segment từ stop đang chọn sang stop kế; stop cuối disabled | Như Offset |
| Angle (°) / Góc (°) | Số thập phân −36.000..36.000 | Geometry của Gradient draft; disabled khi angle/length/center/aspect ratio khác nhau giữa nhiều Path | Enter/Tab ghi presenter draft, không sửa document |
| Length (mm) / Chiều dài (mm) | Số dương 0,001..1.000.000 | Geometry; Linear length hoặc Radial radius; cùng quy tắc mixed như Angle | Như Angle |
| Center X/Y (mm) / Tâm X/Y (mm) | Số thập phân −1.000.000..1.000.000 | Tọa độ tâm Gradient; cùng quy tắc mixed như Angle | Như Angle |

- Dấu chấm hoặc phẩy là dấu thập phân. Giá trị sai/ngoài miền giữ draft mở và không thay đổi nguồn.
- Tab xác nhận giá trị hợp lệ rồi đi qua các field; Esc bỏ field hiện tại. Apply ghi một transaction Undo; Cancel bỏ mọi thay đổi Properties chưa áp dụng.
- Nếu selection có Mixed stops, không lấy stop của Path đầu tiên để ghi đè các dải màu khác. Nếu hình học khác nhau giữa các Path, các field hình học và Angle +45° bị khóa vì presenter hiện ghi toàn bộ ma trận; kind/spread vẫn có thể chỉnh độc lập.
- Khi còn draft chưa Apply, kéo tay nắm trên canvas bị chặn với hướng dẫn Apply hoặc Cancel trước để tránh mất nội dung.

## 4. Icon, focus help và status

| Nội dung | English / Tiếng Việt |
| --- | --- |
| Label | `Gradient` / `Chuyển sắc`; `Stop` / `Điểm dừng`; `Color…` / `Màu…` |
| Focus help | `Select a stop; edit its color and position. Midpoint changes the segment after it.` / `Chọn stop; sửa màu và vị trí. Trung điểm áp dụng cho đoạn phía sau.` |
| Status khi sửa | `Gradient draft · Enter confirms field · Apply commits · Cancel discards` / `Bản nháp Gradient · Enter xác nhận trường · Áp dụng ghi · Hủy bỏ bản nháp` |
| Error | Hiện miền field, hoặc `Mixed stops · select one object to edit stops` / `Stop khác nhau · chọn một đối tượng để sửa dải màu`. |
| Help đầy đủ | Phiếu này; không thêm phím tắt mới. |

## 5. Nghiệm thu

- `test_pikdraw_gradient_native_shell` kiểm các lối vào hiện có, chọn stop, thêm/xóa và ràng buộc tối thiểu, numeric draft, Apply/Cancel và Undo.
- Chụp/kiểm Inspector và stop list bằng harness ở cửa sổ native; không suy rộng layout sang kích thước/DPI chưa xem.
