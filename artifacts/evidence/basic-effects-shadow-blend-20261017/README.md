# Ảnh nghiệm thu Shadow Blend và sửa node hiện có

Hai ảnh 1440×900 được tạo từ luồng capture NativeWindow Win32/GDI trong `test_pikdraw_effect_draft`.

- `drop-shadow-edit-existing-english.bmp`: Properties hiển thị Drop Shadow hiện có, các giá trị offset/sigma/màu/opacity, sáu chế độ Shadow Blend và status “Editing existing Drop Shadow · no new effect node”.
- `drop-shadow-edit-existing-vietnamese.bmp`: cùng trạng thái bằng tiếng Việt, status “Đang chỉnh Drop Shadow hiện có · không tạo node mới”.

Ca kiểm tra trước đó áp dụng Multiply lên node, lưu lại ID, gọi lại action Drop Shadow và xác nhận ID/thông số/số node/revision không đổi. Ảnh xác nhận đường vẽ UI và nội dung hiển thị; chúng không phải ảnh chụp desktop thủ công, cũng không thay thế kiểm tra tương tác OS, DPI hay accessibility.
