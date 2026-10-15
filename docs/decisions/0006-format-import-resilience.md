# ADR 0006: Mở file đồ họa có cô lập và bảo toàn nguồn

- Ngày: 2026-10-16.
- Trạng thái: **Proposed — quyết định thiết kế để triển khai và kiểm chứng**.
- Task: import-resilience-plan-20261016.
- Baseline khảo sát: ed5bd9c4f0dcb9b8c8bef749863e052c3d299419.
- Phạm vi: interop, workers và các yêu cầu đối với document/storage/render/desktop.
- Task implementation contracts `import-file-contracts-20261016` bổ sung C8 ở trạng thái Proposed
  và sửa mô tả C6 để trùng với wire/runtime đang có. C6 wire v1 chưa được đổi trong task này.
- Không có capability runtime mới được chấp nhận hoặc triển khai trong task contracts.

## Bối cảnh

Mục tiêu là mở file CDR/AI/EPS/SVG/PDF nặng hoặc lỗi mà công việc đang mở được bảo vệ,
đồng thời giữ tối đa đường nguồn, layer, chữ, màu và diện mạo. Một parser có thể giải mã được
file nhưng bỏ qua hiệu ứng, làm tròn tọa độ hoặc làm mất cấu trúc chỉnh sửa. Process isolation
giới hạn ảnh hưởng của parser; nó chưa chứng minh UI, renderer hoặc toàn hệ điều hành luôn sống.

Ảnh render đúng, cấu trúc chỉnh sửa đúng, đường sản xuất đúng và xuất lại định dạng nguồn là
bốn kết quả cần evidence riêng. Dữ liệu đã bị bỏ khỏi biểu diễn PDF trong AI không thể tự suy
ra đầy đủ bằng cách đọc PDF. Font, linked assets và engine hiệu ứng nguồn cũng có thể thiếu.

## Quyết định đề xuất

1. Mỗi lần mở file tạo một phiên import cách ly và staging độc lập. Parser, PostScript,
   giải mã ảnh/font/ICC không tin cậy chạy ngoài editor; host cấp quyền và kiểm tra đầu ra.
   Chỉ writer công bố transaction hoàn chỉnh sau kiểm tra epoch/revision/generation.
2. Giữ bản gốc bất biến theo hash trong asset store có quota; native save phải đóng gói nguồn,
   provenance, kết quả import và báo cáo. Đây là yêu cầu schema mới cần task storage riêng.
   Giữ nguyên bytes không đồng nghĩa PikDraw hiểu hoặc chỉnh sửa mọi nội dung trong bytes đó.
3. Bộ chọn adapter dùng chữ ký nội dung, phiên bản và feature coverage có fixtures. Hai bảng
   capability hiện có phải được hợp nhất trong task riêng; nhãn supported phải có bằng chứng
   import/edit/native-reopen/export tương ứng, không suy ra từ phần mở rộng hoặc lời parser.
4. Dùng libxml2 với cấu hình từ chối tài nguyên ngoài cho XML và adapter SVG của PikDraw;
   PDFium là engine khảo sát chính cho PDF và biểu diễn PDF của AI; libcdr/librevenge là
   đường khảo sát CDR. Chọn Ghostscript cách ly cho nhánh EPS → PDF sau gate giấy phép,
   containment và fidelity. Phiên bản/build/hash/dependencies phải pin trước tích hợp.
5. Phần tử hỗ trợ đầy đủ thành đối tượng nguồn chỉnh sửa được. Phần chưa hỗ trợ chỉ được giữ
   dưới dạng tham chiếu chỉ đọc khi có diện mạo kiểm chứng, hoặc chuyển đổi rõ ràng sau lựa
   chọn của người dùng; nếu không thì từ chối import có giải thích. Cả phần tử lẫn vùng ảnh
   thay thế phải có trạng thái riêng. Không xuất vùng tham chiếu/raster thành đường cắt.
6. Giữ hierarchy, painter order, clipping/group opacity, units và curves tại ranh giới parser.
   Preview/LOD và sản xuất là dữ liệu dẫn xuất. Nếu adapter không chứng minh được precision,
   màu nguồn hoặc topology, capability sản xuất của phần đó bị chặn.
7. Host áp deadline tuyệt đối, quota tổng và quota từng job; UI không chờ parser, pipe hoặc
   index build. Các số 5/30 giây và 512 MiB là cấu hình khởi đầu cần đo, không phải cam kết
   tốc độ hoặc khả năng mở mọi file. Quota lớn hơn cần xét memory của toàn ứng dụng.
8. Crash/timeout không tự chạy lại cùng file vô hạn. Worker lỗi bị thu hồi; lần thử lại là job
   có generation mới, có giới hạn và hành động người dùng rõ ràng. Autosave/recovery bảo vệ
   phiên sửa theo cửa sổ mất dữ liệu đã đo; không hứa sống sót qua mọi lỗi driver/hệ điều hành.

## Vì sao chọn đường này

Chuyển mọi định dạng sang SVG có thể làm mất dữ liệu trước khi PikDraw có cơ hội kiểm tra.
Raster hóa toàn file có thể giữ ảnh nhìn thấy nhưng làm mất đường và cấu trúc. Tự viết toàn
bộ parser độc quyền làm tăng phạm vi nghiên cứu. Lựa chọn trên tái sử dụng reader, giữ đường
nguồn trực tiếp khi có thể và giới hạn rõ những gì chỉ xem được.

PDFium cần gate đặc biệt: API FPDFPathSegment_GetPoint trả float. Các API đọc hình và màu
hiển thị cũng không tự chứng minh giữ đủ authoring data, CMYK/spot/ICC/overprint hoặc layers.
Nếu public API không đủ, nhánh đó giữ reference; task nghiên cứu mới đánh giá extractor có
precision phù hợp hoặc engine khác. Không vá bằng cách cast float sang double hoặc gọi API
private chưa pin rồi tuyên bố lossless.

libcdr là reader ứng viên; không cam kết mọi phiên bản CDR, mọi Lens/Mesh/PowerClip hoặc
CDR export. Parser không phát diagnostic cho feature lạ vẫn phải mang trạng thái coverage
chưa biết; một report rỗng không đủ để công bố không mất dữ liệu.

Ghostscript có nhánh giấy phép AGPL/commercial. Phân phối process riêng vẫn cần review
nghĩa vụ dependency; chưa có lựa chọn phân phối được duyệt thì EPS tiếp tục disabled.

## Hệ quả và điều kiện nhận ADR

Cần task contracts riêng cho import scene, trạng thái fidelity, provenance, assets và protocol
truyền dữ liệu lớn. Document/storage phải lưu được các trạng thái trước khi desktop nhận
import nâng cao. Renderer cần compositing đúng và không làm việc không giới hạn từ node lạ.

Chỉ chuyển ADR sang Accepted cho phạm vi đã đạt evidence trong
[kế hoạch triển khai](../roadmap/import-resilience-20261016.md), sau consumer review của
document, storage, workers, renderer, geometry, typography và color. Phần chưa đạt vẫn Proposed.
Không dùng kết quả harness thay containment/fidelity/performance trên app tích hợp.

Task tiếp theo phải tích hợp/consumer-review semantics C6/C8, xây worker dùng chính file corpus
user chọn, rồi mới bật adapter. Kho local hiện có 4 CDR, 1 AI, 3 PDF, 2 EPS, 1 DXF và 4 ảnh; chưa
có SVG. Một file AI trong kho có PDF signature, vì vậy chỉ được probe ở nhánh PDF-compatible AI.
Ba nguồn vượt giới hạn payload v1 16 MiB. Tên file, byte, nội dung và hash nguồn được giữ local,
không sao vào tài liệu hoặc Git.

## Nguồn quyết định

- [Adobe: Save artwork](https://helpx.adobe.com/illustrator/using/saving-artwork.html): AI có tùy chọn lưu biểu diễn PDF; dữ liệu chỉnh sửa riêng được lưu riêng.
- [PDFium public API](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_edit.h): loại đối tượng và precision ở ranh giới API.
- [libxml2 parser options](https://gnome.pages.gitlab.gnome.org/libxml2/html/parser_8h.html): external entity và DTD controls.
- [Ghostscript vector devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html), [SAFER](https://ghostscript.readthedocs.io/en/latest/Use.html#dsafer) và [Artifex licensing](https://artifex.com/licensing).
- [C6](../../contracts/c6-workers.md), [document/runtime](../architecture/document-runtime.md), [foundation spike C](../roadmap/foundation-spikes.md#spike-c--interop-và-containment) và [quality gates](../quality/gates.md).

Nguồn upstream được tra ngày 2026-10-16; đường dẫn main/latest là tài liệu tham khảo,
không phải dependency lock. Bản build triển khai phải lưu commit/tag và license thực tế.
