# Phiếu UI: Nhập SVG và DXF

## 1. Mục tiêu và trạng thái

- **Tính năng / task:** `implement-svg-dxf-import-ui-20261018` · module `desktop`.
- **Mục tiêu:** đưa artwork từ SVG tĩnh hoặc DXF 2D subset vào trang hiện tại; người dùng xem giới hạn fidelity trước khi xác nhận; toàn bộ kết quả được áp dụng bằng một command để Ctrl+Z hoàn tác một lần.
- **Capability đã xác minh trong snapshot:** worker C6 xử lý profile `svg-hierarchy-v2` và `dxf-entities-v1`; worker được đóng gói cạnh app trong `workers/`. Import chỉ nhận nguồn cục bộ do người dùng chọn. Source signature được kiểm với phần mở rộng; xử lý qua snapshot chỉ đọc, worker cách ly và proposal stream có quota.
- **Không thuộc scope:** PDF, AI (cả authoring và PDF-compatible), CDR, EPS, DXF 3D/binary, SVG script/animation/external references và mọi profile không có trong capability matrix. Không có DXF export hoặc cam kết round-trip. Import không chứng minh appearance, production readiness hay khả năng CNC.
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
| Định dạng nguồn | File picker: SVG hoặc DXF 2D subset | Phần mở rộng cùng signature của snapshot; không sửa document | Mismatch/unknown bị từ chối trước commit |
| Đơn vị SVG | Thông tin trong review | Đơn vị của nguồn, quy đổi theo adapter sang mm | Không có override; unsupported/malformed source bị từ chối |
| Đơn vị DXF | `$INSUNITS` nguồn | Chỉ nhận đơn vị được profile hỗ trợ; không giả định đơn vị ngầm | Thiếu hoặc không hỗ trợ thì worker từ chối |
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
| Cancel / error / unsupported | Có thể chạy lại `File > Import…` | Không áp dụng | Không áp dụng | Cancel giữ document nguyên trạng; lỗi nói rõ signature, subset, quota/worker hoặc revision |
| Tài liệu đổi hoặc tab đích đóng | Proposal cũ bị từ chối theo document ID/epoch/revision/job generation | Không áp dụng | Không áp dụng | Không áp dụng commit sang tab khác hoặc revision mới |

## 5. Icon, tên và hướng dẫn

| Nội dung | English / Tiếng Việt |
| --- | --- |
| Icon SVG | Không áp dụng; action ở menu và chưa có icon riêng. |
| Label | `Import…` / `Nhập…`; file filter `SVG vector` / `Vector SVG`, `AutoCAD DXF 2D subset` / `Tập con AutoCAD DXF 2D`. |
| Accessible name | Tên menu native đọc được; các nút review có label đầy đủ `Import editable subset` / `Nhập đối tượng` và `Cancel` / `Hủy`. |
| Tooltip/focus help | Không có tooltip riêng cho menu; status hint và dialog cung cấp ngữ cảnh. Không khai báo shortcut chưa có handler. |
| Status hint | Chuẩn bị nguồn → khởi chạy worker cách ly → nhận record count → xác thực fidelity → đã kiểm tra đề xuất. Trong job: `Esc để hủy` / `Esc to cancel`. |
| Error/disabled reason | Nêu lỗi file, profile, worker hoặc tài liệu đổi; không báo thành công giả. Khi job đang chạy, status cho biết chỉ có một import và Esc để hủy. |
| Help đầy đủ | Mục này và status/review dialog là trợ giúp hiện có; chưa có F1/help browser binding. |

## 6. Hành vi và bằng chứng nhận

- **Bắt đầu:** người dùng chọn `File > Import…`, chọn `.svg` hoặc `.dxf`; filter chỉ hỗ trợ các profile trên. Filter và tiêu đề picker theo ngôn ngữ UI.
- **Thực thi:** tạo source snapshot chỉ đọc; kiểm signature/phần mở rộng; worker riêng đọc nguồn theo quota/timeout; UI nhận tiến trình qua message, không chặn canvas.
- **Review:** tóm tắt format, kích thước trang nguồn, số object/layer, coverage, appearance và production. Phần mở rộng nêu đơn vị, giới hạn, giữ nguyên trang đích và việc provenance/report hiện chưa lưu trong `.pikdraw`.
- **Apply/Cancel:** nút mặc định là Cancel; chỉ `Import editable subset` mới commit. Cancel, Escape, lỗi, tài liệu stale hoặc tab đích đóng đều không thay đổi document. Escape hủy job; Apply tạo một command/Undo unit; Ctrl+Z hoàn tác toàn bộ import.
- **Dữ liệu:** geometry/layers nguồn được thêm vào trang hiện tại; page size không đổi. Adapter normalize SVG units/DXF `$INSUNITS` sang mm. Source hash/source object IDs/report không được persist trong `.pikdraw` ở task này; dialog nói rõ giới hạn đó. Đây là gate C8 còn mở, không được gọi là provenance đầy đủ.
- **Đồng bộ bề mặt:** không có giá trị nào trên bar/Properties, do đó không có state trùng lặp hay Undo khi mở/focus panel. Command document là writer duy nhất và reject stale proposal theo document, epoch, revision và job generation.
- **Fidelity theo format:**

| Nguồn / profile | Representation | Xem/render | Editability | Metadata nguồn | Round-trip export | Production |
| --- | --- | --- | --- | --- | --- | --- |
| SVG `svg-hierarchy-v2` | Static SVG subset; không script/animation/external fetch; units normalize sang mm | Chưa có pixel oracle cho mọi feature; appearance ghi chưa xác minh | Các object được normalize thành model native trong subset | Source hash/IDs/report chưa persist | Chưa kiểm chứng; SVG export hiện có không đồng nghĩa giữ nguyên nguồn | Chưa đánh giá |
| DXF `dxf-entities-v1` | ASCII 2D entity subset, `$INSUNITS` bắt buộc và layer được map | Chưa có CAD reference oracle; ACI 7 phụ thuộc nền, preview dùng trắng | Entity hỗ trợ được normalize thành object native | Source hash/IDs/report chưa persist | Không có DXF export được xác minh | Chưa đánh giá |
| PDF, AI, CDR, EPS | Not applicable ở capability hiện tại | Không có reader được hỗ trợ ở luồng này | Không được đưa vào filter supported | Không nhận | Không suy luận | Không suy luận |

- **Accessibility/layout:** menu, picker và review dùng Win32 native control, bàn phím có thể chọn/đóng; Cancel là default trong review. Chưa kiểm native UI bằng tay ở 1366×768/1920×1080 và scale 100/125/150/200%; không suy ra pass accessibility hoặc fidelity từ harness.
- **Bằng chứng tự động:** `test_pikdraw_desktop_import` bao phủ SVG/DXF worker profiles, một command/undo, giữ nguyên page size, signature mismatch, cancellation và stale revision. Shell test suite hiện có một failure độc lập ở text-settings ellipse auto-select (`test_desktop_shell_text_settings.cpp:612`); test này không đi qua Import.
- **Review còn lại:** nghiệm thu runtime bằng thao tác native ở các kích thước/scale nêu trên; thêm provenance persistence và source-reference policy trước khi nâng claim C8. Không mở rộng format/version capability từ các fixture hiện tại.
