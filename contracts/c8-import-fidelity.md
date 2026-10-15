# Contract C8: Import staging, provenance và fidelity

- Version: 1.0.0 Proposed.
- Status: contract specification; not implemented by the import runtime.
- Producers: per-format adapters in `modules/interop`, supervised by worker host in
  `platform/workers`.
- Consumers: document writer/storage, renderer, geometry, color, typography, production and
  desktop workflow.
- Depends on: C1 units/transforms/curves, C2 writer/snapshots/transactions, C4 color/typography,
  C5 native storage, C6 worker isolation/lifecycle.

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

Tên enum ở trên là semantics đề xuất; consumer implementation tạo enum/schema có version,
không rải string khác nhau giữa renderer và UI.

`complete` chỉ có nghĩa parser hoàn tất format/version scope đã định. Nó **không tự chứng minh**
feature coverage đầy đủ, appearance pixel-identical, editability hoặc production readiness.
`verified_for_catalog` đòi host chọn parser binary/capability từ registry tin cậy và gate/oracle
độc lập đã nhận. Parser không được tự khai báo support cấp quyền. `unknown` luôn giữ unknown kể
cả khi parser trả không có warning.

## 2. Provenance bắt buộc

Proposal của importer phải kèm:

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
5. Host prepare document root/index/assets/background preview theo revision staging. Nặng/O(n)
   chạy background; renderer chưa render tùy ý parser callbacks trong UI.
6. Fidelity policy chọn strict, allow explicit conversion hoặc reference-only theo user action.
   Report/preview tới UI chỉ là staging. Không commit `unknown` như fully supported.
7. Writer kiểm tra lifecycle epoch, document ID, **strict** base revision, job ID/generation/cancel
   tại commit. Base đổi thì từ chối toàn proposal; không implicit rebase.
8. Commit root/index/source assets/provenance/diagnostics trong một transaction và một Undo unit.
   Lỗi giữa chừng, timeout, crash, cancel, mismatch hoặc save failure xóa staging, giữ revision/
   dirty state/history hiện hữu. Document mới được publish sau staging; xử lý document dirty bằng
   desktop workflow trước khi thay document hiện tại.

Kết quả worker cũ sau cancel/undo/close/reopen không bao giờ thay preview hoặc được nhận commit.
Generation đổi trước khi thu hồi resources; cleanup mọi candidate/reservation. Selection/camera
chỉ cập nhật khi commit hoàn tất.

## 5. Giới hạn và profile

Limits host-owned gồm source/spool/decompressed bytes, queue jobs, worker count, payload chunks,
objects, groups/depth/cycles, segments, text runs/glyphs, linked assets, decoded image pixels,
effect scratch surfaces, source archive, output geometry, CPU wall time, commit/working-set target,
handles, disk, undo and cleanup. Không dùng một trần payload IPC như ngân sách tổng memory.

Admission control tính peak toàn app: host copy + worker decode + intermediate scene + native
tree/index + preview/effect surfaces + undo/assets. Coalesce/drop preview updates lỗi thời nhưng
không drop document commands. Khi hết budget, giảm preview hoặc dừng worker có diagnostic; không
thay source geometry/quality output để ép pass.

Số quota/deadline release chỉ chốt sau corpus/máy kiểm chứng và task review. Không công bố 60 FPS,
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

Capability registry do host build/release xác nhận theo adapter/parser hash và fixture catalog.
Matrix có record riêng cho format/version/import appearance/editability/metadata/production/
source-export; không dùng một bool `is_supported()` để cấp mọi quyền. User chọn profile “Xem”,
“Chỉnh sửa” hoặc “Đường sản xuất”; mỗi profile chỉ unlock những điều đã gate.

Native export/save/reopen là capability riêng. Source file được giữ hash/provenance để recovery,
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
