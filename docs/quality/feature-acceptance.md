# Quy chuẩn nghiệm thu tính năng

Agent triển khai chịu trách nhiệm biến yêu cầu thành một số ca kiểm tra có thể xác nhận hành vi thực tế. Quy chuẩn này thống nhất cách mô tả ca và chọn phạm vi; nó không tạo một checklist test cố định phải chạy cho mọi thay đổi.

## Khi cần tạo hoặc cập nhật test

- Khi thêm tính năng hoặc sửa lỗi làm thay đổi hành vi, thêm hoặc cập nhật test cho luồng chính mà người dùng hoặc module tiêu thụ. Với bug, test cần tái hiện lỗi trước khi sửa và xác nhận kết quả sau khi sửa.
- Ưu tiên test đi qua điểm sử dụng thật của hành vi. Unit test phù hợp để kiểm tra quy tắc cục bộ, nhưng không thay cho nghiệm thu luồng UI/consumer khi luồng đó đã chạy được trong ứng dụng.
- Dùng test hiện có nếu nó bao phủ đúng hành vi; mở rộng test đó thay vì tạo trường hợp trùng lặp.
- Tài liệu-only, đổi cấu trúc không đổi hành vi và sửa nội bộ không làm đổi kết quả không bắt buộc thêm test nghiệm thu mới.
- Nếu hành vi thực tế chưa thể tự động hóa (ví dụ thao tác UI, thiết bị hoặc máy sản xuất), ghi các bước nghiệm thu thủ công có thể lặp lại và kết quả quan sát được. Không suy diễn nghiệm thu thiết bị từ mô phỏng hoặc test code.

## Dọn file test tạm

Khi nghiệm thu xong, xóa test source tạo chỉ để chạy một lần, fixture thử nghiệm không còn dùng, file output và log tạm. Không xóa regression test tự động thuộc suite của module chỉ vì lần chạy đã hoàn tất: giữ test nếu nó bảo vệ hành vi đã nghiệm thu hoặc có thể bắt lỗi tái diễn. Khi test cũ thật sự dư thừa, chỉ bỏ nếu coverage tương đương còn lại và báo rõ lý do cho người dùng.

Báo kết quả ngắn trực tiếp; không tạo handoff/snapshot/log trung gian. Output đặt trong PIKDRAW_TEST_OUTPUT_DIR do runner cấp hoặc temp directory riêng, tự dọn bằng RAII/finally. Sau lượt lỗi phải đọc chẩn đoán rồi dọn bằng tooling/native/clean-test-artifacts.ps1; không để dump, screenshot hay dữ liệu khách hàng trong repo. Fixture dùng lại thuộc suite được giữ. Xem [native build](../development/native-build.md).

## Cấu trúc ca dùng chung

Mỗi ca nghiệm thu phải thể hiện đủ các ý sau. Có thể thêm trường chuyên ngành khi cần; không cần tạo tài liệu riêng nếu test code hoặc feature spec đã ghi rõ cùng nội dung.

```text
AC-<id> — <hành vi cần xác nhận>
Given: trạng thái ban đầu, fixture và điều kiện liên quan
When: thao tác hoặc input thực tế
Then: kết quả quan sát được và invariant cần giữ
Verify: assertion/oracle, vị trí test và lệnh chạy; hoặc bước thủ công cùng bằng chứng
```

Mỗi ca kiểm tra một hành vi có ý nghĩa. `Then` phải kiểm tra kết quả độc lập với chi tiết triển khai và phải phát hiện được hồi quy liên quan. Với ca tự động, Arrange/Act/Assert có thể hiện thực cấu trúc này. Với ca thủ công, ghi rõ dữ liệu đầu vào, thao tác, kết quả mong đợi và kết quả thực tế.

## Chọn đủ test, không thêm theo quota

- Bắt đầu bằng luồng chính đại diện cho giá trị của tính năng. Một thay đổi nhỏ có thể chỉ cần một ca tốt; không có số ca tối thiểu chung ngoài việc phải có nghiệm thu cho hành vi mới hoặc hành vi đã sửa.
- Thêm biên lỗi, hủy, undo/redo, lưu-mở lại, stale result, quyền hoặc độ chính xác chỉ khi hành vi hay rủi ro đó nằm trong phạm vi thay đổi.
- Chọn tầng kiểm tra gần nhất có thể bắt hồi quy: module/unit cho quy tắc cục bộ; test consumer hoặc app cho tương tác qua ranh giới; kiểm tra format, persistence, security hay thiết bị khi task đổi đúng ranh giới đó.
- Không nhân ma trận mọi tổ hợp dữ liệu, trạng thái và nền tảng nếu chúng không đổi kết luận. Không thêm test chỉ để tăng số lượng, coverage hoặc lặp lại cùng assertion ở nhiều tầng.
- Mở rộng sang suite rộng hơn khi đổi contract dùng chung, chạm nhiều consumer, thay đổi kiến trúc/đường tích hợp, chuẩn bị phát hành hoặc có bằng chứng hồi quy cần điều tra. Task thông thường chạy test mục tiêu và regression liên quan; không mặc định chạy toàn bộ CTest, harness và app suite.

## Vị trí và bàn giao

Đặt test cạnh module sở hữu hành vi theo cấu trúc hiện có (`core/.../tests`, `modules/.../tests`, hoặc `apps/desktop/tests` cho luồng ứng dụng). Dùng test harness/architecture ở cấp repo cho chính các gate đó; không tạo thư mục test mới nếu cấu trúc hiện tại đáp ứng được.

Trong báo cáo trực tiếp cho người dùng, ghi các ca đã thêm/cập nhật, lệnh test chính xác, kết quả, revision đã kiểm tra và giới hạn còn lại. Nếu một test không chạy được, nêu lý do và bước thay thế. Test xanh chỉ xác nhận điều được assertion kiểm tra; không thay bằng chứng UX, hiệu năng, fidelity, containment hoặc nghiệm thu máy thật.
