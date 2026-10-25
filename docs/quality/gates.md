# Kiểm tra chất lượng PikDraw

Chọn kiểm tra theo phần code bị đổi và rủi ro thực tế. Đây không phải checklist phải chạy nguyên vẹn cho mọi task.

Với tính năng hoặc bugfix đổi hành vi, agent sở hữu phần thay đổi tự thêm hoặc cập nhật test nghiệm thu cho luồng dùng thật. Mọi ca dùng chung cấu trúc Given/When/Then; số lượng và tầng test được chọn theo rủi ro. [Quy chuẩn nghiệm thu tính năng](feature-acceptance.md) định nghĩa mẫu và cách chọn phạm vi; không yêu cầu chạy full suite theo mặc định.

Sau khi chạy test, dọn test source dùng một lần cùng fixture, output và log tạm. Giữ test hồi quy còn giá trị trong suite chuẩn; báo bằng chứng ngắn trực tiếp, sau đó dọn artifact tạm theo AGENTS.

## Luôn ghi rõ

- Task, source snapshot, paths đã đổi và checks thực sự đã chạy.
- Điều gì đã làm, điều gì còn thiếu và giới hạn có thể ảnh hưởng người dùng.
- Chỉ cập nhật project state khi thay đổi đã được tích hợp hoặc trạng thái đang ghi được phân biệt rõ.

## Khi thay đổi document, geometry hoặc rendering

Kiểm tra đúng hành vi và các invariant liên quan: IDs, đơn vị, undo/redo, source geometry, revision và cancellation. Dùng oracle độc lập cho precision, topology hoặc pixel semantics khi thay đổi có thể ảnh hưởng chúng. Không cần chạy suite không liên quan chỉ để tăng số.

## Khi thay đổi định dạng hoặc worker

Nêu rõ format/version/feature đã hỗ trợ. Tách khả năng nhìn giống, khả năng chỉnh sửa và khả năng round-trip. Unsupported data cần được giữ, báo mất mát hoặc từ chối rõ. Worker result phải được kiểm tra trước khi document nhận lệnh. Khi đổi sandbox/importer, kiểm tra quyền file, mạng và child process phù hợp với môi trường thực.

## Khi thay đổi giao diện hoặc hiệu năng

Với UI, review đường truy cập, action, các trạng thái người dùng gặp và thao tác thật trong app; một phiếu hay mockup không chứng minh runtime UX. Với hiệu năng, ghi workload, máy, build và số đo thực; ảnh chụp hoặc FPS trung bình không chứng minh input-to-present latency.

## Khi chuẩn bị phát hành

Build và chạy các regression liên quan trên source tích hợp; kiểm tra license, định dạng công bố, known limits và dữ liệu lưu/mở lại. Chỉ gọi một gate là pass khi có bằng chứng đúng revision.

Tuân thủ quy chuẩn độ dài file <= 2000 dòng (khuyến nghị 300–1800 dòng/file) theo `docs/development/code-organization.md`. Không nối đuôi tính năng mới vào các file đã vượt 2000 dòng.
