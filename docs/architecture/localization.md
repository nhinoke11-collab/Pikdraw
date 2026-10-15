# Kiến trúc bản địa hóa (Localization Architecture)

- **Trạng thái**: Specification (Khóa tại M0 Foundation)
- **Tài liệu liên quan**: [Contract C7 (Actions & UI)](../../contracts/c7-actions-ui.md), [ADR 0004](../decisions/0004-ui-development-loop.md)

---

## 1. Nguyên tắc cốt lõi

1. **Giao diện người dùng mặc định Tiếng Việt (`vi_VN`)**:
   - PikDraw là phần mềm vector chuyên nghiệp phục vụ cộng đồng thiết kế, in ấn và xưởng quảng cáo tại Việt Nam.
   - Ngôn ngữ đối chiếu kỹ thuật chuẩn là Tiếng Anh (`en_US`).
2. **Không biến Localization thành một module runtime core cồng kềnh**:
   - Dữ liệu bản địa hóa là catalog JSON rời đặt trong thư mục `locales/`.
   - Desktop host đọc file catalog và cung cấp dịch vụ dịch chuỗi qua `LocaleService` đơn giản, không kéo theo các thư viện nặng hay ép các module core phải phụ thuộc ngược vào desktop.

---

## 2. Cấu trúc Catalog Chuỗi (`locales/*.json`)

Các file catalog được đặt tên theo mã chuẩn BCP-47:
- `locales/vi_VN.json`: Tiếng Việt
- `locales/en_US.json`: Tiếng Anh

### Định dạng JSON
```json
{
  "locale": "vi_VN",
  "version": 1,
  "translations": {
    "app.title": "PikDraw - Trình biên tập Vector Chuyên nghiệp",
    "action.file.new": "Tạo mới",
    "action.file.open": "Mở tệp...",
    "action.file.save": "Lưu tệp",
    "action.file.save_as": "Lưu bản sao...",
    "action.file.export": "Xuất tệp...",
    "action.edit.undo": "Hoàn tác",
    "action.edit.redo": "Làm lại",
    "action.view.reload_ui": "Nạp lại giao diện (F5)",
    "tool.select": "Chọn đối tượng (V)",
    "tool.pen": "Bút vẽ Pen (P)",
    "tool.node_edit": "Chỉnh sửa điểm Node (A)",
    "dialog.confirm_discard": "Bạn có chắc chắn muốn hủy các thay đổi chưa lưu?"
  }
}
```

---

## 3. Quy trình Xử lý khi Chuyển đổi Ngôn ngữ

1. **Không khởi động lại ứng dụng**:
   Khi người dùng chọn đổi ngôn ngữ trong Cài đặt (Settings), `LocaleService` nạp catalog mới và phát tín hiệu `languageChanged()`.
2. **Cập nhật động trên Widget**:
   Mọi cửa sổ, thanh công cụ, menu và tooltip tự động cập nhật lại tiêu đề và phím tắt thông qua `action_id`.
3. **Bảo tồn Dữ liệu Tài liệu**:
   Đổi ngôn ngữ giao diện tuyệt đối không làm ảnh hưởng đến nội dung chữ (Unicode Text) hoặc thuộc tính màu sắc bên trong tài liệu đang mở.
