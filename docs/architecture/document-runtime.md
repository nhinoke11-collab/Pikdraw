# Document và runtime PikDraw

Đối chiếu source hiện tại; contracts C1–C9 quyết định semantics, [registry](../../registry/modules.json)
quyết định dependency. Workflow theo [AGENTS](../../AGENTS.md).
[Kiến trúc](architecture.md) chỉ rõ phần đã có và phần dự kiến.

## Ba biểu diễn cùng nguồn

| Biểu diễn | Dữ liệu | Quy tắc |
| --- | --- | --- |
| Authoring | mm/binary64, IDs, curves, text/font, màu/profile, layers/pages/artboards | Chỉnh sửa qua command, lưu source gốc và metadata |
| Production/3D dẫn xuất | Polygons/toolpath/mesh gắn source revision, parameters/tolerance | Có thể tái tạo; không dùng pixel viewport làm dữ liệu gia công |
| Preview | Camera-local floats, tessellation/tiles/texture, proxy pixels | Có LOD và quota; không thay tài liệu hoặc giảm chất lượng export ngầm |

DPI không nhân số object vector hoặc kích thước document trong memory.
Trang 5×2 m tại 300 DPI không được tự tạo bitmap toàn trang khi mở/zoom.
Raster effects/export lấy region/scale cần thiết và kiểm pixel/scratch budget trước cấp phát.

## Writes, snapshots và lifetime

Document có một logical writer. Transaction validate rồi publish snapshot bất biến;
IDs ổn định, revision tăng cả Undo/Redo. Epoch phân biệt vòng đời tài liệu.
Async proposal gắn document identity/epoch/base revision/job generation và bị từ chối nếu cũ.
UI transient draft không bị automation ghi đè; batch là một Undo unit.

Snapshot giữ immutable assets theo lifetime. Không clone toàn ảnh khi move/duplicate;
đo retention của history, bytes nguồn, decoded/tile cache, IPC, export scratch chung.
Chunk COW, bounded history và cleanup trên background là hướng tối ưu còn phải nghiệm thu.

## IO và storage hiện tại

Payload schema v23 dùng container v23 khi dưới 1 MiB, hoặc chunked STORE/DEFLATE v24;
schema v24 có raster source/sample tables trong container v25; reader nhận v2–v25. File thiết kế
chỉ dùng suffix .pr. Atomic save stream payload theo chunk 1 MiB vào file tạm và patch metadata/CRC;
`NativeFormat::serialize()` vẫn trả vector theo hợp đồng. Load vẫn đọc toàn container và dựng payload
giải nén trong memory. Không còn trần file 256 MiB hay quota cứng 100.000 object/1.000.000 path
element; wire widths và memory quota vẫn áp dụng. Raster descriptors/edit graph và per-import disk
source provider đã có; tile decode/render, portable lazy loading và export theo vùng chưa tích hợp đủ.

Atomic save ghi temporary cùng volume, flush rồi replace; lỗi trước publish giữ target cũ.
Save revision R không được xóa dirty của edits R+1.
Autosave checkpoint chạy trên worker bounded từ immutable snapshot. Startup recovery scan bắt
đầu sau frame đầu, dùng một worker ưu tiên thấp và có thể hủy giữa các checkpoint; kết quả quay
về UI qua window message. Mỗi candidate vẫn được đọc/giải mã full-buffer, nên scan còn dùng RAM
và I/O theo kích thước file; shutdown có thể đợi parse file hiện tại. UI native `.pr` save/open
cũng dùng một worker ưu tiên thấp: save chỉ ghi snapshot bất biến, open chuẩn bị snapshot rồi UI
kiểm tra tab/epoch/revision/view trước khi commit. Open vẫn full-buffer; shutdown chờ operation
hiện tại và open chưa hủy được trong lúc deserialize. Import/export adapter còn synchronous paths.
[PR format](native-pr-format.md) và gói P5 của
[kế hoạch hiệu năng](../roadmap/performance-storage-automation.md) mô tả container hiện tại và các phần
streaming/lazy còn mở; đổi suffix không xác nhận schema hoặc capability.

## Chia công việc

| Role | Trách nhiệm |
| --- | --- |
| UI | Input, overlays/present, progress; tránh chờ compute/network |
| Writer | Kiểm identity, command validation, publish/Undo |
| Render owner | Snapshot mới nhất, tài nguyên GPU, bỏ preview lỗi thời |
| Compute/I/O | Prepare geometry/tile/export/save từ snapshot chốt |
| Worker process | Parser/converter/AI hoặc tác vụ cần tách lỗi, kết quả host validate |

Các role này là thiết kế trách nhiệm, không khẳng định runtime đã tách đủ thread.
Mỗi job có byte/work/time quota, cancellation và kết quả revision rõ ràng.
Nesting/3D/AI phải dùng role này khi được triển khai.

## Nghiệm thu

Đo prepare/validate/commit/invalidate/present riêng; CPU render time không là FPS màn hình.
Load/save/cancel/corrupt input không publish partial document.
Recovery cần fault injection và thao tác người dùng thật; atomic rename không tự bảo đảm mất điện.
Các nguồn PNG/JPEG/TIFF và PDF/AI/CDR/EPS/SVG/DXF normalize qua adapter riêng theo C8/C9.
PR giữ object/source metadata đã normalize; không cấp source-format export parity.
