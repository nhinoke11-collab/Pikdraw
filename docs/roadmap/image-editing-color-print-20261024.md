# Kế hoạch thiết kế công cụ sửa ảnh, quản lý màu và in thử

Ngày: **24/10/2026**. Mã: `PLAN-IMAGE-COLOR-PRINT-20261024`.
Trạng thái: **P1/P2 chưa đạt; P3 có lát cắt Assign RGB ICC, preview và Convert → sRGB; chưa nghiệm thu mốc hoặc công bố toàn bộ capability**.
Phạm vi: ảnh raster trong tài liệu PikDraw, chỉnh chi tiết, màu/kênh/sắc độ, RGB/CMYK/ICC,
chuẩn bị sản xuất, mô phỏng màu in, in thử và đối chiếu số đo.

## 1. Kết quả sản phẩm cần đạt

Người dùng đặt ảnh vào bản thiết kế, sửa ảnh ngay trong canvas, quay lại sửa từng bước,
giữ file gốc và xuất đúng độ phân giải. Một ảnh có thể dùng cho web, in ảnh qua driver RGB,
in offset CMYK hoặc gửi RIP mà không phải sửa đè bản gốc cho từng đầu ra.

Nhóm chức năng được tổ chức thành **10 họ**, có đường dùng từ cơ bản đến nâng cao:

| Họ | Kết quả người dùng nhận được |
| --- | --- |
| Ảnh và kích thước | Crop, straighten, rotate, perspective, resize/resample, effective PPI, link/embed |
| Ánh sáng và sắc độ | Exposure, Light, Levels, Curves, điểm đen/trắng, histogram và clipping |
| Màu sắc | White Balance, HSL, Vibrance, Color Balance, Replace/Selective Color, Sample & Target |
| Kênh màu | RGB, CMYK, Gray; Lab có gate riêng; Channel Mixer, alpha/mask, spot channel ở giai đoạn nâng cao |
| Vùng chọn và mask | Rectangle/lasso/brush, color/luminance range, feather, refine edge, mask theo node |
| Sửa chi tiết | Clone, heal, spot heal, patch, dodge/burn, brush/eraser trên lớp sửa độc lập |
| Chi tiết và hiệu ứng | Denoise, sharpen, clarity/texture/dehaze, blur, grain, LUT và output sharpening |
| Màu được quản lý | ICC nguồn/working/display/output, Assign/Convert, RGB 8/16 bit, CMYK 8/16 bit |
| Chế bản và in | Soft proof, gamut, separations, overprint/knockout, TAC, black policy, preflight, export |
| In test và tự động hóa | Crop 1:1, bảng biến thể màu, thang xám/màu, hard proof, nhập số đo, preset/batch/AI tùy chọn |

Chi tiết ID, thuật toán và giai đoạn của từng chức năng ở [02 — Danh mục](image-editing-color-print-20261024/02-features-and-algorithms.md).

## 2. Các quyết định thiết kế

1. **Sửa không phá hủy là mặc định.** Giữ nguồn bất biến; crop, adjustment, mask và retouch là dữ liệu
   có phiên bản, stable ID và Undo/Redo. Chỉnh một placement không âm thầm đổi những placement dùng chung nguồn.
2. **RGB/CMYK là model; ICC mới xác định ý nghĩa số màu.** sRGB, Adobe RGB (1998), Display P3 và profile
   máy in phải được nhận diện bằng nội dung/hash, không suy ra từ tên. RGB không đồng nghĩa sRGB;
   CMYK không có một profile, TAC hay công thức rich black chung cho mọi máy/vật liệu.
3. **Tách năm thao tác:** chỉnh ảnh; gán profile; chuyển profile; xem proof; tạo đầu ra.
   Xem proof không đổi kênh nguồn. Selective Color có slider C/M/Y/K trên ảnh RGB vẫn là adjustment RGB,
   chưa phải chuyển ảnh thành CMYK hoặc tạo bản tách màu.
4. **Giữ master, tạo biến thể theo đầu ra.** Ưu tiên master RGB có profile và 16 bit khi cần chỉnh sâu;
   giữ CMYK nguồn khi công việc yêu cầu giữ separation. Không bắt mọi ảnh CMYK phải đi vòng qua RGB.
5. **In theo đích thực.** Driver nhận RGB/Gray, xuất CMYK cho RIP/offset, in proof mô phỏng máy khác
   là các tuyến khác nhau. Một bên chịu trách nhiệm chuyển màu; thông báo rõ khi trạng thái driver không kiểm tra được.
6. **Một editor và hệ UI hiện có.** Dùng Bitmaps, File, View, Settings, Window; Properties bên phải;
   toolbox dùng flyout. Không dựng thêm menu cấp cao hoặc ứng dụng sửa ảnh riêng trong đợt đầu.
7. **Hoàn thiện pipeline trước độ phủ chức năng.** Tile/LOD, precision, alpha, ICC, storage, export cùng
   semantics rồi mới mở rộng filter, retouch, AI. Không đổi framework Win32/D2D chỉ để làm bộ sửa ảnh.
8. **Nghiệm thu màu bằng nhiều bằng chứng.** Pixel oracle, số kênh/profile, save/reopen/export,
   RIP và số đo bản in là các gate riêng. Build pass không chứng minh màu in đúng.

Tài liệu tham khảo chính thức đã tra trực tiếp: Photoshop release notes **27.11, 09/2026**,
trang cập nhật 05/10/2026; Illustrator release notes **30.8, 08/2026**, trang What's New cập nhật
14/10/2026; CorelDRAW/PHOTO-PAINT **2026, September Update 27.2**. Đây là phiên bản mới nhất
được các trang đã kiểm tra nêu, không suy đoán bản phát hành chưa được tài liệu hóa.
Nguồn và bài học cụ thể ở [01 — Tham chiếu và baseline](image-editing-color-print-20261024/01-references-and-baseline.md).

## 3. Bộ tài liệu thiết kế

| Tài liệu | Nội dung |
| --- | --- |
| [01 — Tham chiếu và baseline](image-editing-color-print-20261024/01-references-and-baseline.md) | Adobe/Corel/ICC/Fogra; bằng chứng source; phần kế thừa và khoảng trống |
| [02 — Chức năng và thuật toán](image-editing-color-print-20261024/02-features-and-algorithms.md) | Danh mục cơ bản/nâng cao, precision, ngữ nghĩa màu và giới hạn |
| [03 — Dữ liệu, pipeline, interop](image-editing-color-print-20261024/03-data-pipeline-and-interop.md) | Graph/mask/retouch, CMM, tile/jobs, persistence, format matrix |
| [04 — UI và luồng sử dụng](image-editing-color-print-20261024/04-ui-and-workflows.md) | Menu/action, toolbox/bar/Properties, field/state, help, thao tác |
| [05 — Màu in và in test](image-editing-color-print-20261024/05-print-proof-and-test.md) | Output condition, proof, tách màu, test chart, biến thể và đo màu |
| [06 — Triển khai và nghiệm thu](image-editing-color-print-20261024/06-implementation-and-acceptance.md) | P0–P8, phạm vi code, acceptance, benchmark, lệnh kiểm tra và release gate |

## 4. Quan hệ với nền tảng hiện tại

Kế thừa [plan ảnh lớn](raster-import-large-assets-20261021.md), không làm lại asset store/cache
hoặc đặt cap dung lượng file mới. `RasterEditGraph` hiện có crop/flip/rotate/exposure và command cập nhật
node; phần evaluator, UI, precision và export phải được kiểm lại trước khi nhận làm nền chạy được.

Kế thừa [plan màu đang có](existing-feature-upgrade-20261020/color-management-plan.md),
[C4](../../contracts/c4-color-typography.md), [C8](../../contracts/c8-import-fidelity.md),
[C9](../../contracts/c9-appearance-effects.md). Plan này bổ sung **màu trên pixel ảnh và workflow in thử**;
không mở một hệ ICC/Undo/Properties cạnh tranh với các phần đã có.

Runtime giữ C++20/Win32, D2D/D3D11 hardware hoặc WARP, GDI presentation, CPU fallback.
`registry/modules.json` quyết định dependency. Renderer nhận raster provider qua interface/injection;
`core/document` giữ model, `core/color` giữ màu/CMM, `modules/raster` giữ evaluator.

Source hiện có payload .pr v23; đã có container chunk v24 ngoài payload. Hướng package/index/streaming
còn ở plan. Không dùng số container để suy ra graph mới đã tương thích. Chi tiết migration ở tài liệu 03;
đợt lập kế hoạch này không đổi schema, registry hoặc project-state.

## 5. Lộ trình và các mốc giao được

| Bước | Phạm vi | Điều kiện đi tiếp |
| --- | --- | --- |
| P0 | Audit source, corpus/precision/CMM/output route, chốt semantic vectors | Có baseline và quyết định codec/CMM đo được |
| P1 | Tích hợp asset/tile/graph/mask schema/Undo/native streaming cần thiết | Crop + Exposure qua UI → save → reopen → full-res export |
| P2 | RGB 8/16 bit, Light/Levels/Curves/WB/HSL/Vibrance, histogram/mask cơ bản | Bộ sửa ảnh cơ bản end-to-end, không rơi về preview 8 bit |
| P3 | ICC trên ảnh, monitor, CMYK giữ kênh, intent/BPC, proof và gamut có oracle | Các tuyến RGB/CMYK và Assign/Convert đúng scope |
| P4 | In thử thiết thực: output preset, chart/biến thể/crop 1:1, file xuất và route RGB do WCS hoặc driver quản lý | In thử tái lập được; chưa gắn nhãn contract proof |
| P5 | Retouch/mask nâng cao, Channel Mixer/Selective Color, detail filters, preset/batch | Sửa cục bộ và nhiều ảnh giữ source/Undo; đo RAM/độ trễ |
| P6 | Separations/spot/overprint/TAC pixel và composition, PDF/X có validator/RIP | Bản chế in đúng plate/profile; không lấy RGBA làm separations |
| P7 | Nhập số đo, ΔE00, hard proof hai thiết bị, profile test, acceptance tại xưởng | Có số đo, điều kiện đo và bộ tiêu chí tương ứng đầu ra |
| P8 | RAW/Lab nâng cao/HDR/PSD subset/AI/DeviceLink/white ink theo từng capability | Mỗi mở rộng có adapter, giới hạn và gate riêng |

Mốc A = P0–P2: sửa ảnh cơ bản tin cậy. Mốc B = P3–P4: quản lý màu và in test dùng được.
Mốc C = P5: sửa ảnh chi tiết/nâng cao. Mốc D = P6–P7: chế bản và kiểm chứng màu in.
P8 là backlog có thiết kế điểm mở; không kéo hết vào bản phát hành đầu hoặc bỏ sót khỏi roadmap.

P4 và P5 đều dùng nền P1–P3, nhưng ưu tiên P4 vì yêu cầu in test. P6 phải có evaluator CMYK,
output renderer và adapter xuất thật; P7 hard proof phải có P6 hoặc một tuyến RGB proof đã đạt riêng.
Không gộp toàn bộ thành một lần giao khổng lồ; từng mốc phải chạy xuyên UI/model/storage/export.

## 6. Phạm vi và điều kiện kết thúc lượt kế hoạch

Lượt này giao thiết kế, nguồn tham khảo và acceptance; không cài thư viện, tải model AI, tạo
print job, đổi artwork hoặc sửa source ứng dụng. Chưa chạy build/native test hay đo thiết bị in.
Các con số hiệu năng và dung sai trong tài liệu 06 là **mục tiêu đề xuất**, không là kết quả đã đo.

Không mặc định cấp quyền dùng thương hiệu/thư viện màu, Fogra MediaWedge, profile hoặc model AI.
Tài nguyên được tích hợp sau phải có license riêng; chart chẩn đoán PikDraw tự tạo có nhãn riêng.
Giấy phép engine không tự cấp giấy phép profile/model/thang đo kèm theo.

Trước triển khai đọc lại source/diff và các contract liên quan; tách file theo domain, không thêm
code vào file vượt 2.000 dòng. Giữ các local edits; không tạo worktree/clone/nhánh task.

## 7. Tiến độ triển khai sau khi duyệt kế hoạch

Cập nhật **24/10/2026**: kế hoạch đang được triển khai; P1 còn dở và P2 mới có nền tảng 16-bit một phần; chưa
nghiệm thu mốc hoặc công bố toàn bộ bộ sửa ảnh/in ấn đã hoàn tất.

- Đã thêm evaluator CPU cho crop/flip/rotate/exposure trên RGBA8 sRGB straight-alpha, có hủy
  công việc và giới hạn bộ nhớ; graph có operation ID/algorithm version ổn định và migration từ
  schema graph v1. Export ảnh áp graph đã lưu trước khi ghép trang.
- Menu Bitmaps có Crop, Exposure và Flip ngang/dọc với action thật, lưu qua command/Undo; preview bất đồng bộ
  được cache theo document epoch, ảnh nguồn và graph rồi cấp cho renderer CPU/Direct2D.
- Test pixel và round-trip .pr qua evaluator/graph chạy được. Đây chưa phải bằng chứng UI native
  end-to-end: chưa kiểm thao tác hộp thoại trực quan, save/reopen/export qua app, ảnh lớn/tile,
  import nguồn 16-bit, ICC ảnh/CMYK hoặc in vật lý. Crop hiện là nhập tỷ lệ trong dialog, chưa có handles.
- Evaluator P2 đã có nhánh sRGB RGBA16LE cho crop/flip/rotate/exposure và pixel oracle bảo toàn
  mức chênh dưới một bước 8-bit. Model giữ buffer này song song preview RGBA8; graph schema v3
  lưu/reopen payload và vẫn migrate graph v1/v2; preview bất đồng bộ xử lý RGBA16LE rồi mới giảm
  về 8-bit tại ranh giới renderer. Bằng chứng hiện là unit/pixel/storage tests với buffer nạp trực tiếp,
  chưa chứng minh importer 16-bit hoặc export TIFF 16-bit.
- P1 có `.pr` schema v24 raster source/sample tables và v25 chunk container. Desktop raster import
  hiện staging source qua session disk tạm, gắn descriptor SHA-256/provider và giữ memory lease;
  `.pr` vẫn serialize source bytes trong RAM; desktop open dựng lại provider bằng cách chép từ source
  bytes vào temp session. Provider/session không persistent, bytes vẫn nằm RAM và còn bản disk thứ hai.
  Persistent store, tile/lazy source, full-resolution guarantee và native streaming còn thiếu; P2 chưa đạt mốc.
  P3 đã có primitive ICC U16 và xuất ảnh bitmap RGB sang TIFF CMYK16 theo profile/intent được chọn,
  nhưng Assign/Convert trong document, giữ kênh CMYK nguồn, monitor, soft proof và gamut vẫn mở.
  P4–P8 chưa bắt đầu; acceptance proof, separations, in test và đo màu vẫn mở.

Cập nhật triển khai 24/10/2026: nhập PNG16 qua worker và `.pr` save/reopen/Undo/Redo đã được kiểm
thử; TIFF16 writer API pass round-trip riêng, nhưng page-export còn RGBA8. P2 có Levels 8/16-bit và
menu/action/dialog thật, node graph v4 với persistence/Undo và test pixel; histogram U8/U16 có API
mask-aware và test. Properties hiện vẽ histogram RGB sau graph cùng đường alpha coverage qua worker;
histogram theo mask, ICC/proof và chế độ xem channel riêng còn thiếu. Curves RGB năm điểm đã có graph/evaluator/menu và test round-trip;
White Balance tương đối trên linear-light sRGB hiện đã có graph/evaluator/menu, dialog nhập số,
save/reopen/Undo, pixel tests và kiểm tra ảnh TIFF master sau export. Mask workflow, shared Properties và page composite
16-bit/profile conversion vẫn chưa đạt. Xem trạng thái test chi tiết ở
[06 — Triển khai và nghiệm thu](image-editing-color-print-20261024/06-implementation-and-acceptance.md).

Cập nhật tiếp 24/10/2026: `Bitmaps > Export Bitmap Master (TIFF)…` xuất ảnh được chọn ở kích thước
pixel do graph hiện hành tạo ra; TIFF giữ RGBA16 khi master 16-bit, DPI và ICC phù hợp với basis
mẫu. Test crop ảnh 16-bit 2×1 thành TIFF 16-bit 1×1, đọc lại pixel/alpha/profile/DPI; menu/action
test cùng pass. Đây là xuất một bitmap, không phải page composite 16-bit, tile streaming, linked
lazy export, CMYK master hay BigTIFF. P1 vẫn mở; P2–P8 tiếp tục theo acceptance từng mốc.

Cập nhật P2 24/10/2026: Selective HSL đã có sáu dải Hue 60° với Feather, graph schema v9,
dialog Apply/Cancel, mỗi dải một node, lưu .pr, preview và Undo/Redo. Release tests cho storage,
pixel evaluator, vertical slice, preview service và menu/action pass 7/7; dialog chưa được xác nhận
trực quan. P2 còn thiếu mask workflow, shared Properties, composite 16-bit và profile conversion.

Cập nhật P3 24/10/2026: `Bitmaps > Export CMYK Bitmap Master (TIFF)…` nhận ảnh RGB đang chọn,
profile CMYK hợp lệ và rendering intent, rồi chuyển trên worker sang TIFF CMYK16 với ICC/DPI.
Ảnh bán trong suốt được flatten trên trắng trong RGB tuyến tính trước khi chuyển màu; file TIFF
không có alpha. Output là biến thể xuất, không đổi mẫu hay graph trong `.pr`. TIFF writer ghi strip
classic TIFF, kiểm offset/size và từ chối trên 4 GiB vì BigTIFF chưa hỗ trợ. Ba test
`test_pikdraw_interop_raster_image_writer`, `test_pikdraw_raster_bitmap_export` và
`test_pikdraw_desktop_actions` pass 3/3; canonical Release build pass. Đây chưa kiểm Assign/Convert
trong document, CMYK nguồn native, BPC, proof/gamut, Save dialog hoặc máy in; thao tác native UI
chưa được xác nhận trực quan.

Bổ sung P3 24/10/2026: ảnh có ICC RGB nhúng chưa áp dụng có action `Bitmaps > Convert Bitmap Profile to sRGB`;
worker chuyển RGBA8/U16 sang sRGB hệ thống với Relative Colorimetric, BPC off, giữ alpha, byte nguồn,
ICC và graph. Chỉ commit nếu snapshot/selection không đổi; Undo/Redo và `.pr` reopen giữ basis/buffer.
Pixel oracle và test dispatcher → worker → command → save/reopen pass; menu, progress, Esc và màu trên màn
hình chưa được nghiệm thu trực quan. Assign, CMYK source/native, monitor/proof/gamut và in thử vẫn mở.

Lệnh deterministic gần nhất:
`tooling/native/build.ps1 -Configuration Release -Test -TestTarget @('test_pikdraw_raster_bitmap_export','test_pikdraw_desktop_actions') -TestRegex '^(test_pikdraw_raster_bitmap_export|test_pikdraw_desktop_actions)$' -LockWaitSeconds 120` — **2/2 pass**, gồm pixel TIFF16/profile/DPI và menu/action.
`tooling/native/build.ps1 -Configuration Release -Target pikdraw -LockWaitSeconds 120` — **pass**, link canonical `build/win-dev/apps/desktop/pikdraw.exe`.
`test_pikdraw_drawing_tool_ui` lặp lại timeout `drawing_snap_index_ready`; `test_pikdraw_rendering_rasterizer`
có lần vượt ngưỡng benchmark 8.3 ms (10.51 ms đo được). Hai gate đó chưa được xác nhận pass ở lượt này.
Lượt histogram mới: `test_pikdraw_raster_histogram` và `test_pikdraw_raster_histogram_service` pass
2/2, gồm thay selection khi job đang chạy; `pikdraw` Release build pass. `test_pikdraw_desktop_actions`, `test_pikdraw_snap_properties_ui`
và `test_pikdraw_units_settings_ui` pass; full `test_pikdraw_desktop_shell` và
`test_pikdraw_artboard_tool_ui` segfault, không có stack trace trong `LastTest.log`. UI chart chưa
được xác nhận bằng thao tác cửa sổ native.

Bổ sung P3 24/10/2026: `Bitmaps > Assign Bitmap Profile…` cho một ảnh RGB chọn tệp ICC, giữ nguyên
mẫu RGBA8/U16, alpha, graph và source bytes; command ghi active profile/basis, hỗ trợ Undo/Redo và
`.pr` save/reopen. Resolver preview chạy worker và chuyển ICC được gán sang system sRGB trước khi
đưa vào cache render; profile giống output dùng đường sao chép nguyên mẫu, profile CMM không hỗ trợ
bị chặn trước commit. Release suite gồm `test_pikdraw_document_raster_profile_commands`,
`test_pikdraw_raster_preview_service`, `test_pikdraw_desktop_actions` và
`test_pikdraw_raster_profile_workflow` pass 4/4; verbose preview/workflow pass 2/2, gồm scalar CMM
oracle và menu Assign → Undo/Redo → Convert → `.pr` reopen. Chưa xác nhận thao tác native menu/file
picker, monitor profile, CMYK source, proof/gamut hoặc in; P3 và mốc B vẫn chưa đạt.

Cập nhật triển khai tiếp 24/10/2026: P4 có planner và renderer test sheet chẩn đoán/crop 1:1/lưới biến thể,
menu xuất TIFF sRGB A4 300 ppi; P6 có TAC raster full-resolution qua ICC RGB→CMYK theo block có hủy,
P7 có parser subset CGATS/CSV, so sánh ΔE00 theo patch ID/điều kiện, lưu run .pdmr và UI CSV report với
ngưỡng tùy chọn. P5 thêm graph mask bitmap/rectangle,
blend mask vào evaluator và UI Local Exposure chữ nhật feathered. Các regression tương ứng đã được thêm,
chưa chạy lượt Release tích hợp cuối. P2 thêm node Light U8/U16 qua UI, graph v12, mask, Undo/Redo và TIFF;
Properties/bar đồng bộ cùng bảng chỉnh sửa chưa làm nên mốc A còn mở. P1 source provider/tile/native streaming, P2 composite U16,
P3 monitor/proof/gamut, P4 direct driver, P5 retouch/channel/filter/preset/batch, P6 plates/PDF-X,
P7 run/report UI/hard proof và nhánh P8 vẫn thiếu; các mốc A–E chưa được nghiệm thu.

Cập nhật P2/P5: graph raster schema v13 thêm Channel Mixer 3×3 trên linear-light sRGB, giữ alpha,
mask, precision U8/U16, undo/save/reopen và menu/dialog native. Giá trị mặc định là identity;
ma trận được giới hạn −2…2 và clip đầu ra. Channel Mixer chưa có thanh thuộc tính/Properties
đồng bộ và native UI chưa được nghiệm thu.

Cập nhật P7: service ngưỡng mean/p95/max giữ trạng thái complete riêng assessed/passed; artifact
.pdmr version 1 có CRC32/bounds; UI import và so sánh hai run lưu CSV, ghi rõ trạng thái chưa đánh giá
khi bỏ trống ngưỡng. Đây chưa là chart chuẩn độc lập. Lượt Release/test duy nhất vẫn chờ đến khi code
các phase kết thúc. Hard proof, đo máy thật, route printer/RIP và qualification còn thiếu; P7 và mốc D
chưa đạt.

Bổ sung P4 24/10/2026: `File > Print Test Sheet…` gửi bảng thử qua Windows GDI printer DC, dùng
Windows WCS khi DEVMODE xác nhận `DMICMMETHOD_NONE` hoặc `DMICMMETHOD_SYSTEM`; cấu hình
driver/device ICM bị từ chối để tránh chuyển đổi kép. Dialog ẩn/vô hiệu hóa Print to File, WCS được
bật trước job và bật lại sau `StartPage`, DIB nguồn gắn `LCS_sRGB`. Metadata giữ paper/mode/PPI,
render và spool chạy trong worker; UI ghi rõ màu bản in chưa được xác nhận. Code chưa build/test
hoặc chạy với driver thật; profile đích, giấy/mực, scale vật lý, bản in và ΔE chưa xác minh.
P4 direct-print code đã có route hệ thống, gate B/P4 vẫn mở.

Bổ sung P5 24/10/2026: mask cục bộ thêm linear gradient ngang và radial gradient vào graph schema
v15; Local Exposure dialog chọn hình dạng, invert/density/feather, command và `.pr` giữ geometry.
Pixel/storage regressions đã thêm, chưa chạy. Gradient angle, mask paint/refine/overlay và UI cho mọi
node còn mở. Clone Stamp dạng một dab đã được nối từ `Bitmaps > Clone Stamp Dab…` qua command/Undo,
schema v14 (được giữ tương thích trong v15), save/reopen và evaluator RGBA8/U16. Nguồn dab được lấy từ
graph prefix trước khi ghi; patch có memory reservation 128 MiB, hủy theo hàng/chunk và blend alpha
premultiplied. Regression evaluator/storage/command/vertical-slice/menu đã được thêm, chưa chạy
cho đến lượt Release test cuối. Đây chưa phải brush pointer nhiều stroke hoặc lấy mẫu composite.
P1 streaming/tile, các control Properties/bar, P3 proof/monitor, P5 heal/patch/paint/filter/preset/batch,
P6 plates/PDF-X và các nhánh P8 vẫn chưa đóng; mốc A–E chưa nghiệm thu. P7 hard proof và đo máy thật
cũng cần thiết bị/vật liệu ngoài code.

Bổ sung P4 provenance: printer worker đọc profile đầu ra hiện hành từ printer DC qua `GetICMProfileW`,
hash SHA-256 file theo giới hạn 16 MiB, gắn hash đầy đủ vào condition ID của chart và chặn job nếu profile
không đọc/hash được. `OutputCondition` phân biệt profile nguồn RGB với hash profile đích và kiểm hash
64 ký tự khi route yêu cầu. Chỉ nhận diện file profile tại thời điểm job; chưa xác minh driver thực sự
dùng association đó, in vật lý, paper/ink, scale hoặc ΔE. Test validation đã thêm, chưa chạy theo
yêu cầu một lượt cuối.

Bổ sung P2 Exposure Properties: property bar và Object inspector có field `EV` dùng chung getter,
validation −10…+10, revision/selection scope và `SetRasterEditNodeCommand`; sửa node giữ ID, mask và
enabled state. Dialog Exposure cũng giữ nguyên node/mask. Regression workflow đã mở rộng cho mask,
giá trị ngoài miền, Undo/Redo và `.pr` reopen; chưa chạy theo yêu cầu chỉ test một lượt cuối.
Đây mới là Exposure field; shared inspector cho các node raster khác và visual/focus acceptance vẫn mở.

Bổ sung P1/P2 25/10/2026: save `.pr` phát chunk container trực tiếp vào temp file và patch CRC tăng dần;
payload dùng writer buffer 1 MiB nhưng load vẫn đọc toàn file và payload vào RAM, provider chưa tới
renderer/tile/full-res nên P1 chưa đạt. Inspector có tab Light/Color Balance/HSL/Vibrance, Levels
composite/R/G/B, Curves composite/R/G/B và White Balance tương đối trên linear-light sRGB. Levels
có năm numeric fields với kiểm tra thứ tự input/output; Curves sửa năm output anchor tại input cố định.
Regression pointer workflow, giữ ID/enabled/mask, Undo/Redo và `.pr` reopen đã thêm; chưa chạy.
P3 thêm monitor ICC transform tự cập nhật theo monitor, có system sRGB fallback; visual/physical verification
còn thiếu. Tab Thêm mở hộp thoại Selective HSL và Channel Mixer hiện có; các giá trị này chưa có field
inline đồng bộ với property bar. Release test tích hợp vẫn chờ lượt duy nhất cuối; mốc A–E,
renderer/tile/full-res, proof/gamut, retouch nâng cao, plates/PDF-X, physical proof và P8 vẫn mở.
