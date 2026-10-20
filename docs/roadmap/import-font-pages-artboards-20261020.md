# Kế hoạch import đúng chữ, đa trang và Page/Artboard

Ngày phân tích: 20/10/2026. Cập nhật yêu cầu: PDF khóa chỉnh sửa để xuất ảnh in; **giữ chất lượng, chỉ giảm khi người dùng chọn**. Trạng thái: **kế hoạch triển khai, chưa phải tính năng đã nghiệm thu**.

## 1. Mục tiêu và điều kiện hoàn thành

Người dùng cần mở/nhập CDR, PDF, AI, EPS thành nội dung thực có thể chỉnh sửa; chọn giữ chữ với font nguồn hoặc chuyển chữ thành đường cong vector; đọc đúng tổng số trang/artboard; chọn tất cả hoặc một tập trang; hoàn thiện thao tác Page/Artboard trong PikDraw. Riêng PDF thêm chế độ **giữ trang nguồn, khóa toàn bộ chỉnh sửa nội dung, xuất ảnh in TIF/JPG** theo workflow mở PDF/rasterize có thiết lập như Photoshop. Các yêu cầu trước về layer, hiệu ứng, tọa độ và RAM tiếp tục áp dụng cho đúng chế độ.

Kết quả cần đạt:

1. File > Open tạo một tài liệu chứa đúng các trang đã chọn. File > Import đưa các trang vào tài liệu đích theo lựa chọn rõ ràng. Không lấy thumbnail làm nội dung tài liệu. PDF có hai intent riêng: **Giữ nguyên để in — khóa chỉnh sửa** và **Chỉnh sửa vector**.
2. Hai chế độ chữ có tên và kết quả riêng: **Giữ chữ/font** và **Chuyển chữ thành curves**. Không âm thầm dùng font thay thế rồi báo đã giữ font hoặc giữ outline nguồn.
3. Tổng số trang được reader của đúng representation đọc từ cấu trúc file. Số artboard gốc chỉ được báo khi có dữ liệu authoring đủ tin cậy.
4. Trang rỗng, kích thước khác nhau, tên, thứ tự, tọa độ, layer và tài nguyên dùng chung được xử lý có chủ đích. Cả tập trang được Apply bằng một transaction/Undo.
5. Page/Artboard có command, UI, Properties, lưu/mở lại và phạm vi export thống nhất. Mọi edit tiếp theo dùng model/renderer chung của PikDraw.
6. Không đặt quota RAM riêng cho tiến trình import. Dừng và rollback khi **RAM vật lý toàn máy sử dụng đạt hoặc vượt 90%**, kiểm tra xuyên suốt các bước. Vẫn kiểm tra tính hợp lệ, overflow, tham chiếu và cấu trúc dữ liệu ngoài process.
7. Chỉ báo hoàn thành đầy đủ cho phiên bản/feature đã qua bằng chứng trong ma trận. Nếu reader chưa lấy được glyph nguồn, hiệu ứng hoặc artboard authoring, phải tiếp tục xử lý dependency đó hoặc báo chính xác phần bị chặn; không đóng yêu cầu bằng kết quả trang đầu/ảnh xem trước.
8. PDF khóa để in giữ bytes nguồn và render trang trực tiếp từ nguồn; xuất TIF/JPG đúng kích thước vật lý/DPI, vùng trang và profile đã chọn. Không hạ resolution, recompress ảnh nguồn hay lấy preview để xuất. Chỉ giảm chất lượng khi người dùng chọn rõ; chất lượng JPEG có loss phải hiện trước khi xuất.
9. Khóa nội dung ngăn sửa node/text/effect/layer/transform và các đường command/automation tương ứng. Khóa không tự chứng minh renderer giữ đủ hiệu ứng hoặc màu in: chế độ này có gate appearance và print-color riêng ở mục 4.7, không được tính là vector editable.

Tài liệu áp dụng [hướng dẫn tương thích vector](../architecture/vector-resource-interoperability.md), [C8 import fidelity](../../contracts/c8-import-fidelity.md), [C9 appearance/effects](../../contracts/c9-appearance-effects.md), [chuẩn UI](../design/ui-ux-standard.md), [workspace layout](../architecture/workspace-layout.md) và [feature acceptance](../quality/feature-acceptance.md).

## 2. Phân tích source hiện tại

Các nhận định dưới đây là đọc source ngày 20/10; không thay cho kiểm chứng thao tác Open/Import trên app chạy thật.

| Khu vực | Hiện có | Khoảng thiếu cần sửa |
| --- | --- | --- |
| Native document | [DocumentPage](../../core/document/include/pikdraw/document/model/document_page.hpp), PageId ổn định; [snapshot](../../core/document/include/pikdraw/document/snapshots/document_snapshot.hpp) có nhiều page root; [page commands](../../core/document/include/pikdraw/document/commands/page_commands.hpp) thêm/xóa trang | Hoàn thiện command và UI quản lý trang; thêm artboard có scene dùng chung. Không xây lại nền đa trang đã có |
| Native storage | [native_format.hpp](../../core/document/include/pikdraw/document/storage/native_format.hpp): hiện tại **v9**; đa trang từ v6; có kiểm tra ID xuyên trang | Bổ sung font assets, text runs/source layout và artboard bằng migration mới. Một số snapshot tài liệu còn ghi v5, không dùng thông tin đó để thiết kế lại storage |
| Import proposal/commit | [document_workflow.hpp](../../apps/desktop/include/pikdraw/desktop/workflows/document_workflow.hpp), [workflow import](../../apps/desktop/src/workflows/document_workflow_import.cpp), [ImportContentCommand](../../core/document/src/commands/import_content_command.cpp) chỉ có một bộ kích thước/layers/objects | Thiếu manifest, options, nhiều trang, font resources, page-scoped report; hiện còn từ chối tập object rỗng và có clamp kích thước trang nguồn |
| Worker protocol | [v2_proposal_semantics.cpp](../../platform/workers/src/v2_proposal_semantics.cpp) yêu cầu một Page đầu stream | Phải version profile/schema và state machine; gửi thêm Page record đơn thuần sẽ bị validator từ chối |
| PDF/AI qua PDF | [pdf_worker_adapter.cpp](../../modules/interop/src/adapters/pdf_worker_adapter.cpp) đọc được page count nhưng luôn load page 0; từ chối rotation khác 0 và trang không có object | Chưa import đa trang; text thiếu glyph placement/font nguồn, không hỗ trợ đầy đủ affine text; số trang PDF không chứng minh tổng artboard AI gốc |
| CDR | [cdr_libcdr_bridge.cpp](../../modules/interop/src/adapters/cdr_libcdr_bridge.cpp) nhận mảng trang từ libcdr/librevenge rồi từ chối nếu số trang khác 1 | C ABI/package cần đa trang; phải lấy page metadata và text trước bước SVG làm mất nghĩa |
| Chữ CDR | [cdr_worker_adapter.cpp](../../modules/interop/src/adapters/cdr_worker_adapter.cpp), `FlattenLibcdrTextSpans` | Gộp span bằng thuộc tính đầu; trim từng span làm mất khoảng trắng; mất mixed font/style. Cần collector giữ run/font và khoảng trắng |
| Chữ native | [TextObject](../../core/document/include/pikdraw/document/model/object.hpp), [font identity](../../core/typography/include/pikdraw/typography/shaping/font_identity.hpp), [font resolver](../../core/typography/src/shaping/font_resolver.cpp) | Một bộ font cho cả text; thiếu định danh file/face chính xác, rich runs và layout glyph nguồn. Resolver có fallback hệ thống; reshape lại có thể đổi hình dáng và khoảng cách |
| Chuyển curve | [document_workflow.cpp](../../apps/desktop/src/workflows/document_workflow.cpp), `convert_text_to_curves` | Nếu outline rỗng hiện tạo rectangle; thêm stroke đen 0,5 mm; cần sửa để giữ paint/appearance/order và từ chối có lý do khi không lấy được outline |
| Desktop import | [native_window_import.cpp](../../apps/desktop/src/shell/native_window_import.cpp) chạy worker trước rồi mới review; Open chuẩn bị tab sớm | Thêm bước đọc thông tin/chọn options trước chuyển đổi; điều kiện dispatch extension đang thiếu `.eps` dù picker quảng bá EPS. Kiểm tra tab rỗng phải xét mọi trang, không chỉ active page |
| Async | [async_proposal.hpp](../../core/document/include/pikdraw/document/async/async_proposal.hpp) kiểm document/epoch/revision/job | Chưa gắn target PageId/options generation. [set_active_page](../../core/document/src/document.cpp) không tăng revision: đổi trang trong lúc import có thể đổi đích nếu commit tiếp tục dùng active page |
| Export | [export_job.cpp](../../modules/interop/src/export/export_job.cpp) đã có active/selected/all page targets | Tái dùng chọn trang; bổ sung artboard region. Không suy ra có writer PDF/CDR/AI từ việc đọc được các định dạng đó |
| PDF giữ trang để in | Model hiện chỉ có Path/Text/Group/Image; object/layer có cờ locked. Header PDFium pinned có API render trang/bitmap | Chưa có source-backed PDF page/reference object và document content lock policy. Cờ locked của object chưa đủ để tạo chế độ đọc PDF cho in |
| TIF/JPG output | [format_capability_matrix.cpp](../../modules/interop/src/format_capability_matrix.cpp) khai báo JPEG/TIFF import-only; RGBA8 preview có giới hạn 4M pixels/16 MiB | Cần writers/export profiles riêng; không dùng decoder preview hoặc buffer RGBA8 giới hạn hiện tại làm master ảnh in. Chưa có bằng chứng CMYK/16-bit export |

### 2.1. Nguyên nhân cần sửa theo thứ tự

- Lỗi mở đa trang nằm xuyên adapter → protocol validator → proposal → command → UI. Sửa riêng parser không đủ.
- Sai chữ xuất phát từ thiếu font/glyph/layout nguồn và gộp runs. Thêm combobox “Curves” vào UI hiện tại chưa tạo được curve trung thực.
- Lệch tọa độ cần một hợp đồng transform cho trang, object, glyph, hình ảnh và clip; không sửa bằng cộng/trừ offset theo từng file.
- Layer/effect là các gate độc lập theo C8/C9. Outline chữ không khôi phục PowerClip, lens, transparency group hoặc layer authoring đã mất khi chuyển representation.

## 3. Phạm vi adapter và ma trận fidelity

Mỗi adapter theo định dạng/phiên bản tạo scene trung lập. Sau normalize mới dùng chung command/edit/render. Không đưa type PDFium/libcdr/Ghostscript vào core/document.

| Định dạng/representation | Reader và version cần pin | Trang/artboard | Chữ/font và metadata | Editability/hiệu ứng cần chứng minh | Export/production |
| --- | --- | --- | --- | --- | --- |
| PDF, version thực tế trong header/catalog | PDFium hiện pin 157.0.8100.0/chromium8100; thêm test matrix theo PDF version/features | Đếm Page tree qua API; giữ box, UserUnit, rotation, tên nếu có | Embedded/subset font, glyph IDs/charcodes/positions, mapping Unicode, render mode | Path/text/raster/layer OCG/clip/group/color đánh giá riêng từng feature | Không hứa PDF round-trip; production chỉ cho geometry/color đã đạt |
| PDF giữ nguồn/khóa để in | Source-page renderer trong worker; PDFium pinned là ứng viên RGB8, phải qua oracle; backend print-color riêng nếu cần | Cùng page count/range/boxes/rotation; render cả trang chọn từ nguồn | Giữ nguồn và font/effect semantics trong PDF, không reshape/extract objects để in; nguồn thiếu font vẫn phải báo | Nội dung chỉ đọc; vector editable không áp dụng. Appearance phải đo với renderer nguồn/RIP phù hợp | TIF lossless/JPG có options và ICC/DPI; raster printing có gate riêng, không tuyên bố spot separations hay CMYK nguồn nếu chỉ có RGB bitmap |
| AI có PDF-compatible representation | Reader PDF riêng, lưu metadata cho biết đang đọc phần PDF | Số trang representation là chính xác cho phần đó; tổng artboard authoring có thể chưa biết | Chữ/font của representation; không gắn nhãn font/layer authoring nếu chưa có bằng chứng | Editability của phần đã normalize; live Illustrator effects/hierarchy có gate riêng | Không hứa AI authoring round-trip |
| AI authoring/legacy không có PDF phù hợp | Cần adapter/SDK hoặc reader riêng được duyệt theo version family; P0 xác định khả thi | Đọc artboard name/rect/order và shared scene từ authoring | Font/style/layer/glyph nguồn theo khả năng reader | Bắt buộc có fixture và oracle authoring nếu tuyên bố giữ đủ artboard/layer/effect | Chưa đủ reader thì đây là dependency chặn phần tương ứng, không giả tổng artboard bằng 1 |
| CDR, version family thực tế của corpus | libcdr 0.1.9/librevenge 0.0.6 hiện tại; ghi version nhận dạng từng file | Thu startPage/endPage, kích thước/tên/order; kiểm output so nguồn | Event collector giữ text runs, khoảng trắng, embedded font nếu reader thực sự phát ra | PowerClip/lens/mesh/blends/paragraph và hierarchy phải có gate riêng; mất thông tin ở reader cần thay/mở rộng reader | Không hứa CDR round-trip từ SVG trung gian |
| EPS thường/DOS EPS | Adapter EPS hiện tại; interpreter conversion phải ghi bản build và options thực tế | EPS thường mô tả một vùng BoundingBox; chỉ báo nhiều trang khi reader xác minh được. File PS đa trang không tự gán thành EPS một trang | Font nguồn/path hoặc font của converted representation phải được phân biệt | Sửa được các primitive đã chứng minh; interpreter có thể flatten nên phải đối chiếu | Gắn nhãn converted representation; không tuyên bố giữ live effects gốc |
| SVG | Regression cho adapter/worker hiện có | Không diễn giải viewBox/nhóm như tổng page tùy tiện | Text/font và defs/use theo subset công bố | Dùng chung schema mới nhưng giữ tương thích single-page | Không mở rộng SVG feature bằng suy luận |
| DXF | Regression CAD hiện có | Không tự biến model/paper space thành Page/Artboard trong đợt này | Fonts CAD/units không dùng quy tắc PDF tùy tiện | Geometry/layer/units tiếp tục độc lập với appearance | Không thay đổi contract sản xuất DXF ngoài kiểm regression |

Mỗi hàng phải ghi riêng: xem/render, geometry, text/font, hierarchy/layer, effects, metadata nguồn, khả năng sửa, native save/reopen, export và production. Không gộp thành một cờ “supported”.

## 4. Các quyết định thiết kế

### 4.1. Luồng đọc thông tin rồi nhập

```text
Chọn file → Snapshot nguồn bất biến → Inspect → Chọn trang/chữ/đích
         → Materialize các trang đã chọn → Host validate → Review fidelity
         → Apply nguyên tập bằng một transaction
```

**Inspect** trả `SourceManifest`: source hash/token, format/version, representation kind, reader build, count status (`exact`/`unknown`), số trang representation, số artboard authoring nếu xác minh được, danh mục page locator/index/name/size/boxes/rotation. Font inventory có thể tải tiếp theo trang đã chọn; UI phải phân biệt “đang kiểm tra” với “không có font”.

- Không đếm bằng thumbnail, số ZIP assets hoặc regex `/Count`. Reader phải giải được cấu trúc và báo lỗi tài liệu hỏng/mã hóa/chưa hỗ trợ rõ ràng.
- PDF cho phép inspect page metadata trước khi tạo objects. CDR có thể cần parse toàn file để biết count; dùng collector nhẹ, không dựng toàn bộ SVG/RGBA trong bước đếm. Nếu phải parse lại hoặc spool, giữ một snapshot nguồn và cleanup chắc chắn.
- Danh sách UI dùng số 1-based; internal dùng source index ổn định. Range ví dụ `1,3-5`; chuẩn hóa trùng lặp và giữ thứ tự nguồn. Từ chối 0, ngoài phạm vi, khoảng đảo chiều, chuỗi lỗi và tập rỗng.
- Đổi lựa chọn tăng options generation, hủy kết quả cũ. Nguồn bị thay sau inspect phải inspect lại hoặc tiếp tục đúng snapshot có hash đã thông báo; không trộn hai bản file.
- Trang rỗng là trang hợp lệ. Tổng nguồn, số chọn và số import thành công là ba trường riêng. Không bỏ trang lỗi hoặc trang rỗng rồi báo đủ.
- Không cần materialize object/font/image của trang không chọn; vẫn được đọc tài nguyên cấu trúc dùng chung cần thiết để phân tích file.

### 4.2. Open, Import và tính nguyên tử

- **Open:** tạo một tài liệu gồm đúng các page root được chọn, kích thước nguồn, thứ tự nguồn. Công bố tab có nội dung sau Apply thành công; hủy/lỗi không để tab rỗng mang tên file hoặc ghi đè tài liệu đang sửa.
- **Import mặc định:** “Thêm thành trang mới”, giữ kích thước từng trang. Với đúng một trang đã chọn, cho phép “Đặt vào trang hiện tại”, giữ kích thước trang đích và dùng placement/anchor tường minh. Không tự xếp chồng nhiều trang vào một trang.
- `ImportPagesCommand` validate toàn bộ trước mutation, cấp IDs duy nhất xuyên tài liệu và giữ tham chiếu assets/layers/source locators. Một Apply = một Undo; Redo khôi phục đúng IDs, order, active page và resources.
- Không clamp âm thầm kích thước nguồn. Nếu vượt miền hỗ trợ, báo kích thước và lựa chọn chuyển đổi cụ thể; Open chính xác phải bị chặn nếu không thể biểu diễn.
- Ghi metadata document/tab/epoch/revision/job generation, **target PageId**, source token, options generation và view generation. Mọi content edit làm proposal cũ bị từ chối theo contract strict hiện tại.
- Chỉ đổi active page không phải content edit. Import tiếp tục vào PageId đã chụp; nếu người dùng đã điều hướng thì không tự đổi page/camera/focus khi hoàn thành. Trang đích bị xóa, tab đóng hoặc document thay thế phải từ chối kết quả.
- History hiện tại đã lưu toàn bộ page roots; phải kiểm regression thay vì giả định Undo hiện luôn ghi nhầm active page. Các command mới vẫn phải nhận page scope tường minh.

### 4.3. Hai chế độ chữ

**Trong intent Chỉnh sửa vector, mặc định cho import ngoài: Chuyển chữ thành curves**, ưu tiên giữ hình dáng theo yêu cầu người dùng. Nhớ lựa chọn gần nhất, hiển thị trước khi chạy. Mặc định này chỉ áp dụng khi adapter chứng minh lấy được outline nguồn; thiếu outline phải báo để xử lý, không thay bằng hình khác. Intent PDF giữ nguyên để in mặc định khóa nội dung, không chạy chuyển chữ/normalize vector.

| Chế độ | Dữ liệu cần có | Kết quả chỉnh sửa | Khi thiếu dữ liệu |
| --- | --- | --- | --- |
| Giữ chữ/font | Unicode khi có mapping; runs/style; exact font asset hoặc xác minh exact installed face; glyph placement và source transform | Text sửa được trong phạm vi glyph/font coverage; giữ source layout cho phần chưa sửa | Báo font thiếu/substitution; cho chọn font cụ thể hoặc curves nếu lấy được outline. Thay font là quyết định có cảnh báo loss |
| Chuyển chữ thành curves | Outline glyph thực và placement/matrix nguồn; fill/stroke/render mode/clip | Path có node chỉnh được; giữ holes, contours, paint và appearance. Text gốc chỉ là provenance/tìm kiếm | Không tạo rectangle, không rasterize/trace thay thế tự động, không reshape bằng fallback rồi gọi đó là outline nguồn |

**Font identity và resources:** bổ sung hash bytes, face index, PostScript name, family/style/weight, variation axes, nguồn embedded/system/substituted, subset coverage và embedding permission. Tên family đơn lẻ không chứng minh cùng font file/version. Font subset có thể hiển thị chữ cũ nhưng không có glyph cho chữ mới: UI phải báo và yêu cầu chọn font khi edit vượt coverage.

**Font nhúng:** collection riêng theo document; deduplicate theo hash+face; không cài toàn máy, không tự tải font từ mạng. Worker/host kiểm size, định dạng, table offsets, dữ liệu hỏng và quyền embed/redistribute. Không lưu thêm bytes nếu quyền không cho phép; báo hệ quả khi save/reopen và cho chọn curves nếu được hỗ trợ. File native phải mở lại được theo đúng chính sách đã chọn.

**Text model:** thêm runs và font references trong core/typography + core/document; thêm imported glyph layout authority chứa IDs/advances/offsets/matrices/source mapping. Renderer D2D và software dùng cùng dữ liệu. Giữ source layout khi chưa sửa; edit text/font làm invalid phần layout liên quan rồi shape lại bằng typography chung. Không đưa PDF charcode hay handle thư viện vào model; lưu mapping trung lập và provenance riêng.

**PDF:** kiểm `FPDFFont_GetIsEmbedded` trước `FPDFFont_GetFontData`. Header pinned hiện tại ghi rõ GetFontData có thể trả **font thay thế khi font không embedded**. Không gọi bytes đó là font gốc. Làm spike bắt buộc để xác minh exports và hành vi outline API trên đúng DLL pinned: subset/CID, charcode khác Unicode, ligature, Unicode không mapping, fonts Type1/TrueType/CFF/Type3, vertical/RTL và form XObject transforms. Có API `GetGlyphPath` chưa chứng minh lấy đúng glyph gốc. Nếu public API không đủ, tạo C ABI bridge từ PDFium source pinned để lấy glyph/position/outline nguồn; giữ dependency ngoài core.

**CDR:** thu `startPage/endPage`, text object, span, `insertText` và `defineEmbeddedFont` trước RVNGSVG generator. Librevenge có callback font nhưng phải kiểm libcdr bản pin có phát ra dữ liệu cần thiết hay không. Bỏ cách trim/gộp span mất nghĩa. Nếu reader không cung cấp exact layout/font, phải mở rộng reader hoặc đánh giá adapter/SDK có phép sử dụng; không suy ra đủ dữ liệu từ SVG nhìn gần giống.

**Chuyển chữ native hiện có:** dùng chung dịch vụ outline mới; bỏ rectangle fallback và stroke đen tự thêm; giữ fill/stroke/appearance, layer, z-order, source provenance và selection. Quy định thay type trong command giữ stable object identity khi khả thi; nếu cần một nhóm nhiều path thì root identity và mapping con phải ổn định qua Undo/Redo. Không remove/add làm rơi metadata liên kết.

### 4.4. Tọa độ, layer và effects

- Mỗi source page có một matrix source→PikDraw-mm và metadata origin/boxes/rotation/UserUnit. Object/glyph/image/clip chỉ ghép matrix theo cùng convention một lần; kiểm finite/invertible theo loại transform. Hỗ trợ origin âm, kích thước hỗn hợp, xoay 90/180/270, reflection/shear và nested form/group.
- Đo theo chuẩn tọa độ nguồn và kích thước thật; giữ double sau normalize. Không fit-to-page hoặc thêm stroke để che sai bounds.
- Layer scope theo page/scene; giữ tên/order/visibility/lock và object membership khi reader có dữ liệu. PDF OCG là optional-content semantics, không mặc định tương đương layer authoring. Không gán toàn bộ object vào một tên OCG chỉ vì tìm thấy tên trong file.
- Với AI/CDR, lấy hierarchy/effect trước bước chuyển SVG/PDF nếu bước đó làm mất nghĩa. Lập bảng mapping C9 theo source feature; unknown effect phải có issue locator đến page/object, dữ liệu nguồn giữ được thì giữ provenance, không đổi âm thầm thành Normal blend hoặc solid fill.
- Reference render chỉ được dùng dưới nhãn chỉ đọc khi người dùng chọn rõ; không tính là đạt yêu cầu vector editable hoặc đưa vào đường cắt/CNC. Raster printing được nghiệm thu riêng theo mục 4.7; không dùng gate của CNC để suy ra chất lượng ảnh in. Feature chưa đạt phải chặn tuyên bố “đầy đủ”.

### 4.5. Page độc lập và artboard dùng chung scene

- **Page:** tái dùng `DocumentPage` hiện có; một page sở hữu object root độc lập, kích thước vật lý và tọa độ local. Phù hợp trang PDF/CDR.
- **Artboard:** vùng chữ nhật có `ArtboardId` ổn định, PageId/scene sở hữu, name, rect mm, order và source provenance. Nhiều artboard trong một scene có thể chồng nhau, cùng nhìn thấy một object; không nhân bản object IDs thành nhiều page roots.
- AI PDF representation đưa vào các page độc lập trước khi có dữ liệu authoring đủ để dựng shared scene. UI ghi “N trang PDF”; artboard gốc ghi “Chưa xác định” nếu chưa biết. Không tự coi PDF pages là toàn bộ artboard nguồn.
- Page commands: chọn, thêm, duplicate, xóa (giữ ít nhất một trang), rename, reorder, resize/orientation. Duplicate remap object/effect/path IDs; assets bất biến được chia sẻ. Active-page navigation giữ cơ chế không dirty/không Undo hiện tại.
- Artboard commands: tạo, chọn, đổi tên, move/resize, duplicate, xóa, reorder. Mặc định move/resize chỉ đổi vùng artboard. Tùy chọn “Di chuyển cả nội dung” phải nêu rõ tập object được tác động và xử lý object giao nhiều artboard; không dịch hai lần. Xóa artboard mặc định giữ artwork.
- Scene mới có một artboard mặc định trùng page bounds; legacy pages migrate tương tự. Page resize cập nhật artboard mặc định còn liên kết với page; khi người dùng sửa riêng thì tách liên kết tường minh.
- Page/Artboard selection là view state; geometry/name/order là document edits có Undo. Camera/selection/tool sessions quản lý theo page/scene; đổi trang kết thúc hoặc hủy gesture theo quy tắc tool hiện tại, không để preview ghi sang trang khác.
- Native format tiếp theo lấy nền từ **v9 hiện tại**, dự kiến v10 nếu chưa có thay đổi khác lúc triển khai. Lưu font/runs/layout/artboards/provenance, kiểm CRC/schema/references; migration v2–v9 và version mới phải có test. Đối chiếu thêm contract C5 trước khi chốt schema.
- Export tái dùng page targets; thêm artboard region + clip đúng vùng, kích thước thật. SVG nhiều page/artboard xuất file riêng theo khả năng writer hiện có. Import không bật thêm writer CDR/AI/PDF. Automation đọc được IDs/count/active scope và chọn target tường minh; không đổi nghĩa âm thầm của API active-page cũ.

### 4.6. Protocol, tài nguyên và an toàn Apply

Tách `InspectRequest/Result`, `ImportOptions`, `ImportedDocumentProposal` và `ImportedPageProposal` khỏi desktop UI. Profile mới có page begin/end, font/assets, layer/object page scope, per-page report, aggregate report và digest; compatibility profile một trang phải rõ ràng. Không sửa ý nghĩa record cũ mà giữ nguyên profile version.

Host validate source count/selected indices, count đã kết thúc, IDs/parent references, cross-page scope, fonts/assets, chunk ordering/digests, doubles và report coverage. Page rỗng hợp lệ, proposal bị cắt hoặc thiếu một trang được chọn không hợp lệ. Không commit từng trang khi stream chưa hoàn tất.

Giữ source snapshot một lần; assets immutable chia sẻ giữa trang và placements; outline/font/image chuyển theo chunk/spool khi cần, tránh gom bản sao SVG+RGBA+font ở cả worker và host. Ngưỡng RAM toàn máy 90% áp dụng cả Inspect, parse, outline, staging và commit. Có checkpoint hủy ở vòng lặp và callback; dọn temp khi lỗi/hủy/đóng app. 90% là tín hiệu dừng, không phải lời bảo đảm allocation không thể thất bại trước checkpoint; xử lý allocation failure cũng phải rollback sạch.

Giới hạn cấu trúc/wire/count/time hiện có cần được rà và báo đúng nguyên nhân; không diễn giải chúng thành quota RAM riêng. Không nới sandbox/quyền truy cập để đọc thêm font/linked asset tùy ý; host broker chỉ cung cấp tài nguyên đã chọn và kiểm chứng.

### 4.7. PDF giữ nguyên để in — khóa chỉnh sửa

Đây là intent được người dùng bổ sung, không thay thế intent editable trước đó. Mục tiêu là xuất ảnh của trang thiết kế theo renderer PDF, tránh mất clipping/transparency/blend/shading do chuyển thành primitive của PikDraw. Chỉ bật nhãn “Giữ nguyên để in” sau gate appearance cho đúng renderer/version/feature; khóa một bản import vector sai không sửa được lỗi hình thức đã xảy ra.

**A. Nguồn bất biến và model trung lập**

- Thêm source asset PDF theo hash, bytes nhúng native một lần; mỗi page/reference descriptor giữ source asset ID, source page index, boxes/rotation, kích thước mm, renderer profile/version, visibility/annotation/print policy và trạng thái chỉ đọc. Không dùng `ImageObject` preview làm nguồn chân lý; không đưa handle PDFium vào snapshot.
- Renderer PDF chạy trong worker qua dịch vụ source-page/tile trung lập. Host triển khai provider/broker theo dependency inversion; core/document và core/rendering không phụ thuộc ngược interop/desktop. Validate tile/pixel format/stride/size/coordinate mapping, profile, hash, epoch/job generation trước khi dùng.
- Cache preview chỉ để viewport; cache key gồm source hash/page/box/matrix/visibility/color/renderer/scale. Preview thấp hơn resolution xuất được phép nếu không đổi master; không đánh dấu hạ chất lượng tài liệu. Khi export, luôn render lại từ PDF nguồn với thông số output, không upscale cache preview.
- Native save/reopen giữ bytes nguồn, page descriptors, print settings và lock policy; font/effects/layers gốc còn trong source. Không tuyên bố các layer đó đã thành layer native sửa được. Mất cache không mất nội dung; source bị hỏng/thiếu phải báo lỗi, không xuất thumbnail thay thế.
- PDF mã hóa yêu cầu mật khẩu trong phiên cần thiết; không ghi mật khẩu plaintext vào native/log. Mở lại có thể hỏi lại mật khẩu. Respect quyền in/đọc theo backend đã duyệt; không chạy JavaScript, action, embedded executable hoặc truy cập linked tài nguyên ngoài broker.

**B. Khóa toàn bộ đường sửa nội dung**

- Open bằng intent này tạo document content mode `PdfPrintLocked`. Cho phép xem/zoom/pan/chọn trang, xem thông tin, lưu bản native và chọn thông số xuất; chặn sửa artwork, text/font, node, paint/effect, layer visibility/order, move/scale/rotate, page/artboard geometry và paste/delete/replace làm đổi nội dung. Print crop/box/DPI là output options, không sửa source page.
- Guard nằm ở command/writer policy, không chỉ menu hoặc cờ selection. Keyboard, Properties, drag/drop, automation, plugin proposal và background jobs phải cùng bị chặn với lý do rõ. Nội dung PDF đặt vào document editable bằng Import chỉ đọc trong phạm vi placed PDF source; toàn document đích không tự khóa. Transform của placed PDF cũng khóa mặc định để tránh lệch tỷ lệ.
- Muốn chỉnh sửa chọn “Tạo bản sao để chỉnh sửa vector”: tạo tài liệu/job mới qua pipeline editable và fidelity review; nguồn khóa được giữ nguyên. “Unlock all objects” thông thường không biến reference thành primitive editable. Không hứa sửa được hiệu ứng gốc trong intent khóa.
- Setting xuất thay đổi không thể mutate PDF bytes; Undo/history artwork không xuất hiện edit giả. Active page vẫn là view state. Lưu output preset là settings/provenance riêng.

**C. Raster hóa đúng kích thước và chất lượng**

- Chọn page range, CropBox/MediaBox/TrimBox/BleedBox/ArtBox thực có; mặc định CropBox theo quy tắc PDF, ghi fallback box khi thiếu. Giữ rotation/UserUnit và bleed đúng vùng chọn; không tự trim theo bounds artwork. Print/annotation/form/OCG visibility phải cố định theo policy và có oracle: bản in không tự thay bằng trạng thái screen-view.
- DPI: preset 150/300/600 và số tùy chọn; mặc định đề xuất 300 DPI, hiển thị trước xuất. Đây là sampling resolution của output, không lời hứa ảnh nhúng resolution thấp thành sắc nét hơn. Người dùng thấy W/H mm, W/H pixel, effective DPI sau rounding và dung lượng dự kiến.
- `width_px = ceil(width_mm / 25.4 * dpi_x)` và tương tự height; validate overflow/stride/encoder bounds. Ghi DPI theo kích thước vật lý đã chọn, kiểm sai lệch rounding tối đa một pixel; không tự thu nhỏ trang khi encoder không chứa được.
- Không resample/downsample/recompress ảnh embedded trong bước giữ nguồn. TIFF mặc định lossless; source PDF vẫn nguyên bytes. Chỉ checkbox/options giảm DPI, resize hay JPEG compression do người dùng chọn mới làm giảm output. Không tự bật lại preset giảm chất lượng từ phiên trước mà không hiển thị trạng thái rõ.
- Render theo tiles/strips với **global page matrix và clip**, có vùng chồng phù hợp cho filter/anti-alias; kiểm seam với transparency, blur, softmask và image interpolation. Có fixture so tiled render với whole-page renderer. Không cắt nhỏ scene rồi đổi backdrop/group semantics. Nếu renderer không xử lý tile trung thực, phải dùng phương án progressive/spool phù hợp, không chấp nhận đường nối để giảm RAM.
- Source parser có thể vẫn cần RAM đáng kể; giữ checkpoint 90% toàn máy và xử lý allocation failure. Không hạ DPI ngầm khi gần ngưỡng; hủy sạch rồi gợi ý người dùng tự chọn mức khác. Cache LRU/spool chỉ là tối ưu, không quota import mới.

**D. Writers TIF/JPG và batch export**

| Output | Default/options phải có | Gate riêng |
| --- | --- | --- |
| TIFF `.tif/.tiff` | Lossless None/LZW/Deflate theo encoder xác minh; DPI, ICC, RGB/Gray; alpha hoặc nền trắng tường minh; một file/trang mặc định | Kiểm tags, channel order, alpha convention, profile bytes, resolution units, pixel decode round-trip. Multi-page TIFF tùy chọn sau khi đủ encoder/test cho cả file nhiều frame |
| JPEG `.jpg/.jpeg` | Hiện rõ lossy; quality mặc định cao nhất encoder, chroma-subsampling có kiểm soát nếu encoder hỗ trợ; ICC/DPI; flatten alpha trên nền chọn, mặc định trắng | Mở lại kiểm dimensions/DPI/profile và pixel error; highest quality vẫn không gắn nhãn lossless. Không JPEG lại qua nhiều cache stages |
| CMYK/16-bit hoặc TIFF lớn | Chỉ bật profile khi render/encode pipeline có precision và color proof tương ứng | PDFium bitmap API pinned hiện là Gray/BGR/BGRA8: không tạo 16-bit thực bằng nới 8→16, không gọi RGB→CMYK là giữ separations nguồn. Cần backend/color pipeline riêng nếu mục tiêu đó chưa đạt |

Source ICC/OutputIntent và color spaces phải được inspect; khi có nhiều source spaces, không gán cả trang một profile ảnh đầu tiên. RGB proof/ảnh in quản màu có thể là một capability riêng. Preserve-CMYK/spot/overprint/DeviceN cần renderer/RIP/color-management có bằng chứng; không chuyển qua RGB8 rồi gắn ICC CMYK để gọi là in chuẩn. TIFF/JPG composite không giữ live effects hoặc plate separations; plate export là scope khác cần gate rõ.

Writers mới mở rộng `ExportJob`/options schemas/atomic output hiện có, không dùng capture canvas/screenshot. Nguồn job là immutable snapshot + source assets. Batch All/range đặt tên ổn định page-index/name an toàn; không âm thầm ghi đè. Stage output tạm, mỗi file publish atomic theo filesystem; lỗi giữa batch phải có manifest kết quả và cleanup/recovery rõ, không hứa atomic cả nhiều file khi filesystem không cung cấp.

JPEG/TIFF hiện chưa có exporter trong capability matrix; bước này cần triển khai thật, kèm encoder spike (WIC hoặc thư viện được pin/phù hợp) cho precision/color/compression/size. Nếu vượt giới hạn định dạng/encoder, báo lựa chọn phù hợp như TIFF/BigTIFF khi writer đó đã hỗ trợ; không chia ảnh, giảm kích thước hay đổi định dạng ngầm.

## 5. Đặc tả UI và cách dùng

### 5.1. Hộp thoại Open/Import

Giữ File > Open (`Ctrl+O`) và File > Import (`Ctrl+I`) làm entry hiện có; kiểm action registry thực tế trước khi thêm ID. Sau Inspect, mở hộp thoại “Import options”/“Tùy chọn nhập”:

| Vùng | Field/trạng thái | Hành vi |
| --- | --- | --- |
| Thông tin nguồn | Format/version, representation, tổng trang, tổng artboard gốc hoặc Chưa xác định | Count đang đọc có progress/Cancel; không hiển thị 1 giả định |
| PDF mode | Preserve for print — locked / Editable vector | PDF mặc định đề xuất Preserve for print, người dùng thấy và chọn trước Apply; lựa chọn rõ intent, không trộn report editability |
| Pages/Artboards | All, Selected, range; list index/name/W×H/rotation/blank marker; số đã chọn | Thumbnail nhỏ tùy chọn chỉ để chọn trang, không tham gia nội dung import. Không có count artboard authoring thì không cho chọn “All source artboards” giả |
| Text | Keep text/fonts hoặc Convert to curves; giải thích khả năng sửa text/node | Chỉ active trong intent editable; ở mode khóa giải thích chữ render theo PDF nguồn. Font inventory theo trang chọn; hiện source/actual font, embedded/subset/missing/substituted |
| Fonts | Giữ chính xác, chọn font thay thế hoặc tải file font người dùng có quyền sử dụng | Substitution có preview/report, không Apply âm thầm; thiếu outline ở Curves báo lỗi đối tượng |
| Destination | Open: tài liệu mới; Import: new pages hoặc current page khi chỉ chọn một trang | Current page ghi tên/ID mục tiêu, placement; không resize đích tự động |
| Actions | Open/Import, Cancel; sau materialize: Apply/Cancel + fidelity report | Disable khi count/options không hợp lệ; Esc hủy, lỗi inline có page/object locator |

Không đặt các tùy chọn quyết định file vào Properties của document; **Properties không áp dụng cho hộp thoại import**. Sau Apply, đối tượng là native và dùng Properties như các đối tượng khác.

### 5.2. Page/Artboard trong workspace

| Bề mặt | Đường dẫn/action ID đề xuất | Nội dung |
| --- | --- | --- |
| Menu Layout > Pages | `action.page.add/duplicate/delete/rename/reorder/resize` | Thao tác trang; chọn trang qua navigator và panel |
| Menu Layout > Artboards | `action.artboard.create/duplicate/delete/rename/reorder` | Vùng artboard; tọa độ/kích thước sửa ở bar/Properties |
| Window > Pages & Artboards | `action.panel.pages_artboards` | Tab dock bên phải: danh sách trang, artboards con, count/name/dimensions; phân biệt selection và active page |
| Window > Properties | Dùng action hiện có | Trang: Name, W/H, orientation; Artboard: Name, X/Y/W/H, move artwork, export order |
| Navigator dưới canvas | `action.page.previous/next/select` | Trang trước/sau, số hiện tại/tổng, Add; tách khỏi document tabs |
| Toolbox trái | `action.tool.artboard` | Công cụ Artboard trong nhóm phù hợp; drag tạo vùng và chọn vùng hiện có |
| Contextual property bar | Dùng presenter Page/Artboard chung với Properties | Các field thường dùng, cùng đơn vị/validation/transaction/Undo; không thêm toolbar toàn cục thứ hai |

IDs là đề xuất cần kiểm trùng registry lúc triển khai. Không thêm shortcut mới xung đột; Ctrl+O/Ctrl+I giữ nguyên. Icon SVG theo style hiện hành: page-add, page-duplicate, page-delete, pages, artboard/frame; label ngắn, tooltip và focus help nêu phạm vi. Status hint ví dụ “Trang 3/8 · 210 × 297 mm” hoặc “Artboard 2 · Di chuyển vùng, giữ artwork”.

Field W/H phải hữu hạn và dương, X/Y cho phép âm trong miền model; nhập số có đơn vị thật, lỗi không tự clamp. Multi-selection hiện mixed values; busy/disabled/error khác nhau. Rename hỗ trợ Unicode. Tab order, keyboard focus, screen-reader names và Esc/Enter phải có kiểm chứng. Panel cuộn đúng ở 1366×768, 1920×1080; kiểm DPI 100/125/150/200%.

**Hướng dẫn dùng cần giao cùng tính năng:** mở/nhập file → đợi đọc tổng trang → chọn All/range → chọn Text mode và xử lý font thiếu → chọn đích → xem loss report → Apply; chuyển trang ở navigator, sửa Page/Artboard ở Properties. UI và hướng dẫn theo cơ chế English-first/Vietnamese hiện có, không dịch tên layer/font hay dữ liệu file.

### 5.3. PDF khóa để in và hộp thoại xuất ảnh

- File > Open/Import > PDF > “Giữ nguyên để in — khóa chỉnh sửa” dùng action entry hiện có. Badge trên tab/Properties/status: “PDF chỉ đọc · chất lượng xuất theo DPI”; không dùng icon khóa trần khiến người dùng tưởng có thể Unlock rồi sửa vector đầy đủ.
- Properties hiển thị source page/name/boxes/size/rotation/font warnings/renderer profile/read-only reason; nội dung không sửa được. Property bar chỉ có điều hướng trang và lối mở Print Export Settings. Page/Artboard tools và content edit actions disabled theo guard chung, có tooltip/focus help “PDF nguồn đang khóa để giữ hình thức trang”.
- File > Export > Print image, action đề xuất `action.export.print_image`; dùng chung export panel/presenter, không thêm toolbar toàn cục. Fields: TIF/JPG, Current/All/range, crop box, W/H mm và pixel, DPI X/Y linked, color mode/profile/intent, nền/alpha, compression/quality và output path/naming. Không có mâu thuẫn giữa Properties/panel/dialog: cùng options snapshot/validation.
- Checkbox “Giảm chất lượng để nhẹ file” mặc định tắt. Khi bật phải hiện cụ thể resize/DPI/compression thay đổi gì; trước export có summary. JPEG vẫn hiển thị lossy kể cả checkbox tắt. Preview thấp resolution ghi nhãn và không thay đổi settings output.
- Action `action.pdf.create_editable_copy` ở menu ngữ cảnh/Properties tạo bản editable riêng sau review. Không có action Unlock in-place bỏ qua conversion gate. Chức năng Properties chi tiết có áp dụng; toolbox riêng cho PDF khóa không áp dụng.
- Hướng dẫn: chọn PDF → Preserve for print → chọn trang/box → xem font/color warnings → Open → Export Print image → TIF lossless hoặc JPG với chất lượng đã chọn → kiểm DPI/size/profile → Export. Preset 300 DPI cần được kiểm theo công việc in thực tế, không suy ra đáp ứng mọi máy in.

## 6. Trình tự triển khai và phạm vi code

Làm trực tiếp trong source gốc hiện tại; giữ local edits. Không tạo worktree/branch/lease. Các pha chạm shared model/protocol/storage triển khai tuần tự theo dependency, không sửa chồng. Dùng [registry/modules.json](../../registry/modules.json) để giữ hướng dependency; core không phụ thuộc apps/desktop.

| Pha | Công việc và paths chủ trì | Phụ thuộc và gate ra |
| --- | --- | --- |
| P0 — Xác minh reader | `modules/interop` adapters/tests; inventory corpus `cdr/` bằng manifest local; kiểm PDFium DLL exports/font mapping và libcdr page/text/font events; spike AI authoring/legacy | Chốt version/representation matrix, font oracle và khả năng lấy artboard thật. Ghi rõ cần bridge/SDK nào. Không phát hành nhãn full fidelity khi dependency chưa giải quyết |
| P1 — Page lifecycle/đích async | `core/document` page commands/snapshot/async và tests; `apps/desktop` workflow target binding | Giữ foundation nhiều trang v9; hoàn thiện rename/reorder/duplicate/resize, target PageId và navigation generation. Đạt AC-IPA12/13/16 |
| P2 — Hợp đồng import đa trang | `modules/interop/include/.../import/` mới cho manifest/options/neutral records; `platform/workers` protocol/validator; `core/document` ImportPagesCommand; desktop proposal materializer | Sau P0/P1. Version profile; inspect→options→materialize; blank page; một Undo; chưa bật controls không có backend. Đạt AC-IPA01/02/10/11/14 |
| P3 — Font/runs/outline | `core/typography` font resources/source glyph layout; `core/document` text model/commands; `core/rendering` D2D+software; sửa dịch vụ convert curves | Sau spike P0. Đạt exact font/source glyph proof, subset coverage và paint-preserving curves. AC-IPA05–09; không dùng fallback rectangle |
| P4 — PDF và AI PDF representation | Tách helper inspection/page extraction/text outline/font extraction khỏi `pdf_worker_adapter.cpp`; per-page OCG/resources; desktop materialization chung | Sau P2/P3. All/range/blank/mixed size/rotation; giữ text hoặc curves đúng glyph; raw OCG name scan không là membership proof. AC-IPA01–03/05–09/15 |
| P4R — PDF nguồn khóa/renderer trang | `core/document` immutable source assets/page descriptors/writer guard/storage; `platform/workers` render-page/tile profile; interop provider; desktop PDF mode/read-only Properties | Sau P0/P1/P2; không phụ thuộc hoàn thành glyph-outline P3. Có oracle full page/effects/print flags, tile proof và native persistence. AC-PDFP01–04/07/09 |
| P4X — Raster print export | `modules/interop` writers TIFF/JPEG/options/atomic batch; `core/color` pipeline khi cần; desktop Print image presenter | Sau P4R; RGB8/Gray và CMYK/16-bit là capability riêng, không bật CMYK/precision chưa chứng minh. DPI/ICC/lossless gate, không đọc preview cache. AC-PDFP05/06/08/10 |
| P5 — CDR và EPS | Tách CDR event collector/page package/font bridge; bỏ span flatten; EPS inspect/conversion metadata/dispatch và regression | Sau P2/P3. CDR multi-page thật; EPS route hoạt động từ Open/Import; report converted representation đúng. AC-IPA04/08/15/21 |
| P6 — Artboard model/storage | `core/document` artboard model/commands/storage; `core/rendering` board bounds/clip view; `modules/interop` export regions; automation read/target scope | Sau P1/P3; schema native mới lưu mọi nguồn mới. Shared object semantics, legacy migration, save/reopen/export. AC-IPA17–20 |
| P7 — AI authoring đầy đủ | Adapter authoring riêng trong interop, provenance/shared scene mapping; không ép qua PDF parser | Sau P0/P2/P3/P6. Đọc count/rect/name/order từ authoring và giữ shared scene; kiểm layer/effect matrix. Nếu dependency reader không giải được thì phần yêu cầu này còn mở, không gọi representation import là hoàn tất AI artboards |
| P8 — UI tích hợp | `apps/desktop` import options presenter/dialog, pages/artboards panel/controller, native action/menu/toolbox/Properties và automation | UI từng lát chỉ bật khi backend tương ứng pass; cuối pha kiểm Open/Import trực tiếp canonical exe, không chỉ adapter tests. AC-IPA22/23 |
| P9 — Nghiệm thu file thật | Mở rộng tests hiện có, evidence visual/geometry/font/layer/effect; capability/docs hiện hành | Sau các pha liên quan. Mọi AC tương ứng claim phải đạt; thông báo đúng phần chưa đạt. AC-IPA24/25 |

Trạng thái lát P1 ngày 20/10/2026: proposal import giữ PageId của trang đích và generation điều hướng; đổi trang khi worker chạy không hủy job hoặc làm đổi trang commit. Test xác nhận commit vào A khi đang xem B, giữ B active, Undo/Redo cùng đúng hai page roots; content edit vẫn vô hiệu hóa proposal theo revision. Đây là phần AC-IPA12 đã kiểm chứng, chưa nghiệm thu đóng tab/job nguồn/options thay đổi hoặc UI lifecycle rename/reorder/duplicate/resize. Toàn bộ 4 CDR trong `cdr/` qua isolated worker, materialize, commit và Undo của `test_pikdraw_desktop_import`. CDR embedded image bytes hiện được gửi thành `RasterSourceChunk`, ghép một lần rồi chia sẻ giữa các placements; RGBA8 preview giữ alpha và raster source 24-bit RGB/32-bit RGBA metadata. Test adapter, worker semantics, native shared-source storage và desktop corpus đều pass. Đây là bằng chứng proposal/source retention, không phải oracle cho tọa độ, rotation, ICC/color rendering, source-aware export hoặc chất lượng appearance.

Page rename/reorder/deep-duplicate đã có regression trong `test_pikdraw_document_snapshots`; `ResizePageCommand` bổ sung thay kích thước trang mà không scale/move artwork, giữ PageId/root/active page và có Undo/Redo/no-op/invalid-dimension coverage trong cùng target. Đây mới là command/model evidence; panel Pages/Artboards và thao tác Properties chưa được nghiệm thu UI.

### 6.1. Tổ chức file và dependency

- File mới đề xuất theo domain: `import_manifest`, `import_options`, `import_page_records`, `pdf_page_inspector`, `pdf_text_outline`, `cdr_page_collector`, `cdr_text_collector`, `font_asset_registry`, `imported_text_layout`, `import_pages_command`, `artboard_commands`, `native_window_pages`, `native_window_artboards`, `import_options_presenter`; phần in thêm `source_page_reference`, `document_content_policy`, `pdf_page_render_worker`, `source_page_render_provider`, `tiff_writer`, `jpeg_writer`, `print_image_options`, `print_image_presenter`. Tên/extension cuối cùng tuân conventions module.
- Không đổ toàn bộ logic vào `native_window.cpp`, `document_workflow_import.cpp` hoặc `pdf_worker_adapter.cpp`. Kiểm độ dài lúc sửa; file code tối đa 2.000 dòng, không append feature vào file đã vượt ngưỡng. Thực hiện tách theo domain theo [code organization](../development/code-organization.md).
- Interop tạo DTO trung lập; typography quản font/shaping/outlines; document giữ stable resources/scene và commands; rendering đọc snapshot; desktop chỉ điều phối/UI. Font parser/reader vendor giữ ở adapter/bridge phù hợp, không có core→desktop dependency.
- Chỉ sửa CMake ở module cần thêm source/bridge. Sau mỗi lát chức năng phải có regression cụ thể; không chờ UI cuối cùng mới phát hiện schema không biểu diễn được glyph hoặc page rỗng.

## 7. Tiêu chí nghiệm thu

Các ID sau là ổn định. Mỗi ca phải ghi fixture/reader build, lệnh hoặc thao tác, kết quả và giới hạn. “Verify” cần đo độc lập; pass parse/count đơn thuần không chứng minh giữ hình/layer/effect.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-IPA01 | PDF 5 trang có một trang rỗng và mixed sizes | Inspect rồi Open All | Count=5; đủ 5 page roots, đúng order/name/dimensions | Reader count so oracle; native object/page IDs, save/reopen |
| AC-IPA02 | Nguồn 5 trang | Chọn `1,3-5,3`; nhập lỗi `0`, `6`, `5-3`, tập rỗng | Tập hợp lệ là 1/3/4/5 theo nguồn; lỗi bị chặn trước materialize | Options parser + worker request + số trang thực commit |
| AC-IPA03 | PDF UserUnit, CropBox offset, trang xoay 90/180/270 và nested forms | Import cả hai text modes | Hình/glyph/image/clip cùng hệ tọa độ; kích thước thật đúng | Landmark/vector bounds + overlay với oracle, không fit che sai |
| AC-IPA04 | CDR nhiều trang, trang rỗng, tài nguyên ảnh/font dùng chung | Inspect/All/range | Không lỗi `pages.size()!=1`; đủ trang chọn, assets chia sẻ | So Corel nguồn theo từng page và native save/reopen |
| AC-IPA05 | Embedded/subset font với glyph coverage biết trước | Keep text, save/reopen rồi sửa text | Giữ font/layout chưa sửa; thêm glyph ngoài subset phải có xử lý rõ | Hash+face/coverage, glyph IDs/positions; máy test không có font nguồn |
| AC-IPA06 | PDF không embedded; PDFium dùng substitution | Chọn Keep text hoặc Curves | Không gắn font substitute thành original; quyết định thay thế/thiếu outline rõ ràng | Font origin report, API IsEmbedded, không chỉ so family name |
| AC-IPA07 | CID/subset/ligature, Unicode thiếu mapping, Type1/CFF/Type3 và text transform | Curves | Chỉ sinh curve từ glyph nguồn đã chứng minh; loại chưa hỗ trợ báo cụ thể | Contour topology/glyph mapping và render oracle; không chọn glyph theo Unicode đoán |
| AC-IPA08 | Text CDR mixed font/style và khoảng trắng; tiếng Việt tổ hợp, RTL/vertical có oracle | Keep text và Curves | Runs/spacing/direction/style giữ đúng; không trim/gộp mất nghĩa | Run/source mapping, glyph advances/baseline, visual và chỉnh sửa |
| AC-IPA09 | Text fill-only, stroke, underline, affine, holes, appearance; ca outline thất bại | Convert native/import Curves, Undo/Redo | Không tự thêm stroke/rectangle; giữ paint/order/identity; lỗi không mutation | Paths/paint/IDs trước-sau và D2D/software pixel checks |
| AC-IPA10 | Proposal nhiều trang bị truncate, sai digest, trùng ID, sai page/font reference | Host validate | Từ chối toàn proposal, không có trang nửa chừng | Worker integrity + document revision/history/resources không đổi |
| AC-IPA11 | Job ở inspect/outline/staging; nguồn có shared assets lớn | Cancel hoặc giả lập RAM toàn máy đạt 90% | Hủy/rollback sạch, không per-process RAM quota; temp/resources được thu hồi | Injection deterministic cho RAM monitor và live memory sampling riêng |
| AC-IPA12 | Import vào trang A, người dùng chuyển sang B khi job chạy | Apply | Commit vào A, không cướp view B; edit/delete A hoặc đóng tab làm stale reject | Target PageId, view generation, epoch/revision/job assertions |
| AC-IPA13 | File/options/job bị thay sau Inspect | Kết quả cũ đến muộn | Không Apply kết quả source/options cũ | Source hash/token và options/job generation; document không đổi |
| AC-IPA14 | Document có trang cũ và selected-page proposal | Import new pages rồi Undo/Redo | Một Undo thu hồi cả tập; Redo giữ IDs/order/assets | History/page-root/resource assertions; current-page mode không resize đích |
| AC-IPA15 | Nguồn có OCG/multi-layer/hidden/locked và effects/PowerClip | Import và reselect/edit | Membership/visibility/effect giữ đúng phần đã claim; loss có page/object locator | So source hierarchy và C8/C9 matrix; không dùng tên OCG làm proof |
| AC-IPA16 | Tài liệu nhiều trang và shared raster/font | Add/duplicate/delete/rename/reorder/resize, Undo/Redo | IDs duy nhất, còn ít nhất 1 trang, assets không sao chép bytes vô ích | Command/snapshot tests; navigation không dirty hoặc tạo Undo |
| AC-IPA17 | Hai artboard chồng nhau, một object đi qua cả hai | Move/resize/delete/duplicate board | Artwork không bị nhân bản/mất; move-artwork dịch đúng tập đúng một lần | Object IDs/transforms/board rects/history và selection behavior |
| AC-IPA18 | Native legacy v2–v9 và file mới có pages/fonts/runs/artboards | Migrate, save, reopen | Đủ metadata/source layout/assets; board mặc định hợp lệ | Storage/schema/CRC tests, native content so trước save |
| AC-IPA19 | Nhiều page/artboard khác kích thước | Export selected/all theo writer hiện có | Đúng scope/order/clip/1:1, không đổi file source | ExportJob assertions và file output; không suy ra writer AI/CDR/PDF |
| AC-IPA20 | AI gốc có N artboards, PDF representation chỉ có M | Inspect rồi import representation/authoring | Hiện M PDF pages và source artboards unknown hoặc N có proof; không đánh đồng M=N | Source application count, authoring metadata và shared-scene object IDs |
| AC-IPA21 | EPS thường và DOS EPS trong corpus | File Open và File Import | Extension được dispatch, không lấy preview raster; representation/font loss báo đúng | Native paths/text/raster records + oracle; source thumbnail không là nội dung |
| AC-IPA22 | Canonical app, busy/error/mixed states, DPI/resolution trong mục 5 | Dùng menu/keyboard/options/panel/Properties | Controls reachable, cùng validation/Undo; count/đích/status rõ; Cancel hoạt động | Runtime thao tác thực + semantic query; ảnh chụp chỉ hỗ trợ bằng chứng |
| AC-IPA23 | Native objects sau import và automation client | Edit/reselect, đổi page/board, save/reopen | Cùng document model; thao tác đúng IDs/scope; không sửa cache importer riêng | Commands/revision/snapshot/query và render sau edit |
| AC-IPA24 | File thật trong thư mục `cdr/` và corpus tự tạo có ground truth | Open/Import từng format, All/range, hai text modes | Bảng kết quả theo file/version/feature; lỗi cụ thể, không pass bằng fixture thay thế | Visual, geometry, hierarchy, text/fonts, color, native save, production và export tách riêng |
| AC-IPA25 | Feature hiệu ứng/font/reader ngoài khả năng đã kiểm chứng | Review và thử Apply theo policy | Không báo “đầy đủ”; conversion/reference cần lựa chọn rõ; không lẫn vào geometry sản xuất | Capability + fidelity report + C9 status; release gate đối chiếu claim |

### 7.1. Gate bổ sung PDF khóa và ảnh in

Giữ nguyên IDs AC-IPA hiện có; intent PDF khóa có các ID riêng, không dùng pass editable import thay cho render/export proof.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-PDFP01 | PDF có clip/softmask/shading/blend groups/effects và ảnh nhúng | Open Preserve for print | Giữ source bytes/page descriptors; render đúng feature đã claim, không qua vector normalization | Source hash + oracle source renderer theo page/feature; không chấp nhận thumbnail |
| AC-PDFP02 | Document PdfPrintLocked | Thử keyboard/drag/node/text/Properties/layer/page transform/paste/delete/automation/plugin edits | Mọi đường đổi nội dung bị guard chặn; view/export vẫn dùng được | Writer tests xác nhận epoch/revision/source/history không mutation; UI runtime |
| AC-PDFP03 | PDF khóa và editable document đích | Import placed PDF rồi Create editable copy | Source chỉ đọc đúng scope; document đích không tự khóa; bản editable riêng có review, nguồn không đổi | Object/document content policy + stable IDs/hash/history |
| AC-PDFP04 | Source nhiều trang, box offsets/rotation/UserUnit, annotation/forms/OCG print states | Open All/range, export chọn box | Đủ trang, đúng vùng/matrix và print visibility; không tự dùng screen-state khác | Count/box landmarks + source/RIP oracle theo cấu hình flags |
| AC-PDFP05 | PDF text/vector sắc nét, ảnh nhúng có resolution biết trước; viewport cache thấp | Export 150/300/600 DPI | Render từ source đúng W/H pixels/DPI, không upscale proxy hoặc hạ DPI ngầm | Output decode, DPI tags/physical size, image details; source hashes không đổi |
| AC-PDFP06 | RGBA test pages, ICC/Gray/RGB/CMYK/spot/overprint samples | TIFF lossless và JPG high quality; chọn profile | TIFF lossless pixel/tags đúng; JPG loss hiện rõ; CMYK/16-bit chỉ khi backend đạt | Decode bằng reader độc lập; profile/color oracle và RIP khi claim print color; không gắn ICC giả |
| AC-PDFP07 | Page lớn có blur/transparency qua ranh tile | Tiled render/export, cancel, gần 90% RAM và allocation failure | Không seam/backdrop sai; không giảm chất lượng ngầm; hủy sạch | Whole-page oracle vs tiles ở nhiều vị trí/kích thước, peak memory và cleanup |
| AC-PDFP08 | Batch multi-page, tên trùng/file tồn tại, encoder size limit/lỗi giữa job | Export TIFF/JPG All/range | Output đúng order/name; không overwrite/hạ size âm thầm; stage/publish/recovery rõ | Atomic-per-file + manifest/status tests, original files không bị mất |
| AC-PDFP09 | Native có PDF source/assets/cache; nguồn encrypted có mật khẩu phiên | Save/reopen khi xóa cache hoặc thiếu nguồn ngoài | Nguồn nhúng phục hồi đúng; render lại, giữ khóa; không lưu password plaintext | Storage/hash tests, font/effect render sau reopen; explicit password prompt khi cần |
| AC-PDFP10 | UI mặc định giữ chất lượng, tùy chọn giảm và JPEG compression | Mở options/Properties, đổi DPI, export, quay lại | Giảm chỉ do người dùng chọn; summary đúng; preset không ghi đè master; badges/disabled states rõ | Runtime cùng presenter/options snapshot, file output và source hash |

### 7.2. Corpus và cách đo

- File người dùng ở `C:/Users/CK ART STAGE/Downloads/PikDraw/cdr/` dùng tại chỗ, không đổi/xóa. Manifest local ignored gán ID trung lập + hash/format/version/pages/font/effect features; không chép nội dung hoặc tên riêng khách hàng vào Git.
- Tạo fixture tự sở hữu có 3/5 trang, trang rỗng, mixed sizes/origins/rotation, layers/OCGs, font embedded/subset/missing, glyph Unicode/charcode khác nhau, nhiều artboards chồng nhau. Corpus hiện tại một mình không đủ chứng minh các ca đó.
- Ground truth lấy từ file cấu trúc tự tạo và render/export của ứng dụng nguồn với version/font/color settings ghi rõ. Đối chiếu trước/sau text edit để phân biệt giữ appearance với khả năng sửa.
- PDF-print corpus thêm gradient/shading/softmask/overprint/spot/OutputIntent, ảnh có alpha, glyph nhỏ, đường mảnh, bleed/crop offsets và trang lớn cần tiling. TIFF/JPG mở kiểm bằng Photoshop hoặc reader độc lập; claim CMYK/overprint cần RIP/print-color oracle phù hợp. So pixel cùng PDFium chỉ là regression nội bộ, không là oracle độc lập cho renderer đó.
- Synthetic affine double có thể dùng sai số ≤1e-8 mm khi không đi qua số float; dữ liệu PDFium phải dùng budget theo độ chính xác nguồn. Glyph bounds mục tiêu ban đầu ≤0,01 mm, topology/holes đúng; ca khác cần budget công bố trước, không nới sau khi fail để làm pass.
- So ảnh 300 DPI với alpha/color settings cố định và renderer nguồn độc lập. Không căn dịch ảnh tự động để che lỗi tọa độ. D2D/software cần đồng nhất semantics; pass của một renderer không chứng minh renderer còn lại.
- Tách kiểm pixel/geometry khỏi typography, layer, effect, ICC/overprint, editability, native round-trip và sản xuất. Không suy ra fidelity đầy đủ từ thời gian chạy, build pass, hash hay object count.

## 8. Lệnh kiểm tra khi triển khai

Đây là **lệnh dự kiến**, chưa chạy trong bước viết kế hoạch. Mở rộng test source hiện có của từng module; chỉ thêm file test mới khi là domain riêng, không tạo hàng loạt test tạm. Build/test qua entrypoint giữ shared lock:

```powershell
# Document: page commands, async target binding, atomic import và storage
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_document_commands','test_pikdraw_document_snapshots','test_pikdraw_document_async','test_pikdraw_document_writer_safety','test_pikdraw_document_storage','test_pikdraw_document_storage_fidelity') -TestRegex '^test_pikdraw_document_(commands|snapshots|async|writer_safety|storage|storage_fidelity)$'

# Typography: font identity/resources, source layout và outline
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_typography') -TestRegex '^pikdraw_typography_tests$'

# Format adapters và export scope
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_interop_pdf_worker_adapter','test_pikdraw_interop_cdr_worker_adapter','test_pikdraw_interop_eps_worker_adapter','test_pikdraw_interop_svg_worker_adapter','test_pikdraw_interop_capabilities','test_pikdraw_interop_export_job') -TestRegex '^test_pikdraw_interop_(pdf_worker_adapter|cdr_worker_adapter|eps_worker_adapter|svg_worker_adapter|capabilities|export_job)$'

# Worker wire/validator và desktop apply/UI integration
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_worker_v2_integrity','test_pikdraw_worker_svg_import','test_pikdraw_desktop_import','test_pikdraw_desktop_shell','test_pikdraw_vertical_slice_e2e') -TestRegex '^test_pikdraw_(worker_v2_integrity|worker_svg_import|desktop_import|desktop_shell|vertical_slice_e2e)$'

# App chuẩn, sau đó thực hiện các ca UI ở AC-IPA22
.\tooling\native\build.ps1 -Target @('pikdraw')
```

Ca rendering dùng test target hiện hành trong `core/rendering/CMakeLists.txt` theo source đã chạm; chốt exact target trước khi chạy. Chạy test SVG/DXF regression khi đổi record codec chung. Binary UI nghiệm thu duy nhất: `build/win-dev/apps/desktop/pikdraw.exe`. Không dùng binary cũ làm bằng chứng sau edit.

P4R/P4X mở rộng tests hiện có: `core/document/tests/commands/test_commands.cpp`, storage/async/writer-safety, `modules/interop/tests/adapters/test_pdf_worker_adapter.cpp`, export options/job/atomic export, worker integrity và desktop import/shell. Writers TIFF/JPEG là domain mới: thêm test source/target riêng khi thực sự tạo writer, ghi exact target vào tài liệu lúc đó; chưa có target hiện hành nên không đưa lệnh tên giả. Thêm fixture decode độc lập/metadata, tiled rendering oracle và command guard coverage thay vì chỉ test file được tạo.

Sau kiểm tra, dọn fixture/output/log/ảnh tạm do tác vụ tạo, giữ nguồn corpus người dùng và regression hữu ích. Báo lệnh, pass/fail, reader build, bằng chứng runtime và giới hạn trực tiếp; không tạo task/handoff snapshot. Chỉ cập nhật capability/status hiện hành sau bằng chứng tương ứng; `docs/project-state.json` giữ dưới 8.000 byte, không thêm nhật ký dài.

## 9. Các dependency có thể chặn hoàn thành đầy đủ

| Dependency | Phải quyết định/đo ở đâu | Hành động khi chưa đạt |
| --- | --- | --- |
| PDF source glyph mapping và exact outlines | Spike P0 trên DLL pinned và fixture CID/subset/Type3 | Bổ sung pinned bridge/source reader; không thay bằng Unicode/fallback outline rồi đóng yêu cầu |
| CDR exact text layout/font và live effects | P0/P5 so libcdr events với Corel oracle | Mở rộng reader hoặc tích hợp adapter/SDK có giấy phép/quyền sử dụng phù hợp; ghi version/feature còn chặn |
| AI authoring/artboard/layer ngoài PDF | P0/P7 kiểm data và reader theo version | Xây adapter riêng hoặc giải dependency; báo phần authoring chưa xong nếu chưa có đường đọc, vẫn phân biệt representation pages đúng |
| Quyền nhúng và coverage font | Font inventory P3 và save policy | Chọn font có quyền, substitution có chấp thuận hoặc curves nguồn đủ điều kiện; không tự cài/tải font |
| Appearance graph/C9 chưa biểu diễn được feature | P4/P5/P7 theo feature matrix | Thực hiện model/render mapping cần thiết và oracle trước claim; không dùng panel options để che mất effect |
| PDF source renderer/print color và TIFF/JPG encoder | P0/P4R/P4X | Khóa không sửa lỗi renderer; bổ sung backend/encoder đạt oracle. Tách RGB proof khỏi CMYK/separations/16-bit, không bật options chưa có pipeline |
| RAM toàn máy/alloc failure | P2/P3/P9 với inject và sample thực | Hủy toàn job sạch, giảm peak copies/chunk/spool; không tăng quota giả để làm pass |

Không ấn định thời gian hoàn thành reader authoring khi chưa xong P0. Sau yêu cầu bổ sung, ưu tiên lát đầu **Open PDF nhiều trang → chọn trang → giữ PDF nguồn/khóa nội dung → xuất TIF lossless đúng DPI → native save/reopen**, chạy P0/P1/P2/P4R/P4X mà không chờ xong outline chữ. Pipeline editable vẫn đi tiếp P3/P4/P5: curves đúng glyph, CDR đa trang và font giữ nguyên; Artboard/native AI theo dependency nêu trên. Thực thi shared files tuần tự. Lát PDF-print không thay bằng chứng hoàn thành vector editable hoặc toàn bộ yêu cầu trước.

## 10. Nguồn kỹ thuật đối chiếu

- Source và headers pinned trong checkout là bằng chứng version-specific: `.agent/toolchains/pdfium-8100/include/fpdf_edit.h`, `fpdf_text.h`; header `RVNGDrawingInterface.h` trong toolchain libcdr cho page/text/font callbacks. Phải kiểm DLL exports/reader behavior thực tế trước khi chốt khả năng.
- [Adobe — Saving artwork](https://helpx.adobe.com/illustrator/using/saving-artwork.html): tùy chọn PDF-compatible tạo representation PDF; không mặc định đồng nghĩa dữ liệu authoring có thể đọc đầy đủ.
- [Adobe — Creating PDF files](https://helpx.adobe.com/sg/illustrator/using/creating-pdf-files.html): có thể lưu tất cả hoặc một range artboards vào PDF; vì vậy count representation không tự chứng minh count artboard nguồn.
- [Adobe — Introduction to artboards](https://helpx.adobe.com/illustrator/desktop/create-manage-artboards/add-edit-artboards/introduction-to-artboards.html): artboards là vùng trong workspace; thiết kế shared scene phải được đối chiếu khi đọc dữ liệu authoring.
- [Adobe — Open PDF files in Photoshop](https://helpx.adobe.com/photoshop/desktop/create-open-import-images/import-files/open-pdf-files-in-photoshop.html): workflow chọn trang, crop boundaries, resolution và color settings khi rasterize PDF; là tham chiếu UX cho intent ảnh in, không là bằng chứng PikDraw đã tương thích pixel.
- [Adobe — Save files in graphics formats](https://helpx.adobe.com/uk/photoshop/using/saving-files-graphics-formats.html): đối chiếu tùy chọn TIFF/JPEG, compression và color modes. Capability thực tế của encoder PikDraw phải kiểm riêng.
- Header pinned `.agent/toolchains/pdfium-8100/include/fpdfview.h`: render-page/matrix, flags printing/annotations và bitmap Gray/BGR/BGRA8. API declarations không tự chứng minh preservation CMYK, 16-bit hoặc overprint trên backend hiện tại.

**Gate bàn giao cuối:** các AC liên quan claim đạt trên source/binary mới và file thật; thao tác Open/Import có bằng chứng UI; native save/reopen; intent editable có edit proof, intent PDF-print có writer-guard và xuất ảnh proof; font/layer/effects/tọa độ/color có kết quả riêng; mọi phần chưa đạt vẫn ghi mở. Tài liệu kế hoạch này không tự thay đổi trạng thái runtime.
