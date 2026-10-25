# 01 — Audit import và nguồn tham chiếu

Trạng thái: audit source ngày 21/10/2026, không phải phép đo hiệu năng. [Mục lục](../raster-import-large-assets-20261021.md).

## 1. Luồng hiện tại và chỗ tốn tài nguyên

| Điểm nguồn đã đọc | Hành vi hiện tại | Hậu quả / việc phải đổi |
| --- | --- | --- |
| `platform/workers/src/source_snapshot.cpp`, `SourceSnapshot::Create` | Copy vào file tạm và SHA-256 bằng buffer có giới hạn | Tốt cho RAM nhưng phải đọc hết nguồn trước khi worker bắt đầu; file GiB chậm first-visible. Giữ chế độ snapshot bảo toàn, thêm capability nguồn đã pin cho đường fast placement |
| `platform/workers/src/v2_worker_protocol.cpp`, `VerifySource` | Worker đọc/hash toàn snapshot một lần nữa trước import | Thêm O(file bytes) trước preview. Chốt contract nguồn immutable/verified range trong C6; không bỏ kiểm tra tùy tiện |
| Cùng file, `ReadVerifiedSource` | `vector.resize(expected_bytes)` rồi đọc hết file | Một nguồn N byte chiếm N byte RAM trong worker; thay bằng read/seek handle 64 bit |
| `modules/interop/src/adapters/raster_image_decoder.cpp` | `source_copy(source.begin(), source.end())`, WIC `InitializeFromMemory(DWORD)` | Thêm một bản nguồn đầy đủ; API length DWORD không phù hợp nguồn >4 GiB; thay bằng IStream trên handle |
| Cùng decoder | Tính `output_bytes`, resize toàn `pixels_rgba`, sau đó copy 64 hàng/lần | Buffer đầu ra vẫn rộng×cao×4. Banding hiện tại không giải quyết full-frame và không chứng minh WIC không cache đầy đủ nội bộ |
| `raster_worker_adapter.cpp`, `StreamRasterImportRecords`; `SendRasterImportProposal` | Phát chunk lần lượt, không giữ vector tất cả record trong production path | Giữ ý tưởng backpressure/streaming; chuyển nội dung sang descriptor và tile độc lập, không dựng toàn `decoded.image` trước khi gửi |
| `apps/desktop/src/workflows/document_workflow_import.cpp`, `DecodeCanonicalProposal` | Ghép mọi pixel chunk thành vector toàn ảnh ở host | Disk spool IPC giúp transport, nhưng host vẫn giữ full decoded frame; thay consumer bằng store/provider theo tile |
| Cùng workflow, đoạn `ReadOriginalSource` sau proposal | Nạp nguồn nguyên vẹn vào `original_bytes` để gắn `ImageObject` | Host tiếp tục giữ N byte nguồn trong RAM; dùng AssetRef tới file/blob được quản lý |
| `core/document/include/pikdraw/document/model/object.hpp` | `RasterSourceMetadata::original_bytes/shared_original_bytes`, `ImageObject::pixels_rgba/shared_pixels_rgba` | Shared pointer giảm lặp placement nhưng asset chính vẫn resident; snapshot/undo còn tham chiếu nó. Model cần descriptor nhẹ và version refs |
| `core/document/src/storage/object_codec.cpp`, `native_format.cpp` | Lưu pixel và nguồn bằng blob/count; native header `payload_bytes` U32; dựng `payload.bytes` rồi append sang `container.bytes` | Save/reopen có thể nhân bản dữ liệu và chặn >4 GiB ở container. Chỉ tăng import limit không sửa được persistence |
| `core/document/src/storage/save_recovery.cpp` | `NativeFormat::serialize` trả vector toàn file; open đọc toàn native vào vector | Save/autosave/recovery cũng phải stream. Chuyển import mà bỏ storage sẽ chuyển lỗi RAM sang lúc lưu |
| `core/rendering/src/gpu/d2d/d2d_internal.hpp`, `D2DImageCache`; `d2d_render_backend.cpp` | Audit 21/10 ghi guard headroom 90%; cache/upload cả `ImageObject`, tạo full BGRA tạm, clear cache khi document revision đổi | Import guard hiện là 95%; renderer vẫn cần tile/content cache và reservation thật để tránh upload toàn ảnh, lặp upload và lỗi device-size |
| `core/rendering/src/software_rasterizer.cpp`, `render_image` | Sample `image.rgba_pixels()` đầy đủ | CPU fallback cũng cần tile provider, không được ngầm decode toàn ảnh |
| `modules/interop/src/writers/raster_image_writer.cpp` và native raster export UI | Encoder có strip scratch nhưng đầu vào là toàn bộ `span` RGBA8 | Tái dùng encoder từng dải; thay nguồn pixel bằng provider/pull callback, kiểm đường render trước encoder |
| `modules/raster/README.md`, registry | Module raster còn planned, dependencies document/color | Đây là nơi đặt scheduler/edit engine; core không được phụ thuộc ngược desktop hay decoder vendor |

Ví dụ tính toán, **không phải RAM đo được**: ảnh 30.000×20.000 có 600 triệu pixel; RGBA8 cần 2,4 tỷ byte, khoảng 2,24 GiB cho một buffer. Nếu có source vector, WIC source copy, RGBA worker, RGBA host và bản serialize, peak có thể tăng nhiều lần. Ngay cả JPEG chỉ vài chục MiB cũng có thể giải nén thành mức này.

## 2. Những thay đổi đang dở phải xử lý có chọn lọc

| Giữ / nâng cấp | Thay / gỡ sau khi pipeline mới hoạt động |
| --- | --- |
| AppContainer, handle capability, source/signature validation, C6 negotiation, stale result rejection, transaction/Undo | Nạp nguyên nguồn qua `ReadVerifiedSource` và WIC memory source copy cho đường ảnh lớn |
| Record streaming/backpressure, spool file, CRC/chunk validation | Ghép toàn pixel frame tại host và giữ original bytes trong `ImageObject` mới |
| Metadata DPI/EXIF/source bpp/ICC/pixel color basis; phép biến đổi ICC đã có cần regression | RGBA8 toàn frame làm authoritative input cho sửa ảnh/xuất ảnh |
| Chọn frame TIFF và test trang thứ hai | Dialog bắt nhập số ở mọi TIFF và trần 1–256; thay bằng metadata thật, chọn trang lazy |
| `ScopedSystemMemoryPressureBypass` như một chỗ chuyển tiếp có scope nếu cần | Bypass toàn bộ pressure check mà chưa có reservation/accounting; trần nguồn 128 MiB, output 192 MiB, worker 640 MiB và hard preflight 768 MiB |
| Test decoder/adapter/worker/desktop hiện có | Assertion/doc khẳng định hard quota là thiết kế cuối, hoặc chỉ kiểm fixture nhỏ để kết luận file lớn đạt |

Các giới hạn thử nghiệm hiện thấy ở `raster_image_decoder.hpp`, adapter, `v2_worker_protocol.cpp`, host workflow, `format_capability_matrix.cpp`, README interop, model comment và feature spec import. Host validator mới đặt quota vào `ImportRasterImageRecord` dùng chung: cần kiểm ảnh nhúng PDF/CDR để tránh áp vô ý quota direct-raster vào vector adapters.

Phiên trước đã chạy 5 test target: capabilities, raster decoder, raster adapter, raster worker và desktop import; **5/5 pass, CTest 221,77 giây**. Chúng chạy trên bản có quota, xác minh regression nhỏ/corpus hiện có; không chứng minh RAM thấp, nhanh như Corel/Illustrator, hỗ trợ 1 GiB hoặc thiết kế tile mới. Chưa có số đo baseline Private Bytes/working set/VRAM hay oracle ảnh thực ở bản mới.

## 3. Hành vi Adobe/Corel được xác minh từ nguồn chính thức

Các mô tả dưới đây là tóm lược hành vi sản phẩm. Chúng không công bố decoder, cấu trúc bộ nhớ hoặc kích thước tile nội bộ.

| Mã / nguồn | Hành vi được tài liệu xác nhận | Điều áp dụng cho PikDraw |
| --- | --- | --- |
| S1 — [Adobe: linked và embedded](https://helpx.adobe.com/illustrator/using/manage-linked-and-embedded-files.html) | Links quản lý linked/embedded, Missing/Update Available, relink/update và Edit Original; embedded thành một phần tài liệu | Tách asset khỏi placement, có trạng thái nguồn và workflow cập nhật rõ; source edits không được tự phá graph hiện tại |
| S2 — [Adobe: tối ưu file AI/PDF](https://helpx.adobe.com/uk/illustrator/kb/optimize-native-pdf-file-sizes.html) | PDF compatibility và việc đưa linked content vào file có thể tăng dung lượng lưu | Đo riêng native save, linked package và preview; không lấy file nhỏ hơn làm bằng chứng RAM import thấp |
| S3 — [CorelDRAW: Import bitmaps](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-Import-bitmaps.html) | Nhập trực tiếp hoặc link ngoài; có cập nhật nguồn; import hỗ trợ crop/resample và status về color mode/kích thước/resolution | Mode nguồn rõ, metadata thật và crop không phá hủy; resample nguồn phải là thao tác riêng có nhãn |
| S4 — [CorelDRAW 2026: bitmap effects](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Applying-raster-effects.html) | Properties FX sửa, bật/tắt, sắp thứ tự, xóa và flatten các hiệu ứng | Source bất biến + graph có node IDs, Properties sửa lại cùng node; flatten tường minh |
| S5 — [Corel SDK: ExternallyLinked](https://community.coreldraw.com/sdk/api/draw/26/p/bitmap.externallylinked) | API phân biệt bitmap liên kết ngoài và nhúng | Mode này phải có trong semantic model/automation, không chỉ checkbox dialog |

Các hãng không tuyên bố “không tốn RAM”. Kỳ vọng sản phẩm là tốc độ và mức dùng tài nguyên tốt trên corpus tương đương; mọi claim so sánh phải đo cùng máy, cùng file, cùng chế độ link/embed và cùng chất lượng hiển thị.

## 4. Cơ sở kỹ thuật cho phương án đề xuất

| Mã / nguồn chính thức | Kết luận dùng trong plan |
| --- | --- |
| T1 — [Microsoft: IWICBitmapSourceTransform](https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapsourcetransform) | Decode có scale/ROI trong codec có thể tránh decode full rồi mới scale. Phải query capability từng decoder; đây là API khả dụng, không phải bằng chứng mọi codec bounded |
| T2 — [Microsoft: GetClosestSize](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapsourcetransform-getclosestsize) | Codec Windows hỗ trợ native scaling khác nhau; không suy từ JPEG sang TIFF/PNG. Agent phải kiểm capability và đo working set thực |
| T3 — [Microsoft: CopyPixels](https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nf-wincodec-iwicbitmapsource-copypixels) | Đây là lời gọi kích hoạt pixel processing; caller chịu buffer của vùng cần. Chia dải đầu ra không tự chứng minh internal memory/CPU hiệu quả |
| T4 — [LibTIFF: BigTIFF](https://libtiff.gitlab.io/libtiff/specification/bigtiff.html), [read encoded tile](https://libtiff.gitlab.io/libtiff/functions/TIFFReadEncodedTile.html) | Classic TIFF và BigTIFF khác layout/offset; nguồn >4 GiB cần offsets 64 bit, đọc theo tile/strip và adapter riêng |
| T5 — [PNG specification: interlacing](https://www.libpng.org/pub/png/spec/iso/index-object.html) | PNG thường tuần tự; Adam7 có nhiều pass. Đường tile random phải dựa cache/transcode trên đĩa, không seek tùy ý vào stream DEFLATE |
| T6 — [libjpeg-turbo API source](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/turbojpeg.h), [documentation](https://www.libjpeg-turbo.org/Documentation/Documentation) | Progressive/transform có intermediate coefficient memory. Giới hạn buffer codec không phải cơ chế tự spill; phải spike memory manager/backing store thật |
| T7 — [libvips: demand-driven evaluation](https://www.libvips.org/API/8.16/How-it-works.html) | Tham khảo mô hình vùng được yêu cầu và pipeline theo nhu cầu; không có bằng chứng Adobe/Corel dùng libvips. Không thêm dependency mặc định nếu adapter nhỏ đáp ứng gate |

## 5. Ma trận định dạng / biểu diễn / fidelity

`Giữ nguồn` dưới đây nghĩa là giữ bytes hoặc linked version có thể kiểm; không đồng nghĩa xuất ngược nguyên cấu trúc sau khi sửa. Đây là ma trận **mục tiêu**, ngoài các subset nguồn đã tồn tại.

| Format/version/representation | Xem/render | Chỉnh sửa | Metadata / nguồn | Export/round-trip | Production |
| --- | --- | --- | --- | --- | --- |
| JPEG baseline/progressive; RGB/gray, CMYK có profile | Reduced decode rồi tile; progressive gate coefficient scratch | Placement + graph raster; CMYK chỉ bật phép đã kiểm working space | Byte nguồn, ICC, EXIF, density, frame locator | Copy original được byte-identical khi không sửa; JPEG encode lại là lossy; không hứa EXIF round-trip đầy đủ | Kiểm riêng profile/output; không thành vector cắt |
| PNG gray/indexed/RGB/RGBA, 1/2/4/8/16-bit, non-interlaced/Adam7 | Decode scanline/pass thành cache disk/pyramid | Graph trên sample precision đã hỗ trợ; alpha/tRNS đúng | Original, palette/tRNS, ICC, pHYs/chromatic metadata theo capability | Native giữ nguồn; PNG writer đích chỉ bật sau gate riêng | Không CNC vector-cut |
| TIFF 6.0 strip/tiled, LE/BE, compression được pin | Tile/strip, crop/ROI, frame lazy | Graph; source sample 8/16-bit và float theo gate từng kiểu | IFD/page, resolution unit, ICC/photometric/orientation | TIFF/BigTIFF writer streaming kiểm từng compression/sample layout | Tách proof/color/separations |
| BigTIFF version 43, offsets/counts 64 bit | LibTIFF riêng, không mặc định WIC hỗ trợ | Như TIFF theo sample/codec | Giữ offset identity, exact source và tags được hỗ trợ | Native container mới >4 GiB; BigTIFF export gate riêng | Không suy từ file đọc được |
| BMP chuẩn hiện có | WIC stream hoặc row reader, tile cache | Placement + cùng graph | Source/DPI/channel layout | Không tự mở BMP export | Không CNC |
| PDF / AI PDF-compatible | Ảnh nhúng đi qua asset sink; scene vector vẫn adapter PDFium/page-set | Chỉ những raster/native vector semantics đã có coverage; AI authoring không được suy ra | Page/object locator, ICC, representation, adapter build | Không coi raster cache là source PDF/AI export; locked PDF cache tách biệt | Chưa có production/fidelity toàn format |
| CDR family reader libcdr đã pin | Ảnh nhúng vào asset store, placements chia sẻ; vector không flatten | Canonical objects/graph cho mapping đã kiểm | Source IDs/layer/affine/embedded resource provenance | Không CDR authoring round-trip | Gate riêng theo family/feature |
| SVG static subset | `<image>` chỉ sau capability mới; không fetch ngoài broker | Preserve image transform/clip + graph khi biểu diễn được | MIME/base64/external ref theo C8; không bật external mạng ngầm | Export embedded/link SVG phải gate riêng | Không suy ra production |
| EPS / DXF subset | Không đổi parser/coverage; chỉ nhận raster nếu adapter thực sự hỗ trợ | DXF geometry độc lập; EPS conversion có nhãn | Giữ conversion provenance | Không bật ảnh nhúng hoặc export chỉ vì asset model mới | Gate riêng |
| AI authoring không PDF, PSD/PSB/RAW | Không thuộc đợt này | Không bật UI sửa layers/RAW | Không giả vờ giữ cấu trúc chưa đọc | Đề tài adapter sau | Không áp dụng |

Các input từ mạng chưa thuộc đợt này. Sau này broker tải/cấp handle riêng; codec không được mở URL hoặc truy tìm file theo path trong dữ liệu nhập.
