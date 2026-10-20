# Phiếu UI: Nhập SVG, DXF và ảnh raster

## 1. Mục tiêu và trạng thái

- **Tính năng / task:** `implement-svg-dxf-import-ui-20261018` · module `desktop`.
- **Mục tiêu:** đưa artwork từ SVG tĩnh, DXF 2D subset hoặc PNG/JPEG/TIFF raster vào trang hiện tại; người dùng xem giới hạn fidelity trước khi xác nhận; toàn bộ kết quả được áp dụng bằng một command để Ctrl+Z hoàn tác một lần.
- **Capability đã xác minh trong snapshot:** worker C6 xử lý profile `svg-hierarchy-v3`, `dxf-entities-v1` và `raster-rgba8-v1`; worker được đóng gói cạnh app trong `workers/`. Import chỉ nhận nguồn cục bộ do người dùng chọn. Source signature được kiểm với phần mở rộng; xử lý qua snapshot chỉ đọc, AppContainer worker và proposal stream có giới hạn cấu trúc/timeout; raster rollback khi RAM vật lý toàn hệ thống đạt 90%, không có quota file/pixel riêng. Raster là prototype import-only.
- **Không thuộc scope:** PDF, AI (cả authoring và PDF-compatible), CDR, EPS, DXF 3D/binary, SVG script/animation/external references, TIFF đa trang/16-bit và mọi profile không có trong capability matrix. Không có PNG/JPEG/TIFF hay DXF export, round-trip hoặc cam kết CNC. Import không chứng minh appearance, production readiness hay khả năng CNC.
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
| Dialog/panel | Dùng native Open File dialog và dialog review một lần. Không tạo panel/dock mới: đây là chọn tệp và quyết định fidelity có thể kết thúc bằng Apply/Cancel. |

## 3. Trường và dữ liệu hiển thị

Không có field chỉnh sửa đầu vào trong phiên bản này. File picker lọc định dạng và mở một nguồn duy nhất.

| Giá trị | Nơi hiển thị | Binding / phạm vi | Validation và commit |
| --- | --- | --- | --- |
| Định dạng nguồn | File picker: SVG, DXF 2D subset, PNG, JPG/JPEG, TIF/TIFF | Phần mở rộng cùng signature của snapshot; không sửa document | Mismatch/unknown bị từ chối trước commit |
| Đơn vị SVG | Thông tin trong review | Đơn vị của nguồn, quy đổi theo adapter sang mm | Không có override; unsupported/malformed source bị từ chối |
| Đơn vị DXF | `$INSUNITS` nguồn | Chỉ nhận đơn vị được profile hỗ trợ; không giả định đơn vị ngầm | Thiếu hoặc không hỗ trợ thì worker từ chối |
| Kích thước raster | Pixel, DPI và kích thước vật lý mm trong summary | Một frame; pixel preview RGBA8, kích thước trong giới hạn U32 của protocol/storage; không có quota file/pixel riêng | Import bị hủy và rollback khi RAM vật lý toàn hệ thống đạt 90%; nhiều frame hoặc precision ngoài profile bị từ chối |
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
| Đang nhập số hoặc IME | Không có field số hoặc text input | Không áp dụng | Không áp dụng | Không chiếm phím nhập số/IME |
| Cancel / error / unsupported | Có thể chạy lại `File > Import…` | Không áp dụng | Không áp dụng | Cancel giữ document nguyên trạng; lỗi nói rõ signature, subset, RAM 90%/giới hạn cấu trúc, worker hoặc revision |
| Tài liệu đổi hoặc tab đích đóng | Proposal cũ bị từ chối theo document ID/epoch/revision/job generation | Không áp dụng | Không áp dụng | Không áp dụng commit sang tab khác hoặc revision mới |

## 5. Icon, tên và hướng dẫn

| Nội dung | English / Tiếng Việt |
| --- | --- |
| Icon SVG | Không áp dụng; action ở menu và chưa có icon riêng. |
| Label | `Import…` / `Nhập…`; filter SVG, DXF 2D subset và `PNG/JPEG/TIFF images` / `Ảnh PNG/JPEG/TIFF`. |
| Accessible name | Tên menu native đọc được; các nút review có label đầy đủ `Import editable subset` / `Nhập đối tượng` và `Cancel` / `Hủy`. |
| Tooltip/focus help | Không có tooltip riêng cho menu; status hint và dialog cung cấp ngữ cảnh. Không khai báo shortcut chưa có handler. |
| Status hint | Chuẩn bị nguồn → khởi chạy worker cách ly → nhận record count → xác thực fidelity → đã kiểm tra đề xuất. Trong job: `Esc để hủy` / `Esc to cancel`. |
| Error/disabled reason | Nêu lỗi file, profile, worker hoặc tài liệu đổi; không báo thành công giả. Khi job đang chạy, status cho biết chỉ có một import và Esc để hủy. |
| Help đầy đủ | Mục này và status/review dialog là trợ giúp hiện có; chưa có F1/help browser binding. |

## 6. Hành vi và bằng chứng nhận

- **Bắt đầu:** người dùng chọn `File > Import…`, chọn `.svg`, `.dxf`, `.png`, `.jpg`/`.jpeg` hoặc `.tif`/`.tiff`; filter và tiêu đề picker theo ngôn ngữ UI.
- **Thực thi:** tạo source snapshot chỉ đọc; kiểm signature/phần mở rộng; worker riêng đọc nguồn theo RAM 90%, giới hạn biểu diễn và timeout; UI nhận tiến trình qua message, không chặn canvas.
- **Review:** tóm tắt format, kích thước trang nguồn, số object/layer, coverage, appearance và production. Với ảnh, dialog gọi đây là raster image, nói pixel được chuyển thành RGBA8, màu có thể khác giữa ứng dụng, phóng lớn bị giới hạn bởi độ phân giải và raster không phải vector CNC.
- **Apply/Cancel:** nút mặc định là Cancel; chỉ `Import editable subset` mới commit. Cancel, Escape, lỗi, tài liệu stale hoặc tab đích đóng đều không thay đổi document. Escape hủy job; Apply tạo một command/Undo unit; Ctrl+Z hoàn tác toàn bộ import.
- **Dữ liệu:** geometry/layers nguồn hoặc một `ImageObject` được thêm vào trang hiện tại; page size không đổi. Adapter normalize SVG units/DXF `$INSUNITS` sang mm; raster bounds dùng pixel size và DPI. Raster giữ byte nguồn, media type, DPI, frame, độ sâu bit nguồn, ICC và trạng thái EXIF trong native storage v8. Report/import issue và source IDs của SVG/DXF vẫn chưa persist. Đây chưa phải provenance hoàn chỉnh C8.
- **Đồng bộ bề mặt:** không có giá trị nào trên bar/Properties, do đó không có state trùng lặp hay Undo khi mở/focus panel. Command document là writer duy nhất và reject stale proposal theo document, epoch, revision và job generation.
- **Fidelity theo format:**

| Nguồn / profile | Representation | Xem/render | Editability | Metadata nguồn | Round-trip export | Production |
| --- | --- | --- | --- | --- | --- | --- |
| SVG `svg-hierarchy-v3` | Static SVG subset; không script/animation/external fetch; units normalize sang mm; clip mask giữ khi là Path child | Chưa có pixel oracle cho mọi feature; appearance ghi chưa xác minh | Các object được normalize thành model native trong subset; clip group giữ mask reference | Source hash/IDs/report chưa persist | Chưa kiểm chứng; SVG export hiện có không đồng nghĩa giữ nguyên nguồn | Chưa đánh giá |
| DXF `dxf-entities-v1` | ASCII 2D entity subset, `$INSUNITS` bắt buộc và layer được map | Chưa có CAD reference oracle; ACI 7 phụ thuộc nền, preview dùng trắng | Entity hỗ trợ được normalize thành object native | Source hash/IDs/report chưa persist | Không có DXF export được xác minh | Chưa đánh giá |
| PNG/JPEG/TIFF `raster-rgba8-v1` | Một frame được giải mã thành embedded `ImageObject`; không có quota file/pixel riêng, nhưng U32 protocol/storage bounds áp dụng; TIFF một frame và tối đa 8 bits/channel trong profile này | Codec theo signature chạy trong AppContainer; alpha channel và PNG `tRNS`/DPI/orientation được kiểm bằng fixture; Adobe/Corel color oracle chưa chạy; rollback khi RAM toàn hệ thống đạt 90% | Ảnh raster có thể di chuyển/scale; không trace tự động; preview RGBA8 không thay byte ảnh nguồn | Source bytes, DPI, ICC, frame, source depth và EXIF được persist bằng native v8; import report chưa persist | Chưa có PNG/JPEG/TIFF writer; không suy ra round-trip | Not assessed; ảnh không phải vector cut geometry |
| PDF, AI, CDR, EPS | Not applicable ở capability hiện tại | Không có reader được hỗ trợ ở luồng này | Không được đưa vào filter supported | Không nhận | Không suy luận | Không suy luận |

- **Accessibility/layout:** menu, picker và review dùng Win32 native control, bàn phím có thể chọn/đóng; Cancel là default trong review. Chưa kiểm native UI bằng tay ở 1366×768/1920×1080 và scale 100/125/150/200%; không suy ra pass accessibility hoặc fidelity từ harness.
- **Bằng chứng tự động:** adapter và real worker PNG stream trong AppContainer đã pass; malformed PNG bị từ chối trước proposal; capability test xác nhận import prototype nhưng export false. Regression desktop bổ sung xác minh pixel/source bytes/undo-redo, nhưng chưa chạy được trong build hiện tại vì `pikdraw.exe` đang mở và test target phụ thuộc relink app.
- **Review còn lại:** nghiệm thu native picker/review ở các kích thước/scale nêu trên; kiểm color appearance với Adobe/Corel; lưu report/issue cho SVG/DXF và source IDs trước khi nâng claim C8. PNG/JPEG/TIFF export, TIFF multipage/16-bit/CMYK gate riêng; không bật CNC cho raster.
