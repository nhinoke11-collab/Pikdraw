# Bố cục workspace PikDraw

Đây là blueprint giao diện desktop, chưa phải ứng dụng có thể dùng. [Bản vẽ SVG](workspace-blueprint.svg) minh họa tỷ lệ và vùng chức năng; không quy định pixel cứng cho mọi màn hình. Preview PNG không phải artifact được phân phối trong Git.

## Khung chung

```text
┌──────────────────── Menu / tìm lệnh / tài liệu ─────────────────────┐
│ Vector | In ấn | Thi công | 3D       Tự lưu · tiến độ tác vụ         │
├──────────────── Thuộc tính theo công cụ/selection ─────────────────┤
│ Tool  │ Tab tài liệu · ruler                            │ Inspector │
│ strip │                                                 │ theo ngữ  │
│       │              CANVAS / VIEWPORT                   │ cảnh      │
│       │        chiếm phần lớn không gian làm việc         │           │
│       │                                                 │ Layers /  │
│       │                                                 │ Assets    │
├───────┴─────────────────────────────────────────────────┴───────────┤
│ Trang / màu / tùy chọn dock      Tọa độ · mm · zoom · màu · cảnh báo │
└─────────────────────────────────────────────────────────────────────┘
```

Canvas là vùng chính. Với màn hình desktop 1440 × 900, điểm khởi đầu là canvas khoảng 65–75% bề ngang, thanh công cụ trái 44–56 px và dock phải 260–320 px. Đây là tỷ lệ thử nghiệm, cần kiểm tra DPI, tiếng Việt, màn hình nhỏ và thao tác thực tế trước khi chốt design tokens.

Menu chứa command ổn định; thanh tìm lệnh truy cập tất cả chức năng đã đăng ký. Workspace switcher đổi bộ công cụ và panel, giữ nguyên tài liệu, selection và lịch sử. Thanh thuộc tính hiển thị trường liên quan trực tiếp đến công cụ/selection; Inspector chứa phần sâu hơn.

Dock phải ưu tiên một panel chính và một vùng Layers/Assets thu gọn. Panel có tab, resize, pin, collapse và khôi phục layout. Không mở đồng thời tất cả panel của vector, in, CAD và 3D. Toolbar mở rộng theo menu hoặc flyout; tooltip có phím tắt và tên dễ hiểu.

Thanh trạng thái hiển thị tọa độ, đơn vị, zoom, trạng thái profile màu, cảnh báo lựa chọn và tác vụ đang chạy. Hủy tác vụ nằm cạnh tiến độ, không buộc người dùng mở hộp thoại riêng. Chỉ báo “đã lưu” khi snapshot đã ghi thành công còn tương ứng revision hiện tại; save cũ hoàn tất không làm sạch edits mới. Preview giảm chất lượng/software và kết quả cần tính lại được báo bằng trạng thái dễ hiểu.

## Workspace theo công việc

| Workspace | Công cụ ưu tiên | Inspector/context dock | Output |
| --- | --- | --- | --- |
| Vector | Select, node, pen, shapes, text, fill, outline, Boolean | Transform, text, fill/stroke, effects, pages/layers | Native/SVG, bản duyệt |
| In ấn | Page/scale, bleed, chia khổ, preflight | Profile màu, ảnh/DPI, khổ vật liệu, vùng in | PDF theo preset được kiểm chứng |
| Thi công | Snap, dimension, holes, cut/engrave, parts | Đơn vị, vật liệu, độ dày, số lượng, lỗi đường, bảng kê | DXF, bản vẽ và danh sách chi tiết |
| 3D | Extrude, orbit, pan, material, camera | Chiều dày, liên kết đường nguồn, scene tree, camera | Preview, ảnh duyệt, dữ liệu 3D theo phạm vi hỗ trợ |

Chỉnh ảnh cơ bản xuất hiện theo selection hoặc task-specific panel trong Vector/In ấn. Có thể thêm workspace Raster khi lượng công cụ tăng; không cần mở một cửa sổ sửa ảnh độc lập ngay từ đầu.

Trang 2D và scene 3D có tab/view riêng trong cùng document. Không áp mọi thao tác 2D lên mesh hoặc ngược lại. Selection/command phải công bố loại đối tượng hỗ trợ và vô hiệu hóa có giải thích khi không phù hợp.

## Những luồng phải thiết kế trước

**Nhập file:** chọn file → hiển thị progress có hủy → báo cáo thành phần hỗ trợ/chuyển dạng/thiếu tài nguyên → mở tài liệu → giữ liên kết báo cáo để tra lại. Với font thiếu, hiển thị bản xem trước và lựa chọn thay thế; không thay font âm thầm rồi ghi đè nguồn.

**Xuất sản xuất:** chọn preset → preflight → lỗi có vị trí chọn trực tiếp trên canvas → sửa hoặc ghi nhận lựa chọn → xuất → ghi output revision. Khổ/đơn vị/tỷ lệ và đường cắt phải nằm trong bước kiểm tra dễ đọc, không ẩn ở một nhóm tùy chọn chung.

**Tác vụ tính lâu:** chỉnh source/hủy/đóng tài liệu khi task đang chạy → kết quả lỗi thời không được áp dụng → trạng thái cho chạy lại với source mới. Tool preview khi kéo không tự trở thành đường sản xuất. Pan/zoom/selection giữ phản hồi trong lúc worker chạy.

**Plugin:** danh sách có publisher, version, trạng thái và phạm vi quyền; action xuất hiện trong command registry. Plugin thiếu/lỗi được báo trong khu vực task/plugin status. Dữ liệu plugin chưa đọc được vẫn có mặt trong tài liệu và không bị xóa khi lưu.

**Phục hồi:** danh sách checkpoint có thời gian và tên tài liệu → preview → mở thành bản phục hồi riêng → cho người dùng lưu/so sánh. Tránh ghi đè file gốc trước khi xác nhận lưu.

## Design system và mở rộng

Vòng lặp triển khai dự kiến là **shell Qt Widgets dùng lại cho sản phẩm**, với forms `.ui`,
QSS và icon external trong dev. Build host lần đầu; Reload UI chỉ thay panels/style và rebind
actions, giữ window/viewport/document/history/services. Sửa C++ hoặc custom widget vẫn build
tăng dần. HTML có thể là bản tham chiếu bố cục; chưa có native preview chạy được hiện tại.

Chi tiết lifecycle, widget allowlist, rollback UI lỗi, DPI/keyboard và release resources ở
[kế hoạch sau spike](../roadmap/post-spike-implementation.md), task `ps05-native-preview`.
Task `ps13-vertical-slice` thay fixture presenters bằng services thật. Mock/screenshot không
thay E2E; không tải native plugin ngoài qua form để đi vòng process isolation.

- Semantic tokens cho màu, spacing, typography, focus, stroke, trạng thái lỗi/cảnh báo và docking; token không đặt theo tên feature cụ thể.
- Light/dark dùng cùng hierarchy và contrast. Màu nền UI không được thay đổi quản lý màu của artwork.
- Đơn vị nhập liệu có suffix, cho phép biểu thức đơn giản nếu có đặc tả rõ, báo lỗi tại field; không tự làm tròn dữ liệu nguồn theo số chữ số hiển thị.
- Keyboard navigation, focus ring, shortcut editor, screen-reader names, DPI và văn bản tiếng Việt là yêu cầu nền tảng.
- Plugin dùng host components và command model chung. Chỉ được chiếm vùng canvas/workspace khi có extension point được review; không vẽ menu/chrome riêng tùy tiện.
- Lưu layout theo người dùng/workspace với schema version và nút reset. Thêm plugin hoặc nâng cấp không được khiến layout cũ không mở được.

Mỗi tính năng mới nộp command, selection types, icon token, empty/loading/error states, shortcut và vị trí workspace. Nếu chỉ thêm chức năng vào menu, phải giải thích được người dùng tìm thấy nó trong luồng nào. Screenshot/mockup dùng dữ liệu mẫu, không thay thế kiểm thử bàn phím, tốc độ và tài liệu lớn.
