# ADR 0005 — sửa codec native để không mất dữ liệu nguồn

Ngày: 2026-10-15. Trạng thái: quyết định sửa prototype, chờ kiểm chứng bản tích hợp;
**không phải nghiệm thu C5/M1 hoặc cam kết format phát hành**.

## Vấn đề

Codec v1 tại fed91c5 ghi màu qua RGBA float preview, bỏ CMYK/spot/overprint,
font weight/italic/PostScript identity, fill rule và document ID. Header revision không
được checksum. Reader tin count trước allocation. Save có fallback xóa bản cũ sau lỗi rename.
Không được giữ hành vi đó rồi gọi native round-trip thành công.

## Quyết định

Prototype v2 ghi little-endian tường minh, binary64 cho toàn bộ màu/toạ độ nguồn,
color tag RGB/CMYK/spot/gray cùng overprint/spot alternate, toàn bộ FontIdentity,
path verbs/radii/winding, transforms, visibility/locks, layer/group/order và document ID.
Không thêm metadata giả cho những gì model chưa biểu diễn (assets/ICC bindings/timestamps).

Header 36 byte: magic PKDR (4), version u32=2, reserved flags u32=0, epoch u64,
revision u64, payload length u32, CRC32 u32. Checksum phủ toàn container với 4 byte
checksum ở offset 32 đặt zero khi tính. Payload bắt đầu bằng document ID UTF-8 length-prefixed,
page dimensions, layers và typed objects. Codec phân nhóm byte/color/object/container/file I/O.
Version/tag/boolean/number/count/length/duplicate ID/hierarchy sai bị từ chối toàn bộ.

Giới hạn hiện tại: 256 MiB/file, 100.000 objects, 1.000.000 path elements tổng,
10.000 layers, 16 MiB/string, group depth 256 theo structure validator. Đây là giới hạn
bảo vệ prototype có test, chưa là budgets được nghiệm thu trên máy xưởng.

v1 lossy chưa phát hành bị từ chối có diagnostic, không đoán khôi phục metadata đã mất.
Khách hàng phải giữ nguồn gốc. Trước phát hành cần khóa schema/migrations và đối chiếu lại C5;
v2 hiện chưa có đủ metadata/container model mô tả trong C5 1.0.0.

Lưu trên Windows dùng CreateFileW/CREATE_NEW ở cùng thư mục, WriteFile lặp kiểm tiến độ,
FlushFileBuffers, đóng handle rồi ReplaceFileW (file đã có) hoặc MoveFileExW/WRITE_THROUGH
(file mới). Không fallback delete-target. Lỗi serialization/write/flush/replace trả failure,
chỉ dọn temp được chính lần gọi tạo. Path API nhận UTF-8 và đổi qua filesystem UTF-16.
POSIX có fsync/rename/directory fsync nhưng chưa được kiểm trên máy này.

Recovery checkpoint giữ document ID trong container; filename chỉ mang FNV-1a 64-bit lookup hint
có độ dài cố định để tránh path traversal và tên file quá dài. Reader luôn xác minh document ID
đầy đủ từ container trước khi trả kết quả hoặc xóa file. Chọn file hợp lệ mới theo mtime thay vì
so revision qua các lifecycle. Chưa có autosave scheduler,
recovery index/provenance UI, quota retention hoặc bằng chứng mất điện.

## Bằng chứng và hệ quả

Fault injection AfterWrite/BeforeFlush/BeforeReplace phải giữ bytes file cũ và dọn temp.
CRC32 có known-answer vector; header mutation/truncation/size abuse phải fail.
Native mở lại phải dựng Document mới với epoch mới và vẫn chỉnh sửa/undo được.
Allocator epoch không quay vòng; nếu epoch từ file trùng giá trị vừa cấp thì cấp giá trị tiếp theo.
Xem tests/storage/test_storage_fidelity.cpp và báo cáo tích hợp độc lập cho kết quả thực tế.
Flush/atomic replacement không tự chứng minh sống sót mọi lỗi nguồn điện/firmware/filesystem.
