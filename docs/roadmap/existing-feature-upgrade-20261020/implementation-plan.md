# Hướng dẫn thực thi đại tu tính năng hiện có — U00–U12

Trạng thái cập nhật 21/10/2026: **đã triển khai một phần U00–U12; chưa hoàn tất các gate của kế hoạch.** Các lát cắt đã có gồm Contour floor/default 0,02 mm; field phản ánh effective 0,02 với legacy 0,005 giữ nguyên tới Apply; semantic ID `ContourSteps` tách khỏi `PolygonSides`, `GradientStopOffset/Opacity/Midpoint` tách khỏi Shadow/Polygon IDs và `TextFrameWidth/TextLineSpacing` tách khỏi Shadow IDs, bỏ sentinel effect-index cho nhận dạng field; dispatcher từ chối action thiếu handler; menu và Properties đơn vị trang dispatch qua semantic ActionId, Properties có flyout 8 đơn vị theo active page và Undo giữ nguyên kích thước; hủy hợp tác Snap index; native v13 lưu đơn vị hiển thị riêng theo trang và thước theo đơn vị trang; parser dùng registry suffix chung và nhận `1/8in`, `1 1/2in`, `5ft+6in`; status Snap Grid, measurement tooltips/guideline status, preset khổ trang, import review và recovery size labels theo đơn vị trang; profile resource ICC bất biến với hash SHA-256 từ bytes, page ICC command và native v15 dictionary có dedup/digest verification; native v16 lưu raster preview pixel basis; native v17 lưu ICC binding cho paint RGB/CMYK/Gray, native v18 lưu Dimension/Contour metadata cùng typed Contour paints; decoder/WIC chuyển ICC sang sRGB nếu hỗ trợ, giữ profile/byte nguồn và đánh dấu fallback; paint ICC preview có CPU pixel và Direct2D/CPU parity tests; raster writer có RGB ICC pixel conversion và File > Export Raster UI cho active/all pages TIFF/JPEG; backend TAC validate/fix theo output-condition ID/limit và scope. Settings > Color & ICC nay có manual TAC preflight, Check chỉ đọc, Fix riêng, report freshness guard, tooltip và keyboard focus. Release benchmark raster 10k objects hiện đạt median 5,814 ms và budget 8,3 ms sau khi bỏ hash lookup thừa từ BVH result. Boolean tổng quát nay dùng adapter Clipper2 2.0.1 pin bằng archive SHA-256; path cong được flatten theo error budget, chạy trên integer grid có kiểm tra range/point budget, báo Empty/Unsupported/ToleranceNotMet/WorkBudgetExceeded riêng. Đây là polygon approximation, không là Boolean chính xác trên Bézier; Boundary/Simplify general path, cancel và oracle sản xuất còn mở. Page/paint ICC đã round-trip và giữ binding trong native v17; Dimension/Contour spec và typed CMYK/Spot/alpha/overprint/ICC paints round-trip trong v18 (storage/Contour 3/3, D2D Spot pixel 1/1), và selection Assign/Convert cùng một Undo đã nối. TAC giữ K/alpha/overprint nhưng giảm tỷ lệ CMY; Spot/raster chưa đánh giá và ngưỡng chưa gắn profile ICC. Full rasterizer target nay pass gồm ICC preview regression và Contour Spot pixel regression; native visual walkthrough, proof/gamut oracle ngoài sRGB, interop/performance corpus và print acceptance còn mở. Xem các AC còn lại trong [kế hoạch tổng](../existing-feature-upgrade-20261020.md), [màu C00–C09](color-management-plan.md), [đơn vị S00–S05](units-plan.md) và [UI](../../design/features/editor-consistency-upgrade-20261020.md). Toàn bộ U00–U12/C00–C09/S00–S05 chưa nghiệm thu.

## 1. Điều kiện bắt đầu và thứ tự

1. Đọc AGENTS hiện hành, `docs/START.md`, `registry/modules.json`, `docs/project-state.json`, C1/C8/C9, UI/workspace/code organization/acceptance. Snapshot cũ không thay source/consumer thật.
2. `git status --short`, đọc diff file sắp sửa, đọc native build lock và process owner. Làm tại `C:/Users/CK ART STAGE/Downloads/PikDraw`; giữ dirty work khác, không reset/stash/worktree/branch/task lease.
3. Xác nhận canonical `build/win-dev/apps/desktop/pikdraw.exe` sau build đúng source. Không chạy binary cũ làm bằng chứng sau sửa.
4. Theo U00 → U01 → nền C00/C01/C02 và S00/S01 → U02. Sau đó U03/U04/U05 theo dependencies; U07 sử dụng binding màu/units chung; U08 migrate từng tool. Hoàn tất màu C03–C09, units S02–S05 và U09/U10 trước U11.
5. U06 đo baseline sớm nhưng chỉ tối ưu sau oracle render/màu tương ứng. Tách đo, sửa và nghiệm thu; không đợi đến cuối mới xử lý color hoặc units.
6. Shared model/storage/CMake/shell/action header sửa tuần tự, đọc lại diff trước mỗi lượt. Ownership trong tài liệu là scope logic cho người thực thi; không cho phép ghi đè code công việc đang có.
7. File code tối đa 2.000 dòng; file đã vượt ngưỡng phải extract theo domain trước khi thêm feature, giữ public consumers và includes. Tài liệu không yêu cầu tạo class/file cho từng field.
8. Gói chỉ đóng khi AC liên quan + consumer native + help + fidelity/performance tương ứng đạt. Không tạo bảng handoff/snapshot trung gian; báo trực tiếp kết quả và các AC còn mở.

Các references Graphite/Penpot/Inkscape/LibreCAD/Krita là nguồn ý tưởng và thuật toán theo phạm vi đã audit. Không port nguyên UI hay graph runtime, không bỏ chức năng PikDraw để giống upstream. Library mới phải pin/license/dependency/oracle; GPL/AGPL code không chép vào sản phẩm nếu chưa chốt nghĩa vụ phù hợp.

## 2. Contract nền dùng trong mọi gói

| Contract | Nội dung cần thống nhất | Consumer |
| --- | --- | --- |
| EditorContext | Tab-instance + document ID + epoch + revision + PageId + selection/tool session + view generation; immutable projection | Action/property/jobs/render/automation |
| ActionDescriptor | ID, alias, label/help/icon, shortcut authority, scope/capability/checked/disabled reason, typed execution result | Menu/toolbar/toolbox/context/keyboard |
| PropertyDescriptor | Semantic domain/key, target IDs, source type, QuantityKind/UnitContext hoặc ColorContext, range/default, scope, Mixed/read-only | Bar/Properties/dialog/numeric/canvas |
| EditSession | Original/draft, target/version, validation, merge key, preview generation; Commit/Cancel/no-op | Tools/effects/text and numeric |
| JobKey/Result | Context identity, source rev/targets, session+params hash, view dependencies, owned snapshot, cancellation/budget/result status | Index/contour/render/effect/import/save |
| Color resources/service | ICC bytes identity, source/working/view/output separation, quality/report, Assign vs Convert | Xem C00–C09 |
| Unit registry/context | Canonical mm, typed quantities, field unit/DPI/locale, no rounded writes | Xem S00–S05 |
| Capability/report | Format version/representation/feature/limits; view/edit/source/export/production riêng | Interop/UI/preflight |

Không tạo authority thứ hai sau migration. UI intent chuyển thành command/service; một writer áp dụng mutation. Job result chỉ là dữ liệu chưa được phép commit, phải kiểm đầy đủ context/revision/capability/limits.

## 3. U00 — baseline và bảo toàn / P0

**Sở hữu:** tests hiện có theo module, capability/action inventory trong plan/help hiện có. **Phụ thuộc:** không.

1. Theo mọi declaration đến registration, handler, enable/checked, control/menu, document mutation và save consumer. Label/roadmap/action constant riêng không là feature runtime.
2. Khóa baseline theo nhóm trong kế hoạch tổng: file/tab/page, Pick/transforms, shapes/path/Pen/Node, contour/corners/dimensions, paints/gradients/effects, text, image/import/export, recovery/clipboard/automation/settings.
3. Ghi đường gọi/menu/shortcut/modifier và mọi field hiện có, kể cả Advanced. Mỗi relocation sau này phải có đường mở lại rõ.
4. Chọn regression corpus: native legacy/current; mixed pages, transformed path/hole/large coordinates; Vietnamese text; RGB/CMYK/spot/gray/ICC; raster DPI; effect stacks; import representation subsets.
5. Ghi source/hash, machine/backend/monitor, known failures và evidence type. Đo baseline p50/p95 theo workload §6 master; không gán số mục tiêu thành số đo.
6. Dùng semantic document/node/paint/effect IDs và snapshot assertions trước; native walkthrough xác nhận reachable behavior. Không tạo snapshot docs lịch sử/hàng loạt fixture một lần.

**Gate:** AC-UP-KEEP01; inventory không còn entry “đã làm” chỉ dựa trên declaration. Baseline failures được giữ rõ để phân biệt regression.

## 4. U01 — action/state/shortcut / P0

**Sở hữu:** `apps/desktop/include/pikdraw/desktop/actions/`, `src/actions/`, `src/shell/desktop_window.cpp`, `native_window_menu_actions.cpp`, `native_window_shortcuts*.cpp`, `src/runtime/composition_root.cpp`.

1. Implement descriptor/result trên dispatcher hiện có; phân biệt Executed/NoChange/Disabled/Unsupported/Failed/Cancelled. `dispatch=true` không được chỉ nghĩa “có ID”.
2. Preflight enable rẻ từ snapshot/capability, không dựng Boolean/Contour/shape cache trong paint menu.
3. Migrate File/Undo/View/Properties, tiếp tools/Arrange/Effects/Color; native MenuActionId thành adapter/alias tới cùng command. Giữ native handlers đang đúng đến khi consumer mới đủ evidence.
4. Giữ IDs/shortcut aliases/settings. Re-register dọn shortcut cũ thuộc action đó; xử lý collision/override/reset có policy, không xóa shortcut của action khác.
5. Checked/disabled reason lấy cùng context cho menu/toolbox/keyboard. Placeholder no-op giữ ID/integration nhưng disable có lý do, không success giả.
6. Dispatch diagnostics dùng bounded ring/counters; không đổi document Undo hoặc cắt history người dùng. Tooltip lấy shortcut hiệu lực.
7. Mở rộng `test_pikdraw_desktop_actions`, `test_pikdraw_desktop_shell`, `test_pikdraw_desktop_composition`; native menu/context/keyboard cùng action.

**Gate:** AC-UP-ACT01/02 và KEEP01; không mất command hoặc collision âm thầm. UI mapping trong UI spec.

## 5. U02 — property binding và edit sessions / P0

**Sở hữu:** desktop presenters/controller adapters, `native_window_numeric_input/edit.cpp`, style/text fields, property bar/Properties consumers; document commands khi cần. **Sau:** U01; S01/C02 là nền converter.

1. Projection EditorContext đọc từ authority hiện có; không dùng global active pointer hoặc revision đơn lẻ. Hai tab có cùng default document ID vẫn khác context.
2. Semantic field keys tách `shape.polygon.sides`, `contour.steps`, `gradient.stop.midpoint`, `gradient.stop.offset`, `effect.shadow.offset_x`, `text.font.size`; bỏ overloaded field interpretation từng lát.
3. Tạo typed value/range/unit/color/scope/state descriptor. None/Mixed/empty không là zero; locked/reference-only khác invalid.
4. EditSession start/update/validate/preview/commit/cancel; original và target IDs giữ ổn định. Context đổi reject late event; no-op/open/focus không revision.
5. Migrate Transform + Appearance trước; Color dialog gọi C02, numeric gọi S01. Bar/Properties/canvas ghi một command, không gọi lẫn nhau bằng string.
6. Enter/Apply/pointer-up commit một Undo; Esc cancel draft; capture lost/tool change xử lý theo policy. Local text Undo/IME composition không bị document/canvas shortcut lấy mất.
7. Gộp drag theo session+target+property; không merge khác thao tác vì gần thời gian. Validation lỗi giữ draft/đường sửa hoặc cancel, không áp cho target mới.
8. Mở rộng desktop shell/style/effect/drawing tests, vector tool_session/numeric; native thao tác cả bar lẫn Properties.

**Gate:** AC-UP-SYNC01–04, AC-UNIT03/06, AC-CM04. Ngừng dùng old writer path sau khi alias đã migrate.

**Tiến độ 21/10:** Numeric IDs đã tách Contour spacing/steps/tolerance/miter khỏi Polygon/Arrange; Gradient stop và geometry khỏi Effect/Object IDs; Text frame/line spacing khỏi Shadow IDs và sentinel effect-index; Blur sigma khỏi Shadow offset/feather/opacity. `NumericFieldDescriptor` hiện ánh xạ semantic key + quantity và thay danh sách chiều dài lặp ở seed/input/commit; range, scope, Mixed/Error, context token và edit-session authority dùng chung còn mở. Effect commit kiểm loại node hiện tại. Shell regression kiểm Shadow feather sửa revision một bước rồi Undo; `test_pikdraw_desktop_shell`, `test_pikdraw_gradient_native_shell` và `test_pikdraw_units_settings_ui` pass 3/3. Không coi đây là hoàn tất U02.

## 6. U03 — geometry, Contour và liên kết đo / P0

**Sở hữu:** `core/geometry/src/operations/boolean.cpp`, `offset.cpp`, headers/flattening/queries; vector Node/Contour; document shaping/contour/dimension commands; desktop workflow adapters. **Sau:** U00, tích hợp UI sau U02.

1. Viết operation contract: closed/open, fill-rule, holes, self-intersections, coincident/tangent/degenerate, units/range/error budgets, output order/stable source semantics.
2. Giữ exact rectangle Boolean fast path hiện có. Backend polygon tổng quát mở capability theo subset đã kiểm; không gọi polygon output là exact curve.
3. Đánh giá Clipper2 C++ hoặc candidate phù hợp từ reference audit; pin/license và neutral API trước dependency. Checked scale/local origin không vượt integer range; budget flatten+quantization+operation+reconstruction.
4. Tách Empty/Unsupported/Invalid/Cancelled/BudgetExceeded. Không delete source/target trước khi có result hợp lệ; keep-source/target và IDs theo command.
5. **Contour .02 mm default/min**, kiểm .05/.1; đồng bộ request/controller/UI/validator/tests. Legacy .005 không sửa khi mở; effective .02 được report, explicit Apply normalization có Undo. Không hạ floor về .005.
6. Offset polygon/curve theo contract Inside/Outside/ToCenter; joins/miter/holes/collapse/steps; source transform nonuniform/shear có basis rõ. Không lấy tolerance UI làm numerical predicate epsilon.
7. Node/handles/live corners và associative Dimension dùng source geometry contracts; sửa node không nhân effect/path, measured value không lấy rounded label.
8. Mở rộng geometry `tests/operations/test_offset.cpp` và Boolean tests hiện có, document contour/node/dimension commands, desktop align_boolean/controller tests. Thêm analytic distance/area/containment oracle, oracle topology cho holes, negative/large coordinates.

**Gate:** AC-UP-GEO01–04; AC-UNIT07. Geometry không hỗ trợ vẫn báo đúng, giữ source, không fallback rectangle giả thành công.

## 7. U04 — selection/snap/layers query / P1

**Sở hữu:** `modules/vector/src/selection/hit_test.cpp`, precision/snap, neutral geometry/document query index mới nếu cần, renderer spatial adapter, desktop snap/layers. **Sau:** U02; U03 nếu query changed geometry.

1. Tách source bounds index khỏi rendering effect/ink bounds. Core/module không import desktop; vector dùng BVH trung lập trong `core/geometry`, không phụ thuộc renderer.
2. Index giữ owned bounds, không giữ object pointer. Cache selection khóa bằng snapshot identity/document/epoch/page/revision; renderer dùng cùng khóa cộng camera zoom/world margin.
3. Broad phase giới hạn candidates, narrow phase giữ exact geometry và painter order. Stroke mở rộng bounds bảo thủ; text tolerance mở rộng query theo scale; invalid/overflow bounds hoặc quá 1M entries chuyển về exhaustive path.
4. Giữ exhaustive oracle trong test, so result ordered với indexed trên fixture có overlap, transforms, text, hidden object và unsupported group. Group vẫn không phải editable target theo semantics hiện có.
5. Snap ưu tiên kind/distance/constraint deterministic; preserve guard hiện có; result async theo context. Không snap hidden/reference ngoài policy.
6. Layers cache visible rows và stable IDs, invalidate selection/expand/filter/reorder đúng; focus/scroll giữ khi tree đổi.
7. Mở rộng editor_selection/vector_selection/vector_snap/rendering_culling tests; đo 1k/10k/100k và candidates vs total.

**Gate:** AC-UP-QUERY01/02; không regression exact hit vì broad phase thiếu bounds.

**Tiến độ 21/10:** `geometry::AabbIndex` đã thay BVH riêng trong renderer và được dùng cho hit/marquee broad phase. Selection cache khóa theo snapshot pointer + document ID + epoch + revision + PageId; renderer cũng kiểm các khóa này, camera zoom và margin. Static `SelectionModel` queries vẫn là exhaustive oracle; indexed exact narrow phase giữ thứ tự z và kiểm lại object state. `pikdraw_geometry_tests`, `test_pikdraw_vector_selection`, `test_pikdraw_editor_selection`, `test_pikdraw_rendering_culling` pass 4/4. Regression bao phủ 704 layer entries (701 path objects), text transform, hidden/overlap/unsupported group, page/revision invalidation và cache renderer qua hai document cùng revision; AABB fixture 1,003 entries cho 4 candidates. Chưa đo p95 rebuild/query, peak memory hay 1k/10k/100k workload trên UI runtime; nhóm layer/snap và group editing còn mở.

## 8. U05 — scheduler và derived preview / P1

**Sở hữu:** `apps/desktop/src/tools/snap_index_worker.*`, tool workers/workflows, rendering effects jobs, document async result guards. **Sau:** U02/U04, U03 với Contour.

**Tiến độ 21/10:** `SnapIndexWorker` hủy token của generation đang chạy khi có request mới hoặc đóng worker; `SnapSourceIndex::build` kiểm tra token theo lô ở object/subpath/node/segment và lúc dựng cây. Targeted vector-snap và drawing-controller tests pass. Đây mới là cooperative cancellation của snap index; các job khác, thời gian hủy p95/max và peak memory vẫn chưa được nghiệm thu.

1. Kiểm pending/running ownership và thread/memory quota hiện có trước thêm scheduler. Không tạo threadpool riêng mỗi tool.
2. Latest-preview slot mỗi session; running job có cooperative cancel tại batch boundary, result stale bị loại ở consumer dù worker đã cancel.
3. Tách preview disposable với commands/save/export/import đã được nhận; không coalesce mất output bắt buộc hoặc starvation I/O.
4. Shared JobKey/owned snapshot và budget; tab close/page change/Undo/revoke/device loss cancel đúng scope. Không callback giữ dangling window/document pointer.
5. Derived effect/contour/gradient drafts không ghi source hoặc thêm node để preview; accepted result chỉ attach đúng instance/target.
6. Quota tính scratch/image/cache và queued snapshot owners; eviction không mất source assets. Cancel release bounded resources.
7. Deterministic fake completion A/B/reverse/cancel tests; document_async/writer_safety, effect_draft và controllers; đo cancellation latency/pending peak.

**Gate:** AC-UP-JOB01/02 và SYNC03; latest UI đúng nhưng worker vẫn chiếm CPU quá lâu chưa được coi hoàn tất cancellation.

## 9. U06 — render/readback/present / P1

**Sở hữu:** `core/rendering`, `native_window_canvas*.cpp`, desktop `performance/canvas_trace.*`. **Sau:** U04/U05 và oracle C03 liên quan.

1. Trace riêng culling/build/CPU submit/GPU completion nếu đo được, staging readback, ICC/filter, GDI copy, overlays/present. Không gọi tổng CPU WM_PAINT là FPS.
2. Đo overlay-only, unchanged scene, pan/zoom, drag, blur/shadow, large images và HW/WARP/CPU; chọn bottleneck có số liệu.
3. Giảm invalidation/cache miss thừa trước: no-change reuse, source vs view generations, geometry/paint/text/effect/ICC cache keys và ownership đúng.
4. Dirty ROI giữ halo/backdrop/clip/group/alpha dependencies; damage union old+new bounds. Compare pixel oracle, không chỉ screenshot “trông giống”.
5. Nếu full staging/readback thực là bottleneck, thử partial copy/tile với capability/recovery; direct GPU presentation là bước riêng sau khi đo chứng minh cần. Không đổi toàn renderer trước baseline.
6. Device loss/resize/backend fallback không present frame thiếu; discard wrong-generation frame và recover caches đúng. CPU fallback/oracle vẫn dùng.
7. Mở rộng rendering_pixels/effect_compositor/gradient/culling/canvas_view; benchmark rendering_effects và trace input-to-visible p50/p95.
8. Không đo được GPU completion/present thì ghi not_measured; performance claims chỉ theo machine/workload/config đã chạy.

**Gate:** AC-UP-RENDER01/02; color/units source unchanged; memory/performance có evidence riêng.

## 10. U07 — UI/UX native có một layout model / P1

**Sở hữu:** `shell_geometry.*`, native paint/pointer/focus/events, themes/catalog, `apps/desktop/resources/icons`, Properties/Layers; không tạo frontend khác. **Sau:** U01/U02 và contexts S01/C02.

1. Dùng UI spec làm contract menu/action/toolbox/bar/Properties/labels/help. Chốt compact layout trên 1366×768 và dock resize; full settings luôn reachable.
2. Semantic control tree tính bounds/visibility/tab order theo DIP/text metrics; paint/hit/focus/tooltip/UIA dùng cùng kết quả. Không giữ RECT song song theo nhánh click và paint.
3. Token font/spacing/control/icon/state/theme chung; EN-first/VI catalog, Unicode measured thật; user theme/workspace/toolbox visibility settings được migrate.
4. Migrate chrome/Transform/Appearance trước, tiếp từng U08 vertical. Overflow có tên, giữ focus/edit session, không cắt unit suffix/Apply/Cancel.
5. Keyboard arrows/flyout/Esc/tab/focus-visible, IME ownership, accessible name/role/value/state; icon SVG semantic có provenance/license.
6. Properties menu mở lại dock và focus đúng section; opening không document mutation. Layers cùng dock nhưng không làm mất panel đang edit.
7. Kiểm Mixed/None/Busy/Invalid/Unsupported có text/help và cách tiếp tục; tooltip lấy registry shortcut.
8. Native walkthrough EN/VI, 100/125/150/200%, light/dark/high contrast; UIA inspection; ảnh capture gắn đúng source build.

**Gate:** AC-UP-UX01–03 và SYNC01/02. Layout unit tests/screenshot không tự là nghiệm thu interaction.

## 11. U08 — migrate các tool thành lát hoàn chỉnh / P1

**Sở hữu:** controllers/tools, tool state, bar/Properties/overlays/help. **Sau:** các nền U02/U03/U05/U07 tương ứng.

Một lát phải nối source model → controller/session → canvas/bar/Properties → command → Undo/save/reopen → test/help; không chỉ thay paint.

| Thứ tự | Tool/cụm | Việc bắt buộc và gate riêng |
| --- | --- | --- |
| 1 | Pick/Transform/Arrange | Bbox/anchor/aspect/mixed, local/world basis, selection modes, mm↔display; no rounded writes; keyboard/nudge |
| 2 | Rectangle/Ellipse/Polygon | Shared constraint solver cho preview/commit; Ctrl/Shift đã có; sides riêng domain; flyout giữ mọi entry |
| 3 | Line/Polyline/Pen | Existing precision expressions, snap/direction, numeric context; endpoint/node IDs và curve handle basis; finish/cancel/reopen |
| 4 | Node/Live Corners | Stable node/segment/corner IDs, cusp/smooth/symmetric, numeric+canvas, transformed source; reselect edit existing |
| 5 | Contour | .02 mm physical minimum/default; spacing/steps/direction/join/miter/tolerance, async preview, Break Apart/keep source semantics |
| 6 | Dimension | Existing types/associativity/detached status, primary/dual units/precision/style, source recompute; no display-to-geometry loop |
| 7 | Gradient | Stops/midpoint/opacity/geometry/profile; stable stop IDs, selected stop same color projection; no duplicate paint on activation |
| 8 | Blur/Shadow/Appearance | Sigma/offset/color/blend/stack/order/enabled/opacity, local units, stable effect instance; reselect edits one existing node |
| 9 | Text/Image context | Text font/pt/run/IME và source image DPI/ICC/reference/editable status; không claim raster authoring suite đã làm |

Mở rộng existing tests `test_pikdraw_drawing_tool_controller`, `test_pikdraw_drawing_tool_ui`, `test_pikdraw_pen_native_shell`, `test_pikdraw_node_tool_controller`, `test_pikdraw_contour_tool_controller`, `test_pikdraw_dimension_tool_controller`, `test_pikdraw_gradient_tool_controller`, `test_pikdraw_gradient_native_shell`, `test_pikdraw_effect_draft`. Chỉ chạy targets phần đã sửa; native xác nhận đủ advanced controls.

**Gate:** AC-UP-TOOL01–03 + các AC geometry/units/color tương ứng; giữ toàn bộ options cũ.

## 12. U09 — màu, text và format / P0 màu, P1 tích hợp

**Sở hữu:** core/color/typography, document resources/storage, interop adapters/writers/capabilities, desktop color/text/import. **Sau:** C00–C02 làm sớm; phần tích hợp theo U02/U07.

1. Thực hiện đầy đủ C00–C09; 20 AC-CM trong plan màu. Không để Color “sau UI” hoặc giữ hai converter độc lập.
2. Text theo [font/pages/import plan](../import-font-pages-artboards-20261020.md): preserve Unicode/font identity/glyph clusters, DirectWrite run/bidi/fallback contracts và text editing; không fake glyph thiếu.
3. Page model giữ PageId/settings/associativity, không tạo document giả cho mỗi trang; source PDF page-set/locked-original đang có cần đọc lại thay vì implement trùng.
4. Mỗi SVG/DXF/PDF/AI/CDR/EPS/raster adapter có version/representation/subset và loss report, normalize vào neutral model; C8/C9 không phải một parser dùng chung.
5. Tách view, editability, metadata, export round-trip và production. Export lấy explicit source/output condition, không lấy proof viewport.
6. Raster metadata giữ original bytes/ICC/bit depth/DPI/EXIF dù preview là RGBA8; writer RGB mới không chứng minh CMYK/high-bit print.
7. Run targets typography/color/document/workflow/interop đúng scope; AC-IPA/AC-PDFP hiện có giữ nguyên, thêm AC-UP-DATA và AC-CM.

**Gate:** AC-UP-DATA01–03, AC-CM01–20, AC-IPA/AC-PDFP liên quan và C8/C9; unsupported phải report, không silent loss.

## 13. U10 — lưu, recovery, clipboard và automation / P1

**Sở hữu:** document storage/recovery/async, `native_window_files.cpp`, clipboard, desktop workflows, modules automation/MCP/COM consumers. **Sau:** U01/U02/U05; schema theo C01/S02/U09.

1. Capture immutable snapshot R trên writer; encode/I/O ở job được phép, result chứa tab-instance/doc/epoch/path/save generation.
2. Atomic save/checkpoint, giữ bản cũ đến success; completion save R chỉ đánh dấu saved R, edit R+1 vẫn dirty. Close/reopen/path change không nhận result cũ.
3. Autosave không block UI timer cho encode dài; không starvation; debounce/quota không bỏ snapshot cần recovery.
4. Recovery source hiện đã có modal/tests: kiểm và mở rộng, không tạo lại feature theo project-state cũ. Validate native resource/profile/unit limits trước restore.
5. Clipboard giữ source geometry/mm, style/profile/Unicode/resource data đang hỗ trợ; cross-document units chỉ đổi UI; không paste proof pixels thay editable source khi có source.
6. MCP/COM dùng command/service chung, typed units/color, capability/permission/revoke/session/revision; input ngoài process untrusted và worker quotas giữ.
7. Khi external command gặp active draft: reject/busy hoặc kết thúc draft theo policy được công bố, không overwrite đang nhập. Undo source transaction có boundary rõ.
8. Test deterministic save R/edit R+1/reverse completion/close-reopen/two tabs; document storage_fidelity/writer_safety/save_recovery và automation_rpc; in-process MCP/COM runtime theo capability thật, không suy từ service unit test.

**Gate:** AC-UP-LIFE01/02; AC-CM14/18 và AC-UNIT11; atomic failure không làm mất bản đã lưu.

## 14. U12 — hệ đơn vị / P1, nền sớm

Thực thi [S00–S05](units-plan.md) và AC-UNIT01–12; không chỉ thêm dropdown.

- S00 inventory → S01 registry/parser trước hoàn tất U02.
- S02 metadata/schema phối hợp C01, giữ Dimension enum ordinals và *_mm API.
- S03 migrate toàn bộ length fields/rulers/grid/status; font pt và scalar/count/% giữ đúng loại.
- S04 per-format normalize, CSS px 96/in, image DPI X/Y, design px explicit và OS DPI riêng.
- S05 native/help/legacy/clipboard/automation gates; 12 ca Given/When/Then/Verify.
- mm/cm/m/in/ft/yd, pt/pc và px theo context; đổi đơn vị giữ geometry mm, không rescale artwork.
- Contour .02 minimum/default vật lý độc lập unit; legacy .005 mở không rewrite.

**Gate:** AC-UP-UNIT01/02 + AC-UNIT01–12; mỗi field S00 có consumer evidence.

## 15. U11 — tích hợp và release / P1/P2

**Sở hữu:** tests existing, help/capability docs, snapshot hiện hành ngắn. **Sau:** mọi gói release đã hoàn thành.

1. Theo inventory U00 kiểm không mất entry/field/shortcut/modifier/source metadata; declaration chưa implement vẫn có status đúng, không bị xóa để “sạch”.
2. Build/test theo runner, targeted targets trước; broader suite khi thay shared contracts hoặc release theo repo policy. Không chạy binary stale hoặc report tests không thực sự executed.
3. Native canonical app walkthrough 9 tool verticals, page/tab/file/recovery/clipboard/settings và Color/Units; semantic IDs/revisions/Undo/serialized data làm evidence chính cho dữ liệu.
4. Performance Release cùng corpus/máy/source; p50/p95/max, cancellation/memory/cache counters, HW/WARP/CPU; giải thích claim nào chưa đo.
5. Format corpus theo version/representation; independent check physical size/color/metadata/export; print/CNC/security containment riêng nếu claim liên quan.
6. Help EN/VI, menu/action/icon/tooltip/focus/status và UIA; mismatch UI/runtime phải sửa trước đánh dấu done.
7. Cập nhật docs lỗi thời theo evidence; `docs/project-state.json` current snapshot <=8.000 byte, không append history.
8. Dọn test source/ảnh/log/fixtures dùng một lần thuộc chính task; giữ regression hữu ích và dirty user work. Commit rõ scope chỉ sau build/tests phù hợp pass và không gồm thay đổi người khác.
9. Báo người dùng: đã sửa, paths, lệnh/exit/kết quả, AC đạt/còn mở, giới hạn runtime/performance/production. Không tạo phiếu handoff hành chính.

Nếu một lát mới lỗi, rollback implementation thuộc lát đó về đường baseline đã đúng hoặc disable capability mới có reason; không xóa source feature/data, reset toàn repo hay gỡ options cũ.

## 16. Nhóm lệnh dùng khi triển khai

Đây là lệnh thực thi tương lai, không phải kết quả đã chạy. Luôn kiểm native lock; runner xử lý lock, không xóa lock của process đang sống.

~~~powershell
# Actions/bindings
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_desktop_actions,test_pikdraw_desktop_shell,test_pikdraw_vector_tool_session -TestRegex '^(test_pikdraw_desktop_actions|test_pikdraw_desktop_shell|test_pikdraw_vector_tool_session)$'

# Geometry/Contour
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_geometry,test_pikdraw_document_contour,test_pikdraw_contour_tool_controller -TestRegex '^(pikdraw_geometry_tests|test_pikdraw_document_contour|test_pikdraw_contour_tool_controller)$'

# Units foundations
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_geometry,test_pikdraw_vector_numeric,test_pikdraw_document_dimension_model -TestRegex '^(pikdraw_geometry_tests|test_pikdraw_vector_numeric|test_pikdraw_document_dimension_model)$'

# Color foundations
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_color,test_pikdraw_icc_preview,test_pikdraw_document_commands,test_pikdraw_document_color_workflow -TestRegex '^(pikdraw_color_tests|test_pikdraw_icc_preview|test_pikdraw_document_commands|test_pikdraw_document_color_workflow)$'

# Query
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_editor_selection,test_pikdraw_vector_snap,test_pikdraw_rendering_culling -TestRegex '^(test_pikdraw_editor_selection|test_pikdraw_vector_snap|test_pikdraw_rendering_culling)$'

# Rendering
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_rendering_pixels,test_pikdraw_rendering_effect_compositor,test_pikdraw_canvas_view -TestRegex '^(test_pikdraw_rendering_pixels|test_pikdraw_rendering_effect_compositor|test_pikdraw_canvas_view)$'

# Lifecycle
./tooling/native/build.ps1 -Test -TestTarget test_pikdraw_document_writer_safety,test_pikdraw_document_storage_fidelity,test_pikdraw_save_recovery -TestRegex '^(test_pikdraw_document_writer_safety|test_pikdraw_document_storage_fidelity|test_pikdraw_save_recovery)$'

# Canonical app
./tooling/native/build.ps1 -Target pikdraw
~~~

Các targets bổ sung và AC cụ thể ở master/color/units/UI docs. Recheck registration CMake trước regex; target geometry/color/typography có CTest name khác target. Gate rộng/benchmarks theo §8 master và runner hiện hành, không tự thay toolchain/ABI.

Đợt viết plan: lần đầu Color foundations bị lock PID 28504; sau khi phiên đó nhả lock, chạy lại qua runner đạt **4/4 CTest pass, 5,06 giây**. Đây là baseline tests hiện có, chưa phải triển khai các AC mới; chưa nghiệm thu native UI, overprint/proof accuracy hay physical print. Chi tiết lệnh/kết quả ở plan màu. Kiểm Markdown/link/AC chỉ là kiểm tài liệu.

