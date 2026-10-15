# Interop — bản nháp spike C

Đã có private binary IPC DTO/codec và validation bounded: wire version, frame length chính xác, 16 MiB, object/depth/segment budget, số hữu hạn, ID không rỗng/trùng, document epoch/revision/generation/cancel. Từ chối trailing/truncated frames và count không vừa bytes còn lại trước reserve.

Codec hiện chỉ là tập path/RGBA thử nghiệm; không phải source document interchange contract và không thể bảo toàn đầy đủ text/CMYK/spot/ICC/hierarchy. Không nối codec vào import production trước khi có contract và consumer review.

Không có SVG/PDF/AI/CDR/DXF adapter. Capability registry trả UnsupportedReject cho import/export; worker trả lỗi và không tạo geometry giả từ input tùy ý. Reader candidates còn unselected/unpinned, license và dependency audit pending; process boundary không miễn nghĩa vụ giấy phép.

Manifest corpus hiện rỗng: đã bỏ các entries/hash/size do model tạo khi chưa có tệp thật. Unit vectors trong code không thay 20–30 fixtures có quyền sử dụng hoặc chuỗi import-edit-native-save-reopen-export. Gate fidelity/editability/output/license chưa đạt.
