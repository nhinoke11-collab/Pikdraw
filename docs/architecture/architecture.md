# Kiến trúc PikDraw

Đối chiếu source ngày 23/10/2026. [Registry](../../registry/modules.json) là nguồn dependency,
[project-state](../project-state.json) là snapshot capability. Workflow chỉ ở [AGENTS](../../AGENTS.md).

## Luồng runtime hiện tại

~~~mermaid
flowchart LR
  UI[Win32 input/menu và MCP/COM] --> Q[UI owner: action/workflow/automation]
  Q --> W[Command/transaction: một writer]
  W --> D[Document: IDs, pages/artboards, layers, immutable snapshots]
  D --> R[ViewportRenderer: Direct2D/D3D11 hoặc CPU]
  R --> B[BGRA readback/cache]
  B --> G[GDI canvas và overlays]
  Q --> H[Worker host]
  H --> P[Parser riêng theo format]
  P --> V[Host validate proposal/revision]
  V --> W
~~~

MCP networking/stdio/COM là transport; command vẫn tới writer của document.
Worker không sửa trực tiếp document. Các role không tương ứng một số thread cố định.
Renderer hiện dựng/readback toàn frame; DamageTracker chỉ giảm phần chép GDI.
Qt/Skia, GPU present trực tiếp, render tiles và plugin host chưa là runtime sản phẩm.

## Ranh giới module

| Nhóm | Hiện tại | Hướng mở rộng |
| --- | --- | --- |
| core/geometry, color, typography | Hình học, units, màu/profile, font/shaping | Không phụ thuộc desktop hoặc parser bên ngoài |
| core/document | Commands, snapshots/history, schema v23-v25, chunked v24-v26, streamed atomic save/recovery | Lazy load, shared-asset lifecycle, history budget |
| core/rendering | D2D hardware/WARP, CPU oracle, BVH, geometry/image cache | Frame scheduling, direct present, region/tile/LOD |
| modules/vector | Tools/editable effects/precision | Dùng chung model/commands, giữ source curves |
| modules/raster | Asset store trên đĩa, tile cache/edit graph, source providers cho import và .pr | Decode/render tile, proxy preview và lazy asset integration |
| modules/interop + platform/workers | Adapter và proposal import theo scope | Parser riêng theo format, validate trước atomic commit |
| modules/automation + apps/mcp-server/com-server | Semantic queries, batch, revision, quyền, receipts, projection/cursor và bounded object delta | Nested query, subscriptions, job handles có gate |
| apps/desktop | Win32 input, UI, presenter/workflow và owner queue | Lazy workspace, startup nhỏ, không chứa thuật toán domain |
| modules/production | in_progress | Test sheet, TAC, CGATS ΔE00, .pdmr, WCS print; nesting/CNC planned |
| modules/scene3d | Planned | Scene/mesh/camera riêng, nguồn 2D liên kết bằng IDs/revision |
| packages/plugin-sdk | Planned | Schema/RPC có version; native extension cần ABI review |

CMake liên kết thư viện nội bộ vào desktop EXE; một EXE chính không đo được mức độc lập của module.
Mỗi module dùng public API theo registry. Không chia sẻ con trỏ mutable document qua plugin/process.

## Document dùng chung và công việc nặng

Source geometry dùng mm/binary64, Unicode, source color/ICC, stable IDs và đơn vị hiển thị riêng.
Nesting/CNC/3D/raster giữ nguồn và tham chiếu source IDs, revision, thuật toán, tolerance.
Cache/mesh/toolpath là dữ liệu dẫn xuất; không tự thay nội dung authoring khi workspace đổi.

UI/writer commit ngắn từ snapshot đã chuẩn bị. Compute/I/O/network chạy executor hoặc worker,
có progress/cancel và tài nguyên hữu hạn. Trước commit kiểm epoch/revision/job generation;
kết quả cũ bị từ chối. Không chặn UI trong lúc chờ AI hoặc nesting.

Khởi tạo renderer hiện xảy ra khi tạo document/tab, có D3D/D2D device creation.
Startup còn đọc settings; recovery scan khởi chạy trên worker ưu tiên thấp sau frame đầu, nhưng
vẫn full-buffer đọc từng checkpoint và shutdown có thể chờ parse đang chạy. UI `.pr` save/open cũng
đã chạy nền và áp dụng theo revision; save stream payload vào file tạm, còn open vẫn full-buffer.
Chưa có số cold/warm startup hoặc input-to-present để xác nhận budget. Module nặng tương lai chỉ
khởi tạo khi workspace/job cần dùng.

## Lưu trữ và tài liệu lớn

File thiết kế chỉ dùng .pr. Payload schema v23 dùng container v23 cho payload dưới 1 MiB,
hoặc container v24 chia chunk 1 MiB với STORE/DEFLATE và checksum; schema v24 raster tables dùng
container v25; schema v25 layer hierarchy dùng container v26; reader nhận v2–v26. Atomic save stream payload vào file tạm; load vẫn giữ container
và payload giải nén trong memory. Không còn trần tổng 256 MiB
hoặc quota cứng 100.000 object/1.000.000 path element. Wire widths và memory quota còn áp dụng.
ImageObject vẫn ghi RGBA preview và original encoded bytes; descriptor nguồn trên đĩa
không tự bảo đảm file tự chứa mọi asset. Container version và payload schema là hai version riêng;
xem trạng thái triển khai trong [PR format](native-pr-format.md), không lấy mô tả version cũ làm hiện trạng.

Khổ vật lý/DPI vector không đòi buffer bằng toàn trang. Pixel chỉ cấp cho viewport/effect/export region.
[Định dạng PR](native-pr-format.md) ghi trạng thái đã có của chunk compression và streamed save;
index, lazy load, dedup ngoài raster source/sample, tùy chọn downsample và migration còn mở.

## Tích hợp định dạng và plugin

Đọc [vector interoperability](vector-resource-interoperability.md), C8/C9.
PDF/AI-PDF dùng PDFium mapping, CDR dùng libcdr-to-SVG subset; EPS có adapter path hoặc converter riêng,
SVG/DXF theo capability hiện có. Mỗi format/version/representation có adapter và gate riêng.
Render, editability, metadata, export round-trip và production phải báo độc lập.

Plugin/AI nhận snapshot/semantic handles và gửi validated commands; không chạy script tùy ý.
Quyền explicit có revoke; process isolation/Job Object chưa tự chứng minh filesystem/network sandbox.
License/entitlement thương mại là application service dự kiến, tách khỏi document schema và asset.
Private signing key không nằm trong client; offline cache và xác thực không chặn startup UI.
Hiện chưa có activation, installer/update có chữ ký hoặc commercial release gate đã hoàn tất.

## Thứ tự phát triển

[Kế hoạch hiệu năng/storage/automation](../roadmap/performance-storage-automation.md) là nơi duy nhất
đặt thứ tự nâng cấp nền tảng. Feature plan chuyên ngành còn giữ requirements/UI/acceptance riêng.
Đo baseline trước khi đổi renderer; hoàn thiện memory/asset/storage trước khi quảng bá file lớn,
sau đó thêm production/3D/AI jobs vào cùng scheduler. Không suy FPS từ CPU redraw hoặc screenshot.
