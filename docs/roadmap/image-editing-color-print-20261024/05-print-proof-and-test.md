# 05 — Quản lý màu in, chế bản và in test

[Kế hoạch chính](../image-editing-color-print-20261024.md). Mục tiêu là đầu ra tái lập và
có thể kiểm tra: chọn đúng điều kiện in, thử ít vật liệu, sửa biến thể đầu ra và biết phần
chưa được đo. Các chức năng dưới đây là thiết kế, chưa gửi bất kỳ lệnh in hoặc tạo profile nào.

## 1. Điều kiện in là nguồn cấu hình dùng chung

`OutputCondition` có ID/version và gồm: loại output/route, profile bytes/hash, device/driver/RIP,
vật liệu/giấy, bộ mực, print mode/resolution/pass, intent/BPC, black/preserve policy, TAC/ink limits
do điều kiện in cung cấp, PPI/dimensions/bleed requirements và condition đo/xem.

Lưu profile riêng cho từng tổ hợp máy–mực–vật liệu–chế độ khi dữ liệu có sẵn. Không gán SWOP,
FOGRA39 hoặc một preset coated cho mọi máy bạt/decal/UV/DTF. Profile tiêu chuẩn có thể là
điều kiện **cần mô phỏng**, trong khi máy proof thực dùng profile khác.
Tên profile giống nhau nhưng bytes khác phải là resource khác. [ICC registry/Fogra](01-references-and-baseline.md) [S26–S27].

| ID | Chức năng in | Phạm vi triển khai |
| --- | --- | --- |
| IE-PR01 | Output Presets | Condition và profile thật; duplicate/version; liên kết với file xuất/test run |
| IE-PR02 | Soft Proof / Gamut | Mô phỏng đích trên display; paper/black, gamut overlay, preserve-numbers |
| IE-PR03 | Preflight | Source/profile, resolution, TAC, black, spot/overprint, assets/fonts, output format |
| IE-PR04 | Separations | C/M/Y/K + spot plates, eyedropper ink%, solo/visible; output toggles tách khỏi view |
| IE-PR05 | Print / RIP Output | Composite driver RGB/Gray, profiled output files, PDF print route; owner conversion rõ |
| IE-PR06 | Test Sheet | Crop 1:1, gray/color/sharpness chart, variant grid, run labels và thumbnails |
| IE-PR07 | Hard Proof | Mô phỏng condition A trên proofer B, profile chain và paper/black policy |
| IE-PR08 | Measurements / Repeat Run | Nhập CGATS/CSV, patch matching, ΔE00/statistics, compare baseline/reprint |

## 2. Bốn tuyến xuất và trách nhiệm chuyển màu

| Tuyến | Dữ liệu và chuyển đổi | Điều kiện chặn / báo rõ |
| --- | --- | --- |
| A — Driver quản lý | Ảnh/tài liệu chuyển đến input RGB/Gray được driver công bố; driver thực hiện device conversion | Không app-convert sang printer ICC rồi để driver convert tiếp; input profile/API cần test |
| B — PikDraw quản lý | App dùng actual source → ICC đích có input model phù hợp driver/API; gửi dữ liệu đã transform qua adapter xác minh được | Driver color correction/ICM phải off; nếu không kiểm tra được thì UI ghi “chưa xác minh”, không tự gắn Verified |
| C — File cho RIP/offset | TIFF CMYK/native hoặc PDF với embedded profiles/OutputIntent, spot/preserve policy theo yêu cầu RIP | Không lấy BGRA display làm CMYK. RIP quản lý conversion hoặc nhận số đã tách theo contract rõ, tránh reseparate hai lần |
| D — Hard proof | Source → condition cần mô phỏng A → profile máy proof B; giữ paper/black simulation theo proof setup | A/B phải là hai vai trò riêng; không thay A bằng B rồi gọi đó là proof máy A |

Số lượng màu mực vật lý của máy không quyết định model dữ liệu driver nhận. Nhiều máy in mực
CMYK/nhiều màu vẫn nhận RGB qua driver. Tài liệu Corel mô tả riêng GDI RGB/Gray và PostScript;
Photoshop cũng tách app/printer-managed conversion. [Corel Print Colors](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Print-colors-accurately.html),
[Photoshop Print with Color Management](https://helpx.adobe.com/photoshop/desktop/print/print-with-color-management.html).

P4 có thể giao file test RGB/CMYK theo capability encoder và một tuyến driver RGB đã đo.
Nếu driver/native spool chưa giữ đúng dữ liệu màu, giao **Export Test Sheet** với run metadata
và đánh dấu direct print chưa đạt; không giả vờ Print đã có chỉ vì mở được dialog.
Mốc B chỉ hoàn tất khi tối thiểu một tuyến direct print thật cùng tuyến export đạt gate tương ứng.

Job có state `Draft → Preflight → Rendering → Spooling → Submitted`, rồi `Completed`, `Failed`,
`Cancelled` hoặc `Unknown`. Render/spool chạy ngoài UI trên snapshot frozen; dialog chọn máy/giấy
chỉ cấu hình job. Ghi OS/RIP job ID khi được cấp. Submitted/completed theo spooler không có nghĩa
bản in đã được đo màu hoặc chưa gặp lỗi vật lý. Sau lỗi không rõ đã submit hay chưa, không tự
gửi lại tạo bản in trùng; UI cho kiểm job ID/trạng thái và chủ động chọn gửi lại.

## 3. Soft proof và gamut đúng vai trò

1. Source profiles được diễn giải đúng trước composition. Graph giữ source/master; proof là view.
2. Dựng simulated output appearance bằng condition profile và intent; sau đó về actual monitor profile.
   Paper simulation và black simulation có flags/capability riêng, không thay toàn canvas bằng một màu nền xám.
3. `Preserve CMYK numbers` có nghĩa kiểm mẫu C/M/Y/K nguyên trạng trong condition được chọn;
   không đồng nghĩa convert CMYK sang profile khác mà vẫn giữ appearance/K tự động.
4. Gamut dùng CMM proof/gamut có reference, so vùng/vectors được biết. RGB round-trip distance
   chỉ là diagnostic riêng, không gắn ΔE hoặc coi là test thuộc gamut.
5. A/B/split view, gamut overlay opacity và color, readout Source/Working/Proof/Output; overlay
   không ghi vào bản xuất sản xuất. Export proof visualization phải có nhãn và profile display rõ.
6. Nếu thiếu monitor profile hoặc simulation capability, UI báo đúng tình trạng. Đổi monitor/profile
   invalidates display/proof cache; không đổi source ICC hoặc regenerate original pixels.

Độ chính xác phụ thuộc profile, màn hình và điều kiện xem. Đây là yêu cầu mô phỏng có kiểm
chứng, không hứa mọi màn hình nhìn giống vật liệu in. Workflow hãng tham khảo:
[Corel Soft Proofing](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Soft-proofing.html).

## 4. Chế bản, tách màu và preflight

| Kiểm tra | Thiết kế |
| --- | --- |
| Process plates | Tạo C/M/Y/K từ native CMYK hoặc conversion theo output; Gray/K-only giữ vai trò; per-channel readout trước/sau |
| Spot plates | Stable ink ID/name/tint/coverage, alternate color/profile cho preview; báo trùng tên khác identity; không gộp mọi spot thành process ngầm |
| Overprint / Knockout | Fill/stroke, image/mask scope, knockout/transparency/painter order theo output model; white-overprint cảnh báo; screen Multiply không thay ink semantics |
| TAC | `100×(C+M+Y+K)` tại pixel sau conversion/composition trong condition; maximum, area over limit, heatmap/ROI; sampled estimate không nhận là full preflight |
| Extra ink | Spot/white/varnish có coverage riêng, không cộng vào con số CMYK TAC rồi gọi giới hạn mực chung; multichannel limits cần condition riêng |
| Black policy | K-only cho role phù hợp, rich black recipe từ condition, photo shadows theo profile/GCR; không tự ép pixel tối thành K100 hoặc bật overprint toàn ảnh |
| TAC correction | Preflight trước; sửa là output variant/reseparation/DeviceLink được duyệt và Undo nếu sửa artwork; không clamp CMY/K đại trà rồi nhận đúng appearance |
| UCR/GCR/DeviceLink | Hành vi phụ thuộc profile/engine, phase P8; không có slider “GCR” giả chỉ sửa K theo công thức cố định |
| Resolution | Effective PPI theo transform và yêu cầu job, linked original missing, upsample cảnh báo; ảnh 300 PPI không chứng minh chi tiết đủ |
| Layout | Scale thật, printable area, clipping, bleed/trim/media boxes, margins; tiling/overlap theo job, không cùng một preset cho mọi phương pháp in |
| Color metadata | Untagged/mismatch/invalid profiles, unsupported node, source sample basis; profile đích khớp file/spool actual |
| Vector + text | Embedded/substituted fonts, spot identity, object overprint, transparency và gradients cùng scene; không chỉ kiểm bitmap đơn lẻ |
| Marks | Registration/crop/control marks có ink/position policy, ngoài artwork/bleed cần bảo vệ; Registration all-plates không dùng làm màu đen artwork |

Mặc định **phát hiện trước, sửa theo thao tác rõ**. Report có severity, affected object/region,
expected/observed, proposed fix và trạng thái “chưa đánh giá” khi thiếu capability.
TAC helper của process paints hiện có không được dùng để báo đã kiểm TAC toàn scene/raster.

Separations Preview chỉ đổi view; output plate enable/disable là lựa chọn job khác và được ghi
vào report. Cần fixture RIP cho overprint/knockout, transparency và spot; sơ đồ RGB gần giống
không đủ chứng minh. [Illustrator Separations](https://helpx.adobe.com/illustrator/using/printing-color-separations.html).

PDF/X ưu tiên X-4 cho workflow có color management/transparency; X-1a là lựa chọn legacy
CMYK/spot với flattening rõ. Mỗi preset có validator và RIP/reference fixture. Nhúng ICC đơn
lẻ không làm PDF thành PDF/X; OutputIntent, fonts/assets và các yêu cầu khác phải được kiểm.
PPI/bleed thêm theo condition, không gán thành ngưỡng phổ quát của PDF/X.
[PDF Association](https://pdfa.org/technical-side-and-requirements-of-pdfx).

## 5. Năm loại bản in test

### 5.1 Crop chi tiết đúng kích thước

Chọn vùng da/tóc/text nhỏ/gradient/vùng tối → lấy từ full-resolution output graph → in đúng
tỉ lệ sản xuất **1:1 theo mm**. Sheet có thước 100 mm, kích thước crop, PPI nguồn/đầu ra và
condition. Fit-to-page là mode riêng có ghi scale; không im lặng co crop để vừa giấy.

Cho nhiều crop trên một tờ, rotate layout không đổi ảnh trong thiết kế. Vùng vượt printable
area được chia trang hoặc người dùng đổi layout; hiển thị chỗ cắt trước khi in.

### 5.2 Bảng biến thể chỉnh màu

Tạo lưới 3×3/5×5/7×7 theo **một hoặc hai tham số** có unit; ô giữa là baseline. Tham số
có thể là Exposure EV, Tint, Saturation hoặc một channel correction khi đúng model/capability.
Ví dụ Exposure -0,2/0/+0,2 EV kết hợp Tint -5/0/+5 tạo 9 biến thể.

Mỗi ô có ID, tham số tuyệt đối/delta, cùng crop/size/sharpen/output profile. Không đổi cả
profile, vật liệu và correction trong một lưới rồi không biết nguyên nhân khác màu.
CMYK plate variation dùng điểm phần trăm kênh có label, kiểm TAC/gamut; không giả slider RGB
Exposure là chỉnh riêng lượng K. Profile A/B là mode comparison riêng, không trộn với lưới correction.

Sau khi chọn ô: Apply to Output Variant hoặc Save Preset tạo **recipe output**; không bake vào
master. Không áp cùng correction để “chữa máy in” cho mọi source/vật liệu tương lai.

### 5.3 Thang xám, dải màu và bảng chẩn đoán

PikDraw tự thiết kế diagnostic chart có:

- Gray ramp và near-black/near-white steps để xem bết tối/cháy sáng; neutral theo RGB hoặc
  CMYK K-only/composite có nhãn, không hiểu lầm CMY bằng nhau luôn trung tính.
- Process primaries/overprints, saturation/tone ramps, brand color patches người dùng chọn,
  gradients và skin-tone/photo crops có nguồn sử dụng hợp lệ.
- Fine lines/small text, sharpen halo crops và dither/gradient bands; scale ruler và printable bounds.
- Patch IDs, reference mode/device values, expected Lab khi có profile/reference, profile hash/run ID.

Chart chẩn đoán phục vụ so sánh, không thay chart characterization toàn gamut hoặc control
strip có license. Không đặt tên/đồ họa Fogra MediaWedge lên chart tự tạo.

### 5.4 Hard proof mô phỏng condition khác

Chọn press/condition A, chọn proofer/profile giấy B, proof intent/paper/black settings, rồi xuất
qua tuyến D đã kiểm. Labels ghi cả A/B. Máy proof không tái tạo hết gamut/white/black của A
thì báo phần không mô phỏng được. Tách **bản thử sản xuất tại máy đích** với **bản proof mô phỏng máy khác**.
[Photoshop Hard Proof](https://helpx.adobe.com/photoshop/desktop/print/print-a-hard-proof.html).

### 5.5 Chart để đo/lập hoặc kiểm tra profile

Verification chart dùng profile/condition hiện có để kiểm so reference. Characterization chart
cần device values chưa được biến đổi thêm, không áp creative correction/proof/output ICC hoặc
driver auto enhancement; tuyến gửi giá trị phải có test. Nếu adapter không bảo toàn được device
values, disable characterization mode và cho export chart theo contract của phần mềm profiler.

P7 nhập/đánh giá số đo và hướng dẫn dùng công cụ profiling phù hợp. **Tạo ICC từ chart, fit LUT,
ink limits/GCR và validation profile mới** là capability P8 riêng; không nhận scanner/ảnh chụp
bản in là phép đo quang phổ đã hiệu chuẩn. Nozzle check/head alignment/linearization phần cứng
của máy/RIP có đường hướng dẫn; không giả lập chúng bằng một nút sửa ảnh.

## 6. Field registry của Output / Test Print

Tất cả là proposed. Các field `view` không ghi artwork; `job` thuộc draft print/export;
`condition` thuộc preset có version; `variant` chỉ sửa output recipe. Apply mới persist preset/variant.

| Field / label EN + VI | Kiểu, unit, range, default | Scope và trạng thái |
| --- | --- | --- |
| `output.condition` Output condition / Điều kiện in | ConditionId/version hoặc Not set | job; chưa chọn thì không báo preflight passed |
| `output.route` Output route / Tuyến đầu ra | Windows system-managed/Driver-managed/App-managed/RIP file/Hard proof | job; chỉ hiện route đã hỗ trợ |
| `output.device/media/ink/mode` Device/Media/Ink/Mode / Máy/Vật liệu/Mực/Chế độ | Unicode IDs/labels, profile association | condition; Unknown giữ state, không đoán theo tên printer |
| `output.profile` Output ICC / ICC đầu ra | Valid resource đúng model, hash + readable label | condition; enable conversion theo engine capability |
| `output.intent/bpc` Intent/BPC / Mục đích/Bù điểm đen | 4 intents; BPC off/on capability; Relative + on khi phù hợp | job/condition; Absolute proof không tự kế thừa BPC của photo preset |
| `output.preserve_numbers` Preserve numbers / Giữ số kênh | Off/process model được chọn | job; phân biệt same-condition với CMYK→CMYK khác profile |
| `output.black_policy` Black policy / Chính sách màu đen | Preserve source/Profile/Role preset/DeviceLink supported | condition; không force K/overprint toàn ảnh |
| `output.tac_limit` TAC limit / Giới hạn tổng mực | Explicit 100..400% cho CMYK; default Not set | condition; no assessed limit khi unknown, spot riêng |
| `output.resolution_min/target` PPI / PPI tối thiểu/đích | Positive PPI, Not set đến khi condition chọn | condition/job; ảnh vốn thiếu chi tiết vẫn được report |
| `output.sharpen` Output sharpening / Nét đầu ra | Off hoặc recipe + amount/radius theo output pixel | variant; sau final resample; default Off |
| `output.alpha/matte` Transparency / Trong suốt/Nền | Keep khi format hỗ trợ, hoặc matte color/profile explicit | job; JPEG cần matte, không matte đen âm thầm |
| `output.format/precision` File format / Định dạng/Độ chính xác | PNG/TIFF/JPEG/PDF subset, U8/U16 theo encoder | job; CMYK PNG không có option giả; down-conversion có report |
| `print.paper/orientation` Paper / Giấy/Hướng | Driver sizes/custom mm supported | job; validate printable area |
| `print.scale` Scale / Tỉ lệ | 1:1 default cho crop test; custom >0; Fit riêng | job; đổi scale cập nhật ruler/labels |
| `print.margins/bleed` Margins/Bleed / Lề/Tràn lề | mm ≥0, theo device/condition | job; bleed không bằng margins; clipping cảnh báo |
| `print.tiles/overlap` Tiling/Overlap / Chia trang/Chồng mép | Bool off; overlap mm 0..100 và nhỏ hơn chiều in | job; label index/direction, không trộn page order |
| `print.copies` Copies / Số bản | Integer 1..999, default 1 | job; page count×copies visible trước submit |
| `print.driver_color_status` Driver color handling / Quản lý màu driver | Verified off/on/Unknown; evidence/device signature | job metadata, không tự set Verified từ checkbox người dùng |
| `proof.profile` Simulate condition / Điều kiện mô phỏng | Profile/condition A, distinct from output B | view/proof preset; source unchanged |
| `proof.paper/black` Paper/Black simulation / Mô phỏng giấy/đen | Bool theo CMM/profile; default Off | view; unavailable có reason |
| `proof.preserve_numbers` Preserve numbers / Giữ số khi proof | Bool theo source/output model | view; khác option export preservation |
| `proof.gamut_color/opacity` Gamut overlay / Màu/Độ phủ cảnh báo | Display color; 0..100%, default 50 | view; không export overlay production |
| `test.type` Test type / Loại in test | Crop/Variations/Diagnostic/Hard proof/Characterization supported | job; Characterization advanced có riêng gate |
| `test.crop` Crop source / Vùng thử | Image/page ROI ID + mm bounds + source revision | job; derive từ full-res, source version frozen |
| `test.axis_x/axis_y` Variation axes / Trục biến thể | Distinct supported param IDs; Y optional | variant generation; không cùng một field ở hai trục |
| `test.step_x/step_y` Step / Bước thay đổi | EV/%/degree đúng parameter; signed finite, default per param | job; values ngoài valid range được đánh dấu/không generate |
| `test.rows/columns` Grid / Hàng/Cột | 3/5/7; single-axis 1 cho chiều kia | job; pagination khi ô nhỏ dưới yêu cầu xem/đo |
| `test.patch_size/gap` Patch size/Gap / Cỡ ô/Khe | mm >0; theo aperture thiết bị và white gap yêu cầu | job; 10 mm là preset chẩn đoán ban đầu, không tiêu chuẩn mọi thiết bị |
| `test.labels/metadata` Labels / Nhãn/Thông tin lượt | Bool on; run/condition/profile/correction/size | job; reference values không được trộn với measured values |
| `measure.file` Measurements / Tệp số đo | CGATS.17 subset hoặc CSV schema có header/units | run; parser validate fields/count/type, không thực thi code |
| `measure.reference` Reference / Dữ liệu chuẩn | Target characterization hoặc baseline measurement IDs/hash | run; không so nhầm source RGB numbers với measured Lab |
| `measure.condition` Measurement condition / Điều kiện đo | Instrument, illuminant, observer, M-mode, backing, timestamp, dry-time | run; Unknown chưa đủ gắn measured pass |
| `measure.tolerance` Tolerance preset / Bộ ngưỡng | Internal/custom/qualified standard version; default Not set | run; tiêu chí và scope hiển thị, không tự “ISO pass” |

Nút Actions: Analyze Preflight, Generate Preview, Export Test Sheet, Print, Save Output Preset,
Apply Selected Variant, Import Measurements, Compare, Repeat Run. Tất cả dùng job service;
Generate Preview không in, Print chỉ submit sau khi job draft/route đã đủ thông tin và người dùng chọn In.

## 7. Nhập số đo và đối chiếu

Đợt đầu đọc CGATS có `BEGIN_DATA_FORMAT`/`BEGIN_DATA` và các field được khai báo: patch/sample ID,
device RGB/CMYK nếu có, Lab hoặc XYZ kèm white/illuminant; hỗ trợ CSV schema tương đương.
Spectral data/CxF đầy đủ cần adapter/version riêng. Unknown fields được giữ metadata hoặc báo,
không đổi thứ tự patch theo vị trí hàng mà bỏ ID.

1. Freeze `PrintRun` trước xuất: snapshot/source/graph/profile hashes, color owner, media/mode,
   engine/build, chart patch map, layout/scale và output file hash.
2. Import file, match patch IDs và chart hash/version; phát hiện thiếu/trùng/ngoài range, order,
   decimal/encoding, NaN/Inf, unit/observer/illuminant/M-mode/backing không tương thích.
3. Lab reference và measurement phải cùng điều kiện để tính ΔE00 có ý nghĩa. Conversion XYZ→Lab
   cần đúng white point; không giả metadata còn thiếu là D50/2°/M1 đã đo.
4. Tính ΔE00 theo vectors [Sharma](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/),
   mean/p95/max, neutral ΔL*/Δa*/Δb*, white/black, critical brand patches và coverage đầy đủ.
5. Hiện bảng measured/reference/difference, chart heatmap, trend giữa các run; click patch xem
   nguồn/correction. Report tách predicted-vs-measured và baseline-vs-new-run.
6. Ngưỡng thuộc tolerance preset có tên/version. Có thể đặt mục tiêu nội bộ ví dụ mean≤2,
   p95≤4, max≤6 ΔE00 và brand≤3; đó là lựa chọn thử nghiệm, **không phải ngưỡng ISO mặc định**.
7. Repeatability và thời gian ổn định mực được ghi theo vật liệu/quy trình. Reprint sau đổi giấy,
   mực, profile, driver/RIP hoặc chế độ in tạo condition/run mới; không dùng lại badge đạt cũ.

Quy trình đo cần thiết bị đo phù hợp, calibrate theo hướng dẫn của thiết bị và ghi điều kiện
M0/M1/M2/M3/backing/observer. [Fogra measurement conditions](https://fogra.org/en/testing/materials-environment/paper-testing).
Ảnh scanner/camera có thể dùng so hình/đăng ký vị trí nhưng không tự trở thành Lab measured reference.

## 8. Mức độ công bố và lưu kết quả

| Trạng thái | Được nói |
| --- | --- |
| Preview only | Đã tạo mô phỏng/ảnh xem theo chain cụ thể; chưa có bản in đo |
| Test exported | Đã tạo file test đủ resolution/profile/metadata; chưa xác nhận driver/RIP hoặc màu vật lý |
| Printed, unmeasured | Có bản in thực với route/condition ghi lại; đánh giá bằng mắt có điều kiện xem, chưa có ΔE đo |
| Measured internal | Có số đo/patch coverage/tolerance preset; đạt hay không trong phạm vi preset nội bộ |
| Qualified contract proof | Chỉ sau control strip hợp lệ/license, điều kiện, tiêu chuẩn/version, tolerances và quy trình verification tương ứng |

Contract proof có yêu cầu riêng; Fogra MediaWedge và chứng nhận không phát sinh từ việc dùng
ICC hay tự thêm logo. [Fogra control medium](https://fogra.org/en/shop/mediawedge-cmyk),
[Fogra prepress certification](https://fogra.org/en/certification/prepress-technology).

Kết quả người dùng chủ động lưu/xuất là product artifact: .pr nếu lưu test layout editable,
TIFF/PDF/JPEG theo capability, report JSON/CSV và measurement source được quản lý. Không nhét
log/debug vào docs. Output regression/probe do agent sinh là generated tạm, dọn theo AGENTS
sau khi đọc kết quả; dữ liệu đo/in test người dùng lưu không bị coi là rác test để xóa.
