# ADR 0003: Kiến trúc lưu trữ Native và Độ bền tệp (Storage Durability)

- **Trạng thái**: Accepted (Khóa tại M0 Foundation)
- **Ngày**: 2026-10-15
- **Người đề xuất**: Gemini 3.8 Flash High & Codex Coordinator
- **Phạm vi**: `modules/storage`, `core/document`

---

## 1. Bối cảnh và Thách thức

Một phần mềm đồ họa vector chuyên nghiệp dùng trong xưởng in và thi công biển bảng lớn không được phép làm hỏng hoặc mất file của khách hàng khi xảy ra sự cố đột ngột (như mất điện xưởng, sập nguồn, crash driver đồ họa hoặc treo máy).

Các lỗi phổ biến ở các phần mềm truyền thống:
- Ghi đè trực tiếp lên file gốc: Khi đang ghi dở nửa chừng mà bị mất điện, file bị cụt (truncated) và hỏng hoàn toàn.
- File tạm không dọn dẹp: Rác file `.tmp` ngổn ngang làm người dùng bối rối.
- Autosave ghi đè lên file làm việc khiến người dùng mất quyền Undo về trước phiên mở.

---

## 2. Quyết định Kiến trúc

1. **Giao thức lưu nguyên tử (Atomic Replace Protocol)**:
   - Dữ liệu luôn được ghi vào một file tạm độc lập nằm cùng thư mục: `<file>.pr.tmp.<PID>.<Epoch>`.
   - Gọi hàm `FlushFileBuffers` trên Windows trước khi đóng handle để ép dữ liệu từ bộ nhớ đệm ổ cứng xuống bề mặt vật lý.
   - Hoán đổi nguyên tử vào đường dẫn đích bằng Win32 `ReplaceFileW`.
   - Nếu có bất kỳ lỗi nào xảy ra trong quá trình ghi, file tạm bị hủy ngay lập tức và file gốc giữ nguyên 100%.

2. **Chỉ số phục hồi sự cố (Autosave & Crash Recovery)**:
   - Các bản lưu tự động diễn ra theo chu kỳ (mặc định 3 phút) nếu tài liệu có cờ dirty.
   - Bản lưu tự động ghi vào thư mục cách ly `%APPDATA%/PikDraw/Recovery/<UUID>/` với chỉ mục `recovery_index.json`.
   - Không ghi đè lên file làm việc của người dùng.
   - Khi ứng dụng tắt sạch hoặc đóng tài liệu bình thường, các file recovery tương ứng được dọn dẹp sạch sẽ.

3. **Toàn vẹn và Định danh Phiên bản (Integrity & Versioning)**:
   - Magic Header chuẩn 8 bytes: `PKDR\x01\x00\x00\x00`.
   - Đuôi file lưu trữ mã kiểm tra CRC32 hoặc SHA-256.
   - Tệp mang `format_version` và `min_compat_version` rõ ràng để hỗ trợ nâng cấp phiên bản trong tương lai.
