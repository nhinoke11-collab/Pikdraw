# Plan 01 — Node Tool: chỉnh node, handles và topology

Ngày: 17/10/2026. Trạng thái: **Planned**, chưa chứng nhận UI/runtime.
Đọc [master và baseline](../master-implementation-plan-core-tools-and-dimensions.md) trước khi nhận task.
Mã gói: `N`; thay phần Node của master cũ và phạm vi D11 trong catalog drawing-tools.

## 1. Kết quả người dùng và giới hạn

Người dùng chọn path, chọn một hoặc nhiều node/segment, kéo hoặc nhập số để sửa, chuyển loại node,
thêm/bớt/bẻ/nối/đóng/đảo đường, rồi Undo và lưu/mở lại mà vẫn giữ source, style, IDs và hình học đúng.
Đợt đầu đi hết luồng **một PathObject**; đợt mở rộng thêm nhiều path và nối qua object với chính sách style rõ ràng.
Chỉ gọi Node hoàn tất khi cả hai đợt bắt buộc N01–N06 đạt; nếu giao từng đợt phải ghi capability cụ thể.

Không làm Simplify/curve fitting, brush, NURBS, fillet, Boolean tổng quát, auto-stitch, driving constraints hoặc 3D.
Text/Image/Contour dẫn xuất không có node sửa trực tiếp; phải chọn source hoặc Convert có chủ đích khi capability đó tồn tại.

## 2. Nguồn chính thức và quyết định PikDraw

| Nguồn đã tra | Hành vi tham khảo | Áp dụng trong plan |
| --- | --- | --- |
| [CorelDRAW 2026 Node types](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Mac/CorelDRAW-en/CorelDRAW-Node-types.html) | Cusp có hai handles độc lập; Smooth đối hướng, có thể khác độ dài; Symmetrical đối hướng và bằng độ dài | Ba mode phải có quy tắc hình học và marker phân biệt; không đồng nhất Smooth với C2 |
| [Adobe Illustrator Convert anchor points, 11/02/2026](https://helpx.adobe.com/uk/illustrator/desktop/draw-shapes-and-paths/modify-paths/convert-anchor-points-on-a-path.html) | Direct Selection chọn anchor; convert và kéo handles thay đường | Phân biệt chọn object/node/handle; property bar và Properties dùng cùng command |

Các lựa chọn range, modifier, deletion, join và phím dưới đây là quyết định PikDraw, không phải lời hứa tương thích phím với nhà cung cấp.

## 3. Đọc source và khóa semantic trước code

Đường dẫn trong code block của plan tính từ source gốc; chúng là owned-path candidates, không phải file đã tồn tại.
Đọc `core/document/{README,AGENTS}.md`, `modules/vector/{README,AGENTS}.md`,
`core/geometry/AGENTS.md`, `apps/desktop/AGENTS.md` và các source trong master.

| Có trong source | Reuse / việc còn thiếu |
| --- | --- |
| `EditablePath`, typed IDs, immutable authoring_source và Path2D adapter | Source là authority duy nhất; không sửa cả source và adapter độc lập |
| `MovePathNodeCommand`, `SplitPathSegmentCommand`, `DeletePathNodeCommand`, `JoinPathSubpathsCommand`, `ClosePathSubpathCommand` | Viết test consumer cho hành vi thực trước khi reuse; không xây commands cùng nghĩa lần hai |
| `PathTopologyMapping` có Preserved/Created/Removed/Merged | Thiếu mapping khoảng tham số/split/reverse phục vụ Dimension; N01 phải bổ sung hoặc có result companion được review |
| `split_curve`, `measure_curve_length`, `nearest_point_on_curve`, analytic EllipticArc | Reuse nguồn binary64; không pick/split từ polyline render |
| `action.tool.node_edit`; menu Node disabled; F10 xử lý WM_SYSKEYDOWN mở menu | Có ID không chứng minh tool tồn tại; N05 phải tích hợp handler thật |

**N00 là gate chặn mutation mới.** C1 nói handles là vectors từ anchor, trong khi `path_adapter.hpp`,
`editable_path.hpp` và Move trong `core/document/src/commands/command.cpp` đang dùng điểm control tuyệt đối local.
C1 còn cần đối chiếu Corner có handles, AutoSmooth khi drag và endpoint Smooth với validator/helper.
Ưu tiên giữ bytes/geometry hiện có, chốt contract theo representation đã chứng minh với storage/renderer/Pen consumers;
không đổi dữ liệu file hiện hữu thành offsets bằng một patch UI. Nếu đổi encoding phải có schema migration và fixture riêng.
N00 ghi quyết định/ADR nếu đổi hợp đồng kiến trúc, không tự chuyển trạng thái C1 extension thành Accepted.

## 4. Hợp đồng hành vi bắt buộc

### 4.1. Chọn, drag, tọa độ và history

- Key chọn node là `(ObjectId, SubpathId, NodeId)`, segment tương tự; selection thuộc interaction state.
  Mở tool không convert source, không tạo Undo. Path chưa có EditablePath cần lệnh chuyển nguồn rõ ràng,
  giữ geometry/style và IDs object; nguồn không thể map thì disable với lý do.
- Hit-test trong DIP, thứ tự handle đang hiện → node → segment → object; marker không che mất node trùng.
  Click chọn; Shift-click thêm/bớt node; kéo marquee chọn node nằm trong vùng; không chọn node hidden/locked.
  Đợt đầu chưa lasso; entry không được giả hoạt động. Ctrl+A chỉ chọn nodes khi canvas Node có focus;
  khi edit field/IME thì dùng hành vi text. Esc: hủy drag trước, rồi bỏ sub-selection, rồi thoát tool.
- Preview clone nguồn bất biến. Drag theo world delta, inverse composite transform về local; multi-node dịch
  cùng world delta. Singular transform/read-only/broken target: không sửa; không cộng số screen pixel vào source mm.
- Kéo anchor của Bézier dịch handles tương ứng cùng delta. Smooth giữ đối hướng và độ dài phía còn lại;
  Symmetric cập nhật hai độ dài bằng nhau. Cusp độc lập. Không tạo NaN khi handle zero hoặc tangent chưa xác định.
- Khi đổi mode, active handle quyết định tangent; nếu không có active handle thì dùng next-anchor trừ previous-anchor,
  fallback chord một phía nếu hợp lệ. Giữ độ dài handle hiện có khác 0, thiếu handle thì khởi tạo bằng 1/3 chord cạnh kề;
  Symmetric dùng active length hoặc trung bình hai lengths đã hợp lệ. Tangent zero/adjacent arc không phù hợp thì reject.
  Line → cubic phải chuyển Corner sang mode cho phép handles theo kết luận N00, không để source vi phạm validator.
- Đợt đầu không dùng Alt để vừa bypass snap vừa đổi mode. Việc đổi mode qua control rõ; Shift khi drag anchor
  khóa hướng theo construction frame hiện tại, Shift trong numeric/IME giữ nghĩa của field. Modifier press/release
  phải được router phân giải theo phase, không copy logic Pen qua Node vô điều kiện.
- Pointer moves không đổi document. Mouse-up/Enter hợp lệ: một atomic command trên base revision;
  drag nhiều nodes là một batch, lỗi một target rollback toàn bộ. No-op/cancel/capture loss không thêm history.
  Pan/zoom suspend draft; camera đổi chỉ bỏ screen hint. Source đổi ngoài gesture thì hủy draft có lý do, không rebase ngầm.

### 4.2. Các phép chỉnh sửa

| Action đề xuất | Điều kiện và semantics | ID mapping / thất bại |
| --- | --- | --- |
| `action.node.select_all`, `.invert_selection` | Trong eligible paths của Node session, không bao gồm hidden/locked | Không document transaction |
| `action.node.move` | Một anchor: nhập world X/Y; nhiều anchor: ΔX/ΔY, không giả X/Y=0; world transform được invert | Giữ mọi IDs; validate range trước publish |
| `action.node.add` | Split segment tại nearest source parameter, `0<t<1`; menu Add dùng t=0.5 được ghi nhãn `Add at midpoint (t)` | Giữ endpoint IDs và leading segment ID; tạo node/trailing segment ID; map hai khoảng t |
| `action.node.delete` | Với node nội bộ: preview bỏ hai cạnh kề, nối hai anchor còn lại bằng line; action gọi rõ thay đổi dạng. Endpoint: bỏ segment kề. Không auto refit | Node/segments mất phải map Removed; không tạo closed subpath <2 nodes; xóa toàn path qua command riêng |
| `action.node.break` | Open path: node nội bộ được tách thành hai endpoint; closed path: mở tại node đã chọn | Node gốc làm endpoint đầu, endpoint bản sao ID mới; preserve segments/traversal và map aliases rõ |
| `action.node.join_bridge` | Chính xác hai open endpoints; thêm line bridge, không kéo endpoints về giữa | Giữ IDs; khác object chờ N06 |
| `action.node.merge` | Hai open endpoints trong threshold do người dùng nhập; chọn survivor rõ, chuyển endpoint kia tới survivor và preview phần thay đổi | Không merge mọi điểm gần; trả Merged + survivor. Trường hợp không hợp lệ reject toàn bộ |
| `action.node.close` | Subpath hở: thêm closing line. Nếu trùng endpoint, yêu cầu merge-to-start rõ ràng | Không duplicate first node/zero-length edge |
| `action.node.reverse` | Đảo thứ tự traversal, swap handles, đảo sweep arc | Giữ IDs; map t mới = 1−t; fill/holes không được đổi nghĩa |
| `action.node.to_line` | Selected source segments → chord, preview mất độ cong | Giữ segment ID nếu cùng semantic identity; cập nhật mode/handles hợp lệ, không xóa handles còn dùng bởi cạnh khác |
| `action.node.to_curve` | Line → cubic với controls 1/3 và 2/3 chord; quadratic → cubic degree elevation đúng hình | Không tự chuyển EllipticArc sang cubic; đổi loại khác nghĩa cần mapping/diagnostic |
| `action.node.mode.cusp/smooth/symmetric` | Chỉ áp vào node có adjacent Bézier phù hợp; corner-to-smooth tạo controls từ chord và preview | Smooth G1, symmetric thêm equal length; endpoint/zero-length không giả smooth |

AutoSmooth có source mode nhưng UI mở rộng chỉ bật khi derivation/persistence và chuyển sang manual đã qua oracle.
Không gọi `Delete` từ Node selection vào handler Delete Object. Conversion có thay geometry phải có preview/Undo;
không thêm dialog xác nhận cho từng thao tác nhỏ.

**Analytic arc (N04):** split/reverse giữ arc chính xác. Anchor gắn arc không đi qua Move thuần Bézier.
Trong arc endpoint mode, giữ center/basis, chiếu pointer lên ellipse bằng nearest analytic, đổi start/sweep theo nhánh
được chọn; preview chỉ đúng điểm trên ellipse. Numeric off-ellipse bị từ chối với hướng dẫn dùng arc parameters.
Sửa center/radii/sweep là controls riêng, cập nhật cả endpoint anchors nguyên tử. Với chain không thể giữ topology
và adjacent relations thì disable; không ép arc thành cubic. Convert Arc to Path Approximation chỉ là task mở rộng riêng.

### 4.3. Mutation và references

Proposed input: target IDs + base epoch/revision + gesture generation + payload typed.
Proposed result: new source + IDs preserved/created/removed/merged + source segment parameter intervals,
orientation và semantic type change. Đây là đề xuất API, phải chốt N01 cùng D00 trước khi consumer dùng.
Split và reverse cho phép map segment refs xác định; delete/ambiguous merge trả broken reason;
không tìm node gần nhất để đoán relink Dimensions/Contour. Undo khôi phục IDs/source và reference state.

N06 nối khác object: chỉ top-level unlocked PathObjects tương thích style/appearance/source metadata;
chọn target style rõ trước commit, giữ target ObjectId, remap element IDs/source references từ object bị tiêu thụ,
chuyển layer/order có policy và Undo đầy đủ. Khác paint/effect chưa có conversion policy thì reject kèm lý do;
không hàn hai hình rồi lấy ngẫu nhiên style của phần tử đầu. Instance/group isolation chỉ bật theo consumer đã kiểm chứng.

## 5. Phiếu UI dùng để triển khai

Theo [chuẩn UI](../../design/ui-ux-standard.md) và [workspace](../../architecture/workspace-layout.md).
Menu chính `Tools > Node Tool` → existing `action.tool.node_edit`; operations đặt trong `Object > Nodes > …`.
`Object > Properties` focus `Properties > Nodes`; action mở `action.node.properties.focus` không sửa source.
Toolbox: dùng slot Node sẵn có dưới Pick, không hardcode lại toàn bộ thứ tự toolbox; không thêm standard toolbar.

F10 là binding **đích đề xuất**, chỉ bật ở N05 sau khi xử lý WM_SYSKEYDOWN/menu và test Alt-menu/Shift+F10.
Không gán A vì source đang dùng cho Text; nếu chưa giải quyết F10, Node vẫn truy cập đầy đủ qua menu/toolbox.
Delete chỉ dùng trong canvas sub-selection; các phép khác chưa có shortcut toàn cục, không quảng bá Ctrl+K.

| Field ID / EN — VI | Vị trí | Range/default, scope | State và commit |
| --- | --- | --- | --- |
| `node.x/y` / X, Y — X, Y | Bar và Properties > Position | Finite mm theo C1/range source; default vị trí selected node, selection | Một node editable; nhiều nodes hiện Mixed và chuyển Δ mode; Enter một batch |
| `node.dx/dy` / Move X/Y — Dịch X/Y | Properties > Position | Finite mm, default 0; selection draft | Enable cho multi-node, preview rồi Apply; 0/0 no-op |
| `node.mode` / Node type — Loại đỉnh | Bar và Properties > Handles | Cusp/Smooth/Symmetric, default từ source; selection | Mixed khi khác loại; incompatibility disable với lý do |
| `node.handle.in/out.length/angle` / Handles — Tay đòn | Properties > Handles | Length ≥0 finite theo range C1, angle rad internally/° display; nguồn hiện tại | Chỉ Bézier hợp lệ; same semantics với drag, Enter/Apply một transaction |
| `node.merge_tolerance` / Merge distance — Khoảng nối | Properties > Topology | Đề xuất 0…1 mm, default 0.001 mm; tool preference, merge draft | Không thay C1 coincidence; chỉ merge explicit, preview displacement |
| `node.arc.center/radii/start/sweep` / Arc — Cung | Properties > Arc | R>0 finite; sweep khác 0, độ lớn sweep≤2π trong representation hợp lệ; selection | N04; invalid geometry/adjacent constraints reject, không fallback |
| `node.selection_count` / Selected — Đã chọn | Bar/read-only Properties | Count thực | Không thay geometry/Undo |

Bar chỉ count, position/mode và Add/Delete thường dùng; Break/Join/Close/Reverse và handle/arc details trong Properties.
Nút `Node properties` cùng menu mở lại đúng section khi dock đóng hoặc bar hẹp. Cả hai bề mặt dùng cùng presenter,
validation, mixed/locked state và command. Không thay selection chỉ vì mở panel.

| Context | Hành vi |
| --- | --- |
| Không document | Tool/edit disable, hint tạo/mở tài liệu |
| Có document, chưa chọn path | Tool active; hint chọn path; Properties không hiện giá trị selection giả |
| Một eligible path / nhiều eligible paths | Hiện source markers; multi-path chỉ khi N06 nhận; không âm thầm sửa object đầu |
| Hidden/locked/Image/Text/reference-only/derived contour | Không sửa; lý do và đường chọn source phù hợp |
| Numeric/IME / pan / focus change | Giữ draft theo ToolSession; shortcuts không chiếm text; mouse capture loss cancel drag |
| Busy/stale/error | Bỏ preview cũ; giữ artwork; error gần control, có thể thử lại từ source hiện tại |

Icons đề xuất `node-edit.svg`, `node-cusp.svg`, `node-smooth.svg`, `node-symmetric.svg`, `node-break.svg`,
`node-join.svg` trong `apps/desktop/resources/icons/`, kiểm asset registry đang có trước khi thêm.
Nét/viewBox/theme theo bộ hiện hành; tự vẽ hoặc nguồn license được ghi, không copy icon Corel/Adobe.
Message IDs `ui.node.*`; label ngắn `Node/Đỉnh`, `Join/Nối`, `Break/Tách`, `Reverse/Đảo chiều`.
Tooltip/focus `Node Tool — Edit anchors and handles / Công cụ đỉnh — Sửa đỉnh và tay đòn`;
hint `Select nodes. Drag to move. Esc cancels. / Chọn đỉnh. Kéo để sửa. Esc hủy.`
Marker mode có shape và tên, selected/focus không chỉ dựa màu. Tab/Shift+Tab đi controls, không cycle candidate bằng Tab.

Help đề xuất `Help > Node Tool`, `action.help.node`, nội dung 5 bước: chọn path → chọn node/handle → kéo/nhập số
→ Join/Break theo điều kiện → Undo/lưu/mở lại. Ghi arc, locked và conversion limitations; không quảng bá F1 trước binding.

## 6. Task có thể giao cho agent

Tất cả **planned**. Danh sách paths dưới đây là candidate tối thiểu; trước claim phải resolve file thực và leases.
Mỗi task cập nhật đoạn evidence của plan này; cùng file plan và CMake/shared shell thì sửa tuần tự.

| Task / phụ thuộc | Owned paths cần claim | Đầu ra và gate |
| --- | --- | --- |
| N00 / baseline | `contracts/c1-geometry.md`; `core/document/tests/commands/test_commands.cpp`; quyết định ADR nếu cần | Representation matrix controls/Corner/AutoSmooth/arc; consumer review Pen/storage/render; test trước mọi mutation mới |
| N01 / N00 | `core/document/include/pikdraw/document/commands/path_commands.hpp`; `core/document/src/commands/command.cpp`; `core/document/src/commands/path_command_support.hpp`; new `core/document/include/pikdraw/document/commands/node_commands.hpp`, `core/document/src/commands/node_commands.cpp`; command tests; CMake document | Atomic commands, expanded topology/parameter mapping; reference vectors review với D00; không đổi codec khi chưa có nhu cầu |
| N02 / N01 | New `modules/vector/include/pikdraw/vector/tools/node/`, `modules/vector/src/tools/node/`, `modules/vector/tests/tools/node/` | Node selection/drag/numeric/session, một path, stale/cancel, new target `test_pikdraw_vector_node` |
| N03 / N02 | Cùng feature paths N02 và `modules/vector/CMakeLists.txt` khi đăng ký | Break/bridge/merge/close/reverse, selected segment conversion, undo/IDs |
| N04 / N03 | Feature node paths; geometry curve files chỉ khi audit cho thấy cần và có lease riêng; source command tests | Analytic arc edits, quadratic/cubic controls, mode conversions và save/reopen; không sửa renderer source geometry |
| N05 / N02–N04 | New `apps/desktop/src/tools/node_tool_controller.*`; shared `native_window.hpp`, events/shortcuts/paint/canvas/menus; action routing; icon/localization registry; desktop tests/CMake | Native integration, Properties/help, F10 conflict resolution; shared shell chỉ claim khi leases trước đã release |
| N06 / N05 | Node feature paths; cross-object commands/validation và related tests; consumer reference tests | Multi-path/cross-object compatible-style join, hierarchy policy, IDs/undo/source-style acceptance |
| N07 / N06 | Plan này, evidence Node; `docs/project-state.json` khi riêng lease có sẵn | Kiểm đúng integrated snapshot, công bố phạm vi nhận; handoff/limitations |

## 7. Ca nghiệm thu và bằng chứng

Mỗi hàng là Given/When/Then/Verify; mở rộng test hiện có trước khi thêm target mới.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-N01 | Cubic không ở origin, absolute controls và transform shear/reflection | Kéo anchor + nhập world X/Y | Preview=commit, handles đi cùng delta, source/IDs/style giữ | Command+Node tests; so independent transformed control points, không dùng adapter làm oracle duy nhất |
| AC-N02 | Node Cusp có unequal handles | Chuyển Smooth, kéo một handle, chuyển Symmetric | G1 đối hướng; Symmetric lengths bằng; no C2 claim, invalid/zero case có outcome rõ | Numeric cross/dot/length oracle, Undo/Redo |
| AC-N03 | Quadratic/cubic/arc source; closed edge cuối | Add tại t=0.37 rồi reverse | Hình không đổi trong budget analytic, IDs mapping intervals đúng | De Casteljau/analytic evaluation nhiều t và continuity endpoints; document commands |
| AC-N04 | Open/closed subpath có node nội bộ | Break → Bridge/Merge → Close → Undo từng bước | Không duplicate closing node, không zero-length; IDs restored và mappings đúng | Command + native workflow test |
| AC-N05 | Hai endpoints cách 0.002 mm, threshold 0.001 mm | Merge rồi tăng threshold tường minh | Lần đầu reject; lần sau preview/commit displacement, survivor đúng | No auto-stitch; revision/history unchanged ở reject |
| AC-N06 | Arc có center/basis và node cạnh Bézier | Kéo endpoint theo arc mode / nhập điểm ngoài ellipse | Giữ analytic source hoặc reject rõ; không biến thành cubic | Source endpoint/sweep oracle; save/reopen |
| AC-N07 | Drag nhiều nodes và source bị xóa/Undo/tab đổi giữa lúc drag | Mouse-up hoặc async result cũ trả về | Không half-commit/no silent rebase; capture loss hủy, pan không làm drift | ToolSession + shell stale/cancel tests |
| AC-N08 | Properties đóng, bar hẹp, modes Mixed | Mở Node properties, sửa mode/handle rồi Undo | Đúng section, mở panel không revision; bar đồng bộ; scope đầy đủ | Native shell integration, keyboard/focus và ảnh đúng exe/source |
| AC-N09 | Text/IME hoặc numeric field có focus; Win32 menu | Nhấn Delete/Ctrl+A/F10/Alt/Shift+F10 | Không xóa object khi sửa node, không chiếm text; binding/menu đúng policy | Native input test và OS thao tác trực tiếp ghi riêng |
| AC-N10 | Hai path cùng style và hai path khác effects | Join qua object rồi Undo/lưu-mở | Compatible join atomic + mapping; incompatible không mất paint; Undo restore layers/order/IDs | Command/storage + consumer D04 reference regression |

Lệnh baseline hiện có (chỉ chạy khi task sửa đúng layer; không phải kết quả của plan):

```powershell
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_document_commands,test_pikdraw_document_storage_fidelity -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_document_commands|test_pikdraw_document_storage_fidelity)$'
# Sau khi target Node mới được đăng ký:
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_vector_node,test_pikdraw_desktop_shell,test_pikdraw_desktop_actions -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_vector_node|test_pikdraw_desktop_shell|test_pikdraw_desktop_actions)$'
./tooling/native/build.ps1 -Configuration Debug
```

Nếu đổi C1/storage/shared commands, chạy thêm Pen, geometry, snap và consumer references liên quan trên binaries mới.
Kiểm manual: 1366×768 và 1920×1080, DPI 100/125/150/200%, light/dark, bàn phím, capture/focus;
ghi riêng những ô chưa kiểm. Không tự nhận native UX từ unit test hoặc ảnh GDI offscreen.

## 8. Interop và điều kiện dừng

| Format/representation/feature | Render/edit | Metadata/native | Export/production |
| --- | --- | --- | --- |
| Native EditablePath các segment đã test | Sửa source sau N gates | Giữ IDs/modes/handles/arc; native save/reopen bắt buộc | Chưa là chứng nhận CNC |
| SVG 1.0/1.1 static subset → adapter → EditablePath | Reuse Node sau normalization; không sửa raster/reference/effect chưa map | Giữ data hiện có; provenance C8 cần gate riêng | SVG path có thể mất node-mode semantics; round-trip riêng, không hứa Corel/Adobe handles mode |
| ASCII DXF `dxf-entities-v1` LINE/ARC/CIRCLE/LWPOLYLINE → adapter | Chỉ nguồn đã normalized; CIRCLE dưới transform không được giả circle | Units/layer/source mapping theo fixture | Không DXF writer trong plan này; production not assessed |
| PDF/AI authoring/AI PDF/CDR/EPS | Unsupported hiện tại; không áp dụng với native authoring | Không có source metadata/round-trip claim | Không bật định dạng mới bởi Node pass |

Đọc C8/C9 theo master; mỗi format có adapter riêng, share commands sau normalize, không ép qua converter chung.
Node không đổi source color/effects/provenance ngoài lệnh rõ ràng. Dừng mở capability nếu N00 chưa chốt,
mapping không đủ cho references, geometry source khác preview, hoặc command xóa mất metadata/style.
Handoff theo protocol, ghi source/exe hash, commands/results, ca chưa chạy và limitation, không ghi Node complete chỉ vì có icon.
