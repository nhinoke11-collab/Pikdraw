# 02 — Danh mục chức năng và ngữ nghĩa xử lý ảnh

[Kế hoạch chính](../image-editing-color-print-20261024.md). Tất cả ID `IE-*` dưới đây là
**yêu cầu dự kiến**, không phải capability runtime hiện có. P0–P8 là giai đoạn ở tài liệu 06.
Range số là thiết kế PikDraw; không công bố mô phỏng thuật toán độc quyền của Photoshop/Corel.

## 1. Ảnh nguồn, hình học và độ phân giải

| ID | Chức năng | Thiết kế và phạm vi | Bước |
| --- | --- | --- | --- |
| IE-G01 | Image Info | Pixel W/H, bit/channel, model, profile thực, alpha, orientation, source/link status; copy metadata được | P1–P3 |
| IE-G02 | Crop / Uncrop | Crop không phá hủy; free/aspect/fixed pixel hoặc mm+PPI; kéo handle và nhập số; khôi phục toàn nguồn | P1 |
| IE-G03 | Flip / Rotate / Straighten | Lật H/V, góc chính xác, đường chân trời; right-angle có thể không resample; bố trí bounds/anchor rõ | P1–P2 |
| IE-G04 | Resize / Resample | Tách đổi kích thước đặt, metadata PPI và thay số pixel; giữ tỉ lệ; nearest/bilinear/bicubic/Lanczos có gate | P2 |
| IE-G05 | Perspective / Distort | Bốn góc/homography, grid, constrained preview; resample từ nguồn một lần tại evaluation; invalid transform bị chặn | P5 |
| IE-G06 | Effective PPI | PPI theo kích thước đặt cuối, kể cả scale không đều; threshold do output condition, không gán mọi banner thành 300 PPI | P2–P4 |
| IE-G07 | Embedded / Linked | Source/version/hash, missing/modified/relink/update/package; đổi nguồn kiểm tương thích graph và mask | P1, kế thừa ảnh lớn |
| IE-G08 | Export Edited Image | Selection/page/crop, px/mm/PPI, profile/model/bit depth, alpha/matte, sharpening; dùng master và graph đủ chất lượng | P2–P6 |

`PPI_x = width_px × 25,4 / placed_width_mm`; tương tự trục y sau transform hợp lệ.
Với shear/perspective, báo sampling density theo vùng hoặc min/max; không dùng một số PPI giả cho toàn ảnh.
Printer DPI là độ phân giải thiết bị, PPI là mật độ pixel ảnh; hai giá trị có label riêng.

## 2. Ánh sáng và sắc độ

| ID | Chức năng | Tham số / yêu cầu | Bước |
| --- | --- | --- | --- |
| IE-T01 | Histogram / Clipping | Composite/channel; linear hoặc encoded có label; count/scope, black/white clipping, mask ROI; approximate/final riêng | P2 |
| IE-T02 | Exposure | EV; nhân linear light, giữ alpha, không clip float giữa pipeline chỉ để hiển thị | P1–P2 |
| IE-T03 | Light | Exposure, Contrast, Highlights, Shadows, Whites, Blacks; chế độ advanced có tonal width/local radius; identity preset | P2 |
| IE-T04 | Levels | Input black/white, midtone gamma, output black/white; composite hoặc từng channel; eyedropper đen/xám/trắng | P2 |
| IE-T05 | Curves | Điểm số input/output, thêm/xóa/kéo, composite/channel, picker lên ảnh, histogram; preset và reset channel/all | P2 |
| IE-T06 | Auto Tone / Auto Levels | Phân tích histogram deterministic; percentile cắt được xem và chỉnh; kết quả thành node/parameter sửa lại được | P2 |
| IE-T07 | Local tone / Dodge-Burn | Exposure mask theo shadows/midtones/highlights, bảo vệ hue; stroke lưu riêng, không sửa pixel nguồn | P5 |
| IE-T08 | Black & White / Duotone | Grayscale mix có kiểm soát sắc gốc; tint sáng/tối. Duotone có tên mực và plate riêng chỉ mở sau gate spot | P5 / P8 cho duotone |

Exposure dùng `linear_out = linear_in × 2^EV`. RGB mã hóa phải giải transfer function của
working space trước. Alpha không qua gamma/EV. Giá trị vượt [0,1] trong float có thể được giữ
để chỉnh tiếp; preview/output SDR cần tone/clip policy tường minh.

Levels phiên bản đầu dùng `u = clamp((x - black_in)/(white_in - black_in), 0, 1)`;
`y = black_out + (white_out - black_out) × u^(1/gamma)`. Lưu rõ x/y thuộc encoded RGB,
ink channel hoặc Gray nào. `black_in < white_in`, gamma dương; kiểm NaN/Inf và channel tồn tại.

Curves lưu điểm theo [0,1], x tăng nghiêm ngặt; interpolation PCHIP shape-preserving v1,
không overshoot ngoài miền segment do spline tự do. Cho y không đơn điệu để tạo hiệu ứng,
hiển thị clipping; không tự ép đường cong trở thành monotonic. Đổi interpolation/kernel cần
semantic version; graph đã lưu không đổi kết quả khi nâng ứng dụng.

Light/Highlights/Shadows cần thuật toán riêng có version, tonal masks/edge policy và vectors.
Không lấy sáu slider đổi tên trên cùng một công thức brightness làm sáu chức năng giả.
Chi tiết bị clip trong JPEG không thể khôi phục như dữ liệu RAW chưa bị clip.

## 3. Màu sắc và cảm nhận màu

| ID | Chức năng | Tham số / yêu cầu | Bước |
| --- | --- | --- | --- |
| IE-C01 | White Balance | Eyedropper trung tính, Temperature/Tint tương đối cho RGB đã render; Kelvin có ý nghĩa hiệu chuẩn chỉ với RAW/profile phù hợp | P2; RAW P8 |
| IE-C02 | Hue / Saturation / Lightness | Master và dải màu, center/width/feather trên vòng hue; picker; Colorize tách mode | P2 |
| IE-C03 | Vibrance | Tăng saturation có trọng số theo màu hiện có; skin protection là tùy chọn thuật toán cần corpus riêng | P2 |
| IE-C04 | Color Balance | Cặp cyan-red/magenta-green/yellow-blue theo shadows/midtones/highlights; Preserve luminosity có định nghĩa | P5 |
| IE-C05 | Replace Color | Sample/color range, tolerance, feather, target hue/chroma/lightness, preserve shading, mask trước khi áp | P5 |
| IE-C06 | Selective Color | 9 color families, Relative/Absolute, C/M/Y/K adjustments; RGB creative adjustment, không phải separation | P5 |
| IE-C07 | Sample & Target / Match Color | Chọn source/target patches, Lab/RGB reference, giữ luminance/chroma tùy chế độ; không áp một pixel noisy cho toàn ảnh | P5 |
| IE-C08 | Gradient Map / Colorize | Ánh xạ tone sang gradient có profile và stop alpha; interpolation rõ, masks/opacity; không dùng nhầm vector gradient ID | P5 |
| IE-C09 | Color grading / LUT | Shadows/midtones/highlights wheels, balance; LUT .cube subset có input/output space, domain, interpolation và loss report | P5; log/HDR P8 |

“Sắc độ” trong UI phải phân biệt **Hue / Sắc màu**, **Saturation / Độ bão hòa**,
**Lightness / Độ sáng HSL**, **Luminance / Độ chói tuyến tính**, **L* / Độ sáng Lab**.
Không dùng chung một nhãn “Độ sáng” cho tất cả rồi ghi vào sai channel.

HSL v1 được tính trên encoded RGB của working space đã khai báo; hue wrap qua 0°/360°,
gray có hue undefined và không nhảy màu khi kéo saturation từ 0. “Preserve luminosity”
phải chỉ ra metric được giữ và sai số, không đồng nghĩa giữ cả Lab L* và linear luminance.

Selective Color cần công bố weighted-family function và clamp/version của PikDraw.
Relative tác động theo thành phần hiện có, Absolute tác động theo bước tuyệt đối trong miền
adjustment đã định nghĩa; kiểm white/black/gray/overlap. Slider C/M/Y/K ở đây không thể dùng
để báo TAC máy in. Chỉnh lượng mực CMYK thật dùng IE-K04 sau profile/output binding.

## 4. Kênh màu, alpha và mực

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-K01 | Composite / Channel View | RGB R/G/B, CMYK C/M/Y/K, Gray và alpha; gray/tinted display; eye/solo chỉ đổi view | P2–P3 |
| IE-K02 | Channel Histogram / Samplers | Histogram theo channel; sampler nhiều điểm, average 1/3/5/11 px, source/working/proof/output numbers có label | P2–P4 |
| IE-K03 | Channel Mixer | Ma trận source→output, constant, monochrome; tổng weights hiển thị; không tự normalize khi người dùng chưa chọn | P5 |
| IE-K04 | Native CMYK Channel Edit | Curves/Levels/brush correction trên C/M/Y/K, alpha riêng; giữ separation/K khi không yêu cầu reseparate; kiểm TAC sau composition | P3 cơ bản, P5 brush |
| IE-K05 | Channel to Mask / Mask to Channel | Copy channel hoặc tổ hợp thành mask độc lập; edit mask không mặc nhiên đổi channel mực | P3–P5 |
| IE-K06 | Split / Combine / Calculations | Export component Gray TIFF; combine có kiểm size/profile/order; multiply/add/subtract/screen subset có version | P5 |
| IE-K07 | Spot Channels | Stable ink ID, Unicode name, tint/coverage plane, alternate Lab/profile, opacity/solidity chỉ để preview; plate output riêng | P6–P8 |
| IE-K08 | Lab Components | L*/a*/b*, ranges và D50 PCS/white adaptation rõ; channel edit riêng, conversion explicit; không giả Lab bằng 3 slider RGB | P8 |

Chế độ **view channel**, **edit channel** và **ink output plate** là ba state khác nhau.
Eye-off trên Channels/Separations không được làm mất mực trong export; muốn tắt plate khi xuất
phải dùng lựa chọn Output riêng với report. Thay visible channel không tạo document Undo.

CMYK 4×16 bit là 64 bit/pixel chưa kể alpha; không gọi “CMYK 32 bit” rồi hiểu nhầm
32 bit cho mỗi channel. Schema dùng `sample_type + bits_per_channel + channel_layout` tách biệt.

## 5. Vùng chọn và mask

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-M01 | Basic Selection | Rectangle/ellipse/lasso; replace/add/subtract/intersect; undo vùng chọn tách history artwork theo policy | P2 |
| IE-M02 | Brush Mask | Size/hardness/opacity/flow/spacing; tablet pressure capability; paint/reveal/erase; stroke chia tile nhưng một Undo | P2 cơ bản / P5 pressure |
| IE-M03 | Color / Luminance Range | Picker, tolerance, smoothness, contiguous/all, hue hoặc Lab metric có version; xem mask trước khi Apply | P5 |
| IE-M04 | Refine Edge | Grow/shrink, feather, smooth, contrast/shift edge; hair/decontaminate là bước độc lập, có plate/matte tests | P5 |
| IE-M05 | Mask Operations | Invert/disable/delete/duplicate/link/unlink; raster mask khác vector clip; density/feather và overlay | P2–P5 |
| IE-M06 | Vector Mask | Lấy path canonical, giữ editable path/transform; rasterize coverage theo tile; không phá PowerClip hiện có | P5 |
| IE-M07 | Saved / Luminance Masks | Mask resources có ID, named groups, reuse có version; cập nhật mask dùng chung là command tường minh | P5 |
| IE-M08 | Subject / Background Mask | Model tạo đề xuất alpha, refine thủ công, giữ source; local/cloud adapters có quyền và giới hạn riêng | P8 |

Mask dùng coverage [0,1], không bị color-managed. Không gộp alpha nguồn, opacity node,
opacity object, vector clip và mask thành một giá trị. Vùng chọn tạm là session draft;
chỉ mask đã gắn/được lưu mới thành dữ liệu bền. Pixel alpha=0 không được gây viền màu sau filter/export.

## 6. Sửa chi tiết và nét vẽ

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-R01 | Clone Stamp | Source anchor, aligned/unaligned, sample current/current+below/composite, brush transform và clipping | P5 |
| IE-R02 | Healing Brush | Lấy texture từ source, hòa biên ánh sáng/màu ở target; solver radius và work bound; vùng không hội tụ báo rõ | P5 |
| IE-R03 | Spot Healing | Context neighborhood cố định, preview, deterministic seed nếu ngẫu nhiên; không đồng nhất với AI remove | P5 |
| IE-R04 | Patch | Source/target region, transform, blend edge; giới hạn ảnh lớn và cancel; lưu patch/source sampling revision | P5 |
| IE-R05 | Dodge / Burn / Sponge | Exposure/saturation strokes, tonal ranges, protect hue; thông số áp lực/flow chung với brush mask | P5 |
| IE-R06 | Paint / Erase | Lớp raster sửa độc lập, foreground/profile, opacity/blend subset; erase coverage của lớp sửa, giữ original | P5 |
| IE-R07 | Advanced Cleanup | Red-eye, dust/scratch; frequency separation recipe; content-aware fill/inpaint có gate riêng | P5 red-eye/dust; P8 còn lại |

Retouch là **patch/tile delta bất biến** hoặc stroke recipe có sample snapshot xác định.
Không lưu pointer vào canvas preview; clone lấy từ composite phải chốt revision/graph prefix
để reopen/reorder không tạo vòng lặp. Thuật toán heal được kiểm cả biên tile, biên selection,
vùng trong suốt và ảnh CMYK; thuật toán RGB-only phải yêu cầu một conversion node tường minh.

## 7. Chi tiết, filter và phục hồi ảnh

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-D01 | Unsharp / Sharpen | Amount/radius/threshold, luminance-only, halo preview; radius theo source pixel | P5 |
| IE-D02 | Output Sharpening | Theo kích thước/PPI output và vật liệu; bước cuối sau resample; không ghi đè capture/detail sharpening | P4–P5 |
| IE-D03 | Noise Reduction | Luminance/chroma, detail preservation; median/bilateral trước, phương pháp nâng cao theo corpus | P5 |
| IE-D04 | Clarity / Texture / Dehaze | Tách scale local contrast/texture và haze; có halo/gamut tests, không ba tên cho một sharpen kernel | P5 |
| IE-D05 | Blur | Gaussian sigma, motion length/angle, lens blur có mask/depth riêng; edge mode và alpha đúng | P5; lens/depth P8 |
| IE-D06 | Grain / Dither | Grain amount/size/roughness/seed; output dither giảm bit depth là thao tác khác, xuất tái lập theo source coordinates | P5; dither P2 |
| IE-D07 | Lens / Geometry | Profile lens, distortion/chromatic aberration/vignette, copyright dữ liệu lens; không tự nhận mọi máy ảnh | P8 |
| IE-D08 | Super Resolution | Resample truyền thống trước; AI upscale riêng có model/version/source provenance, không bịa chi tiết thành nguồn thật | P8 |
| IE-D09 | Creative Filters | Invert/threshold/posterize, gradient map, duotone/halftone preview; in tram sản xuất cần RIP gate | P5 cơ bản / P8 tram |

Kernel, edge extension, support radius/halo, working space, precision và scale của mỗi filter
phải nằm trong node descriptor. Preview LOD không được đổi tham số làm radius cuối khác bản xuất.
Global histogram/noise estimation là job thống kê riêng; không phân tích mỗi tile một kiểu rồi tạo đường ghép.

## 8. Quản lý màu và đầu ra

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-CM01 | Image Color Settings | Source model/profile, source/working/output precision, untagged/mismatch policies; per-image khác page default | P2–P3 |
| IE-CM02 | Assign Profile | Giữ số channel, đổi interpretation; scope ảnh/selection/document tách rõ; preview và một Undo | P3 |
| IE-CM03 | Convert Profile | Nguồn/đích thực, bốn intents, BPC theo capability; node conversion giữ original, export variant không đổi master | P3 |
| IE-CM04 | Working RGB | sRGB/Adobe RGB/Display P3, profile khác qua load/validate; wide-gamut 16-bit pipeline và output warning | P2–P3 |
| IE-CM05 | CMYK / Gray | CMYK8/16 nguồn/đích, giữ numbers đúng điều kiện; Gray profile-aware; K-only khác RGB equal channels | P3 |
| IE-CM06 | Monitor Pipeline | Profile theo monitor, generation khi đổi màn hình/profile, proof composition cuối; SDR trước, HDR riêng | P3; HDR P8 |
| IE-CM07 | Soft Proof / Gamut | Profile điều kiện in, paper/black simulation, preserve numbers, gamut mask theo CMM có oracle | P3–P4 |
| IE-CM08 | Presets and Diagnostics | Profile missing/invalid/unsupported, intent fallback không im lặng; preset chứa profile hash và output condition | P3–P7 |

Các chức năng **preflight, separations, overprint, TAC, black policy, PDF/X, hard proof,
test chart, variation sheet, measurement và repeat run** được đặc tả đầy đủ ở
[05 — Màu in](05-print-proof-and-test.md), dùng cùng source/graph/color service.

## 9. Năng suất và mở rộng nâng cao

| ID | Chức năng | Thiết kế | Bước |
| --- | --- | --- | --- |
| IE-A01 | Adjustment Stack | Thêm/sửa/reorder/enable/reset/remove node, before/after, copy recipe; node unsupported có trạng thái rõ | P1–P5 |
| IE-A02 | Presets | Category/favorite, schema+semantics version, profile requirements, referenced resources; preset không kèm source khách hàng | P2–P5 |
| IE-A03 | Batch / Sync | Chọn ảnh hợp lệ, relative/absolute policy, per-image mask khác; progress/cancel/errors; output tên riêng | P5 |
| IE-A04 | RAW Develop | Camera subset, demosaic/WB/highlight recovery, camera/lens profile, sidecar giữ nguyên RAW, worker adapter | P8 |
| IE-A05 | PSD / PSB Exchange | Layer/mask/blend/adjustment subset theo version; unsupported giữ original và report, không hứa PSD round-trip toàn phần | P8 |
| IE-A06 | AI Assist | Background/subject, remove, denoise/upscale/harmonize theo model; preview/proposal/cancel/epoch, local hoặc opt-in network | P8 |
| IE-A07 | Advanced Production | DeviceLink/black-preserving reseparation, DeviceN/white/varnish/duotone; RIP-specific contracts | P8 sau P6–P7 |

Trace bitmap là workflow vector đã có plan riêng. AI Generate/text-to-vector trong các bản mới
của Corel/Illustrator là tham khảo mở rộng; không làm điều kiện cho chỉnh màu và in test cơ bản.

## 10. Bộ quy tắc thuật toán xuyên suốt

1. RGB exposure/convolution và alpha interpolation dùng miền đã khai báo; HSL/curves có thể dùng
   encoded RGB, CMYK curves dùng ink channels. Không ép mọi phép toán vào linear RGB rồi đổi nghĩa.
2. Giữ source bit depth/profile/channel order. Internal float32 cho evaluation màu, float64 cho
   parameter/geometry khi cần. BGRA8 chỉ là surface trình bày; conversion xuống 8 bit ở output có policy.
3. Blend/filter spaces của ảnh mới có semantic ID. Không sửa mặc định C9 của vector/shadow/gradient
   đã lưu; compatibility adapter giữ legacy cho đến khi người dùng chọn chuyển.
4. Color conversion làm trên straight color, alpha đi riêng. Convolution/resample có alpha dùng
   premultiplied working colors đúng miền; xử lý alpha=0 an toàn, không halo ở nền trắng/đen.
5. Mask composition/retouch dùng hệ tọa độ node khai báo. Reorder qua geometry node phải remap
   có kiểm chứng hoặc báo không hợp lệ; không âm thầm trượt vùng da/tóc đã chỉnh.
6. Noise/grain/inpaint ngẫu nhiên có seed; stable across tile order/thread count/export runs theo
   tolerance đã chốt. Cancel không để lộ nửa stroke/node/asset vào document.
7. Mỗi chức năng cần vectors identity/boundary/negative, save/reopen và export oracle độc lập.
   Không coi icon, field trong model hoặc thumbnail thay cho thuật toán và hành vi thật.
