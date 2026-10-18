# Phiếu UI: Drop Shadow Properties và Thao tác Kéo bóng Trực tiếp

## 1. Mục tiêu và phạm vi

- **Task:** `drop-shadow-interaction-and-properties-20261018` · module `desktop`.
- **Mục tiêu:** 
  1. Khắc phục lỗi đảo ngược trục dọc (Y-axis inversion) khi kéo bóng tương tác trên canvas: kéo chuột xuống làm bóng đổ xuống đồng bộ theo đúng hướng trỏ chuột.
  2. Nâng cao độ mịn và độ chính xác của thao tác kéo: hỗ trợ phím `Shift` khóa góc $15^\circ$, phím `Ctrl` khóa bước $0,5\text{ mm}$, làm tròn mặc định $0,1\text{ mm}$.
  3. Bổ sung thanh thuộc tính ngữ cảnh (Contextual Property Bar ribbon) khi làm việc với Drop Shadow: hiển thị đầy đủ Offset X, Offset Y, Feather (Độ nhòe), Opacity (Độ mờ), Mẫu bóng (Presets), Màu nguồn (Color swatch), Chế độ hòa trộn (Blend mode), Nút xóa bóng (Clear shadow), và Nút chuyển đổi về thuộc tính vật thể (Toggle object).
- **Phạm vi:** Đối tượng Path hoặc Text được chọn trên canvas có gắn hiệu ứng `DropShadowEffect`.
- **Giới hạn:** Hiệu ứng được thực thi bởi Direct2D pipeline hoặc Software rasterizer; không tạo node trùng lặp (mỗi đối tượng tối đa một node Drop Shadow theo đặc tả Appearance v1).
- **Tham chiếu:** [chuẩn UI/UX](../ui-ux-standard.md), [bố cục workspace](../../architecture/workspace-layout.md).

## 2. Đường vào và vị trí

| Bề mặt | Quyết định |
| --- | --- |
| Menu chính | `Effects > Drop Shadow` (`MenuActionId::EffectsDropShadow`); kích hoạt hoặc chọn node bóng hiện có và mở Property Bar bóng. |
| Toolbox | Flyout nhóm 3 (Appearance) > mục 2 `Drop Shadow`; cùng action với menu chính. |
| Contextual Property Bar | Khi đối tượng có bóng đổ và đang ở chế độ Drop Shadow: hiển thị thanh điều khiển gồm Label, Mẫu ▾, X:, Y:, Nhòe:, Mờ:, ■ Màu, Hòa trộn, ✕ Xóa, ⛶ Vật thể. Khi ở chế độ Vật thể: có thêm nút `◐ Bóng` để chuyển sang thanh bóng đổ. |
| Canvas Handle | Đường nối teal nối từ tâm đối tượng đến tay nắm tròn trắng tại `(center.x + dx, center.y + dy)`. Kéo tay nắm điều chỉnh `dx` và `dy` trực tiếp. |
| Properties Dock | Tab Inspector > Appearance > Drop Shadow hiển thị chi tiết các trường số, màu và danh sách blend mode đồng bộ. |

- **Tooltip / Focus help:**
  - Nhãn hiển thị song ngữ VI/EN (`◐ Bóng:` / `◐ Shadow:`, `Mẫu ▾` / `Preset ▾`, `Nhòe:` / `Feather:`, `Mờ:` / `Opacity:`).
  - Status bar thông báo rõ tọa độ lệch X, Y kèm trợ giúp phím tắt (`Shift: 15° · Ctrl: 0.5mm · Enter lưu · Esc hủy`).

## 3. Trường và trạng thái điều khiển trên Property Bar

| Điều khiển | Kiểu / miền | Trạng thái & Thao tác | Commit / Undo |
| --- | --- | --- | --- |
| Mẫu ▾ (Presets) | Menu / chu kỳ 6 mẫu chuẩn | Dưới-Phải, Dưới-Trái, Trên-Phải, Trên-Trái, Tỏa bóng, Đổ đậm | Áp dụng tức thì vào bản nháp/document; 1 bước Undo |
| X: (Offset X) | Số mm, ±1.000.000 | `NumericFieldId::EffectOffsetX`; nhập trực tiếp trên ribbon | Enter xác nhận, Tab chuyển ô, Esc hủy; Undo phục hồi |
| Y: (Offset Y) | Số mm, ±1.000.000 | `NumericFieldId::EffectOffsetY`; nhập trực tiếp trên ribbon | Như Offset X |
| Nhòe: (Feather / Sigma) | Số mm, 0–1.000.000 | `NumericFieldId::EffectSigmaX`; chỉnh độ mờ biên | Như Offset X |
| Mờ: (Opacity) | Phần trăm, 0–100% | `NumericFieldId::EffectOpacity`; độ đậm nhạt của bóng | Như Offset X |
| ■ Màu (Color) | Swatch màu RGB | Bấm để mở hộp thoại `edit_source_color` chọn màu | Cập nhật màu nguồn; 1 bước Undo |
| Chế độ hòa trộn | Chu kỳ 6 mode | Normal → Multiply → Screen → Overlay → Darken → Lighten | Chuyển chế độ tức thì; 1 bước Undo |
| ✕ Xóa (Clear) | Nút đỏ | Xóa hiệu ứng Drop Shadow khỏi đối tượng | 1 bước Undo; chuyển về thanh Vật thể |
| ⛶ Vật thể (Toggle) | Nút teal | Chuyển thanh ribbon về chế độ Transform vật thể (X, Y, W, H, Rotation) | Chỉ đổi giao diện hiển thị, không đổi dữ liệu |

## 4. Kiểm tra và nghiệm thu

- Kiểm tra Direct2D shader coordinate mapping: `dy_px = -effect.dy_mm * camera_zoom` đảo ngược chiều render D2D khớp với tọa độ thế giới Cartesian (+Y hướng lên) của PikDraw.
- Thao tác kéo chuột xuống: canvas handle và bóng render đều dời xuống dưới theo con trỏ.
- Giữ `Shift` khi kéo: góc lệch bắt dính theo bước $15^\circ$.
- Giữ `Ctrl` khi kéo: khoảng cách bắt dính theo lưới $0,5\text{ mm}$.
- Tự động làm tròn $0,1\text{ mm}$ loại bỏ rung giật số thực.
- Các bài test `test_pikdraw_effect_draft` và `test_pikdraw_rendering_drop_shadow` chạy pass 100%.
