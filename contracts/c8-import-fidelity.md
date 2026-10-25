# Contract C8: Import staging, provenance và fidelity

- Version: 1.1.0 (đồng bộ source; không đổi enum/wire/storage trong lượt này).
- Audited: 2026-10-24, source đang làm việc.
- Status: adapters, host validation, desktop review/apply và native page fidelity reports đã có; full per-element provenance/oracle, source round-trip và production qualification còn giới hạn dưới đây.
- Producers: per-format adapters in `modules/interop`, supervised by worker host in
  `platform/workers`.
- Consumers: document writer/storage, renderer, geometry, color, typography, production and
  desktop workflow.
- Depends on: C1 units/transforms/curves, C2 writer/snapshots/transactions, C4 color/typography,
  C5 native storage, C6 worker isolation/lifecycle.

## 0. Phạm vi hiện có trong source

Pipeline v2 không còn chỉ là proposal: desktop nhận records đã validate, dựng object/page/asset,
đưa report tới review và áp dụng qua document command hoặc replace lifecycle. Import vào document
và Open document mới có semantics khác nhau; xem C2. Hủy/loại proposal trước apply không làm bẩn
document. Bitmap placement có bước preview/click/cancel riêng, không đồng nhất preview với commit.

| Nguồn / representation | Adapter hiện có | Giới hạn cần giữ rõ |
| --- | --- | --- |
| SVG static 1.0/1.1 subset | Built-in worker; path/text/group theo subset, một số presentation opacity attributes. | Không phải toàn bộ CSS/filter/mask/gradient. Group opacity và effect/blend không hỗ trợ không được nhận là native tương đương. SVG export có subset riêng. |
| DXF ASCII `dxf-entities-v1` | LINE/ARC/CIRCLE/LWPOLYLINE 2D, đơn vị tường minh và layer theo profile. | Không có DXF export; blocks/INSERT, paperspace và entities ngoài profile không tự trở thành supported. |
| PDF / AI PDF-compatible | PDFium, page set editable path/text/image; PDF còn có locked-source intent riêng. | AI authoring/private data không được đọc đầy đủ. Embedded fonts, exact layout, clipping/effects/color và OCG membership còn giới hạn. |
| CDR RIFF/ZIP | libcdr -> SVG normalization -> native records; embedded bitmaps và geometric clipping groups theo subset. | Không dùng thumbnail làm nội dung import. Một trang theo profile hiện có; version do parser chấp nhận không chứng minh mọi effect của version đó. |
| EPS | Plain DSC operator allowlist; DOS EPS qua Ghostscript hệ thống cô lập -> PDFium. | Conversion phải được báo; Ghostscript không bundle. Source layers, effects, color và round-trip không được giữ đầy đủ. |
| PNG/JPEG/TIFF/BMP | WIC -> RGBA8 ImageObject, source metadata/encoded bytes theo profile. | Ảnh không thành vector/CNC geometry. Host còn full-frame buffers; record U64 chưa mở quyền import/save mọi nguồn >4 GiB. |

Adapter/source-version authority là [capability matrix](../modules/interop/src/format_capability_matrix.cpp),
[interop README](../modules/interop/README.md) và code adapter tương ứng. `SupportedPrototype`
chỉ là trạng thái subset; boolean `IsFormatSupported` không cấp quyền cho mọi feature.
PDF locked-source giữ PDF bytes và screen preview riêng; preview cache không phải nội dung editable
hoặc chứng minh đã có print-quality raster export.

### Report đang lưu thực tế

`ImportFidelityReport` trên page lưu format/profile, source byte count, source page index/dimensions,
coverage/appearance/editability/production và issues (severity, locator, feature/diagnostic code).
PKDR từ v20 lưu report; writer hiện là v23, suffix `.pr`. Các enum document thực tế:

| Trục | Enum hiện có |
| --- | --- |
| Coverage | Unknown, Partial, Complete |
| Appearance | Unknown, Partial, Verified |
| Editability | Unknown, Editable, ExplicitConversion |
| Production | Unknown, Blocked, Assessed |

Wire worker có enum riêng và phạm vi hẹp hơn; host mapping phải qua
`make_document_import_report`. Có enum Verified/Assessed không có nghĩa mọi import đạt mức đó.
Report bền vững hiện không chứa đầy đủ source hash, parser build hash, asset manifest hoặc mapping
mọi native element. SourceSnapshot/hash/job identity có trong job nhưng không đồng nghĩa tất cả
đều được lưu trong page report. Full source bytes chỉ được giữ ở các model/profile có hỗ trợ.

Các mục tiếp theo mô tả **semantics đích và yêu cầu consumer**. Chỗ rộng hơn model/report hiện tại
là việc còn phải triển khai, không phải schema runtime mới đã được bật bởi tài liệu này.

## 1. Kết quả import là hai trục độc lập

Một report luôn phân loại riêng từng element và toàn document:

| Trục | Giá trị |
| --- | --- |
| Parse outcome | `complete`, `complete_with_diagnostics`, `rejected`, `cancelled`, `timeout`, `worker_failed`, `resource_limit` |
| Feature coverage | `verified_for_catalog`, `partial`, `unknown` |
| Appearance | `verified`, `partial`, `unknown`, `not_available` |
| Editability | `native_editable`, `reference_only`, `explicit_conversion`, `unsupported` |
| Production | `verified_for_profile`, `blocked`, `not_assessed` |
| Export | Target format + exact object/metadata coverage; không suy ra từ import |

Tên ở bảng này là taxonomy đích, không phải enum đang serialize. Dùng enum hiện hành ở mục 0
và adapter mapping; mở rộng taxonomy cần schema/version/consumer review, không rải string mới vào payload.

`complete` chỉ có nghĩa parser hoàn tất format/version scope đã định. Nó **không tự chứng minh**
feature coverage đầy đủ, appearance pixel-identical, editability hoặc production readiness.
`verified_for_catalog` đòi host chọn parser binary/capability từ registry tin cậy và gate/oracle
độc lập đã nhận. Parser không được tự khai báo support cấp quyền. `unknown` luôn giữ unknown kể
cả khi parser trả không có warning.

## 2. Yêu cầu provenance đầy đủ và phần chưa đạt

Để công bố full provenance, proposal/storage/report cần các trường dưới đây. Hiện job đã có
identity/hash, còn durable report chỉ có subset nêu ở mục 0; không coi checklist này là đã hoàn tất:

- source content SHA-256 được host tính trên đúng snapshot bytes; source size và mã định dạng
  từ signature + suffix (mismatch là diagnostic, suffix không là parser selector duy nhất);
- format family/version scope cụ thể, adapter ID/version, parser build hash và dependency lock
  do host xác nhận;
- units, transform assumptions, precision input/output và sai số chuyển đổi nếu có;
- font/profile/linked assets được cấp qua broker, hash/trạng thái nhúng hoặc thiếu;
- source locator (page, object/layer/element khi biết) và mapping tạm tới proposed native IDs;
- feature coverage catalog/version, issues có severity/loss code, đối tượng/phạm vi liên quan;
- appearance/editability/production/export status riêng; người dùng chọn chuyển đổi nào;
- document epoch, base revision, import job ID/generation và manifest/checksum staging.

Không ghi nội dung, đường dẫn khách hàng đầy đủ, text riêng tư, file gốc hoặc binary vào log.
Local-only test corpus dùng ID trung tính/hash; quyền file gốc thuộc người dùng và retention được
native storage contract quyết định.

## 3. Dạng element sau import

Mỗi source element chỉ được đưa qua một nhánh có báo cáo:

1. `native_editable`: map được object/layer/text/color/geometry nguồn và semantics theo subset
   đã kiểm chứng. Writer cấp stable IDs; source ID giữ trong provenance, không làm authority.
2. `reference_only`: giữ source reference + renderer/asset có appearance kiểm chứng, group bị
   khóa khi cần để backdrop, clip, blend và effect cùng tồn tại. Không giả thành path editable.
3. `explicit_conversion`: chuyển outline/expand/raster có chủ đích, ghi rõ feature/đối tượng,
   sai số/tolerance, raster resolution/profile hoặc text/font thay thế. Giữ nguồn để phục hồi.
4. `unsupported`: user strict policy từ chối/hoãn import; không tạo geometry hoặc preview giả.

Không map được source hierarchy/layer thì không tạo layer giả cùng tên như dữ liệu gốc.
Không khớp renderer fallback với effect source thì không gọi là `reference_only`. Không hiển thị
reference/raster như geometry sản xuất hoặc đường cắt. Một page/document có thể chứa các element
với status khác nhau; report gắn provenance từng element và aggregate status bảo thủ.

## 4. Import proposal và atomic writer

Raw parser output chưa phải document object. Pipeline chuẩn:

1. Writer phát base snapshot (document ID, epoch, strict base revision) và generation.
2. Host broker chốt nguồn read-only, hash, capabilities và budget. Worker parser nhận file qua C6;
   đường dẫn nguồn do worker gửi bị từ chối.
3. Adapter tạo bounded intermediate scene/provenance/report vào staging. Không sửa snapshot,
   không cấp native IDs, không ghi source file.
4. Host xác thực version/schema/capability/source hash, payload/checksum, count/depth/graph refs,
   IDs local, text/assets sizes, finite scalars, matrix/range, unit conversion, geometry topology,
   enum values, allocations và report completeness. Dữ liệu worker luôn untrusted.
5. Host prepare document root/index/assets/preview theo revision staging. Mục tiêu là đưa bước
   nặng/O(n) khỏi UI; hiện materialization, full-frame buffers và một số bước apply vẫn cần audit
   hiệu năng. Worker ngoài process không tự chứng minh toàn bộ UI không bị block.
6. Fidelity policy chọn strict, allow explicit conversion hoặc reference-only theo user action.
   Report/preview tới UI chỉ là staging. Không commit `unknown` như fully supported.
7. Writer kiểm tra lifecycle epoch, document ID, **strict** base revision, job ID/generation/cancel
   tại commit. Base đổi thì từ chối toàn proposal; không implicit rebase.
8. Import edit commit phần content/report được model hỗ trợ trong một transaction/Undo unit.
   Lỗi trước hoặc trong apply, timeout, crash, cancel hay mismatch phải giữ state trước edit.
   Open/replace tạo lifecycle mới và có policy dirty-document riêng. Save sau khi import là thao tác
   riêng theo C5: save thất bại giữ document dirty, không tự rollback một import đã commit.

Kết quả worker cũ sau cancel/undo/close/reopen không bao giờ thay preview hoặc được nhận commit.
Generation đổi trước khi thu hồi resources; cleanup mọi candidate/reservation. Selection/camera
chỉ cập nhật khi commit hoàn tất.

## 5. Giới hạn và profile

Các loại ngân sách cần quản lý gồm source/spool/decompressed bytes, queue jobs, worker count, payload chunks,
objects, groups/depth/cycles, segments, text runs/glyphs, linked assets, decoded image pixels,
effect scratch surfaces, source archive, output geometry, CPU wall time, commit/working-set target,
handles, disk, undo and cleanup. Không dùng một trần payload IPC như ngân sách tổng memory.

Yêu cầu admission toàn app tính peak: host copy + worker decode + intermediate scene + native
tree/index + preview/effect surfaces + undo/assets. Coalesce/drop preview updates lỗi thời nhưng
không drop document commands. Khi hết budget, giảm preview hoặc dừng worker có diagnostic; không
thay source geometry/quality output để ép pass.

Hiện C6 có structural/representational bounds và pressure waiter ở ngưỡng 95% RAM, tối đa năm phút
chờ cộng dồn; nhiều byte quota import đã được bỏ. Đây chưa phải budget/reservation toàn app;
source/pixel/proposal buffers vẫn có thể cùng resident. Số quota/deadline release cần corpus/máy kiểm chứng. Không công bố 60 FPS,
thời gian mở mọi file, 512 MiB cứng hoặc “không bao giờ treo” khi chưa có evidence. C6 timeout,
cancel, quotas và OS containment là gate riêng với fidelity.

## 6. Chính sách theo nguồn và định dạng

- PDF-compatible AI chỉ phân loại theo PDF representation. Không tuyên bố dữ liệu authoring của
  Illustrator, toàn bộ artboards hoặc effect/plugin groups nếu không nằm trong representation.
- PDF objects, fonts, OCG/layers, clip/shading, color spaces/separations/transparency và forms
  có coverage từng loại; không đưa form action/JavaScript vào runtime.
- CDR coverage phải nêu major/minor/version family và feature catalog thực nghiệm. Report rỗng
  của parser không chứng minh Lens/Mesh/PowerClip/linked image được giữ.
- EPS/PostScript conversion là chained jobs do host điều phối. Chuyển sang PDF được report là
  converted; outline/font substitute, rasterized shading, transparency loss và unroll đều lộ rõ.
- DXF vector import phải giữ units, model/paper space, blocks, insert transforms, layer, entity
  identity và entity type được whitelist. Spline/circle/arc bị polygonize là loss; không gọi
  geometry chuẩn sản xuất khi tolerance/topology chưa được xác nhận.
- SVG là XML namespace/tree với CSS/style inheritance, groups/transforms, units/viewBox, defs/use,
  clip/mask/filter, text, images/ICC và painter order theo subset. Chặn external entities/DTD,
  external resources, scripts, animation, recursive use và traversal/decompression không giới hạn.

Thiếu font không thành text đúng; nếu dùng font khác phải ghi substitution và preview status.
Thiếu ICC không thành CMYK chuẩn; spot/overprint phải được kiểm ở output RIP/press phù hợp.
Kiểm tra pixel bổ trợ geometry/layer/text/color/topology oracle, không thay thế.

## 7. Capability, version và compatibility

Registry hiện có format/version-scope/import/export status, dependency và notes; đây chưa phải
feature matrix hoàn chỉnh gắn oracle cho từng effect/element. Registry đích cần tách appearance,
editability, metadata, production và source-export theo parser/build/fixture. Không dùng một bool
`is_supported()` để cấp mọi quyền. UI profile “Xem/Chỉnh sửa/Đường sản xuất” chỉ được công bố khi
đúng route và gates đã có; không suy ra ba profile đều hiện diện từ đặc tả này.

Native export/save/reopen là capability riêng. Source hash/provenance được giữ theo capability storage thực tế;
nhưng PDF/AI/CDR/EPS không có native round-trip nếu adapter writer + fixtures chưa đạt.
Storage schema version hóa metadata/assets; import retry và mở lần sau không phải re-run parser
ngẫu nhiên trong document load.

## 8. Consumer vectors và gate

Vector fixtures ở [import-fidelity-vectors.json](fixtures/import-fidelity-vectors.json).
Producer/consumer dùng cùng expected outcomes và oracle độc lập; test JSON shape chưa chứng minh
semantics. Contract changes cần consumer review document/storage/workers/interop/renderer/UI trước
chuyển trạng thái Accepted.

Mỗi adapter gate theo [import roadmap](../docs/roadmap/import-resilience-20261016.md): rights/hash,
source version/feature, fidelity/editability/export matrix, semantic geometry/color/text/layer,
import-edit-undo/redo-save/reopen và explicit unsupported outcomes; negative/stale/cancel/fault;
live sandbox canaries; measured input-to-visible and UI-stall; independent review đúng merged SHA.

## 9. Source authority và phạm vi lần rà soát

- [Worker record/report enums](../modules/interop/include/pikdraw/interop/adapters/svg_worker_adapter.hpp), [durable page report](../core/document/include/pikdraw/document/model/import_fidelity_report.hpp).
- [Desktop materialization/report mapping](../apps/desktop/src/workflows/document_workflow_import.cpp), [native review/placement](../apps/desktop/src/shell/native_window_import.cpp).
- [Import command](../core/document/src/commands/import_content_command.cpp), [storage versions](../core/document/include/pikdraw/document/storage/native_format.hpp).
- [Adapter regression sources](../modules/interop/tests/adapters), [native report/fidelity regression](../core/document/tests/storage/test_storage_fidelity.cpp).

Đây là audit source và sửa tài liệu; không có lượt chạy corpus/native tests mới. Coverage, render, editability, source metadata, export và production phải có evidence riêng cho từng hàng capability.
