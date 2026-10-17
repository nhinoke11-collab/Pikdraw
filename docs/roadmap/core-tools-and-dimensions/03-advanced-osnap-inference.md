# Plan 03 — Advanced OSNAP và suy luận động 2D

Ngày: 17/10/2026. Trạng thái: **Planned** cho advanced queries; snap cơ bản có source riêng cần giữ.
Đọc [master](../master-implementation-plan-core-tools-and-dimensions.md) trước claim.
Mã gói `S`; thay phần Advanced OSNAP của master cũ và D15 trong catalog drawing-tools.

## 1. Kết quả và phạm vi nhận

Người dùng đang vẽ/sửa/đặt kích thước có thể lấy điểm đúng trên source geometry, thấy marker/tên quan hệ,
chọn nghiệm khi có nhiều nghiệm, acquire điểm để gióng và khóa hướng trong gesture. Kết quả sau click
trùng với source candidate đã hiển thị, không bị kết quả cũ hoặc numeric lock khác kéo lệch.

Không thêm solver ràng buộc tồn tại lâu dài, không sửa geometry chỉ vì hover, không dựng UCS 3D/raycast,
không snap vào outline raster/blur/drop-shadow hay bounding box rồi gọi là analytic center/intersection.
Các thao tác biến source để hàn/khép thuộc Node hoặc production validation, không thuộc OSNAP.

| Đợt | Capability cần nhận | Chưa được suy rộng |
| --- | --- | --- |
| S-core / S00–S02 | Giữ Endpoint/SourceNode/LineMidpoint/Grid; thêm source center/quadrant/nearest/half-length và line-line queries | Có enum không có nghĩa provider đã có |
| S-curves / S03 | Pair intersections và perpendicular/tangent line/arc/ellipse/quadratic/cubic theo từng row oracle | Degenerate/coincident/unstable roots chưa giải phải trả Indeterminate, không marker giả |
| S-inference / S04–S05 | Tracking/parallel/extension/acquisition/lock và native consumers | Không claim 3D, apparent intersection hoặc persistent constraint |

Không gọi advanced OSNAP complete nếu chỉ S-core pass. Từng checkbox được bật theo capability provider thực tế.

## 2. Nguồn chính thức

| Nguồn đã tra | Điều tham khảo / quyết định PikDraw |
| --- | --- |
| [Autodesk Object Snaps](https://help.autodesk.com/cloudhelp/ENU/AutoCAD-Web-Help/files/Drafting-and-Creating/AutoCAD_Web_Help_Drafting_and_Creating_Osnap_html.html) | Phân loại object points và quan hệ; apparent intersection là nghĩa khác intersection thật. PikDraw đợt này chỉ world 2D intersection |
| [Autodesk object snap tracking](https://help.autodesk.com/cloudhelp/2021/ENU/AutoCAD-Core/files/GUID-665DC37F-8C3E-414A-9369-72A13C0BE07A.htm) | Acquisition và alignment rays là hỗ trợ thao tác; không đồng nghĩa ràng buộc source |
| [SketchUp drawing/inference locking](https://help.sketchup.com/en/sketchup/introducing-drawing-basics-and-concepts) | Tooltip/marker, hover để tham chiếu và khóa inference theo ngữ cảnh. Không sao chép các arrow bindings 3D vào PikDraw |
| [Fusion drawing object snaps](https://help.autodesk.com/view/fusion360/ENU/?contextId=DWG-OBJECT-SNAPS) | Phân biệt quadrant với Curve X/Y Min/Max; UI cần nhãn đúng cho extrema |

Dwell 300 ms, tolerances/quotas và cách xếp hạng là **đề xuất PikDraw**, không coi là số tiêu chuẩn do các hãng quy định.

## 3. Baseline và source integration

Đọc vector/geometry/desktop AGENTS và README, registry và C1/C2/C7 theo master.

| Source | Reuse / thiếu |
| --- | --- |
| `modules/vector/include/pikdraw/vector/precision/snap/snap_config.hpp` | Enum đã có center/quadrant/nearest/t-mid/half-length/intersection/perp/tangent/parallel/extension/tracking; default acquire 8 DIP, release 12 DIP |
| `.../snap/snap_index.hpp` | SnapQueryStamp đủ document/epoch/revision/camera/config/capability/tool/query; stats/budgets và WorkBudgetExceeded |
| `modules/vector/src/precision/snap/snap_index.cpp` | Point KD trees và mặc định Endpoint/SourceNode/LineMidpoint/Grid; không phải curve-pair spatial query đầy đủ |
| `apps/desktop/src/tools/snap_index_worker.*` | Có host async adapter, cần audit lifecycle trước reuse; không dựng worker thứ hai tùy tiện |
| `direction_snap.hpp/.cpp`, numeric parser, ToolSession, InputRouter | Đã có Ortho/Polar/numeric locks; hợp nhất thứ tự giải chứ không viết một router cạnh tranh |
| `core/geometry/.../curves/curve.hpp` | Có analytic arc, split/length/nearest và line-line intersection; chưa suy ra general curve intersection/tangent provider |
| NativeWindow `register_drawing_actions` và shortcuts | F3 hiện mở Snap and Direction panel, không bật/tắt master |

S00 phải nhận kết luận N00 nếu query dùng handles; nghiên cứu thuật toán/oracle độc lập vẫn tiến hành được.
Không cần đợi Contour hoặc toàn bộ Node UI. Cập nhật mapping/stamps cùng Node/Dimensions theo master khi query key thay shape.

## 4. Hợp đồng query

### 4.1. Point semantics

| Kind | Định nghĩa nguồn và điều kiện | Không được thay bằng |
| --- | --- | --- |
| Endpoint / SourceNode | IDs anchor nguồn đã transformed; endpoint khác interior node | Góc bbox hoặc point trên flatten LOD |
| LineMidpoint | (A+B)/2 của line source | Midpoint mọi curve |
| BezierTMidpoint | B(0.5), label `Curve t-midpoint` | Nửa chiều dài |
| HalfCurveLength | t thỏa arc-length từ đầu = L/2 trong error budget; tính trong world transform | t=0.5 cho đường không đều |
| AnalyticCenter | Center C của analytic circular/elliptic arc, transform C ra world | Bbox center/centroid đường Bézier hoặc tâm suy từ ellipse cubic |
| Quadrant | Source ellipse parameter angles 0, π/2, π, 3π/2 trong basis đã xác định; chỉ điểm nằm trên arc span | X/Y extrema của ellipse quay/shear |
| World X/Y Extrema | Nghiệm đạo hàm thành phần world bằng 0; endpoints xét riêng; kind/label riêng nếu mở | Quadrant tham số |
| Nearest | Min khoảng cách world trên finite source curve với verified bound, hit-radius đánh giá trong DIP | Projection trên control polygon |
| Intersection | Điểm nằm trên hai finite source segments trong parameter domains | Giao của extension rays hoặc apparent intersection |
| Perpendicular | Từ anchor A đã đặt, `(B(t)-A)·B'(t)=0`, derivative không suy biến | Nearest bất kỳ mà không kiểm quan hệ |
| Tangent | Từ anchor A, `cross(B(t)-A,B'(t))=0`, đạo hàm khác 0 và hướng hợp phase | Thay endpoint gần hoặc derivative zero làm tangent |
| Extension / Parallel / ReferenceTracking | Line/ray ảo từ eligible source/acquired point, có state và kind riêng | Source edge thật hoặc persistent relation |

Circular tests phải xét transformed axes: nonuniform scale/shear có thể thành ellipse; không giữ R cũ để giải tangent.
Partial arc kiểm periodic span, signed sweep và reflection; circle/full ellipse không đoán từ bbox.

### 4.2. Broad phase, narrow phase và nghiệm

1. Giữ KD trees cho static source points. Thêm curve AABB index (BVH hoặc R-tree được S01 đo/chốt ADR nếu cần
   dependency mới); không thay toàn hệ index vì tên thư viện trong master cũ. Key curve dùng object/subpath/segment IDs.
2. Chuyển aperture DIP qua inverse camera thành conservative world bounds; rotation/shear/nonuniform DPI không
   dùng một hệ số zoom đơn khi không đủ. Narrow-phase cuối đo candidate bằng transform world→DIP.
3. Curve pair query chỉ xét overlapping bounds gần aperture sau culling; không all-pairs O(n²) mỗi pointer move.
   Những quan hệ cần điểm xa (center của arc/anchor tracking) có provider/bounds query riêng, không bị cắt nhầm vì arc ngoài aperture.
4. Line-line dùng predicate/overlap status hiện có. Arc/circle/ellipse có analytic reduction khi well-conditioned.
   General Bézier: isolate roots bằng interval/subdivision bounded rồi refine; chứng minh residual/domain/branch.
   Newton đơn từ một initial guess không đủ để nói đã tìm tất cả nghiệm hoặc đã xử lý near-tangent/repeated root.
5. Trả `ExactForPrimitive`, `BoundedApproximation`, `Overlap`, `NoSolution`, `Indeterminate`, `BudgetExceeded`
   (enum/API cụ thể S00 chốt). Không đổi Indeterminate thành nearest marker. Overlap interval không chọn một điểm tùy tiện.
   Candidate certified theo catalog có geometric error mục tiêu ≤0.001 mm, residual ≤0.0001 mm cho fixture well-conditioned;
   khi conditioning không cho bound này thì không hiện như nghiệm chính xác. Đây là query gate, không manufacturing tolerance.
6. Ranking giữ existing per-tool priority → configured kind priority → DIP distance → stable IDs/parameters.
   Dedup nghiệm bằng parameter intervals/source keys, không gộp hai source nodes khác IDs chỉ vì gần tọa độ.
   Nghiệm nhiều nhánh có `Next solution` action qua panel/menu; Tab tiếp tục làm numeric field navigation.

Residual phải có cùng đơn vị mm trước so ngưỡng: intersection dùng khoảng cách hai evaluated points;
perpendicular dùng độ lớn dot chia norm tangent; tangent dùng độ lớn cross chia norm tangent.
Không so trực tiếp dot/cross đơn vị mm² với ngưỡng mm. Reject derivative norm không xác định hoặc connecting vector
bằng 0 khi tool cần hướng; residual nhỏ không tự chứng minh forward error nhỏ ở nghiệm kém điều kiện.

Đề xuất query API mở rộng gồm `PairSourceRef{first,second}`, parameters/root intervals và branch identity;
existing SnapCandidateKey chỉ có một object/element nên phải nâng bằng type/version rõ và consumer migration.
Kết quả Intersection cần cả source refs; gửi một ID rồi gọi associative dimension là không đủ.
Candidate là dữ liệu không tin cậy từ executor: host kiểm finite/range/capability/refs/stamps/bounds trước dùng.

### 4.3. Budgets, async và transient state

- Không rebuild index mỗi mouse move. Build từ immutable snapshot; coalesce edits, publish một index đủ current stamp.
  Trong lúc build, chỉ dùng các providers đã current hoặc báo đang cập nhật; không tái dùng index cũ rồi giấu badge stale.
- Giữ query entry/result caps hiện có; S01 đề xuất bổ sung 4096 curve-pair visits, 100000 subdivision nodes/request,
  depth 32, 8192 results tối đa và measured memory cap cho curve index. Limits là admission policy cần đo,
  không rải constants tùy từng tool; exhaustion trả status, không treo UI.
- Mục tiêu query UI CPU p95 ≤4 ms trên fixture 100k source segments/20 m và máy được ghi rõ.
  Nếu không đạt, narrow-phase chạy executor bounded, chỉ newest pointer request; never drop document command.
  Ghi query/build CPU, peak RAM/scratch, cancel/UI-stall và input-to-visible riêng; không suy FPS từ query time.
- Before publish và consume: kiểm document ID/epoch/revision, camera, config, capability, tool context, query generation;
  commit command vẫn kiểm strict base revision. Async result cũ sau Undo/reopen/tab/source delete không hiển thị hoặc áp dụng.
- Acquired point lưu source key + stamp, không chỉ world coordinate. Config/selection/source/tool/doc change invalidate phù hợp;
  camera đổi giữ semantic ref nếu còn current nhưng phải reproject/requery screen hints trước hiển thị.

### 4.4. Inference state machine và numeric priorities

`Idle → HoverCandidate → Acquired → Tracking → Locked → Commit/Cancel`.
Dwell đề xuất 300 ms bằng monotonic clock, cùng key nằm trong acquisition aperture; jitter ra ngoài reset timer,
không acquire mọi endpoint đi ngang qua. Tối đa 4 acquired points/session, FIFO rõ; Esc bỏ lock trước rồi tracking.
Tracking rays 0/45/90 theo profile hoặc parallel/perp nguồn hợp lệ; chỉ hiển thị các modes đã bật.
Snap master và Polar là hai state khác nhau; master Off không tự đổi direction setting.

Thứ tự: input numeric/IME → hard numeric locks → active tool modifiers → direction/inference → candidate hợp constraints.
Không snap làm thay L/θ đã khóa. Numeric conflict: hint lý do và dùng numeric endpoint, không silently project tới snap.
Line/Polyline opt-in Shift giữ current eligible inference; thả Shift unlock và re-evaluate.
Node Shift-drag/Pen handle mode theo router của tool, không bị inference chiếm. Arrow keys chưa có inference binding mặc định
vì đang nudge/numeric/navigation. AltGr không được coi Alt bypass; pan/capture/focus có release cleanup.
Khóa hướng chỉ trong gesture; không lưu constraint giữa objects sau commit.

## 5. Phiếu UI

Menu chính `View > Snap > Snap and Direction…` → existing `action.tool.snap_panel`, giữ F3 mở panel cho tương thích hiện tại.
New `action.snap.toggle_master` là toggle riêng, không gán F3 trong đợt này. `action.snap.next_candidate`,
`action.snap.clear_tracking` không shortcut toàn cục; menu/panel truy cập được bằng bàn phím.
Không thêm toolbox tool hoặc standard toolbar vì snap là drawing aid dùng chung.
Reuse magnet/status controls hiện có; status có compact Snap/Polar indicators đúng state, không thêm một hàng form ở đáy.
Click indicator gọi đúng toggle; action có tên `Snap settings` mở/focus **Properties > Snap and Direction**,
tái dùng snap panel hiện có như cùng surface/state, không nhân đôi bảng settings.

| Field ID / EN — VI | Vị trí | Range/default, scope | State/validation |
| --- | --- | --- | --- |
| `snap.master` / Object snap — Bắt điểm | Status/Properties | Bool từ existing preferences; app/tool override | Chỉ current state; không tạo document Undo |
| `snap.mode.*` / Endpoint, Center… — Điểm mút, Tâm… | Properties > Object points / Relations | Bool per-kind theo config hiện tại; new modes opt-in sau provider gate | Unsupported checkbox disable + lý do; parent nhóm có partial state |
| `snap.acquire/release` / Acquire, Release — Bắt, Nhả | Properties > Sensitivity | Đề xuất 1..64 DIP, release≥acquire; giữ default 8/12 | Không đổi model mm tolerance; invalid không commit preference |
| `snap.dwell` / Acquire delay — Thời gian ghi điểm | Properties > Tracking | Integer 100..1000 ms, default 300; app/tool preference | Chỉ khi Tracking hỗ trợ; monotonic timer |
| `snap.direction` / Free, Ortho, Polar — Tự do, Vuông, Góc | Status quick + Properties > Direction | Existing enum/validated angle profile | Numeric locks vẫn ưu tiên; shared state |
| `snap.angle_reference` / Angle reference — Mốc góc | Properties > Direction | WorldX/construction frame/previous segment theo capability | Không sửa geometry khi đổi preference |
| `snap.targets` / Target policy — Nguồn bắt | Properties > Targets | Current layer/locked as reference/self/hidden theo current policy | Hidden mặc định không; locked có thể read-only reference khi bật rõ |
| `snap.display` / Markers, Hints — Dấu bắt, Gợi ý | Properties > Display | Bool từ profile | Không tắt query chỉ vì tắt marker; state có tên đọc được |
| `snap.next_solution` / Next solution — Nghiệm tiếp | Panel/menu action | Chỉ khi >1 current candidate | Re-evaluate current stamp; không tạo Undo |

No document: config có thể sửa app preferences, geometry candidates không có. Có document/no eligible source: không marker giả.
Draft giữ qua focus vào Properties; thay config increment config generation, requery; không mất numeric locks.
Busy/stale không giữ marker cũ như current. Multiple values trong per-tool settings hiển thị inherited/override/partial,
không dùng giá trị object đầu vì snap không là selection property.
Panel mở lại được khi dock đóng/bar hẹp; mở/focus không đổi config/document revision.

Icons: reuse magnet/polar assets; đề xuất `snap-center.svg`, `snap-intersection.svg`, `snap-perpendicular.svg`,
`snap-tangent.svg`, `snap-tracking.svg` trong bộ SVG hiện có. Marker hình học + nhãn, contrast tokens light/dark;
không hardcode Cyber Teal `#00f2c3`. Provenance/license và viewBox theo registry, không copy vendor icons.
Message IDs `ui.snap.*`; tooltip/focus `Snap settings — Choose points and relations / Thiết lập bắt điểm — Chọn điểm và quan hệ`.
Status ví dụ `Intersection / Giao điểm`, `Direction locked / Đã khóa hướng`, `Numeric angle takes priority / Ưu tiên góc đã nhập`.
Help `Help > Snapping`, `action.help.snap`: bật kind → hover/acquire → chọn ray/solution → khóa khi tool cho phép
→ đặt điểm/Esc; nói rõ t-mid/half-length/quadrant/extrema và không tạo constraints. Không công bố phím chưa bind/test.

## 6. Task và owned paths

Tất cả **planned**; new filenames/API có thể chỉnh theo tổ chức source nhưng phải giữ output/gate.

| Task / phụ thuộc | Owned paths candidates | Output / gate |
| --- | --- | --- |
| S00 / baseline, N00 cho source interpretation | Plan này; proposed geometry query contract delta ở C1; new query semantic fixtures/tests | Provider matrix + candidate pair/branch/stamp contract, analytic independent oracles, budgets; review Node/Dimensions consumers |
| S01 / S00 | New `core/geometry/include/pikdraw/geometry/queries/`, `core/geometry/src/queries/`, tests; snap curve index helper mới và CMake paths cần thiết | AABB broad phase, point KD reuse, measured limits/range/transform, no all-pairs; source registry thay chỉ qua lease |
| S02 / S01 | `modules/vector/.../precision/snap/` exact index/config/provider files; relevant vector tests/CMake | Center/quadrant/nearest/half-length/line-line and unsupported flags chính xác |
| S03 / S01 | Geometry query helpers/tests; snap provider integration shared với S02 sửa tuần tự | Bézier/arc/ellipse pair intersections, perp/tangent, branch/degenerate/cancel và residual gates từng row |
| S04 / S02+S03 | New vector tracking/inference helper; input_router/direction_snap exact files; new desktop snap integration; shared worker/tool controllers/menu/properties/localization/tests | Dwell/tracking/lock + numeric priority; integrate Line/Polyline/Pen/Node qua opt-in adapter |
| S05 / S04 | Shared shell paint/events/properties/actions/resources; exact native tests; benchmark fixture/report | Properties/status/help/keyboard + lifecycle/UX/performance; CMake and shell sequential với N/C/D |
| S06 / S05 | Plan/capability report; project-state khi lease riêng | Công bố từng provider row đã nhận và giới hạn query, handoff source/exe hash |

Chỉ delegate nếu được cho phép sau này; S02 và S03 không cùng sửa snap_index/config; giao narrow-phase geometry
và index read-only interface riêng rồi tích hợp tuần tự. Build do một coordinator dùng runner/lock chung.

## 7. Nghiệm thu và oracle

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-S01 | Existing endpoint/node/line-mid/grid fixture + camera rotated/DPI 150% | Toggle new kinds, snap ở nhiều zoom | Existing result/priority giữ; acquire 8/release12 DIP đúng; world point không drift | Existing snap+direction tests; independent transform/aperture calculations |
| AC-S02 | Cubic bất đối xứng có độ dài không đều | Bật t-mid rồi half-length | Hai points khác đúng source; half integral sai số trong profile | Independent high-precision integration/bracket, không gọi cùng function production làm oracle |
| AC-S03 | Ellipse quay/shear/reflection và partial arc | Center/quadrant/extrema | Center chính xác, quadrant đúng parameter span; extrema khác khi cần | Analytic conic fixture, transformed expected points |
| AC-S04 | Curves có 0/1/2/n nghiệm, near tangency và overlap | Query pair và Next solution | Tất cả current branches theo catalog; overlap/indeterminate rõ, residual/bound đúng | Independent resultant/interval/high-precision roots, not screen pixel match |
| AC-S05 | External anchor và circle/ellipse/cubic | Perpendicular/Tangent rồi đổi anchor | Roots đáp ứng dot/cross và domain; no derivative-zero fake tangent | Analytic circle formula + independent numeric certified cases |
| AC-S06 | Pointer hover 299/300 ms, jitter, source delete | Acquire rồi tracking/Shift lock/release | Dwell đúng, ray đúng, source delete invalidates, no persistent constraint | Fake monotonic clock unit + real native input trace |
| AC-S07 | Numeric L/θ locked; Pen handle mode; Node drag; IME/AltGr | Hover tangent/inference rồi nhấn modifier/Tab | Numeric không lệch, tool modifiers ưu tiên, Tab vẫn đổi field, IME không bị cướp | Tool/router + native consumers; release order and capture loss |
| AC-S08 | Async query đang chạy | Undo/reopen/config/camera/tool/tab đổi rồi result trả về | Mỗi stamp stale bị chặn cả marker lẫn commit; source không bị sửa bởi snap | Index worker/session tests, strict revision assertions |
| AC-S09 | 100k sparse segments và dense-overlap scene | Pointer sweep/build/cancel | Bounded pair visits/results, no O(n²) stall; fail budget có diagnostic | Stats + measured CPU p50/p95/p99/RAM/cancel; present/input tách riêng |
| AC-S10 | Snap Properties đóng/bar hẹp, master Off nhưng Polar On | F3, sửa mode ưu tiên/targets rồi trở canvas | Mở đúng panel không toggle; states đồng bộ; zero document revisions | Native shell/actions/preferences persistence + manual DPI/theme/focus |
| AC-S11 | Imported normalized SVG/DXF source và reference-only/image/effects | Query points | Chỉ eligible source, không snap nhầm raster/halo thành vector | Consumer source-type tests, no imported-format capability expansion |

Lệnh baseline hiện có; chỉ chạy theo thay đổi, ghi kết quả thực:

```powershell
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_geometry,test_pikdraw_vector_snap,test_pikdraw_vector_direction_snap,test_pikdraw_vector_input_router -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(pikdraw_geometry_tests|test_pikdraw_vector_snap|test_pikdraw_vector_direction_snap|test_pikdraw_vector_input_router)$'
# Sau native integration, rebuild các consumer mới trước CTest:
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_drawing_tool_controller,test_pikdraw_pen_native_shell,test_pikdraw_desktop_shell -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_drawing_tool_controller|test_pikdraw_pen_native_shell|test_pikdraw_desktop_shell)$'
```

New query/inference tests đăng ký trong suite geometry/vector, không tạo executable tạm ngoài runner.
Native app build và thao tác OS theo master; báo DPI/theme/capture/keyboard nào chưa chạy.
Nếu kernel không cho error bound/residual yêu cầu thì giữ row disabled, không tăng tolerance để biến pass.

## 8. Format/representation/feature và bàn giao

| Format/source | Render/query | Edit/metadata | Export/production |
| --- | --- | --- | --- |
| Native source line/quadratic/cubic/arc | Query đúng normalized type qua providers đã nhận | Snap không mutate; candidate refs chuyển command dưới writer | Không tạo output format, không manufacturing claim |
| SVG static 1.0/1.1 subset | Adapter map units/transforms/segments trước query; không query trên rendered preview | IDs nội bộ khác source locator; C8 provenance chưa tự hoàn tất | SVG export semantics không đổi; curve snap pass không chứng minh source round-trip |
| ASCII DXF `dxf-entities-v1` | LINE/ARC/CIRCLE/LWPOLYLINE trong profile; tâm/quadrant chỉ nguồn analytic thực | Preserve unit/layer mapping trong fixture; unsupported entities vẫn reject | Import-only; không DXF writer/DIMENSION bởi OSNAP |
| PDF/AI authoring/AI PDF/CDR/EPS | Chưa adapter hiện tại; không áp dụng cho native query | Không suy geometry từ ảnh/reference | Không claim export/production |

Mỗi format/version cần adapter riêng theo C8/C9; query chung bắt đầu sau normalization, không tự tải tài nguyên mạng.
Handoff: provider matrix rows/status, stamps/API delta, analytic/reference fixtures, query/build/cancel/peak stats,
commands/results, source/exe hash, native evidence và rows còn unsupported. Dừng enable provider nếu identity/source
representation chưa chốt, pair ref không đủ hoặc stale candidate còn được dùng.
