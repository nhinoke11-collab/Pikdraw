# Kế hoạch nâng cấp các tính năng hiện có của PikDraw — 20/10/2026

Trạng thái: **đã triển khai một phần, chưa hoàn tất toàn bộ U00–U12/C00–C09/S00–S05**. Đợt hiện tại đã nâng Contour lên ngưỡng vật lý 0,02 mm, mở rộng nhập đơn vị trong shell, thêm chọn đơn vị trang tại Properties, lưu ICC binding theo trang trong native v15, pixel-basis raster trong native v16, paint ICC dictionary trong native v17 và Dimension/Contour metadata cùng typed Contour paints theo ObjectId trong native v18. Page/document/selection Assign/Convert có command, preflight theo scope và một Undo. Raster decoder chuyển ICC sang sRGB đúng một lần khi WIC hỗ trợ, giữ ICC/byte nguồn và ghi basis hoặc fidelity issue khi chưa chuyển được. Raster writer TIFF/JPEG chuyển pixel sang profile RGB đầu ra; File > Export Raster có dialog cấu hình active/all pages, DPI và ICC. SVG giữ luồng phạm vi + hộp lưu tệp. Menu và Properties đơn vị trang dispatch qua semantic ActionId. Targeted command/UI tests và canonical app build đã chạy; export actions/SVG/TIFF/JPEG writer tests đạt 3/3. Paint ICC preview có CPU pixel test và isolated Direct2D/CPU parity test; full rasterizer target và Direct2D Contour Spot pixel regression nay pass. Native visual walkthrough/pixel validation tổng, gamut/proof, rulers, interop, performance và release gates vẫn mở theo từng plan bên dưới.

Giữ hướng sản phẩm và kiến trúc hiện tại: editor vector/CAD phục vụ thiết kế, in và sản xuất; C++20/Win32, ViewportRenderer Direct2D 1.1/D3D11/WARP và CPU fallback. Nâng chất lượng tính năng đã có bằng thuật toán phù hợp, dữ liệu nhất quán và UI dễ thao tác. Mỗi thay đổi phải có lý do từ lỗi, giới hạn hoặc chi phí đo được.

Tài liệu kèm theo:

- [Kế hoạch thực thi chi tiết U00–U12 cho agent triển khai](existing-feature-upgrade-20261020/implementation-plan.md).
- [Kiểm tra và kế hoạch quản lý màu C00–C09](existing-feature-upgrade-20261020/color-management-plan.md).
- [Kế hoạch đơn vị đo S00–S05](existing-feature-upgrade-20261020/units-plan.md).
- [Đối chiếu source của năm repo và quyết định áp dụng](existing-feature-upgrade-20261020/reference-audit.md).
- [Đặc tả nâng cấp UI/UX và binding của editor](../design/features/editor-consistency-upgrade-20261020.md).
- Chuẩn [UI/UX](../design/ui-ux-standard.md), [workspace](../architecture/workspace-layout.md), [tương thích vector](../architecture/vector-resource-interoperability.md), [nghiệm thu](../quality/feature-acceptance.md).

## 1. Phạm vi và điều kiện giữ chức năng

1. Giữ toàn bộ chức năng đang chạy: đường gọi, shortcut đã đăng ký, công cụ, tùy chọn nâng cao, dữ liệu, stable IDs, Undo/Redo và tương thích file.
2. Thay vị trí control phải có ánh xạ cũ → mới, đường menu/keyboard thay thế và kiểm chứng khả năng tìm thấy. Không bỏ tham số vì khó bố trí.
3. Giữ một document model, một writer logic và commands/transactions. UI, automation, importer và renderer dùng chung semantics sau normalization.
4. Tách metadata nguồn, geometry nguồn, preview và output sản xuất. Thay engine preview không sửa geometry hoặc màu nguồn.
5. Tính năng khai báo nhưng chưa có handler thực được ghi rõ là chưa triển khai; giữ ID/khả năng tích hợp. Không báo thành công giả hoặc tính nó là chức năng đã chạy.
6. Học cách xử lý từ Graphite, Penpot, Inkscape, LibreCAD, Krita. Chỉ tích hợp mã/thư viện sau khi chốt API, license, dependency và oracle; không đổi toàn bộ nền tảng theo một repo tham khảo.
7. Raster authoring đầy đủ, 3D, CAM/G-code, cộng tác cloud và plugin SDK mới không thuộc đợt nâng cấp này. Các module đó hiện là planned trong registry.
8. Source gốc dùng đúng workspace hiện tại `C:/Users/CK ART STAGE/Downloads/PikDraw`; không tạo checkout/branch task, không reset/stash, không qua task create/claim/lease.

Ở U00, mỗi tính năng nhận một quyết định: **giữ implementation** khi đúng semantics và đáp ứng workflow/đo lường; **sửa lỗi** khi có mismatch tái hiện được; **nâng thuật toán** khi subset/precision/chi phí chưa đáp ứng; **nâng UI** khi bố trí/state/binding/khả năng truy cập chưa đạt. Gói triển khai là nơi gom các sửa đổi cần thiết, không bắt viết lại mọi tính năng chỉ để giống upstream. Tối ưu có rủi ro phải chứng minh trên workload trước khi thay đường đang đúng.

### Mốc khảo sát

HEAD đầu đợt: `ba25ad73acbbb19095ffc1b7dd0a3b664f64ee42`; working tree ban đầu có **39 entries thay đổi**. Đây không phải bản source sạch đã build. Những file import, storage, workers và rendering đang được chỉnh tiếp phải được đọc lại trước lúc triển khai.

Đã kiểm source/registry/CMake và tải đọc 33 file mã/giấy phép của năm repo ở các commit cố định. Đây là khảo sát các phần liên quan, không phải audit mọi dòng của năm upstream hoặc chạy thử cả năm ứng dụng.

112 hằng action ID và 165 entry enum MenuActionId tìm thấy chỉ là **số khai báo source**, có entry hệ thống/placeholder; không phải 112/165 tính năng đã nghiệm thu. Khi U00 chạy phải theo đến registration, handler, enable state, surface và consumer thật.

## 2. Phát hiện cần xử lý

| ID / ưu tiên | Bằng chứng source | Tác động / việc cần làm |
| --- | --- | --- |
| F01 / P0 | `core/geometry/src/operations/boolean.cpp::boolean_paths` trước đây chỉ nhận exact axis-aligned rectangles; `document_workflow_internal.hpp::compute_selected_boolean` dùng hàm này | General filled paths nay dùng Clipper2 2.0.1, pin archive SHA-256; curves flatten theo error budget, quantize trên local integer grid có range/point-budget checks và giữ fill rule/holes. Geometry + shell Boolean tests pass. Đây là polygon approximation; Boundary/Simplify, cancel, near-contact oracle và manufacturing gate còn mở. Giữ nhánh rectangle chính xác và semantics keep-source/target |
| F02 / P0 | `kContourDefaultArcToleranceMm` và `kContourMinimumArcToleranceMm` là 0,02 mm; `compute_contour` từ chối thấp hơn floor; legacy `contour_spec` 0,005 được đọc giữ nguyên trước Apply | **Người dùng đã chốt 0,02 mm là đúng cho Contour.** Preview, field và status báo effective floor 0,02; source legacy 0,005 không bị ghi lại khi xem, Apply tường minh chuẩn hóa về 0,02 trong một Undo. `test_pikdraw_vector_contour` pass; đo sai số hình học theo corpus và storage round-trip toàn diện còn mở. Không áp floor này cho offset hoặc tolerance tầng khác |
| F03 / P0 | `NumericFieldId` trước đây dùng chung Polygon/Contour/Gradient, Blur/Shadow, Gradient stop và Text fields. Các nhóm này nay có semantic IDs riêng; `NumericFieldDescriptor` ánh xạ key và quantity để input/commit dùng chung phân loại length | Shell regressions kiểm parser, draft, commit, Undo và type guard Blur/Shadow. Range/scope/state binding và EditorContext/edit session dùng chung vẫn còn mở; tiếp tục U02, chưa coi property binding thống nhất hoàn tất |
| F04 / P0 | `ActionDispatcher` trước đây coi descriptor thiếu handler là dispatch thành công; `DesktopWindow` có placeholder no-op và NativeWindow vẫn có MenuActionId switch riêng | Dispatcher nay trả false nếu không có handler, không ghi lịch sử cho action chưa chạy; các shortcut file placeholder đã nhường về native key handler. Nguồn action/menu và enable state vẫn chưa hợp nhất; tiếp tục theo từng lát, không suy ra mọi menu đang hỏng |
| F05 / P1 | `geometry::AabbIndex` dùng chung cho selection và renderer; indexed pick/marquee còn exact narrow phase, exhaustive API được giữ làm oracle | Bốn targeted suites pass; page/revision, painter order, transform, hidden, overlap và invalid-bound fallback có regression. Chưa đo p95/peak-memory ở 1k/10k/100k hay UI runtime; snap/layer queries còn mở |
| F06 / P1 | Selection/rendering caches nhận diện snapshot, document, epoch, revision và PageId; renderer còn khóa zoom/margin | Regression cross-document cùng revision và page/revision invalidation pass. Còn đo thời gian rebuild/ownership dưới workload và runtime multi-tab; không tuyên bố hiệu năng đã nghiệm thu |
| F07 / P1 | `d2d_render_backend.cpp` CopyFromBitmap toàn target rồi Map/Unmap; project-state ghi full render/readback cho snapshot đổi | Đo riêng render/readback/ICC/copy/present. Ưu tiên giảm invalidation và reuse; partial GPU/tile/direct presentation phải có gate riêng |
| F08 / P1 | `SnapIndexWorker` giữ generation guard; SnapSourceIndex nay nhận callback hủy tại ranh giới object/subpath/node/segment và khi dựng cây index | Hủy hợp tác đã được thêm cho request cũ; unit/controller tests pass. Chưa đo p95/max latency, peak memory hoặc thời gian bên trong từng phép curve/partition; giữ result freshness guard và tiếp tục gate workload lớn |
| F09 / P1 | Paint/menu/pointer/numeric nằm ở nhiều file; paint lặp kích thước 38/28/36/26/52 và tọa độ control; theme RGB/language ternary rải ở shell | Dùng chung layout/control model cho paint, hit-test, focus, tooltip, accessibility; token/catalog chung. Source cho thấy nguy cơ lệch/tràn, chưa là nghiệm thu UX trên màn hình |
| F10 / P1 | `native_window_files.cpp::autosave_documents` serialize/write checkpoint đồng bộ trong UI timer | Chuyển phần encode/I/O được phép sang snapshot job; completion cập nhật đúng tab/revision. Giữ checkpoint cũ đến khi bản mới atomic thành công |
| F11 / P1 | `format_capability_matrix.cpp` và SVG adapter: opacity presentation attributes nay normalize vào `Appearance` cho Path/Rect và opacity/fill-opacity cho Text; percentage và clamp [0,1] có regression. Group opacity, CSS style declarations, gradient/stop opacity, effect và non-Normal blend vẫn reject; đây chưa phải full SVG/C9 round-trip | Giữ source loss report/capability theo feature; tiếp tục kiểm appearance/render/export độc lập và giữ unsupported report rõ, không mở rộng claim sang PDF/AI/CDR/EPS/DXF |
| F12 / P1 | Source có recovery modal/tests và PDF page-set/locked-source work; project-state vẫn mô tả một số giới hạn cũ | Reconcile docs từ source và bằng chứng build thực. Không gắn nhãn missing/complete theo snapshot cũ |
| F13 / P1 | Shaper DirectWrite đang reject bidi/sideways và mixed-font glyph fallback theo run model hiện tại | Giữ tiếng Việt/Unicode đã có; nâng run/font/glyph contract theo plan font hiện có, không thay glyph thiếu bằng hình chữ nhật |
| F14 / P2 | `ActionDispatcher::register_action` từng để shortcut cũ và history không giới hạn | Đã dọn binding cũ khi đăng ký lại, giải quyết xung đột xác định, giới hạn history 1024 và có test. Việc gom dispatcher với native MenuActionId switch vẫn thuộc F04 |
| F15 / P0 | Page ICC resource bất biến lưu deduplicated trong native v15; raster preview pixel basis trong v16; paint ICC bindings trong v17; path feature metadata trong v18. Settings gán page profile; reusable ICC transform + page/document/selection Assign/Convert commands đổi scope cùng Undo. Raster decoder/WIC chuyển ICC sang sRGB khi hỗ trợ, giữ profile/byte nguồn và ghi basis/fidelity issue; writer TIFF/JPEG đã chuyển pixel sang ICC RGB đầu ra và dialog Raster có active/all pages, DPI, quality và profile. Windows CMM BPC và independent colorimetric oracle chưa có. TAC validate/fix hiện chỉ cho process CMYK paints, output condition chưa gắn profile | Quản lý màu **chưa đồng bộ đầy đủ**. Native dialog chưa visual-verified; pixel oracle ngoài sRGB, CMYK/Gray output, selection raster, custom paint-profile preview, proof/gamut oracle, overprint compositor, raster separation và interop preservation còn mở. TAC fix giữ K/alpha/overprint nhưng giảm CMY tỷ lệ, không có ICC appearance guarantee. Xem C00–C09; focused tests không chứng minh proof, gamut, overprint hay print đạt |
| F16 / P1 | Registry/parser nhận mm/cm/m/in/ft/yd/pt/pc; parser nhận `1/8in`, `1 1/2in`, `5ft+6in`; suffix parse/format dùng registry chung; native v13 lưu page display unit, v2–v12 fallback mm; Layout menu đổi trang qua `action.page.units.*`, Settings/ruler và Page/Object/Contour/Drop Shadow ribbon dùng page unit từng phần; Image W/H nhận `px` theo source X/Y DPI có EXIF axis correction khi có context | Unit/numeric/geometry, image px DPI-context và Pen native shell mixed-fraction tests pass; geometry vẫn canonical mm. Chưa có native walkthrough; nhiều Properties/toolbar, clipboard/automation và mọi format gate còn mở. Tiếp tục S00–S05, giữ API *_mm |

F01/F03 còn giới hạn trực tiếp trong source; F02/F04/F08/F14/F15/F16 đã có sửa một phần theo trạng thái ghi tại bảng. F05–F10 vẫn cần đo/tái hiện mức ảnh hưởng. Đợt này chưa hoàn thành walkthrough native hoặc đánh giá UI trên màn hình thật.

## 3. Danh mục bảo toàn và điểm nâng cấp theo tính năng

Mỗi dòng phải được đánh dấu source-supported/runtime-verified/declared-only ở U00. Tính năng có UI hoặc test không tự trở thành runtime-verified.

| Nhóm hiện có | Giữ lại | Nâng cấp và nguồn tham khảo | Gói |
| --- | --- | --- | --- |
| File/tab/home/recent/template entries | Open/New/Save/Save As, tab dirty/save path; phân biệt entry chưa chạy | Action state và file lifecycle chung; no-op có reason; Penpot/Inkscape | U00/U01/U10 |
| Pick/move/resize/rotate/nudge/flip | Anchor, aspect lock, đơn vị, multi-select | Broad/narrow phase, pivot/transform contract, field mixed; Inkscape/Graphite | U02/U04/U07 |
| Rectangle/Ellipse/Polygon | Drawing defaults, sides/rotation, Ctrl/Shift hiện có | Preview và commit dùng cùng constraint solver; flyout gọn; Graphite/LibreCAD | U02/U08 |
| Line/Polyline/Pen | Nhập tọa độ/dài/góc, đóng/nối path, draft recovery | FSM/session nhất quán, snapping có thứ tự, Undo trong draft; Inkscape/Graphite | U02/U04/U08 |
| Node/segment/handles/Live Corners | Stable node/segment IDs, bend/convert, round/scallop/chamfer | Dùng chung đường cong/tolerance; chọn node và sửa lại bằng bar/Properties; Inkscape/Graphite | U03/U08 |
| Boolean/shaping/Knife | Các operation và keep source/target; giới hạn Knife hiện có | Path tổng quát có topology/empty/failure rõ; không thay bằng bounding box; Inkscape/LibreCAD | U03/U08 |
| Contour/offset/break apart | Steps, direction, join/miter, source association | Robust offset/holes/collapse, tolerance thật, preview cancellable; LibreCAD/Inkscape | U03/U05/U08 |
| Snap/inference/grid/guides | Candidate priorities, options, snap panel và modifiers | Candidate pruning, hysteresis nếu cần, giải thích snap kết quả; LibreCAD/Graphite | U04/U08 |
| Dimension/Quick Dimension | Associativity/annotation và kiểu hiện hỗ trợ | Source-vs-display units, recompute theo dependency, text collision/placement có giới hạn; LibreCAD | U03/U08 |
| Align/distribute/array/step-repeat | Target scope, khoảng cách và order | Empty/locked/group targets, exact spacing, one Undo và preview; Penpot/LibreCAD | U02/U07 |
| Group/order/lock/hide/isolation | Hierarchy, stable IDs, painter order | UI và query cùng isolation/editability; group opacity/clip đúng semantics; Inkscape/Krita | U01/U04/U07 |
| PowerClip/mask workflows | Place/make/release/edit/finish/fit/lock có trong source; workflow còn lỗi cần nghiệm thu | [Plan sửa PowerClip](powerclip-editing-upgrade-20261021.md): hierarchy/query, child drag, Edit/Finish, transforms, lock/fit/extract và source fidelity; đối chiếu Corel Help | U04/U06/U07 |
| Layers | Tree, filters, rename/reorder/visibility/lock | Flatten visible rows, virtualized paint/hit/focus; selection hai chiều; Krita/Penpot | U04/U07 |
| Pages/document setup | Units, kích thước, orientation, page navigation và per-page settings | Target PageId rõ, page/tab tách nhau, không mất draft; LibreCAD/Penpot | U02/U07/U09 |
| Text/canvas edit/font/paragraph | Unicode, tiếng Việt, caret/selection, frame/layout đã hỗ trợ | Cluster-aware caret, IME, run/font fidelity và preview/layout chung; Inkscape/Krita/Penpot | U02/U09 |
| Fill/stroke/opacity/style | RGB/CMYK/spot/gray, alpha, source metadata | Một property model; cap/join/dash chỉ hiện khi backend đủ; Inkscape/Penpot | U02/U07/U09 |
| Linear/Radial Gradient | Stops/IDs/midpoint/geometry, màu nguồn | Stop UI và canvas đồng bộ, sửa stop không nhân node/stop; Graphite/Inkscape | U02/U05/U08 |
| Gaussian Blur/Drop Shadow/blend | Effect order/ID, parameters và re-edit | Cache theo dependency, halo/bounds đúng, bar/Properties đủ tham số; Graphite/Inkscape/Krita | U05/U06/U08 |
| Color modes/conversion/select same/proof | Document/Page/Object scope, nguồn màu, intent/flags | Color matching có profile/alpha/spot policy, ICC cache đúng, không đổi nguồn khi preview; Krita/Inkscape | U09 |
| Bitmap placement/import | Encode bytes, pixels chia sẻ, DPI/EXIF/ICC, transform | Admission/cache/memory thống nhất; không coi module raster authoring đã hoàn thiện; Krita | U06/U09 |
| SVG/DXF/PDF/AI/CDR/EPS và export hiện có | Adapter từng format, source provenance, loss report | Semantics sau normalize dùng chung; capability từng representation; Inkscape/LibreCAD | U09 |
| Autosave/recovery/clipboard | Checkpoint/tab isolation, Unicode clipboard và paths | Snapshot encode/I/O job, atomic save và exact saved revision; Krita/Penpot | U10 |
| MCP/COM/automation | Typed services, permissions/revoke, revision guard | Các đường sửa cùng command/validation với UI; stale job không ghi đè draft; Graphite/Penpot về cách phân tầng | U01/U02/U10 |
| Preferences/theme/language/shortcuts/help | Tùy biến toolbox/workspace và shortcut đã có | Catalog/token/focus/hints và trạng thái nhất quán, bảo toàn user settings; Krita/Inkscape/Penpot | U01/U07/U10 |
| Hệ đơn vị toàn editor | mm canonical; parser và Dimension/import units đã có | mm/cm/m/in/ft/yd; pt/pc/px đúng context; rulers/fields/Dimension/format thống nhất; không phụ thuộc DPI màn hình | U12/U02/U07/U09 |

## 4. Kiến trúc nâng cấp

### 4.1. Một context và binding có kiểu

`EditorContext` là projection chỉ đọc từ DocumentId + tab-instance + Epoch + Revision + PageId + selection + tool-session + view-generation. Tab-instance cần giữ vì test hiện có cho thấy hai tab có thể dùng cùng default document ID. Không dùng pointer/global active tab làm identity.

Property key gồm domain và semantic key, ví dụ `contour.steps`, `gradient.stop.offset`, `text.font.size`; đây là key mới nội bộ, không đổi enum/schema cũ trước migration. Binding biết unit/range/scope/mixed/read-only và command. Bar/Properties/canvas/shortcut lấy cùng projection; conversion từ string sang giá trị có một implementation.

`EditSession` giữ original value, typed draft, target IDs/version, validation và merge key. Preview không sửa nguồn; Enter/Apply/pointer-up commit một transaction; Esc phục hồi source; no-op không tạo revision/Undo. Khi target đổi, field lỗi phải được giữ để sửa hoặc hủy rõ ràng, không commit sang selection mới.

### 4.2. Geometry và precision

- Geometry/transform nguồn giữ binary64/mm. Một tolerance policy phân biệt render pixels, hit-test DIP, numerical predicates và output mm; không dùng cùng epsilon cho mọi tầng.
- Giữ analytical curves, exact rectangle fast path và stable editable geometry. Backend polygon mới có contract riêng cho fill rule, holes, self-intersections, open paths, degenerate/tangent/coincident và output ordering.
- Ưu tiên đánh giá Clipper2 C++ như backend **polygon** bổ sung; xem nguồn trong appendix. Phải pin version/hash/license, chọn local origin/scale và checked range trước quantization. Không gọi polygonized output là exact Bézier.
- Với curve→polygon, tổng sai số gồm flatten + quantization + operation + reconstruction, phải nằm trong tolerance đã công bố; hết budget trả lỗi rõ. **Contour dùng mặc định và floor 0,02 mm theo quyết định người dùng**, kiểm thêm 0,05/0,1 mm; không ép tolerance này lên Boolean, render, hit-test hoặc các predicate C1. Đây chưa là cam kết độ chính xác máy sản xuất.
- Chọn curve-preserving backend chỉ khi oracle/degenerate cases và license đạt. Mở operation từng tập geometry; giữ nguồn khi user chọn keep source/target. Knife ngoài subset hiện có cần gate riêng.
- `can_execute` chỉ kiểm target/capability rẻ; không chạy Boolean/outline/contour đầy đủ khi paint menu. Phân biệt Empty, Unsupported, Invalid, Cancelled, BudgetExceeded.

### 4.3. Query, scheduler và cache

Index broad phase source bounds nên ở `core/geometry` hoặc document query API trung lập; vector/rendering là consumer. Rendering vẫn có effect/ink bounds riêng. Cache object refs phải giữ snapshot owner hoặc dữ liệu owned.

Job key: tab-instance, document ID, epoch, source revision, page, object generation/targets, session generation, parameters hash và view generation nếu kết quả phụ thuộc camera. Chỉ UI writer được commit. Job stale/cancel không sửa source; source import/cache không fetch linked assets ngoài broker.

Coalesce preview/latest request; **không gộp mất** các command đã commit, save/export hay import độc lập. Chia ngân sách index/render/filter/thumbnail/I/O; cancel cooperatively giữa batch; ưu tiên thao tác đang vẽ và không starvation autosave/export. Bảo toàn generation guard đang có ở SnapIndexWorker.

Cache phân theo geometry, paint/effect, text layout, ICC, viewport/pixels. Key chứa đầy đủ dependency; không dùng camera-zoom để vô hiệu mọi geometry nguồn. Dirty propagation theo object/appearance/hierarchy; effect ROI bao gồm halo, transform, clipping và backdrop dependency.

### 4.4. UI cùng hệ layout

Giữ menu trên, standard toolbar, một property bar theo context, toolbox trái, canvas giữa, Properties/Layers phải và status dưới. Layout/control tree tính một lần theo DIP; paint, hit-test, focus, tooltip, accessibility dùng cùng bounds và semantic IDs.

Đặc tả chi tiết ở tài liệu UI: token, narrow-window overflow, fields, states, menu/action mapping, icon/help và migration từng feature. Không đưa class/epoch/backend vào luồng vẽ thường ngày.

## 5. Các gói triển khai

P0: tính đúng và ngăn lệch dữ liệu. P1: hiệu năng/luồng UI chính. P2: polish sau khi cùng semantics và khả năng truy cập đã đạt. Ưu tiên không thay thế dependency.

| Gói | Scope sở hữu dự kiến | Phụ thuộc | Kết quả/gate |
| --- | --- | --- | --- |
| **U00 — baseline và bảo toàn** / P0 | `apps/desktop/tests`, test existing của module; bảng action/capability trong tài liệu này | Không | Theo tất cả entry đến handler thật; baseline file native/IDs/Undo/settings và luồng dùng chính. Chốt known failures; không biến giới hạn source thành test pass. AC-UP-KEEP01 |
| **U01 — action và state chung** / P0 | `apps/desktop/src/actions`, `include/.../actions`, `shell/desktop_window.cpp`, `native_window_menu_actions.cpp`, `native_window_shortcuts*.cpp`, `runtime/composition_root.cpp` | U00 | Action descriptor có capability/checked/disabled reason/result; alias menu/shortcut/context một lệnh; dọn stale shortcut và bounded dispatch diagnostics. Giữ IDs/aliases cũ. AC-UP-ACT01/02 |
| **U02 — property và edit session** / P0 | Desktop `presenters`, numeric/text/style shell, tool controllers; commands trong `core/document` khi cần | U01 | Typed bindings + shared EditorContext, Mixed/range/unit; preview/commit/cancel/Undo chung; tách field overload theo domain. Migrate Transform + Appearance trước rồi các tool. AC-UP-SYNC01–04 |
| **U03 — geometry đúng và ổn định** / P0 | `core/geometry/.../operations,queries,flattening`, vector node/contour, document shaping/contour/dimension commands | U00; tích hợp UI sau U02 | Boolean polygon/path và offset robust theo contract; sai số thật, topology, empty và cancellation. Nâng cơ chế associative dimension theo source dependency; giữ IDs khi sửa đối tượng hiện có. AC-UP-GEO01–04 |
| **U04 — selection/snap/layers query** / P1 | Geometry query index mới nếu cần; `modules/vector/src/selection,precision`; renderer spatial adapter; desktop snap/layers | U02; geometry sửa dùng U03 | Broad phase index + narrow phase, identity/lifetime keys; z-order/isolation/locked/clip đúng; ưu tiên snap deterministic; layer visible-row cache. AC-UP-QUERY01/02 |
| **U05 — scheduler và derived preview** / P1 | Desktop tool workers/workflows, `core/rendering/src/effects`, document async guard; platform service chỉ khi dùng chung thật | U02/U04; U03 với contour | Latest-preview coalescing, cancellation theo batch và bounded work/memory; cache effect hiện có theo dependency. Giữ appearance list, stable node IDs và source semantics. AC-UP-JOB01/02 |
| **U06 — render/copy/present** / P1 | `core/rendering`, desktop `native_window_canvas*.cpp`, `performance/canvas_trace.*` | U04/U05 | Đo từng stage; giảm full invalidation/đọc lại vô ích; tối ưu dirty/ROI trước. Tile/partial GPU rồi direct presentation là các bước chỉ mở khi baseline chứng minh bottleneck và oracle đạt. CPU/WARP/device-loss vẫn có gate. AC-UP-RENDER01/02 |
| **U07 — nền UI/UX native** / P1 | `shell_geometry.*`, `themes`, `native_window_paint*.cpp`, pointer/focus/events, `resources/icons`; binding U02 | U01/U02 | Layout + control model dùng chung; token/font/icon/locale, focus/IME/UIA, property sections/overflow, Layers rows. Migrate chrome và Transform/Appearance trước. AC-UP-UX01–03 |
| **U08 — nâng từng tool** / P1 | Desktop `tools`, property bar/Properties/overlays/tooltips; vector tool state | U02/U03/U05/U07 cho phần liên quan | Pick → Shape/Line/Pen → Node/Live Corners → Contour/Dimension → Gradient/Blur/Shadow. Mỗi tool giữ tất cả tùy chọn, modifier và khả năng sửa lại. AC-UP-TOOL01/02/03 |
| **U09 — text/màu/format nhất quán** / P0 màu, P1 tích hợp | `core/typography,color`, interop adapters/writers/capabilities, document model/storage, desktop color/text/import UI | C00/C01/C02 làm sớm sau U00/U01; phần UI dùng U02/U07, hiệu năng nối U05/U06 | Thực hiện C00–C09 trong plan màu; quản lý ICC/source/conversion/proof/overprint/export là gate độc lập. Giữ AC-IPA/AC-PDFP/AC màu hiện có và bổ sung AC-CM. AC-UP-DATA01/02/03 |
| **U10 — lưu/phục hồi/clipboard/automation** / P1 | Document storage/recovery/async; desktop files/recovery/clipboard; automation/MCP/COM consumers | U01/U02/U05; U09 nếu schema đổi | Snapshot encoding/I/O async có identity; atomic save, saved R và dirty R+1; bảo toàn clipboard source, permissions/revoke và draft. AC-UP-LIFE01/02 |
| **U11 — tích hợp và nghiệm thu** / P1/P2 | Tests hiện có, docs/help và snapshot hiện hành ngắn | Các gói đưa vào release | So baseline toàn bộ capability; source build + test đúng phạm vi + thao tác native + performance/fidelity theo claim. Cập nhật docs/project-state chỉ sau bằng chứng và <=8.000 byte |
| **U12 — hệ đơn vị thống nhất** / P1, nền S00/S01 sớm | Geometry units/parser, vector numeric, document display/Dimension/storage, desktop fields/rulers/page, interop units, automation | S00 sau U00; S01 trước hoàn tất U02; phần UI sau U07; schema phối hợp C01 | S00–S05 theo plan đơn vị. Units hiển thị/nhập không scale lại geometry; pixel dùng context DPI rõ, không OS DPI; preserve *_mm API. AC-UP-UNIT01/02 |

Trình tự chi tiết ở implementation-plan: U00 → U01 → C00/C01/C02 và S00/S01 → U02 → U03/U04/U05. U07 dùng binding/context màu và đơn vị chung; U08 migrate từng tool. Hoàn tất C03–C09 và S02–S05 cùng U09/U10 trước gate release U11. U06 tối ưu sau khi oracle màu/geometry liên quan đạt; không để quản lý màu thành việc cuối cùng. Các dấu “/” biểu thị nhánh công việc theo dependency, không yêu cầu spawn agent hoặc sửa shared file đồng thời.

Mỗi gói làm trực tiếp source gốc. Trước khi sửa phải đọc diff mới nhất của đúng file, giữ local edits đang có. Hợp đồng/CMake/registry/shell chung chỉnh tuần tự. Không thêm code vào file vượt 2.000 dòng; tách theo domain, không tách máy móc. Commit chỉ gồm thay đổi thuộc gói sau khi kiểm tra đạt và không trộn dirty work có sẵn.

### Phối hợp kế hoạch hiện hành

- [Font/pages/artboards/import](import-font-pages-artboards-20261020.md): giữ AC-IPA và AC-PDFP; source đang có page-set/locked PDF phải đọc lại, không triển khai lần hai theo trạng thái tài liệu cũ.
- [Hệ màu nâng cao](color-system-advancement-plan-20261020.md): giữ mục tiêu và AC per-page/Document/Object; source audit và quyết định thực thi màu hiện hành ở C00–C09. Không coi TAC 320% là chuẩn chung, không dùng Ctrl+Y thay Ctrl+Alt+Y, không coi overprint helper là compositor đã hoàn tất.
- [Open/import/export/CNC](open-import-export-cnc-20261018.md): giữ profile export/production, không tuyên bố DXF/CNC đầu ra từ import pass.
- [Architecture remediation](performance-storage-automation.md): tái dùng phần code đã có và kiến thức kỹ thuật; quy trình task/lease lịch sử trong tài liệu được thay bằng AGENTS hiện hành.
- Không sửa phiên bản native theo số cũ trong tài liệu. Đọc writer/reader thực và migration table tại thời điểm triển khai.

## 6. Đo hiệu năng và ngân sách đích

Đây là **mục tiêu dự kiến**, chưa phải số đo hoặc lời hứa đạt trên mọi máy. Ghi CPU/GPU/RAM/display/DPI, source hash, build Release, backend HW/WARP/CPU và corpus hash. Warm-up rồi lấy tối thiểu 100 mẫu cho thao tác lặp; cold open/import đo riêng, lưu p50/p95/max và mức cấp phát.

| Workload | Đo | Ngưỡng đích ban đầu / quyết định |
| --- | --- | --- |
| 1k/10k/100k objects, phần lớn ngoài viewport | Broad candidates, narrow tests, rebuild và query p95 | Broad query tính theo candidate thay vì mọi object; hit/snap p95 <=8 ms trên corpus máy chuẩn; 100k là stress, nếu không đạt phải ghi số thực và giới hạn |
| Chọn/kéo/resize/rotate/Pen/Node | Input event → visible result và UI-thread stall | UI routing p95 <=4 ms; input-to-visible p95 <=33,3 ms trên cảnh tương tác chuẩn; không gọi CPU paint time là FPS |
| Camera pan/zoom và chỉ đổi overlay | Render/readback/copy counts và present intervals | Overlay-only không rebuild source geometry/index; snapshot/camera không đổi không render/readback lại source |
| Blur/Shadow/Contour/Gradient liên tục | Pending/active jobs, cancellation latency, scratch/cache peak | Preview pending tối đa một bản mới nhất mỗi session; worker cũ dừng tại batch boundary, mục tiêu <=50 ms; hủy không tạo Undo |
| 5×2 m và fixture lớn theo giới hạn thật | Sai số nguồn mm và viewport precision | Zoom/DPI không sửa source; viewport local-origin tránh float mất chính xác; không rasterize source để đạt tốc độ |
| Layer tree lớn/thu gọn/filter | Rows traversed/painted, focus/selection update | Paint/hit vùng hiện thấy, giữ focused row khi reorder; không rebuild toàn tree cho hover |
| Save/autosave/import/export và multi-tab | Encode/I/O, UI stalls, cancellation, peak RAM | UI không chờ cả encode/I/O dài; giữ policy RAM/worker hiện hành; admission theo ngân sách thực, không tăng quota vô căn cứ |

Direct2D render CPU timing không đồng nghĩa GPU completion. Input-to-visible/present cần trace hoặc capture có timestamp phù hợp; nếu thiếu công cụ, giữ not_measured. Không suy ra bộ nhớ GPU từ kích thước vector CPU hoặc cache limit.

Nếu một tối ưu không cải thiện bottleneck hoặc làm sai source/pixels thì không đưa vào release; giữ đường đã đúng. Ngưỡng hiệu năng chỉ được điều chỉnh có workload/máy/số đo và lý do, không giảm im lặng để pass.

## 7. Nghiệm thu bắt buộc theo phần thay đổi

Các ca chưa chạy; dùng IDs ổn định này trong implementation. Mở rộng test module hiện có, không nhân file test một lần hoặc kiểm cùng assertion ở mọi tầng.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-UP-KEEP01 | Baseline các chức năng đang chạy, file legacy, settings và shortcut | Migrate từng gói rồi mở/chỉnh/lưu lại | Không mất chức năng/field/entry point hoặc source data; mọi relocation có mapping | Inventory U00 + runtime canonical exe + native semantic diff |
| AC-UP-ACT01 | Cùng action trên menu/context/toolbar/shortcut; no-op declared entry | Gọi các surface ở cùng context | Enable/checked/result nhất quán; unsupported có reason, không báo success | Desktop actions/shell tests + thao tác menu thật |
| AC-UP-ACT02 | Action có shortcut cũ và action khác chiếm shortcut mới | Re-register/rebind/reset | Không còn alias cũ ngoài policy; tooltip và execution đúng | test_pikdraw_desktop_actions |
| AC-UP-SYNC01 | Một selection và bar/Properties đều mở | Sửa cùng field từ hai nơi rồi Undo/Redo | Cùng giá trị/scope/validation; một edit tương ứng một Undo | Desktop shell + semantic IDs/revision |
| AC-UP-SYNC02 | Selection mixed/locked/reference-only; field đang nhập | Nhập sai/đổi selection/tab/page/tool | Không biến Mixed thành 0; không commit sang target mới; draft được giữ hoặc hủy theo policy | Numeric/session + desktop consumer |
| AC-UP-SYNC03 | Effect/gradient/live corner đã có; deselect/reselect | Chọn tool và sửa lại tham số | Không nhân node/stop/effect; stable IDs; focus panel không tăng revision | Effect/gradient/node controller + document tests |
| AC-UP-SYNC04 | Kéo field/canvas liên tục, IME hoặc text numeric draft | Release/Enter hoặc Esc/capture lost | Commit một bước; Cancel không mutation; undo draft khác undo document | Tool session/text/Win32 event tests |
| AC-UP-GEO01 | Rectangles baseline, polygon lõm/lỗ/đường cong, tangency/coincident/self-intersection theo profile | Các Boolean và keep-source/target variants | Topology/fill rule đúng; output Empty phân biệt lỗi; hình chữ nhật cũ không hồi quy | Geometry operations/subset + desktop align_boolean; analytical/synthetic area/topology oracle |
| AC-UP-GEO02 | Contour mới default 0,02 mm, ca 0,05/0,1; input mới 0,005 và tài liệu legacy chứa 0,005 | Offset Inside/Outside/ToCenter, đổi đơn vị và save/reopen | Default/floor 0,02 đồng bộ; input dưới floor từ chối rõ; legacy không bị rewrite khi chỉ mở; sai số theo profile đã công bố hoặc reject | Offset/model/controller/storage tests với distance/containment oracle |
| AC-UP-GEO03 | Node/handle/bend/live corner dưới shear/nonuniform scale | Chỉnh bằng canvas và số, Undo/Redo/save/reopen | Endpoint/tangent/source IDs đúng; kết quả hai đường gọi bằng nhau trong contract | Existing node/curve/document/storage tests |
| AC-UP-GEO04 | Dimension liên kết path, units/view/DPI khác nhau | Sửa source rồi Undo và đổi đơn vị hiển thị | Measured source value/association đúng, không sửa geometry khi đổi display; vị trí nhãn theo policy | Dimension model/commands/controller + known-distance fixture |
| AC-UP-QUERY01 | Overlap, clipping/group/isolation/hidden/locked và text/image; nhiều object ngoài view | Click/region/cycle-selection/snap | Candidate index không bỏ target hợp lệ; đúng painter order và editability | So indexed query với exhaustive oracle trên cùng fixture |
| AC-UP-QUERY02 | Hai tab/page/snapshot cùng revision number, snapshot cũ hết vòng đời | Switch/Undo/rebuild/delete trong lúc query | Không reuse refs/key sai; owner lifetime an toàn; deterministic snap result | Culling/selection/snap tests và lifetime instrumentation |
| AC-UP-JOB01 | Preview A chạy; B mới hơn; đổi tab/page/target hoặc đóng file | A/B trả đảo thứ tự, Cancel giữa batch | Chỉ result đúng key/session được nhận; không late commit; queue/memory có giới hạn | Controlled scheduler/fake completion, document async/writer safety |
| AC-UP-JOB02 | Preview/render đang dày cùng save/export đã nhận | Coalesce/resize/cancel/revoke | Không làm mất job bắt buộc hoặc starvation; final committed output dùng snapshot đúng | Job consumer tests và trace hàng đợi |
| AC-UP-RENDER01 | Blur/shadow/gradient/clip/alpha/blend/ICC fixture có pixel oracle | Bật cache/ROI/tile hoặc đổi camera/DPI/backend | Pixels/bounds/halo/backdrop đúng theo tolerance; source/history không đổi | Existing pixels/compositor/gradient + culling/canvas tests |
| AC-UP-RENDER02 | HW healthy, WARP, simulated recoverable loss và overlay-only | Drag/pan/resize/device loss | Recovery/fallback có trạng thái đúng; không present frame thiếu; counters phản ánh tối ưu thật | D2D/CPU oracle + trace input-to-visible riêng |
| AC-UP-UX01 | 1366×768/1920×1080, 100/125/150/200% và dock thu gọn | Mở mọi tool/Properties, dùng overflow/flyout | Không che field thiết yếu; paint/hit/focus trùng; mọi tham số còn reachable | Layout/Win32 runtime và native capture đúng build; thiết bị thiếu ghi chưa kiểm |
| AC-UP-UX02 | Mouse/keyboard/IME/high contrast/light/dark; EN/VI | Tab/Shift+Tab/arrows/Esc và nhập số/Unicode | Focus rõ; named controls/tooltip focus/UIA; canvas shortcut không ăn text | Windows UIA inspection + keyboard/IME walkthrough |
| AC-UP-UX03 | Tính năng disabled/busy/unsupported/error/no document | Hover/focus/execute/cancel | State phân biệt và có cách tiếp tục; không undo từ mở panel; label EN/VI không sửa artwork | Action/control model + native walkthrough |
| AC-UP-TOOL01 | Pick/Shapes/Line/Pen/Node, constraint+snap hiện có | Canvas so numeric/bar/Properties, chuyển tool và sửa lại | Cùng constraint/geometry/session; giữ shortcut/modifier được hỗ trợ | Existing drawing/pen/node/editor selection tests |
| AC-UP-TOOL02 | Contour/Dimension/Gradient/Live Corners | Dùng cả quick controls và advanced settings | Đủ tham số, preview/commit/cancel đúng, reselect không duplicate | Contour/dimension/gradient/controller/native tests |
| AC-UP-TOOL03 | Blur/Shadow/opacity/blend và effect stack đã có | Sửa tham số/reorder/disable bằng UI mới | Order/type/instance IDs/source color đúng; UI không tạo effect để thay đổi view | Effect draft/appearance/storage/pixel tests |
| AC-UP-DATA01 | Tiếng Việt tổ hợp, glyph cluster/ligature theo capability, missing font | Edit/caret/IME/layout/outline/import/save/reopen | Unicode/source font và glyph mapping đúng; fallback có diagnostic, không fake glyph | Typography + text consumer + oracle font không cài sẵn theo AC-IPA |
| AC-UP-DATA02 | RGB/CMYK/spot/gray + ICC/alpha; document/page/selection scope | Preview/conversion/select same, Undo và export | Preview không đổi nguồn; convert đúng scope; matching dùng policy profile/spot/alpha đã chốt | Color/ICC/document color tests và profile fixtures |
| AC-UP-DATA03 | Sáu format, version/representation/feature hỗ trợ và unknown | Import→edit→save→export khi capability cho phép | Report riêng appearance/editability/provenance/production/export; no silent loss; unsupported giữ nguồn | Adapter-specific corpus và AC-IPA/AC-PDFP; đối chiếu từng hàng |
| AC-UP-LIFE01 | Dirty R+1 trong khi save/autosave R; hai tab cùng default document ID | Job hoàn tất/hủy/lỗi hoặc recovery | File là snapshot R, R+1 vẫn dirty; checkpoint/session đúng; atomic và không mất bản trước | Save recovery/storage + deterministic save/edit interleaving |
| AC-UP-LIFE02 | UI draft đang hoạt động, clipboard/automation ghi cùng tài liệu | RPC/COM stale/revoke/close-reopen và Undo | Một writer, target/revision guard giữ draft/local edits; clipboard không mất source metadata đang hỗ trợ | Existing automation RPC/MCP E2E/clipboard consumers |
| AC-UP-UNIT01 | Cùng geometry 25,4 mm và contexts mm/cm/m/in/ft/yd, text pt/pc, px có DPI | Nhập/đổi unit từ bar/Properties/page/ruler | Đúng value/unit, không double conversion hoặc sửa geometry do đổi display; bare input theo binding context | AC-UNIT01–AC-UNIT12 trong units-plan |
| AC-UP-UNIT02 | Legacy native/mm RPC và source SVG/DXF/PDF/CDR/EPS khác units | Import/edit/save/export/automation sau đổi UI units | Source sizes/provenance và *_mm APIs đúng; không dùng DPI màn hình để quy đổi; migration có version | Units/interop/storage/automation consumer tests |

## 8. Lệnh kiểm tra khi triển khai

Ví dụ theo target đã có trong CMake tại đợt khảo sát. Gói chọn các dòng liên quan; test đích CTest đôi khi khác executable (geometry/color/typography). Đọc lại CMake nếu source đổi.

~~~powershell
# Geometry + shaping consumer
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_geometry,test_pikdraw_desktop_shell -TestRegex '^(pikdraw_geometry_tests|test_pikdraw_desktop_shell)$'

# Action, numeric/session và các tool đã migrate
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_actions,test_pikdraw_vector_numeric,test_pikdraw_vector_tool_session -TestRegex '^(test_pikdraw_desktop_actions|test_pikdraw_vector_numeric|test_pikdraw_vector_tool_session)$'

# Selection / snap / culling
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_editor_selection,test_pikdraw_vector_snap,test_pikdraw_rendering_culling -TestRegex '^(test_pikdraw_editor_selection|test_pikdraw_vector_snap|test_pikdraw_rendering_culling)$'

# Frame và appearance
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_rendering_pixels,test_pikdraw_rendering_effect_compositor,test_pikdraw_canvas_view -TestRegex '^(test_pikdraw_rendering_pixels|test_pikdraw_rendering_effect_compositor|test_pikdraw_canvas_view)$'

# Async / storage / recovery
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_writer_safety,test_pikdraw_document_storage_fidelity,test_pikdraw_save_recovery -TestRegex '^(test_pikdraw_document_writer_safety|test_pikdraw_document_storage_fidelity|test_pikdraw_save_recovery)$'

# Text/color và tích hợp import
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_typography,test_pikdraw_icc_preview,test_pikdraw_document_color_workflow,test_pikdraw_desktop_import -TestRegex '^(pikdraw_typography_tests|test_pikdraw_icc_preview|test_pikdraw_document_color_workflow|test_pikdraw_desktop_import)$'

# Build canonical app cho nghiệm thu UI sau khi các gói tích hợp
.\tooling\native\build.ps1 -Target pikdraw
~~~

Benchmark Release dùng `benchmark_pikdraw_editor_tools`, `benchmark_pikdraw_rendering_effects` khi target còn đúng và trace `canvas_trace`; đọc runner/CMake trước chạy. Timing debug/assert-enabled khác release phải ghi riêng. Release tests phải xác nhận assertions/oracle còn hoạt động, không dùng kết quả tests bị NDEBUG loại bỏ làm bằng chứng đúng.

Thay contract nhiều consumer hoặc chuẩn bị release mới chạy gate rộng `tooling/native/check.ps1 -Suite all` dưới build runner/lock đúng policy. Không dùng source checks thay UX, performance, fidelity, production hoặc security containment.

## 9. Điều kiện hoàn tất và hiện trạng tài liệu

Mỗi lát migrate phải giao source + tests thích hợp + usage help + kết quả native trong cùng capability scope; giữ source cũ đã đúng đến khi đường mới đủ evidence. Không để hai đường ghi song song lâu dài sau khi migrate. Có thể rollback implementation/cache/UI của lát đó mà không mất file hoặc bỏ chức năng người dùng.

Chỉ đánh dấu hoàn tất khi các AC liên quan đạt, baseline không mất tính năng, key dữ liệu/Undo/save được bảo toàn, performance có phép đo đúng claim và UI đã thao tác trên canonical exe. Mọi mục chưa đo/chưa kiểm phải ghi rõ.

Đợt này cập nhật kế hoạch và bổ sung hướng dẫn thực thi, kiểm tra màu và hệ đơn vị. Không sửa runtime/schema/CMake/dependency lock/project-state. Lần chạy bốn target màu đầu bị chặn bởi native-build.lock thuộc PID 28504; sau khi phiên đó nhả lock, đã chạy lại bằng runner chuẩn: **4/4 CTest pass, tổng 5,06 giây**. Lệnh chính xác và giới hạn bằng chứng ở color-management-plan. Đây là baseline tests hiện có trên working tree, không phải triển khai/kiểm đủ các AC mới hoặc nghiệm thu màu in/UI.

Kiểm tài liệu đạt: 7 file, 94 liên kết, 62 AC duy nhất (30 AC-UP + 20 AC-CM + 12 AC-UNIT), 45 tên target hiện có đối chiếu CMake; không thiếu local link, sai cấu trúc bảng/fence hoặc AC reference. Tên target đề xuất trong plan màu cũ đã có cảnh báo chưa phải target được tạo. Kiểm tài liệu không thay nghiệm thu UI, màu in, performance hoặc sản xuất; lệnh triển khai phải chạy lại trên source thực của agent nhận việc.
