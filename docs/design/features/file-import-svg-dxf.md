# Phiếu UI: Nhập SVG, DXF và ảnh raster

> **Snapshot lịch sử ngày 18/10/2026:** các quota 128/192/640/768 MiB và trần TIFF 256 trang bên dưới không còn là chính sách hiện hành. Hiện không áp quota staging byte cố định; luồng import chờ ở ngưỡng 95% RAM, tiếp tục khi có headroom và timeout sau tối đa 5 phút cộng dồn. Xem [kế hoạch import hiện hành](../../roadmap/import-fidelity-performance-20261022.md) và [kế hoạch raster](../../roadmap/raster-import-large-assets-20261021.md).

## 1. Mục tiêu và trạng thái

- **Tính năng / task:** `implement-svg-dxf-import-ui-20261018` · module `desktop`.
- **Mục tiêu:** đưa artwork từ SVG tĩnh, DXF 2D subset hoặc PNG/JPEG/TIFF raster vào trang hiện tại; người dùng xem giới hạn fidelity trước khi xác nhận; toàn bộ kết quả được áp dụng bằng một command để Ctrl+Z hoàn tác một lần.
- **Capability đã xác minh trong snapshot:** worker C6 xử lý profile `svg-hierarchy-v3`, `dxf-entities-v1` và `raster-rgba8-v1`; worker được đóng gói cạnh app trong `workers/`. Import chỉ nhận nguồn cục bộ do người dùng chọn. Source signature được kiểm với phần mở rộng; xử lý qua snapshot chỉ đọc, AppContainer worker và proposal stream có giới hạn cấu trúc/timeout. Raster dùng quota 128 MiB cho file nguồn, 192 MiB cho pixel RGBA8 đã giải mã và 640 MiB cho worker; cần tối thiểu 768 MiB RAM vật lý còn trống. Quota riêng của raster thay ngưỡng RAM toàn máy khi đạt mức trống này. Windows WIC đọc đầu ra theo dải 64 hàng và proposal phát chunk lần lượt, không giữ thêm toàn bộ danh sách pixel chunk trong worker. Raster là import-only.
- **Không thuộc scope:** PDF, AI (cả authoring và PDF-compatible), CDR, EPS, DXF 3D/binary, SVG script/animation/external references, TIFF 16-bit và mọi profile không có trong capability matrix. TIFF chọn một trang 1–256; profile chuyển pixel sang RGBA8. Không có PNG/JPEG/TIFF hay DXF export, round-trip hoặc cam kết CNC. Import không chứng minh appearance, production readiness hay khả năng CNC.
- **Nguồn thiết kế:** `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`, `docs/architecture/vector-resource-interoperability.md`, C8/C9, capability matrix tại `modules/interop/src/format_capability_matrix.cpp` và audit `docs/reviews/ui-logic-gap-audit-20261018.md`. Chọn File workflow vì đây là thao tác lấy dữ liệu nguồn; không tạo tool vẽ hay panel riêng.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `File > Import…` / `Tệp > Nhập…`; lệnh thao tác với file, cùng nhóm Open/Save. |
| Action ID | `action.file.import`; một action mở file picker rồi chạy job bất đồng bộ. |
| Loại thao tác | Import một lần, có tiến trình, có thể hủy trước commit; xác nhận fidelity là quyết định riêng trước khi thay đổi tài liệu. |
| Standard toolbar | Không đặt: import ít thường xuyên hơn New/Open/Save và toolbar không nên lặp toàn bộ menu. |
| Toolbox / flyout | Không áp dụng: đây không phải công cụ tương tác trực tiếp trên canvas. |
| Property bar | Không áp dụng: không có tham số chỉnh artwork hay tool state trong lúc chọn nguồn. |
| Properties | Không áp dụng: báo cáo là kết quả của một nguồn nhập, không phải thuộc tính selection; không lưu report vào document. |
| Mở bảng thuộc tính | Không áp dụng vì không có section thuộc tính. |
| Context menu / shortcut | Chỉ menu chính. Chưa gán shortcut trong NativeWindow; không quảng bá Ctrl+I cho đến khi có binding được kiểm chứng. |
| Dialog/panel | Dùng native Open File dialog, hộp chọn trang TIFF khi cần và dialog review. Không tạo panel/dock mới: trang là tham số nguồn dùng một lần, còn review kết thúc bằng Apply/Cancel. |

## 3. Trường và dữ liệu hiển thị

Không có property field chỉnh sửa đầu vào trong phiên bản này. File picker lọc định dạng và mở một nguồn duy nhất; TIFF mở hộp chọn số trang trước khi tạo job.

| Giá trị | Nơi hiển thị | Binding / phạm vi | Validation và commit |
| --- | --- | --- | --- |
| Định dạng nguồn | File picker: SVG, DXF 2D subset, PNG, JPG/JPEG, TIF/TIFF | Phần mở rộng cùng signature của snapshot; không sửa document | Mismatch/unknown bị từ chối trước commit |
| Đơn vị SVG | Thông tin trong review | Đơn vị của nguồn, quy đổi theo adapter sang mm | Không có override; unsupported/malformed source bị từ chối |
| Đơn vị DXF | `$INSUNITS` nguồn | Chỉ nhận đơn vị được profile hỗ trợ; không giả định đơn vị ngầm | Thiếu hoặc không hỗ trợ thì worker từ chối |
| Kích thước raster | Pixel, DPI và kích thước vật lý mm trong summary | JPEG/PNG/BMP dùng frame đầu; TIFF dùng trang đã chọn; nguồn tối đa 128 MiB, RGBA8 tối đa 192 MiB | File/trang vượt quota hoặc không tồn tại bị worker từ chối trước commit; memory cap worker 640 MiB |
| Trang TIFF | Hộp thoại native sau khi chọn TIFF; nhập số trang 1–256, mặc định 1; worker kiểm tra count thật | Tham số một lần cho nguồn; ánh xạ sang frame index 0–255; không sửa document hay lưu state | Trang không tồn tại bị từ chối trước commit |
| Chuyển đổi raster | Review nêu RGBA8, alpha, giới hạn màu và production | Pixel preview RGBA8 giữ alpha; giữ byte nguồn, DPI, ICC và EXIF orientation trong `ImageObject` | Người dùng xác nhận ảnh raster; không tạo vector path hoặc tuyên bố có thể cắt CNC |
| Kích thước trang nguồn | Summary review, mm | Proposal đã normalize | Chỉ thông tin; kích thước trang tài liệu hiện tại không đổi |
| Số object/layer | Summary review | Proposal đã kiểm tra | Chỉ commit khi người dùng chọn Import editable subset |
| Coverage / Appearance / Production | Summary review | Báo cáo worker/capability | Luôn phân biệt; trạng thái hiện tại lần lượt là partial / chưa xác minh / chưa đánh giá |

Không có draft property bar, mixed selection, unit entry hoặc tab order cho field. Nếu bổ sung override đơn vị hay mapping tùy chọn, cần feature task riêng với field, scope và validation cụ thể.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint / kết quả |
| --- | --- | --- | --- | --- |
| Không có tài liệu | Import không có hiệu lực; cần document hiện hành | Không áp dụng | Không áp dụng | Báo không thể khởi tạo phiên nhập |
| Có tài liệu, không selection | `File > Import…` hoạt động | Không áp dụng | Không áp dụng | Thêm layer/object vào trang hiện tại |
| Một hoặc nhiều object được chọn | Cùng action; không phụ thuộc selection | Không áp dụng | Không áp dụng | Import không sửa selection đang có |
| Đối tượng khóa/read-only | Không áp dụng trực tiếp; command/document validator quyết định | Không áp dụng | Không áp dụng | Từ chối nguyên proposal, không commit một phần |
| Đang chạy | Menu không khởi chạy job thứ hai | Không áp dụng | Không áp dụng | Status bar báo giai đoạn/số record và “Esc để hủy” |
| Đang nhập số hoặc IME | Chỉ hộp TIFF nhận số trang qua edit control; dialog review không nhận text | Không áp dụng | Không áp dụng | Focus trong edit control; giá trị giới hạn 1–256 |
| Cancel / error / unsupported | Có thể chạy lại `File > Import…` | Không áp dụng | Không áp dụng | Cancel giữ document nguyên trạng; lỗi nói rõ signature, quota raster/trang, subset, giới hạn cấu trúc, worker hoặc revision |
| Tài liệu đổi hoặc tab đích đóng | Proposal cũ bị từ chối theo document ID/epoch/revision/job generation | Không áp dụng | Không áp dụng | Không áp dụng commit sang tab khác hoặc revision mới |

## 5. Icon, tên và hướng dẫn

| Nội dung | English / Tiếng Việt |
| --- | --- |
| Icon SVG | Không áp dụng; action ở menu và chưa có icon riêng. |
| Label | `Import…` / `Nhập…`; filter SVG, DXF 2D subset và `PNG/JPEG/TIFF images` / `Ảnh PNG/JPEG/TIFF`. |
| Accessible name | Tên menu native đọc được; các nút review có label đầy đủ `Import editable subset` / `Nhập đối tượng` và `Cancel` / `Hủy`. |
| Tooltip/focus help | Chưa có tooltip riêng cho menu; hộp TIFF ghi `Chọn số trang TIFF cần nhập (1–256)` / `Choose the TIFF page to import (1–256)` và báo trang 1 mặc định. |
| Status hint | Chọn tệp → chọn trang TIFF nếu cần → chuẩn bị nguồn → worker cách ly → xác thực fidelity → review. Trong job: `Esc để hủy` / `Esc to cancel`. |
| Error/disabled reason | Nêu lỗi file, quota raster, số trang không hợp lệ, profile, worker hoặc tài liệu đổi; khi job chạy, status cho biết chỉ có một import và Esc để hủy. |
| Help đầy đủ | Mục này và status/review dialog là trợ giúp hiện có; chưa có F1/help browser binding. |

## 6. Hành vi và bằng chứng nhận

- **Bắt đầu:** người dùng chọn `File > Import…`, chọn `.svg`, `.dxf`, `.png`, `.jpg`/`.jpeg` hoặc `.tif`/`.tiff`; filter và tiêu đề picker theo ngôn ngữ UI. TIFF hỏi trang 1–256, mặc định trang 1.
- **Thực thi:** tạo source snapshot chỉ đọc; kiểm signature/phần mở rộng; raster giới hạn file 128 MiB, RGBA8 192 MiB và worker 640 MiB; cần 768 MiB RAM vật lý trống. Windows WIC sao chép đầu ra theo dải 64 hàng; worker gửi record tuần tự để tránh bản sao pixel chunk toàn ảnh. UI nhận tiến trình qua message, không chặn canvas.
- **Review:** tóm tắt format, kích thước trang nguồn, số object/layer, coverage, appearance và production. Với ảnh, dialog gọi đây là raster image, nói pixel được chuyển thành RGBA8, màu có thể khác giữa ứng dụng, phóng lớn bị giới hạn bởi độ phân giải và raster không phải vector CNC.
- **Apply/Cancel:** nút mặc định là Cancel; chỉ `Import editable subset` mới commit. Cancel, Escape, lỗi, tài liệu stale hoặc tab đích đóng đều không thay đổi document. Escape hủy job; Apply tạo một command/Undo unit; Ctrl+Z hoàn tác toàn bộ import.
- **Dữ liệu:** geometry/layers nguồn hoặc một `ImageObject` được thêm vào trang hiện tại; page size không đổi. Adapter normalize SVG units/DXF `$INSUNITS` sang mm; raster bounds dùng pixel size và DPI. Raster giữ byte nguồn, media type, DPI, frame, độ sâu bit nguồn, ICC và trạng thái EXIF trong native storage v8. Report/import issue và source IDs của SVG/DXF vẫn chưa persist. Đây chưa phải provenance hoàn chỉnh C8.
- **Đồng bộ bề mặt:** không có giá trị nào trên bar/Properties, do đó không có state trùng lặp hay Undo khi mở/focus panel. Command document là writer duy nhất và reject stale proposal theo document, epoch, revision và job generation.
- **Fidelity theo format:**

| Nguồn / profile | Representation | Xem/render | Editability | Metadata nguồn | Round-trip export | Production |
| --- | --- | --- | --- | --- | --- | --- |
| SVG `svg-hierarchy-v3` | Static SVG subset; không script/animation/external fetch; units normalize sang mm; clip mask giữ khi là Path child | Chưa có pixel oracle cho mọi feature; appearance ghi chưa xác minh | Các object được normalize thành model native trong subset; clip group giữ mask reference | Source hash/IDs/report chưa persist | Chưa kiểm chứng; SVG export hiện có không đồng nghĩa giữ nguyên nguồn | Chưa đánh giá |
| DXF `dxf-entities-v1` | ASCII 2D entity subset, `$INSUNITS` bắt buộc và layer được map | Chưa có CAD reference oracle; ACI 7 phụ thuộc nền, preview dùng trắng | Entity hỗ trợ được normalize thành object native | Source hash/IDs/report chưa persist | Không có DXF export được xác minh | Chưa đánh giá |
| PNG/JPEG/TIFF `raster-rgba8-v1` | Một frame được giải mã thành embedded `ImageObject`; source ≤128 MiB, RGBA8 ≤192 MiB; TIFF chọn frame 1–256 | Codec chạy trong AppContainer; Windows WIC copy theo dải và proposal truyền chunk tuần tự; alpha, PNG `tRNS`, DPI/orientation và chọn frame có fixture; Adobe/Corel color oracle chưa chạy | Ảnh raster có thể di chuyển/scale; không trace tự động; preview RGBA8 không thay byte ảnh nguồn | Source bytes, DPI, ICC, frame, source depth và EXIF được persist bằng native v8; import report chưa persist | Chưa có PNG/JPEG/TIFF writer; không suy ra round-trip | Not assessed; ảnh không phải vector cut geometry |
| PDF, AI, CDR, EPS | Not applicable ở capability hiện tại | Không có reader được hỗ trợ ở luồng này | Không được đưa vào filter supported | Không nhận | Không suy luận | Không suy luận |

- **Accessibility/layout:** menu, picker và review dùng Win32 native control, bàn phím có thể chọn/đóng; Cancel là default trong review. Chưa kiểm native UI bằng tay ở 1366×768/1920×1080 và scale 100/125/150/200%; không suy ra pass accessibility hoặc fidelity từ harness.
- **Bằng chứng tự động:** adapter và real worker PNG stream trong AppContainer đã pass; malformed PNG bị từ chối trước proposal; capability test xác nhận import prototype nhưng export false. Regression desktop bổ sung xác minh pixel/source bytes/undo-redo, nhưng chưa chạy được trong build hiện tại vì `pikdraw.exe` đang mở và test target phụ thuộc relink app.
- **Review còn lại:** nghiệm thu native page picker/review ở các kích thước/scale nêu trên; đo peak RAM và thời gian trên ảnh thực 20–100 MP; kiểm color appearance với Adobe/Corel; lưu report/issue cho SVG/DXF và source IDs trước khi nâng claim C8. PNG/JPEG/TIFF export, TIFF 16-bit/CMYK gate riêng; không bật CNC cho raster.
