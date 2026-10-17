# Bố cục workspace của editor PikDraw

Ngày: 16/10/2026. Trạng thái: **đặc tả bố cục đích, chưa nghiệm thu UI runtime**.
[Chuẩn UX/UI](../design/ui-ux-standard.md) quyết định menu, placement, tương tác và help;
[phiếu UI](../design/feature-ui-template.md) ghi phần riêng của từng tính năng.
Tài liệu này chỉ giữ phân vùng, density và quy tắc dùng chung để không có nhiều design system cạnh tranh.

## 1. Khung editor

```text
+--------------------------------------------------------------------------+
| Title: PikDraw - document name / modified state                 [min/max/x]|
+--------------------------------------------------------------------------+
| Menu: File Edit View Layout Object Effects Bitmaps Text Tools Settings ... |
+--------------------------------------------------------------------------+
| Standard toolbar: [New] [Open] [Save] | [Undo] [Redo] | Zoom / view controls |
+--------------------------------------------------------------------------+
| Context property bar: target | geometry | appearance | active-tool options |
+----+---------------------------------------------------------+-----------+
|Tool| Document tabs / ruler                                   | Properties|P
|box +---------------------------------------------------------+           |a
|    | ruler |                                                 | Transform |l
|    |       |        CANVAS / PAGE / PASTEBOARD                | Appearance|e
|    |       |                                                 | Tool/type |t
|    |       |                                                 | Advanced  |t
|    |       |                                                 |           |e
+----+-------+-------------------------------------------------+-----------+
| Page navigation / document palette when applicable                        |
+--------------------------------------------------------------------------+
| Status hint for active tool/focus | selection / coordinates / color / zoom |
+--------------------------------------------------------------------------+
```

Đây là sơ đồ trách nhiệm, không là danh sách button có chữ trên runtime. Toolbar/toolbox dùng icon theo chuẩn;
menu cấp cao chưa có nội dung không dựng rỗng. Palette là bề mặt màu gọn, không phải dock bắt buộc luôn mở.
Các panel khác dùng tab/dock bên phải hoặc mở theo nhu cầu; không thu canvas để mở tất cả một lúc.

## 2. Vị trí và khả năng thích ứng

| Vùng | Trách nhiệm và cách dùng |
| --- | --- |
| Title bar | Tên app, tên tài liệu, trạng thái sửa/lưu có thật; không có badge PRO hoặc saved pulse giả |
| Menu bar | Lệnh có nhóm rõ theo chuẩn; giữ tên và thứ tự ổn định giữa các tool |
| Standard toolbar | Icon của lệnh thường dùng; có separator nhóm, overflow khi thiếu chỗ |
| Context property bar | Một vùng cố định dưới thanh lệnh; nội dung theo selection/tool, không phát sinh bar mỗi feature |
| Toolbox trái | Một cột tool chính, flyout cho họ công cụ; active/focus và cách mở nhóm rõ |
| Document area | Tab tài liệu, thước, trang và pasteboard; canvas là vùng làm việc lớn nhất |
| Properties phải | Một bảng theo context, section mở/thu; panel khác dùng tab. Inspector trong code không là bảng thứ hai |
| Palette | Swatch gọn theo capability, có trạng thái None và mixed đủ rõ; không đổi dữ liệu màu chỉ vì chọn tab palette |
| Page navigation | Trang hiện tại và cách chuyển/thêm trang khi đã hỗ trợ; tách khỏi tab tài liệu |
| Status bar | Hint ngắn và thông tin cần dùng; phần chi tiết chuyển Help, không nhét form/log vào đáy |

Không gộp menu, tên file, tìm kiếm, workspace switcher, quảng cáo và nút Export lớn vào một thanh tiêu đề chật.
Search/workspace/theme shortcut chỉ đặt khi đã có capability và không làm lấn các nhóm editor chính.
Workspace chỉ đổi view/toolset; không thay document, selection hoặc lịch sử một cách ngầm định.
Production/3D chưa có workflow không được trình bày như đang dùng được.

## 3. Density và kích thước

Các số sau là **baseline thiết kế PikDraw theo DIP**, không phải tiêu chuẩn do Corel/Adobe công bố,
không phải pixel cố định phải chép từ ảnh và không sửa token runtime trong task docs.

| Thành phần | Baseline để thiết kế/review |
| --- | --- |
| Nhãn/chữ UI | Font hệ thống dễ đọc, thường 12–13 DIP; số có tabular digits nếu font hỗ trợ |
| Icon toolbar/toolbox | 16 hoặc 20 DIP theo token chung; một hệ viewBox/nét nhất quán |
| Vùng bấm icon bằng chuột | Tối thiểu 24×24 DIP, ưu tiên khoảng 28–32 DIP theo density; touch profile cần spec riêng |
| Menu / toolbar | Mỗi loại một hàng gọn; field X/Y hoặc W/H có thể xếp cặp trong nhóm, không làm các bar tự nở dần |
| Toolbox | Khoảng 32–40 DIP cho cột đơn, tùy control/padding chung |
| Properties | Khoảng 280–340 DIP ban đầu, resize được; nội dung theo section, không ép theo phần trăm cửa sổ |
| Status bar | Đủ một dòng chữ và control có focus; không cố giữ 26 pixel bằng cách cắt chữ |
| Khoảng cách | Bước 4/8 DIP và nhóm có separator/spacing; không tự bịa padding cho mỗi feature |

Cửa sổ hẹp: chuyển action phụ vào overflow, thu/collapse panel không cần thiết, giữ unit và trường đang thao tác
truy cập được. Không thu font, cắt Apply/Cancel hoặc cho bar tràn lên canvas. Property bar và Properties dùng
cùng dữ liệu; vị trí bị thu gọn vẫn có đường mở lại qua menu/keyboard. Không khóa canvas ở tỉ lệ 65–75% cứng.

Thiết kế kiểm ở 1366×768 và 1920×1080, cùng scale 100/125/150/200% theo môi trường có thể kiểm.
Thiếu thiết bị/độ phân giải nào thì ghi chưa kiểm, không suy rộng pass từ một screenshot.

## 4. Theme, typography và icon

Dùng màu nền/panel trung tính, phân tầng vừa đủ, chữ và selection/focus dễ thấy.
Ảnh CorelDRAW sáng là tham chiếu bố cục và mật độ; không phải yêu cầu đổi theme đang lưu của người dùng.
Light/dark theo Settings và một bộ token chung; không để tính năng tự đặt palette riêng.
Một accent chủ đạo phục vụ active/selection/focus, không mặc định glow, neon, gradient hoặc animation trang trí.
None, warning/error, mixed và disabled có tín hiệu đủ rõ ngoài màu đơn lẻ.

Ưu tiên font hệ thống; không yêu cầu tải Plus Jakarta Sans/JetBrains Mono hoặc font web chỉ để giống HTML cũ.
Artwork/font của tài liệu tách khỏi font UI. Dùng SVG icon có nguồn và license phù hợp,
không dùng emoji, chữ cái tool hoặc icon thương hiệu sao chép từ ảnh.
Giữ nhãn field/section dễ đọc, tooltip/focus help và accessible name như chuẩn UX/UI.

## 5. Nguồn sự thật và runtime

- [Chuẩn UX/UI](../design/ui-ux-standard.md) là nguồn duy nhất cho phân loại lệnh, bề mặt và help.
- [Phiếu UI](../design/feature-ui-template.md) mô tả field/state/commit và layout của feature trong khung này.
- Runtime hiện tại là Win32/GDI theo [project-state](../project-state.json); Qt/QSS/UI Reload là hướng kiến trúc
  có phạm vi và evidence riêng. Khung này không quyết định toolkit hoặc cấp phép đổi stack.
- Native UI vẫn đi qua action/presenter và command document, immutable snapshots; không đưa renderer/UI type vào storage/plugin contract.
- English mặc định, Vietnamese tiếp theo qua catalog; thay ngôn ngữ không sửa tài liệu/production data.

Task chỉ sửa docs không tạo icon, sửa form/QSS/paint code hoặc dựng bản build mới. Khi triển khai,
review cần ảnh native và thao tác đúng source snapshot; ảnh wireframe/HTML không chứng minh UI đã chạy.
