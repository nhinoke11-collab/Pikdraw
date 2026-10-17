# Phiếu thiết kế UI trước khi phát triển tính năng

**Agent triển khai tự phân tích, điền và quyết định nội dung phiếu. Không giao người dùng điền hoặc chờ họ duyệt từng lựa chọn.**
Agent sao chép mẫu này vào tài liệu tính năng có tên rõ ràng dưới `docs/design/features/`, hoặc ghi phần tương đương
trong đặc tả đang có rồi liên kết từ task/handoff. Không tạo bản trùng ở nhiều nơi.
Đọc [chuẩn UX/UI](ui-ux-standard.md), đối chiếu UI hiện có, tự chọn phương án phù hợp và ghi lý do ngắn.
Điền mục không áp dụng bằng lý do; tự kiểm đủ nội dung rồi tiếp tục code trong scope, không để quyết định thiết kế trôi nổi khi code.
Phiếu là bản ghi quyết định và cơ sở review, không là form xin phép hoặc cổng ký duyệt bắt buộc trước code.
Mẫu là tài liệu, không phải schema task mới và không phải form runtime.

## 1. Mục tiêu và trạng thái

- Tên tính năng / task ID / module / owner:
- Người dùng cần hoàn thành việc gì, đầu vào và kết quả nhìn thấy:
- Capability hiện có; phần dự kiến; nội dung không hỗ trợ cần báo:
- Tài liệu/source snapshot được dùng để thiết kế; agent quyết định và lý do; reviewer nếu đã được giao; phần còn mở:
- Nếu chỉ là core API: lý do chưa có UI và điểm consumer/UI dự kiến, không tạo nút giả:

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `Menu > Submenu > Action…`, giải thích vì sao thuộc nhóm này |
| Action/command ID | ID ổn định; existing hay proposed; nguồn label/enable state |
| Loại thao tác | Tool có mode / action một lần / toggle / tác vụ bất đồng bộ |
| Standard toolbar | Icon shortcut nào hoặc lý do không đặt |
| Toolbox / flyout | Nhóm tool, vị trí, cách mở bằng chuột và bàn phím hoặc không áp dụng |
| Property bar | Tập thiết lập quan trọng/thường dùng để sửa nhanh, thứ tự và lúc xuất hiện |
| Properties | Bắt buộc với chức năng nhiều thuộc tính/tùy chọn phụ: bảng/section chứa đầy đủ tham số đã hỗ trợ, nhóm cơ bản/nâng cao; chức năng đơn giản ghi lý do nếu không áp dụng |
| Mở bảng thuộc tính | Menu hoặc action mở/focus đúng bảng/section; cách truy cập khi dock đóng hoặc bar hẹp; không bắt bảng luôn mở |
| Context menu / shortcut | Alias của cùng action; phím thực tế hoặc “chưa gán” |
| Dialog/panel mới | Có hay không; lý do không tái dùng bề mặt hiện có; đường mở/đóng |

## 3. Trường thuộc tính

Mỗi field là một dòng; tách giá trị hiển thị, source binding và phạm vi ghi.

| ID / label EN + VI | Bar / Properties / dialog | Kiểu, unit, range, default | Scope và source binding | Khi nào enable / Mixed | Preview, commit, validation |
| --- | --- | --- | --- | --- | --- |
| Điền từng field | | | App / document / new object / selection / draft | | |

- Trường trên bar và Properties cùng hiển thị một giá trị nào; cách đồng bộ khi selection đổi:
- Danh sách thuộc tính/setting phụ chỉ có trong bảng chi tiết; section tương ứng, scope và capability; không để chỉ truy cập qua overflow/dialog rời:
- Bar và Properties dùng chung validation, enable/mixed state, command và Undo như thế nào:
- Property bar quá hẹp: phần nào vào overflow, phần nào phải giữ; đường truy cập thay thế:
- Phân biệt default của đối tượng mới với thuộc tính của selection:

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint / lý do không dùng được |
| --- | --- | --- | --- | --- |
| Không có tài liệu | | | | |
| Có tài liệu, không selection | | | | |
| Một đối tượng hợp lệ | | | | |
| Nhiều đối tượng / mixed values | | | | |
| Đối tượng không phù hợp / locked / read-only | | | | |
| Đang có draft, nhập số hoặc IME | | | | |
| Busy / cancel / error / unsupported | | | | |

## 5. Icon, tên và hướng dẫn

| Nội dung | EN / VI hoặc tham chiếu resource |
| --- | --- |
| Icon SVG | Asset ID/path hiện có hoặc proposed; bộ icon, trạng thái, provenance/license |
| Label ngắn | Menu, button cần chữ, section, field; message IDs |
| Accessible name | Tên đọc được của icon và control không có chữ |
| Tooltip + focus help | Tên, shortcut thực, một câu tác dụng |
| Status hint từng bước | Bắt đầu → đang thao tác → hoàn tất/hủy; modifier đã kiểm chứng |
| Error/disabled reason | Nguyên nhân và cách tiếp tục |
| Help đầy đủ | Đường tài liệu/help ID; menu mở; ví dụ 3–5 bước, tham số, sửa lại/hủy và giới hạn |

Không đặt câu hướng dẫn dài lên button. Không điền F1/shortcut/modifier như thể đã chạy khi chưa có binding.

## 6. Hành vi và bằng chứng nhận

- Bắt đầu / preview / Apply hoặc kết thúc gesture / Cancel-Escape / Undo-Redo:
- Điều gì được lưu trong tài liệu, điều gì chỉ là view/app/tool preference:
- Focus, tab order, nhập số/đơn vị, clipboard và IME; không mất draft khi đổi focus:
- Mở/đóng Properties, đổi selection/tool, đổi document; busy/job cancel nếu có:
- Với chức năng nhiều tùy chọn: ca Given/When/Then cho mở lại đúng bảng/section khi dock đóng hoặc bar hẹp, sửa tham số phụ và đồng bộ bar/Properties; mở/focus bảng không đổi dữ liệu hay tạo Undo:
- Light/dark, scale/DPI, cửa sổ hẹp; cách mở tool/option không cần hover:
- File/link mockup nếu cần; ghi rõ mockup hay ảnh native, context và phiên bản:
- Checklist/test/ảnh native cần chạy khi implementation; kết quả nào đã có, kết quả nào chưa chạy:
- Agent tự kiểm placement/menu/labels; kết quả review khi có, limitation và hành động tiếp theo:

Task chỉ sửa docs có thể giao phiếu và wireframe làm bằng chứng thiết kế;
không đổi trạng thái thành UI implemented hoặc runtime UX passed từ phiếu đã điền.
