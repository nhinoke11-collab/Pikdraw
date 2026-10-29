# File thiết kế .pr và hướng lưu tài liệu lớn

Ngày đối chiếu source: 25/10/2026. Đuôi file thiết kế duy nhất là **.pr**.
Payload schema v23 vẫn dùng cho tài liệu không có encoded raster source; schema v24 có bảng raster
source/sample deduplicate theo bytes và được mang trong container chunk PKDR v25. Schema v25 lưu
LayerId/group/role và có cờ cho biết raster tables có mặt hay không; nó dùng container chunk PKDR v26.
Container PKDR v24 vẫn đọc/ghi payload schema v23 lớn.

## 1. Hành vi hiện tại

| Thành phần | Source hiện tại |
| --- | --- |
| Open/Save/Save As và MCP | Chỉ nhận .pr, không phân biệt hoa/thường; MCP tự thêm .pr nếu thiếu suffix |
| Nội dung | Schema v23 cho tài liệu thường; schema v24 có raster tables; schema v25 có metadata layer hierarchy; reader nhận container v2–v26 |
| Container | Schema v23 dưới 1 MiB ghi PKDR v23/header 36 byte, từ 1 MiB ghi PKDR v24. Schema v24 ghi PKDR v25 chunked; schema v25 ghi PKDR v26 chunked; chunks 1 MiB, tổng độ dài 64-bit |
| Nén | Mỗi chunk chọn DEFLATE (zlib-ng compatibility, level Z_BEST_SPEED) nếu nhỏ hơn; nếu không thì STORE. CRC32 kiểm container và từng chunk sau giải nén |
| Giới hạn codec | Không còn trần tổng 256 MiB, 100.000 objects hoặc 1.000.000 elements. Các count/length u32 của schema cũ vẫn có trần biểu diễn u32; string 16 MiB và các quota riêng theo trường vẫn giữ |
| Raster | Profile, descriptor và edit graph theo object; encoded source cùng buffer RGBA8/RGBA16LE giống nhau được ghi một lần ở payload v24 và image record tham chiếu chỉ số |
| Layer hierarchy | Payload v25 ghi LayerId, group_id và LayerRole; file v2–v24 được migration LayerId theo thứ tự layer và role Artwork. Payload v25 có raster-table flag nên tài liệu vector không cần tạo bảng asset giả |
| Memory | Reader cấp quota theo một nửa RAM vật lý còn trống. Save mã hóa payload vào bộ đệm tối đa một chunk 1 MiB, nén rồi ghi chunk thẳng vào temp file; load vẫn đọc toàn file và dựng payload giải nén trước khi parse |
| Recovery | Checkpoint nội bộ .pkdr, không phải định dạng thiết kế để trao đổi |

Nguồn: core/document/include/pikdraw/document/storage/native_format.hpp,
core/document/src/storage/{native_format.cpp,native_chunk_encode.cpp,byte_codec.hpp,object_codec.cpp,file_io.cpp}.
Schema cũ tiếp tục phục vụ migration/regression nội bộ, không mở thêm suffix tài liệu.
Luôn xác minh magic/version/CRC/semantics; đổi tên một file bất kỳ thành .pr không làm nó hợp lệ.
Asset descriptor trên đĩa chưa tự bảo đảm một file .pr có thể chuyển sang máy khác.

## 2. Giai đoạn tiếp theo: package có index và dedup mở rộng

PKDR v24/v25/v26 nén payload tuần tự thành các chunk độc lập, chưa có index để đọc một object riêng.
Save phát payload qua bộ đệm chunk 1 MiB; `NativeFormat::serialize()` vẫn giữ API trả vector cho
các consumer cần bytes trong RAM, còn atomic save gọi `serialize_to()` và không giữ thêm vector payload
toàn tài liệu. Reader vẫn đọc hết file và giải nén payload trước khi parse.
Schema v24 lưu encoded raster sources và các buffer RGBA8/RGBA16LE
giống byte một lần; PDF sources chưa dùng bảng dedup này. Thiết kế ZIP64 dưới đây vẫn là
phương án cần benchmark cho giai đoạn package có index; chưa phải format mà app đang ghi.
ZIP64 phục vụ offsets/kích thước lớn; không đồng nghĩa được cấp phát RAM theo kích thước khai báo.
Tham chiếu [PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

~~~text
design.pr
  manifest.json                 package/schema/min-reader/required-features
  scene/index.bin               IDs, pages, layers, object-to-chunk offsets
  scene/chunks/<chunk-id>.bin    canonical editable objects, float64 geometry
  assets/<sha256>/original       encoded source bytes, deduplicated
  profiles/<sha256>.icc          source color profiles
  provenance/<source-id>.json    adapter/version/hash and fidelity metadata
  previews/thumbnail.webp        optional, derived and disposable
~~~

- Bản v1 dự kiến chỉ STORE và DEFLATE. Scene/metadata nén lossless theo từng chunk;
  JPEG/PNG và các nguồn đã nén ưu tiên STORE, không giải mã rồi lưu lại mặc định.
- Mỗi member có declared compressed/decoded sizes, codec, CRC; asset có SHA-256.
  Hash xác định nội dung và dedup, không phải chữ ký chứng minh tác giả hoặc độ tin cậy.
- Chunk chứa record có length framing, object ID và type/schema. Index ánh xạ IDs trực tiếp;
  không buộc agent/editor đọc toàn scene để lấy một object.
- Float64 geometry, units, curve topology, text Unicode/font identity, colors/ICC, hierarchy,
  clips, effect/edit graphs, layer order, pages/artboards và stable IDs được giữ nguyên.
- Manifest v1 bắt buộc package_version, document_schema, min_reader, document_id, saved_revision,
  required_features, scene_index và asset table. Giới hạn manifest/index phải chốt trong codec
  cùng test trước khi bật writer; không đưa arbitrary app settings hoặc license key vào file.
- Required feature/schema chưa hiểu: từ chối trước khi công bố document. Optional unknown member:
  giữ nguyên nếu có thể round-trip an toàn, nếu không phải báo loss trước Save As.
- Không lưu Undo/cache toàn bộ vào tài liệu mặc định. History/recovery là vòng đời riêng;
  cache thiếu hoặc không hợp lệ thì tính lại từ nguồn, không làm mất khả năng sửa.

Wire format container v24-v26: giữ prefix PKDR 36 byte, version=24, 25 hoặc 26, flags=1, payload length u32
dùng sentinel `0xffffffff`, CRC32 container ở offset 32; extension chứa tổng byte giải nén u64
và chunk count u64. Mỗi chunk có record 16 byte: raw size u32, stored size u32, codec u8,
3 byte reserved phải bằng 0, CRC32 của bytes giải nén u32, rồi dữ liệu chunk. Codec 0 là STORE,
codec 1 là zlib-wrapped DEFLATE. Container v24 chứa schema v23. Payload v24 trong container
v25 thêm ba bảng sau PDF và trước page count: encoded source blobs, packed RGBA8 pixels và
RGBA16LE buffers. Payload v25 trong container v26 thêm raster-table flag, rồi giữ nguyên ba bảng
khi flag bật; khi tắt, image data tiếp tục dùng biểu diễn inline của schema cũ. Mỗi layer v25 ghi
LayerId, group_id và role sau thuộc tính màu. Các image record v24/v25 có bảng ghi chỉ số cho các
buffer này (source/U16 index 0 nghĩa là không có); pixel payload được bỏ khỏi object record.
Nội dung byte giống nhau dùng chung index; descriptor, ICC, dimensions, clip và edit graph vẫn
giữ trong từng object.
Reader đòi chunk count khớp ceil(payload_size / 1 MiB), chunk cuối đúng kích thước còn lại,
không có trailing bytes, CRC hợp lệ và tổng giải nén trong quota bộ nhớ động.

## 3. Ảnh nhẹ, nhiều bản sao và DPI lớn

Một ảnh nguồn chỉ lưu một lần theo hash; các object dùng asset ID + transform + crop + edit graph.
Duplicate 100 bản không được nhân 100 encoded blobs hoặc 100 full-resolution decoded buffers.
Decode theo tile/LOD trong ngân sách cache, ưu tiên vùng nhìn; upload GPU có eviction riêng.
Preview nhanh có thể giảm độ phân giải, nhưng xuất/in sử dụng nguồn gốc và báo lỗi nếu thiếu nguồn.

Kích thước trang tính bằng mm và DPI xuất không cấp phát một bitmap toàn trang khi mở.
Ví dụ bảng 5 m × 2 m ở 300 DPI tương đương khoảng 59.055 × 23.622 pixels,
RGBA8 toàn trang khoảng 5,2 GiB trước scratch. Phải xuất theo stripe/tile với halo cho effects.
Đổi DPI không được tự đổi kích thước vật lý hoặc topology vector.

Chế độ lưu mặc định **Giữ chất lượng gốc**: bảo toàn encoded nguồn/ICC, nén scene lossless.
Chế độ **Tối ưu ảnh** tương lai phải là lựa chọn rõ ràng ở Save As: quality, effective DPI theo
kích thước đặt ảnh, resample policy, estimated size và cảnh báo phần ảnh bị thay đổi.
Ảnh alpha/line art dùng lossless phù hợp; không áp JPEG mất alpha cho mọi ảnh.
Không hứa tỷ lệ nén cố định, không gọi đây là thuật toán nội bộ của CorelDRAW.

## 4. Nhúng nguồn và linked asset

Mặc định file portable nhúng đầy đủ tài nguyên bắt buộc. Linked mode là tùy chọn riêng, có
asset hash/version, relink và trạng thái missing/changed. Không coi absolute path trên máy tác giả
là identity portable; không lưu credential/token. Mở file không tự tải URL hoặc đọc path ngoài grant.
Save portable phải kiểm tất cả nguồn còn sống trước publish; asset mất phải báo rõ, không silently
thay bằng thumbnail. Nguồn cũ được giữ tới khi snapshot/job/Undo cuối cùng nhả ownership.

## 5. Streaming save/open và recovery

1. UI writer lấy snapshot R + immutable asset handles rồi trả control ngay.
2. Worker ghi từng chunk/asset vào file tạm cùng volume; không gom toàn payload vào vector RAM.
3. Kiểm index, checksum và manifest; flush rồi atomic replace. Giữ bản cũ nếu lỗi/cancel.
4. Receipt mang document/epoch/revision/job generation. Save R chỉ đánh dấu R sạch khi vẫn
   đúng phiên bản; sửa R+1 trong lúc save không bị đánh dấu đã lưu.
5. Open đọc manifest/index có giới hạn, publish scene hợp lệ theo chiến lược lazy loading đã kiểm;
   decoded pixels nạp theo viewport, file handle/asset ownership tồn tại suốt job liên quan.
6. Recovery dùng journal/checkpoint riêng có compact định kỳ; không append ZIP mãi làm file phình.
   Nhịp autosave có debounce/coalesce, không xếp hàng mọi revision.

Atomic save v24-v26 hiện encode payload theo từng chunk 1 MiB, nén và ghi trực tiếp vào file tạm;
CRC container kết hợp checksum prefix/body rồi patch header sau khi biết kích thước và số chunk.
`serialize()` vẫn tạo vector đầy đủ theo hợp đồng API. Load vẫn đọc toàn container và tạo payload giải nén
trước khi parse; lazy/index và bounded whole-document memory vẫn là mục tiêu, nên chưa xử lý hết peak
RAM hoặc thời gian UI khi mở file rất lớn. Việc stream save giảm bản sao payload trung gian, không chứng minh
peak RAM, thời gian save hay UI không lag.
Checkpoint .pkdr vẫn là container nội bộ. Atomic replace không thay thế fault-injection tests
cho crash, hết đĩa, flush failure và reopen.

## 6. Giới hạn và dữ liệu chưa tin cậy

Không giải nén ra thư mục tùy ý. Chỉ đọc member whitelist; từ chối absolute/drive/UNC path,
.., symlink, duplicate/case-collision names, nested archives và offsets chồng lấn.
PKDR v24-v26 kiểm tổng decoded bytes theo quota động bằng một nửa RAM vật lý còn trống, count/length
chunk, codec, checksum, chunk boundaries và trailing bytes trước khi công bố snapshot. Container
không còn trần cố định 256 MiB; metadata/asset lengths của schema v23/v24 vẫn có trường u32, strings
giữ cap 16 MiB, pages/layers/effects/ICC/path specs giữ quota theo miền. Độ dài tổng container
v24-v26 và số chunk là u64, nhưng buffer hiện phải vừa trong `size_t` và quota bộ nhớ. Cancellation
được kiểm trong đọc theo khối 1 MiB, CRC/parse và cửa sổ inflate/CRC tối đa 64 KiB; atomic container save
phát payload theo chunk và giữ tối đa một raw chunk cùng buffer nén, nhưng file-load progress và streaming load chưa có,
nên quota tiếp tục là quota bộ nhớ chứ không phải giới hạn streaming. Không giải nén ra thư mục tùy ý,
và không tin declared size nếu chưa qua checked
arithmetic/quota admission.

## 7. Ma trận biểu diễn nguồn và C8/C9

Thay container không tăng capability nhập/xuất. Mỗi adapter đưa nguồn vào model trung lập;
native save giữ phần model đã có, không tự tái tạo semantics mà adapter đã bỏ mất.

| Nguồn/representation | Đường hiện có | Gate còn cần trước khi cam kết |
| --- | --- | --- |
| .pr / PKDR v2–v23 | Legacy reader; schema v23 writer khi không có encoded raster source | Migration fixture và semantics theo suite storage |
| .pr / PKDR v24-v26 | Chunked DEFLATE/STORE; v25 mang schema v24 raster tables, v26 mang schema v25 layer hierarchy | Encoded source và RGBA8/U16 dedup round-trip có regression; layer identity/role/group round-trip được kiểm bằng regression; atomic save/open >256 MiB, peak RAM, UI/cancel latency và progress còn cần đo |
| .pr / package ZIP64 có index | Đề xuất; chưa triển khai | Streaming trực tiếp, seekable object index, dedup cho PDF/profile/preview và malformed input |
| PDF / từng page và object được hỗ trợ | PDFium mapping + nguồn tham chiếu | Appearance/editability/ICC, clipping, rotation theo fixture; export PDF gate riêng |
| AI / PDF-compatible | PDF adapter | Không đại diện AI authoring/live effects; authoring khác từ chối nếu thiếu adapter |
| CDR / family version reader pin hỗ trợ | libcdr → SVG subset | Version/effects/PowerClip/metadata từng fixture; không claim CDR writer |
| EPS / path subset hoặc conversion | Worker riêng | Converted representation phải báo rõ; fidelity/color/source export riêng |
| SVG / static subset | SVG adapter | CSS/defs/clip/filter coverage; script/network bị chặn; export subset riêng |
| DXF / entity subset | DXF adapter | Units/layers/arcs/editable curves và tolerance; CNC phải kiểm theo machine profile |

Đối chiếu [C8](../../contracts/c8-import-fidelity.md), [C9](../../contracts/c9-appearance-effects.md)
và [hướng dẫn vector](vector-resource-interoperability.md). Báo riêng render, editability,
metadata nguồn, export round-trip, production; mỗi mục chưa có oracle là chưa xác minh.

## 8. UI và tiêu chí nghiệm thu

Hiện dùng File > Open / Save / Save As, action.file.open / action.file.save / action.file.save_as,
Ctrl+O/Ctrl+S và icon chuẩn Open/Save hiện có. Bộ lọc và status dùng .pr; suffix khác bị từ chối.
Không thêm toolbox tool, property bar hoặc Properties vì suffix là thuộc tính file.
Tooltip/focus help vẫn mô tả mở/lưu tài liệu; trạng thái lỗi nêu .pr nếu suffix không hợp lệ.
MCP file_name là basename theo grant hiện hành; thiếu suffix thêm .pr, suffix khác không được lưu.

Nén chunk v24-v26 chạy tự động lossless; không có lựa chọn nén riêng trong UI. Tùy chọn Optimize ảnh trong
Save As vẫn là hướng tương lai, chỉ bật quality/effective DPI khi người dùng chọn rõ chế độ đó.
Properties ảnh tiếp tục hiển thị source/effective DPI và crop/edits; cùng validator/command nếu sửa
dữ liệu document. Không thêm toolbox/property controls cho việc đổi container.

Regression hiện có trong `test_pikdraw_document_storage`: round-trip chunked DEFLATE, checksum lỗi,
cancel xác định được giữa các cửa sổ inflate/CRC, và tùy chọn
`PIKDRAW_TEST_LARGE_NATIVE_CONTAINER=1` ghi rồi mở lại `.pr` có kích thước thật trên
256 MiB qua `SaveRecoveryService`. Regression này xác nhận capacity/open/save ở máy test, không đo
peak RSS, frame time, cold start, latency dừng hoặc khả năng chạy job nền.

Gate bắt buộc khi triển khai package:

- PR01: Save/open/reopen, Undo sau mở, stable IDs/units/Unicode/ICC/artboards/effects còn đủ.
- PR02: 100 references một ảnh chỉ có một asset; size tăng theo object metadata; đo peak RSS.
- PR03: Portable file mở trên profile/máy mới không còn cache gốc; không mất ảnh hoặc dùng proxy để xuất.
- PR04: Cancel/crash/đầy đĩa/truncated/corrupt/ZIP bomb không half-publish và không hỏng bản cũ.
- PR05: Save R đồng thời edit R+1, đóng/reopen tab và stale worker không ghi nhầm document.
- PR06: File lớn >4 GiB, offsets 64-bit và lazy assets đo trên corpus thực; không vượt RAM admission.
- PR07: Không đổi semantics/pixels ngoài chế độ lossy được chọn; đo size, open/save time và export fidelity.

Thứ tự và owned paths ở [kế hoạch triển khai](../roadmap/performance-storage-automation.md).
