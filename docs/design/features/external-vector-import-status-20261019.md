# Mở và nhập CDR, AI, PDF, EPS — trạng thái capability

> Snapshot lịch sử ngày 19/10/2026. Số liệu RAM/quota trong phần dưới mô tả snapshot đó; chính sách hiện hành là không quota staging byte cố định, chờ ở ngưỡng 95% và timeout cộng dồn tối đa 5 phút. Xem [kế hoạch import hiện hành](../../roadmap/import-fidelity-performance-20261022.md).

## 1. Mục tiêu và trạng thái

- Tính năng: mở/nhập định dạng vector ngoài PikDraw; module desktop + interop + worker.
- Mục tiêu người dùng: đưa hình học, layer, chữ, màu và hiệu ứng có thể sửa vào tài liệu PikDraw.
- Trạng thái triển khai: PDF và AI có PDF-compatible representation được đọc trong worker cô lập thành page set path/text/ảnh raster native theo subset PDFium; desktop nhập mọi trang kể cả trang rỗng, còn C6 adapter có thể chọn một trang. Geometry được chuẩn hóa sang hệ tọa độ PikDraw (Y hướng lên), áp dụng page box và rotation 0/90/180/270; text giữ hướng theo rotation trang. PDF object có optional-content mark được gán theo tên khi PDFium export API cung cấp; nếu chỉ có một OCG nhưng không có membership trên object, adapter gán tên đó cho supported objects và báo `pdf-single-ocg-layer-assignment-assumed`. Nhiều OCG chưa được ánh xạ membership chính xác. Plain DSC EPS dùng profile native `eps-path-v1` cho allowlist path/color/transform và quy tọa độ theo bounding box. DOS EPS được diễn giải bởi Ghostscript cài ngoài app trong AppContainer (network không cấp, child process bị cấm), ghi PDF vào thư mục tạm riêng rồi đọc bằng PDFium. Ghostscript không được bundle; DOS EPS cần bản cài hệ thống đọc được. CDR RIFF/ZIP dùng libcdr 0.1.9 và librevenge 0.0.6 trong worker, chuyển qua SVG rồi tạo path/text/group, giữ tên/membership layer khi có Layer ID nhận diện được và `ImageObject` native cho bitmap PNG/JPEG/TIFF/BMP nhúng. Transform root/group/object từ SVG của libcdr được áp dụng khi subset biểu diễn được. Raster placeholder giữ affine transform của bốn góc ảnh; các placement dùng chung asset giữ chung pixel RGBA bất biến, còn bounds/ID riêng. Preview RGBA8 bảo toàn alpha channel có sẵn và PNG `tRNS` cho RGB24/grayscale/indexed; bytes ảnh gốc được lưu riêng để không hạ độ phân giải nguồn. Không lấy preview/thumbnail thay nội dung.
- PDF/AI đang ở mức prototype subset: path/text/ảnh theo kiểu object và transform được hỗ trợ, form child bị làm phẳng có diagnostic. Shading, transform/text style hoặc object ngoài ánh xạ bị từ chối proposal. Object-level rotated/sheared text vẫn bị từ chối; layer/effect/clipping/color-management nguồn và AI authoring data không được giữ đầy đủ; người dùng phải xác nhận subset trước khi commit.
- Effects: PDF báo `pdf-effects-and-clipping-not-mapped`; transparency được chẩn đoán nhưng blend/soft-mask/clipping graph chưa được ánh xạ sang effect/clipping native. EPS path subset không có object model cho live effects/layers; DOS EPS là kết quả diễn giải PostScript qua PDFium. libcdr-to-SVG không cung cấp đủ Corel authoring graph để dựng lại mọi live effect. Đây là loss được báo trước khi Apply, không phải fidelity đã đạt.
- RAM: CDR/PDF/EPS/SVG/DXF vẫn dùng guard RAM vật lý toàn máy ở mức 90%; proposal dở dang không commit. Raster `raster-rgba8-v1` dùng quota file 128 MiB, decoded RGBA8 192 MiB, worker 640 MiB và preflight 768 MiB RAM khả dụng; Windows WIC copy theo dải và raster proposal phát record tuần tự. Hash stream được tính tăng dần; payload được giải phóng sau khi gửi/giải mã và raster của asset dùng chung được chia sẻ. Còn giới hạn giao thức/biểu diễn như frame, record, object/segment count, timeout và giới hạn cấp phát 32-bit.
- Phần còn mở: map đủ PDF OCG membership/visibility, effects/clipping/color management, Corel live effects/PowerClip/ICC và nguồn provenance; đo visual/coordinate fidelity với oracle Acrobat/Corel từng phiên bản; save/reopen và export round-trip; UI runtime. Không thể khôi phục authoring semantics không có trong SVG trung gian của libcdr.
- Tài liệu đối chiếu: `docs/architecture/vector-resource-interoperability.md`, `contracts/c8-import-fidelity.md`, `contracts/c9-appearance-effects.md`, capability matrix runtime trong `modules/interop`.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định |
| --- | --- |
| Menu chính | `File > Open…` để mở tài liệu; `File > Import…` để thêm nội dung vào tài liệu đang mở. |
| Action ID | `action.file.open` và `action.file.import`, dùng handler và enable state hiện có. |
| Loại thao tác | Chọn file và chạy import bất đồng bộ. |
| Standard toolbar | Giữ nút Open hiện có; không thêm nút Import. |
| Toolbox / flyout | Không áp dụng; đây là thao tác tài liệu, không phải công cụ vẽ. |
| Property bar | Không áp dụng; import không sửa trực tiếp thuộc tính selection. |
| Properties | Không áp dụng khi chưa có đối tượng nguồn được nhận vào tài liệu. |
| Mở bảng thuộc tính | Không áp dụng. |
| Context menu / shortcut | Dùng action hiện có: Ctrl+O và Ctrl+I. |
| Dialog/panel mới | Không thêm panel. Open/Import picker có nhóm PDF/AI subset, EPS path subset và CDR subset. PDF/AI/EPS/CDR luôn hiện dialog xem lại loss, kể cả Open; Apply nhập native object subset, Cancel không commit. |

## 3. Trường thuộc tính

Không thêm field vào property bar hoặc Properties: chúng chỉ sửa selection đã commit. PDF/AI tạo một layer làm việc tên `PDF Page 1`, EPS tạo `EPS Artwork`; không profile nào tuyên bố đây là layer nguồn. Path, text PDF, màu RGB/CMYK khi adapter đọc được, kích thước trang và vị trí có thể sửa bằng công cụ/Properties sẵn có. Effect, clipping, ICC, overprint, liên kết ảnh và metadata authoring chưa được ánh xạ.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Hành vi |
| --- | --- |
| Không có tài liệu | Open PDF/AI/EPS/CDR tạo proposal trong worker; người dùng xem loss trước commit. Apply tạo tài liệu theo kích thước trang nguồn trong cùng undo unit. |
| Có tài liệu, không selection | Import PDF/AI/EPS/CDR áp dụng path/text/group/ảnh nhúng trong subset vào tài liệu hiện tại bằng một transaction, giữ nguyên khổ trang hiện tại. |
| Selection | Selection không đổi trong lúc đọc; sau khi commit selection được clear theo hành vi import hiện tại. |
| Busy / cancel / error / unsupported | Hủy, lỗi hoặc từ chối proposal không thêm ảnh preview, object, layer hay Undo unit. CDR version/object ngoài libcdr/SVG subset và EPS operator ngoài profile bị từ chối toàn bộ. |

Khi `File > Open…` hoặc `File > Import…` một CDR thành công, canvas căn vừa bounding box nội dung để các object nằm ngoài page vẫn nhìn thấy. PikDraw giữ nguyên page size đích và tọa độ nguồn; không tự dời artwork vào trang. Nếu nội dung vượt page, status báo rõ và phím `F` vẫn căn vừa page.

## 5. Icon, tên và hướng dẫn

| Nội dung | Quyết định |
| --- | --- |
| Icon SVG | Dùng icon Open/Import hiện có; không thêm asset. |
| Label ngắn | Giữ `Open…` / `Mở tệp…` và `Import…` / `Nhập tệp…`. |
| Accessible name | Dùng accessible name hiện có của action. |
| Tooltip + focus help | Giữ tooltip hiện có; không gán phím mới. |
| Status hint | Dùng hint progress/cancel hiện có; CDR Open/Import báo nội dung vượt trang khi có và cho biết `F` căn vừa page. Các format khác báo `Opened file` hoặc `Imported N objects`. |
| Error/disabled reason | PDF/AI/EPS/CDR subset dialog liệt kê giới hạn; parser/converter từ chối source ngoài profile và tài liệu không đổi. DOS EPS cần Ghostscript được cài; lỗi sandbox/conversion sẽ hiện ra trước commit. CDR text runs khác cỡ/font có thể được gộp thành một text object theo font đầu tiên. |
| Help đầy đủ | Tooltip/focus help của action Open/Import hiện có; chưa có help chi tiết theo format. |

## 6. Hành vi và bằng chứng nhận

- PDF profile `pdf-page-set-v3` và AI profile `ai-pdf-compatible-page-set-v3` tạo page set proposal path/text/ảnh, giữ family name cùng PostScript base name khi PDFium cung cấp, và báo thiếu font identity thay vì tự gán Arial. Native text nhận PostScript name để typography resolver kiểm font face. Bytes font nhúng, glyph/source layout và cảnh báo font chưa cài chưa được nối; AI private data không được đọc. Desktop nhập mọi trang mặc định; UI chưa có chọn trang/range. Profile thumbnail cũ và vector profile v1/v2 cũ bị worker từ chối.
- AC-01: Given PDF một trang chỉ có path/text trong subset, When Open/Import, Then preview dialog báo giới hạn; Apply tạo path/text native trong một undo unit, Cancel giữ nguyên revision.
- AC-02: Given AI PDF-compatible một trang, When Import, Then tạo path/text native và báo rõ AI authoring data/layer/effect không được giữ.
- AC-03: Given PDF nhiều trang hoặc có ảnh/form được hỗ trợ, When Open/Import, Then mọi trang (kể cả trang rỗng) được nhập thành page set, ảnh thành ImageObject, form child được làm phẳng và có diagnostic. Given shading/object chưa hỗ trợ, Then worker từ chối proposal toàn bộ, không chèn thumbnail hay object một phần.
- AC-04: Given CDR thật nằm trong profile libcdr, When Open/Import và Apply, Then worker tạo path/text/group native editable, ánh xạ Layer ID cấp cao khi có, biến bitmap nhúng thành `ImageObject` theo từng placement (cùng asset dùng chung pixel), không dùng thumbnail thay nội dung, và report có loss; Undo khôi phục snapshot trước đó. Given ZIP chỉ có preview hoặc CDR ngoài profile, Then từ chối toàn bộ và snapshot không đổi.
- AC-07: Given CDR có object nằm ngoài page nguồn, When Open hoặc Import và Apply, Then PikDraw căn view vào bounds artwork, giữ nguyên page đích và tọa độ object, status báo nội dung vượt trang; nhấn `F` trở lại view page. Verify bounds/camera trên snapshot trước và sau phải cho thấy geometry không đổi.
- AC-05: Given plain EPSF có path/fill/stroke trong `eps-path-v1`, When Open/Import, Then tạo path native và fidelity report; Open/Import có một undo unit, Cancel không commit.
- AC-06: Given DOS EPS và Ghostscript hệ thống có sẵn, When Open/Import, Then PostScript được diễn giải trong AppContainer (không network/child process), chuyển qua PDF và ánh xạ theo subset PDFium thành object native; file gốc không đổi. Thiếu Ghostscript, lỗi conversion hoặc PDFium từ chối thì không commit object một phần.
- Kiểm chứng tự động: `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_import -TestRegex '^test_pikdraw_desktop_import$'` với `PIKDRAW_CDR_REAL_FIXTURE_DIR=cdr` và `PIKDRAW_VECTOR_IMPORT_CORPUS_DIR=cdr` đã chạy qua toàn bộ corpus CDR, PDF, AI, EPS: worker import, kiểm tra object/page native, commit và Undo đều pass (244.67 s). Lệnh PDF adapter `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_interop_pdf_worker_adapter -TestRegex '^test_pikdraw_interop_pdf_worker_adapter$'` cũng pass hình học trang xoay tổng hợp 0/90/180/270 độ và giải mã manifest cũ (1/1). Các test này chưa chứng minh pixel giống tuyệt đối, tọa độ nguồn so với Corel/Acrobat, đầy đủ effect/layer hay hành vi menu UI.
- UI spec: `File > Open…` / `File > Import…`; action IDs `action.file.open` / `action.file.import`; Open trên standard toolbar, không thêm toolbox, contextual property bar, Properties field hay icon SVG. Dialog subset dùng Apply/Cancel hiện có; label song ngữ ngắn, tooltip và focus help dùng action hiện có; status hint dùng progress/result hiện có.
- Manual UI check still needed: mở CDR/PDF/AI từ File > Open…, xác nhận dialog fidelity, đối tượng và tab mới; xác nhận Cancel giữ tài liệu không đổi. Kiểm tra thêm Open/Import với CDR ZIP và các version nguồn khác.
- Giới hạn nghiệm thu: 10 fixture thật đã import thành object native và qua commit/Undo; test hình học PDF tổng hợp cho rotation 0/90/180/270 cũng pass. Chưa chứng minh pixel-identical hay tọa độ CDR nguồn với oracle Corel/Acrobat. CDR layer IDs được ánh xạ khi adapter nhận diện được; PDF có một layer OCG exact trong fixture đã kiểm tra nhưng fallback một OCG chỉ là giả định; EPS dùng layer làm việc tổng hợp. Effect/clipping, Corel live effect, mọi phiên bản CDR, save/reopen, export round-trip, UI runtime và production chưa được chứng nhận.
