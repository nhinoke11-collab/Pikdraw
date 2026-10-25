# 03 — Giai đoạn triển khai và đặc tả UI

[Mục lục](../raster-import-large-assets-20261021.md). P0–P7 là một đợt bắt buộc; P8 là mở rộng sau đó. Không có task claim/lease hoặc yêu cầu tạo nhánh mới. Các tên file mới là đường dẫn đề xuất, không phải file đã tồn tại.

## 1. Thứ tự và phạm vi sửa

```text
P0: baseline + codec feasibility + inventory consumers
  → P1: asset model/store + storage streaming + contracts
  → P2: handle streams + codecs + tile jobs
  → P3: import lifecycle / embedded / linked
  → P4: D2D + CPU tile rendering / adaptive budgets
  → P5: crop / transforms / exposure + native graph persistence
  → P6: Links + Properties + full-resolution export
  → P7: corpus GiB / regression / performance / packaged runtime
P8: mask/paint/advanced filters/AI (sau foundation)
```

Có thể làm adapter trong P2 khi interface P1 đã chốt, nhưng schema, CMake, `object.hpp`, native storage và shell shared files phải được hợp nhất tuần tự. Nếu sau này user yêu cầu nhiều agent, phân công theo các phạm vi dưới đây và không cho hai agent sửa shared files cùng lúc. Phiên lập plan này không tạo agent con.

### P0 — Baseline, quyết định codec và hợp đồng tối thiểu

**Đọc/sửa trong scope:** decoder/adapter, worker raster tests, desktop import consumer và benchmark tooling liên quan; chưa thay model hàng loạt.

1. Chụp diff trước mỗi file đang dirty. Đọc source hiện tại và tìm mọi consumer `ImageObject`, `rgba_pixels()`, `original_data()`, serialization/clone/validation/export. Lập inventory ngắn ngay trong PR/báo cáo hoặc plan này, không tạo một loạt snapshot docs.
2. Ghi baseline trên đúng canonical executable: file source bytes/dimensions/format, first-visible, decode hoàn tất, source copy/hash, host+worker Private Bytes, working set, GPU và disk bytes. Giữ build/hash/source dirty state để người review tái tạo được.
3. Spike handle-backed WIC stream; JPEG native shrink; PNG sequential/Adam7; TIFF tile/strip/BigTIFF; progressive JPEG coefficient backing. Không đánh đồng band output với bounded memory. Pin kết quả chọn backend và những variant được bật.
4. Chốt C6 source lease/hash-pending, tile schema và C5 container U64 mới; C9 raster graph extension. Chạy corpus nhỏ quan sát pixel/metadata trước khi đổi model chung.

**Gate:** AC-RI-01 thiết lập baseline, AC-RI-03/04 feasibility nguồn lớn và codec path. Nếu variant khó cần backend/patch, giải quyết ở đây/P2; không thay yêu cầu thành quota 128 MiB hoặc tự dùng fixture nhỏ hơn. Lỗi format chưa hỗ trợ phải có evidence cụ thể và không được trình bày như đã đạt toàn bộ đợt.

### P1 — Asset model, managed store và storage

**Owned paths:** `core/document/include/pikdraw/document/assets/` và `.../model/raster_edit_graph.hpp` mới; `core/document/src/assets/` mới; `core/document/include/.../model/object.hpp`; `src/storage/{native_format,object_codec,save_recovery}.cpp`; stream container/legacy reader files mới; model/storage/command tests hiện có; contracts C5/C6/C8/C9 trong phạm vi schema đổi.

1. Định nghĩa AssetRef/Revision/Descriptor/graph và representation rõ: `LegacyInline` hoặc `AssetReference`, không suy từ pixel vector rỗng. Validator kiểm đúng backing, dimensions, nguồn và profile. Procedural images/fixtures cũ có adapter tương thích; ảnh lớn không quay về inline để tránh viết consumer.
2. Managed store quản lý blob immutable, cache tile, authoritative edit deltas, lifetime pins và GC. Interface byte-range không lộ vendor/GPU/UI types. Inject I/O service/lifetime, không lưu `HANDLE` vào native model.
3. Storage mới stream manifest/blobs + index U64; chọn version chưa dùng, old readers reject rõ. Save/open không tạo vector toàn file. Legacy read/migration bảo vệ màu/paint/path specs hiện hành.
4. Snapshot/clone/Undo/Redo chỉ copy descriptor/ref và params. Commit asset registration + placement cùng transaction khi import được nối ở P3. Atomic-save, R/R+1 dirty, crash/disk-full và recovery pins là một phần công việc này.

**Gate:** AC-RI-05, 06, 07, 14, 19, 20. Tối thiểu có thể save/reopen một nguồn blob có offset >4 GiB mà không đọc tất cả vào RAM; sparse fixture chỉ chứng minh offset, chưa thay test ảnh thật.

### P2 — Worker nguồn stream, tile decode và codec

**Owned paths:** `modules/interop/include/.../adapters/raster_image_decoder.hpp`, decoder sources tách theo codec/source-stream; raster adapter; `platform/workers/src/v2_worker_protocol.cpp`, source capability helpers, host proposal validation; `platform/workers/include/...`; tests adapters và `raster_worker_import_tests.cpp`; third_party lock/license/build setup.

1. Thêm `Probe`, `DecodePreview`, `DecodeRegion`, `BuildDiskCache`; span API nhỏ cũ chỉ là wrapper/fixture compatibility. Handle production path không đi qua `ReadVerifiedSource`→`vector`→`source_copy`.
2. Custom IStream Read/Seek/Clone/Stat 64 bit, không quyền đọc ngoài capability. Thêm feature negotiation mới, request/response typed, tiled file sink, backpressure/cancel/release. Quota source-byte và decoded-whole-image được thay bằng checked arithmetic + per-request work reservation.
3. JPEG native shrink và sequential cache; progressive coefficient spill có test thật. PNG row/pass cache, alpha/tRNS/palette/16-bit/orientation; TIFF LibTIFF client I/O, classic/BigTIFF, tiles/strips, frame enumeration có paging. Không cấp quyền worker mở tùy ý thư mục của nguồn.
4. Disk cache builder ghi atomic tile/index entries; CRC/hash validation host không dựng toàn frame. Metadata/frame index có windowing và cycle detection. Từng job trả diagnostic codec/layout, không general “file quá lớn”.
5. Ảnh nhúng PDF/CDR dùng `IRasterAssetSink` cùng descriptors; giữ source locator/affine/placement sharing. SVG images chỉ vào scope runtime sau reader/validation gate riêng. Parser nguồn vector vẫn riêng; không flatten toàn scene.

**Gate:** AC-RI-02, 03, 04, 08, 09, 16, 17, 18. Tất cả codecs bật cho large path phải có số liệu scratch và boundedness; profile source lớn được host/worker negotiate tương thích. Không bật fallback decoder trong UI process.

### P3 — Import coordinator và source lifecycle

**Owned paths:** workflow import hiện có, các file `document_workflow_raster_import.*`/`raster_source_broker.*` mới theo domain; import presenter/native picker hiện có; source store composition root; import command/async tests.

1. Tách raster workflow khỏi file import chung (audit khoảng 1.400 dòng) trước khi thêm state machine lớn. Không rewrite các nhánh PDF/EPS/CDR đang dirty ngoài adapter sink cần thiết.
2. Metadata/preview từ pinned source xuất hiện trước full-source copy/hash. Embedded mặc định staged managed store; Linked có status/version và đường Update/Relink. Một transaction đặt asset+ImageObject, Undo/Redo đúng vòng đời.
3. Không bắt chọn trang ở TIFF một trang. Nhiều trang có metadata/lazy list chọn trang/range có thật; trang >256 truy cập được. Hủy chọn không đổi doc. Không import tất cả trang chỉ để chọn một trang.
4. Job cancellation/source close/error/disk-full/recovery theo state machine trong tài liệu 02. Save barrier không chặn message pump/canvas; hiển thị tiến độ thật. Khi import chạm 95% RAM, UI báo đang chờ bộ nhớ; job tiếp tục khi headroom phục hồi và timeout sau 5 phút cộng dồn.
5. Khi production path mới đạt, bỏ **toàn bộ** hard gates 128/192/640/768 ở host/worker/adapter và notes/assertions tương ứng; không tăng chúng thành 1 GiB/4 GiB. Chỉ giữ giới hạn trường/codec có lý do kỹ thuật và per-job scratch được quản lý.

**Gate:** AC-RI-02, 05, 06, 10, 13, 14, 16; initial commit không có object nửa dữ liệu, incomplete cache không giả final-quality.

### P4 — Provider, CPU/GPU cache và scheduling

**Owned paths:** `modules/raster/CMakeLists.txt`, `include/pikdraw/raster/`, `src/{cache,scheduler,tiles}/` mới; renderer provider interface từ core; `core/rendering/src/gpu/d2d/d2d_internal.hpp`, `d2d_render_backend.cpp`, helper image tile files mới; `software_rasterizer.cpp`; desktop composition; registry/CMake tương ứng.

1. Implement CPU cache/LRU/reservation/single-flight, disk cache index và executor injection. Tách tổng budget đa document/jobs khỏi budget mỗi cache. Không coi WIC internal scratch là zero.
2. D2D render visible tile/LOD; upload bounded premultiplied conversion từng tile, cache theo content version thay vì xóa mọi ảnh khi document revision đổi. Áp ICC đúng một lần theo pixel basis. UI render thread không dùng guard tổng máy để từ chối toàn ảnh chỉ vì áp lực RAM; giữ thao tác hủy và báo trạng thái chờ.
3. CPU fallback dùng cùng provider, kiểm Y-up/orientation, pixel center, clip và compositing. Pan/zoom không mutate document. Effect halo/gutter có seam oracle.
4. Low-memory/device-loss/cancel viewport không mất original hay graph. Tile notifications kiểm version/generation trước invalidate/present; không lặp rebuild toàn pyramid khi chọn lại object.

**Gate:** AC-RI-08, 09, 10, 11, 15, 21, 23. D2D hardware và WARP có bằng chứng riêng; CPU time không được ghi là FPS.

### P5 — Lát cắt chỉnh sửa ảnh thật

**Owned paths:** document graph/commands/storage; `modules/raster/src/edits/` và evaluator; presenters bitmap Properties; crop tool/controller scoped, numeric fields; image/pixel/model/storage tests hiện có và test module raster mới khi module chưa có suite.

1. Crop không phá hủy, flip/rotate placement, Exposure v1 theo công thức tài liệu 02. Mỗi operation có type/schema ID, input/output coordinate/sample space, bounds/halo contract và migration rule.
2. Preview draft không tăng revision mỗi lần rê/nhập; Apply/gesture cuối một Undo unit, Esc bỏ draft. Reselect gọi edit node cũ; reordering/enabled state persist. Không áp `Appearance` Path/Text cho Image khi C9 chưa mở scope.
3. Graph dùng chung renderer/export evaluator; cache invalidation theo node params/source version. Dup placement và shared-source edit có semantics rõ. RGB/gray 16-bit đi qua sample precision phù hợp, không bị flatten qua preview RGBA8.
4. Với operation/sample mode chưa có gate, disable field/action có lý do rõ; original vẫn retained. Không tạo UI cho mọi filter tương lai rồi map chúng sang một phép gần giống.

**Gate:** AC-RI-12, 13, 15, 19, 22; source hash nguyên vẹn qua sửa/Undo/save/reopen và chi tiết ở ngoài preview vẫn xử lý được.

### P6 — Links/Properties và output full resolution

**Owned paths:** action IDs/dispatcher/menu registration; image Properties/property bar; Links tab; native raster export workflow; interop raster writer provider API; export job snapshots; locale/help/icon assets; UI feature spec.

1. Implement bề mặt UI ở mục 2; action ID hiển thị/keyboard/Properties đi cùng handler và Undo semantics. Scope selection/mixed/missing/busy rõ.
2. Update/Relink giữ graph, crop/clip, physical bounds và IDs. Embed/Unembed/Package làm streaming copy nguồn, không decode rồi encode lại. Edit Original chỉ do user action; external result vào version mới, không overwrite managed original tại chỗ.
3. Encoder kéo pixels từ provider theo strip/tile cho snapshot đã pin, output size/DPI/profile được chọn. Không yêu cầu `std::span` toàn ảnh ở upstream renderer; không xuất preview để né OOM.
4. PNG/JPEG/TIFF/BigTIFF capability writer cập nhật đúng cái đã triển khai; test full-res crop/exposure và pixel sample ở vùng cache chưa decode trước export. JPEG format limits/loss được ghi riêng; native/self-contained không chứng minh source format round-trip.

**Gate:** AC-RI-07, 12, 14, 17, 20, 22, 23. Nếu system chưa có editor ngoài phù hợp, Edit Original báo không có ứng dụng thay vì shell launch đường dẫn ngoài scope.

### P7 — Gate phát hành và tài liệu hiện hành

**Owned paths:** regression suites đã chạm, benchmark corpus ngoài repo, feature specs/capability registry/README, `docs/project-state.json` snapshot và build/package wiring.

1. Chạy toàn bộ AC trong tài liệu 04 trên đúng binary sau thay đổi; mở rộng suite khi đụng C5/C6/C9 chung. Build app và worker được staging cùng version; không chạy worker cũ từ thư mục khác.
2. Corpus thật ≥1 GiB và BigTIFF >4 GiB; import→move/scale→crop/exposure→save/reopen→export. Thử lại sau xóa derived cache để chứng minh nguồn/graph đủ tái dựng.
3. Báo RAM/thời gian cold/warm, Linked/Embedded riêng, phase timing và hardware/software path. Nếu có ứng dụng nguồn đã cài/license, so sánh cùng máy/file/mode; không có thì ghi chưa đo vendor parity.
4. Cập nhật capability matrix, README raster/interop, feature import/export và project-state ≤8.000 byte sau bằng chứng. Dọn fixture/output/log tạm nhưng giữ regression/corpus manifest có thể tái sử dụng.

**Gate:** AC-RI-01…24; báo đúng case pass/fail/chưa chạy. Không chốt “đã nhanh/tiết kiệm như Corel” chỉ từ build hay test nhỏ.

### P8 — Mở rộng sau foundation

Mask/brush tile COW, levels/curves/white-balance/saturation, blur/sharpen với halo, heal/clone, background removal/AI, adjustment presets, PSD/PSB/RAW adapters và editor ngoài nâng cao. Mỗi nhóm có spec/schema/codec/sample/working-space và oracle riêng. Provider/graph/store đã có phải được dùng lại; tránh mỗi filter tạo một bản full bitmap riêng.

## 2. Đặc tả UI bắt buộc trong P3–P6

Tên action mới bên dưới **là đề xuất**, phải kiểm collision/registration trước khi dùng. English mặc định, tiếng Việt qua catalog; tên file/profile/source metadata không dịch.

| Bề mặt / menu chính | Action semantic ID | Label ngắn / thao tác |
| --- | --- | --- |
| File > Import…; Ctrl+I đã có | `action.file.import` hiện có | Import / Nhập; picker có Embedded / Linked, mặc định Embedded, nhớ lựa chọn rõ |
| File > Package… | `action.file.package_assets` mới | Package / Đóng gói; copy links + native và báo missing; không flatten |
| File > Export…/Export raster hiện có | `action.file.export_raster` hiện có | Export / Xuất; chọn output format/DPI/profile, source/graph full quality |
| Bitmaps > Crop | `action.bitmap.crop` mới | Crop / Cắt; drag handles, Enter/Apply, Esc/Cancel |
| Bitmaps > Adjustments > Exposure… | `action.bitmap.exposure` mới | Exposure / Phơi sáng; focus node Properties hiện có hoặc tạo draft một node |
| Bitmaps > Source > Update / Relink… | `action.bitmap.update_source`, `action.bitmap.relink_source` mới | Update / Cập nhật; Relink / Liên kết lại |
| Bitmaps > Source > Embed / Unembed… | `action.bitmap.embed`, `action.bitmap.unembed` mới | Embed / Nhúng; Unembed / Tách nguồn; chọn path khi tạo linked source |
| Bitmaps > Source > Edit Original | `action.bitmap.edit_original` mới | Edit Original / Sửa ảnh gốc; explicit external app action, không khi hover/chọn ảnh |
| Object > Properties / Window > Properties | Tái dùng IDs đang đăng ký; không tạo panel thứ hai | Focus section Image / Show-hide Properties; không tạo Undo |
| Window > Links | `action.window.links` mới | Links / Liên kết; tab ở right dock, không thêm bar/cửa sổ nổi |

**Standard toolbar:** tái dùng Import nếu đang có; các Source actions nằm menu/Properties/Links, không thêm một hàng nút. **Toolbox:** chỉ Crop trong nhóm image tool khi crop đã làm việc; Import không là drawing tool. **Property bar khi chọn ảnh:** X/Y/W/H theo units hiện có, ratio lock, Crop quick action, mode/status icon và nút focus Image Properties. Không nhồi toàn graph/metadata lên bar.

**Properties > Image:** source filename/mode/status; source dimensions và sample layout read-only; DPI/effective PPI, orientation/ICC basis; frame selector nếu multi-frame; Source actions; crop ROI; graph node list + params/enabled/order/reset. Frame change là command giữ asset/version; mixed selection có Mixed/disabled đúng trường, không chọn ngầm một ảnh đại diện rồi áp sai tất cả.

**Links tab:** danh sách assets có virtualized rows, source mode/status/usage count, chọn usage trên canvas, Update/Relink/Embed. Cần tab riêng vì người dùng phải quản lý nhiều nguồn dù không chọn placement; vẫn dùng right dock hiện có, không cạnh tranh Properties đang edit selection.

| State | UI và hành vi |
| --- | --- |
| Staging | “Đang nhúng ảnh…” + progress bytes có thật; placement vẫn thao tác được; Save có barrier/cancel rõ |
| Preview / Refining | “Đang tăng độ chi tiết…”; không gọi preview là original bị giảm chất lượng |
| Linked / Embedded ready | Badge ngắn + source info trong Properties; selection không tự bật dialog |
| Missing / Modified | Status nguồn + Relink/Update; cache còn có thể xem có nhãn, output chặn nếu thiếu nguồn cần |
| Resource paused | “Đang tạm dừng xử lý ảnh” + Retry/giảm chất lượng xem khi phù hợp; không báo file invalid |
| Decode/Unsupported error | Nêu variant/tài nguyên và thao tác tiếp; không silent blank rectangle hoặc tự bỏ effects |

**Icons:** đề xuất `image-crop.svg`, `image-link.svg`, `image-embed.svg`, `image-update.svg`, `image-exposure.svg` dưới `apps/desktop/resources/icons/`; tái dùng glyph hệ thống đang có khi cùng nghĩa. SVG cùng viewBox/stroke/tokens, có provenance/license, light/dark/high contrast; không dùng icon thương hiệu/emoji. Plan không tạo bitmap/icon giả làm runtime evidence.

**Tooltip/focus help ví dụ:** “Linked image — Keep a reference to the original file”; “Embed — Store an independent original with this document”; “Crop — Hide image edges without discarding the original”; “Exposure — Adjust exposure without changing the original”. Dịch qua locale; accessible name luôn có bằng keyboard, không hover-only.

**Status hints:** import ghi đúng stage; crop “Drag a handle to crop. Enter applies; Esc cancels.”; Exposure hiển thị EV và draft/commit trạng thái; các modifier chỉ quảng bá sau handler/test. Không lộ protocol/class/budget kỹ thuật ra flow thiết kế thường ngày.

**Help sử dụng:** Import → chọn Embedded/Linked → đặt ảnh → mở Image Properties để crop/adjust → Update/Relink khi nguồn thay đổi → Save/Package để chuyển máy → Export độ phân giải yêu cầu. Giải thích ngắn cache xem trước khác nguyên bản; cảnh báo linked thiếu nguồn khi chuyển máy. Có menu help ổn định, F1 chỉ ghi khi binding đã có.

**Layout/accessibility:** keyboard Tab/Enter/Esc, numeric caret/copy/paste/unit validation, labels đọc được và focus rõ; bar/Properties dùng cùng command/validation/scope/Undo. Kiểm 1366×768, 1920×1080 tại scale 100/125/150/200%, theme hiện hành. Không thêm panel tự bật lại sau khi người dùng đóng, không thu chữ để ép controls.
