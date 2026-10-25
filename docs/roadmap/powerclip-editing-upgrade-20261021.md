# Nâng cấp và sửa PowerClip: chọn, vào sửa và thao tác nội dung

Ngày lập: 21/10/2026. Cập nhật: 22/10/2026. Trạng thái: **đã triển khai phần native geometric PowerClip và chạy gói tích hợp; chưa đủ bằng chứng để đóng toàn bộ AC-PC-01…26**. Xem bảng kết quả ở mục 9.
Audit trên working tree tại commit nền ba25ad7, có nhiều local edits. Dẫn chiếu source phản ánh nội dung đang đọc, không chỉ HEAD.

## 1. Yêu cầu và kết quả cần đạt

Người dùng báo không thấy nút Edit, không vào được đối tượng chứa clip, nội dung bên trong không kéo được và nhiều thao tác PowerClip lỗi.

Workflow bắt buộc: **chọn khung từ canvas → Edit Contents → chọn/kéo/resize/rotate nội dung → Finish → sửa khung/lock/fit → Undo/Redo → save/reopen/export**.

Phạm vi:

- Khung path kín, rectangle/ellipse/compound path; nội dung Path/Text/Image/Group; PowerClip lồng nhau.
- Edit Contents, Select Contents, Edit Frame, Place Inside Frame, thêm vào khung có sẵn, Make, Extract, Release, empty frame, Lock Contents, Center/Fit/Fill/Stretch và Copy Contents From.
- Pick/region selection, keyboard, Properties, property bar, Layers, CPU/D2D/WARP và tọa độ lồng nhau dùng cùng semantics.
- Geometric clips nhập từ CDR/SVG đã normalize vào native model sửa qua cùng command/query/render.
- Artistic Text làm khung cần geometry provider từ glyph thật và gate riêng. Thiếu gate thì disable có lý do; không outline/rasterize ngầm rồi báo hỗ trợ text frame.

Alpha mask, lens, transparency group, group effects/pass-through/knockout có profile C8/C9 riêng. Sửa editor native không khôi phục dữ liệu đã mất trong parser hoặc tự mở capability mọi CDR version.

Lượt lập plan chỉ đọc source/tài liệu và viết roadmap; chưa chạy build/test hay thao tác native. Báo cáo người dùng là bằng chứng UX được báo; bảng audit dưới đây phân biệt source đã xác nhận với tác động cần tái hiện.

## 2. Tài liệu Corel đã đối chiếu

Nguồn chính thức:

1. [CorelDRAW 2026 Help — PowerClip objects](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-PowerClip-objects.html).
2. [Corel Discovery Center — PowerClip tutorial](https://learn.corel.com/tutorials/clipping-objects-into-other-objects-with-powerclip/).
3. [CorelDRAW 2024 v25 SDK — PowerClip](https://community.coreldraw.com/sdk/api/draw/25/c/powerclip).

Các hành vi tham chiếu từ Help:

- Edit/Finish là cặp lệnh rõ; lúc sửa nội dung, khung hiện wireframe và không chọn để sửa.
- Nội dung gồm vector/bitmap; có clip lồng nhau.
- Khóa liên kết quyết định nội dung có đi cùng khi di chuyển khung, khác khóa cấm sửa object.
- Extract giữ vị trí nội dung và để lại khung rỗng.
- Center, Fit Proportionally, Fill Proportionally và Stretch có nghĩa riêng.
- Auto-center có lựa chọn theo tình huống, luôn hoặc không; không có quy tắc duy nhất “luôn căn giữa”.

Tutorial bổ sung chọn trực tiếp bằng Alt và thao tác vị trí/kích thước/xoay nội dung. SDK tách Shapes, ContentsLocked, EnterEditMode, LeaveEditMode và ExtractShapes. Đây là tham chiếu hành vi, không phải tài liệu thuật toán nội bộ của Corel.

PikDraw bố trí theo [chuẩn UI](../design/ui-ux-standard.md) và [workspace](../architecture/workspace-layout.md): property bar + Properties + menu/breadcrumb; không sao chép toolbar nổi mặc định đè artwork.

## 3. Audit source và nguyên nhân

P0 chặn workflow chính; P1 sai semantics/dữ liệu/đường truy cập; P2 độ đầy đủ và kiểm chứng.

| ID / mức | Điều đã thấy trong source | Tác động cần kiểm tại runtime |
| --- | --- | --- |
| PC-B01 / P0 | [SelectionModel::bounds](../../modules/vector/src/selection/region_selection.cpp), khoảng dòng 109, đi qua editable_target và bỏ Group. [Selection overlay](../../apps/desktop/src/shell/native_window_selection_paint.cpp) cùng hit vùng mini-toolbar trong [pointer dispatch](../../apps/desktop/src/shell/native_window_pointer_dispatch.cpp) phụ thuộc selection bounds. | Group/PowerClip đã select nhưng không có bounds để vẽ/chạm Edit. Đây là nguyên nhân code phù hợp trực tiếp với báo cáo mất nút. |
| PC-B02 / P0 | [hit_test.cpp](../../modules/vector/src/selection/hit_test.cpp), khoảng dòng 122–240, duyệt layer roots; narrow phase chỉ nhận Path/Text. [vector_controller.cpp](../../modules/vector/src/vector_controller.cpp) cũng lập index từ roots, ghi Group chưa có picking đầy đủ. | Group, Image và descendants không có đường pick thống nhất. Filter isolation phía shell không tìm được object vốn chưa được query trả về. |
| PC-B03 / P0 | [edit_target.hpp](../../modules/vector/include/pikdraw/vector/gestures/edit_target.hpp) chỉ nhận object nằm trực tiếp trong layer.object_ids và từ chối Group. [TransformGesture](../../modules/vector/include/pikdraw/vector/gestures/transform_gesture.hpp) dùng guard này ở begin/commit. | Child được select bằng code khi Enter vẫn không kéo được. Cần sửa ownership-aware editability, không bỏ locked/stale guards. |
| PC-B04 / P0 | [native_window_selection.cpp](../../apps/desktop/src/shell/native_window_selection.cpp), khoảng dòng 330–363, hit/filter/promote theo isolation rồi gọi controller.on_pointer_down; controller hit lại không mang scope. | Hai kết quả pick có thể khác nhau; gesture không dùng target shell vừa xác thực. |
| PC-B05 / P1 | [workflow group transform](../../apps/desktop/src/workflows/document_workflow_group_transform.cpp) move/resize/transform áp cùng delta lên Group và mọi descendant. Renderer lại nhân transform cha. get_object_bounds(child) chưa cộng ancestors. | Nguy cơ double transform, sai handles/fit/coordinates khi ancestors đã rotate/scale. Phải dùng world-matrix oracle, không chỉ group identity. |
| PC-B06 / P1 | lock_contents_to_frame được UI sửa và storage lưu; search đường transform vector/workflow chưa thấy consumer sử dụng flag. | Lock có trạng thái nhưng chưa quyết định frame/content transform; phải tách với object/layer locked. |
| PC-B07 / P1 | [menu actions](../../apps/desktop/src/shell/native_window_menu_actions.cpp), khoảng dòng 731, Place Inside dùng cùng Make. place_inside_powerclip(content_id, frame_id) chọn hai ID rồi Make lấy topmost làm mask. Make gọi Center bằng command sau. | Không bảo đảm frame ID; thiếu target-pick; CurrentPosition có thể vẫn bị center; một thao tác có thể cần nhiều Undo. |
| PC-B08 / P1 | release_clipping_mask_selected gọi Ungroup; nút mang label Extract cũng gọi hàm đó. | Extract/Release nhập làm một, không giữ empty frame. Ungroup chỉ nhận top-level nên nested extraction cần command riêng. |
| PC-B09 / P1 | Paint/chrome/pointer lặp layout các nút; paint dùng inspector width thực, một nhánh hit dùng 280 cố định. Finish nằm trong nhánh selection của paint. | Width/dock/selection rỗng có thể làm mất đường Finish hoặc lệch vùng bấm. Edit và Select hiện cùng gọi Enter. |
| PC-B10 / P1 | [isolation session](../../apps/desktop/src/shell/native_window_isolation_menus.cpp), khoảng dòng 397, chỉ push ID/đổi selection/status. [canvas](../../apps/desktop/src/shell/native_window_canvas.cpp) render snapshot thường, dim bằng wash rectangles. Renderer không nhận edit scope. | Chưa có reveal nội dung ngoài frame và wireframe mask theo view context; scope không nằm trong cache key. |
| PC-B11 / P1 | isolation_stack_ là vector ID ở NativeWindow; Enter chưa kiểm chain/duplicate. Empty frame có fallback select mask. Không thấy reset/rebind stack ở các lifecycle khác qua usages đã rà. | Có thể sửa nhầm mask, giữ ID cũ khi tab/page/document đổi hoặc vào group không thuộc scope; cần tái hiện lifecycle. |
| PC-B12 / P1 | [tạo shape trong isolation](../../apps/desktop/src/shell/native_window_selection.cpp), khoảng dòng 710, AddObjectCommand rồi apply_mutation trực tiếp append child vào group. | Reparent chưa chung transaction/Undo; tại nhánh này không loại root membership. Cần kiểm ownership/double-render/Undo. |
| PC-B13 / P1 | Make chưa kiểm frame geometry khớp backend; nhánh mask [D2D](../../core/rendering/src/gpu/d2d/d2d_render_backend.cpp) và [CPU](../../core/rendering/src/software_rasterizer.cpp) kỳ vọng PathObject. | Frame Text/Image/Group cần provider hoặc diagnostic; không tạo clip UI cho phép nhưng render thiếu. |
| PC-B14 / P2 | [ActionId](../../apps/desktop/include/pikdraw/desktop/actions/action_id.hpp) khai báo action.object.powerclip.*; search chưa thấy registration/consumer của các constant này trong .cpp. Native menu đi bằng MenuActionId/Isolation riêng. | Nhiều nguồn enable/execute; action có tên không chứng minh semantic entry point chạy được. |
| PC-B15 / P2 | [suite hiện có](../../apps/desktop/tests/test_desktop_shell_align_boolean.cpp), khoảng dòng 680–778, gọi workflow/Enter trực tiếp và kiểm render nhỏ; chưa kiểm pointer pick → child drag → Finish. | Test có thể pass dù workflow chính hỏng. Phải thêm regression xuyên consumer. |

[Existing feature upgrade](existing-feature-upgrade-20261020.md) có mục PowerClip/selection/isolation nhưng chưa giải quyết cụ thể các lỗi này. [C9](../../contracts/c9-appearance-effects.md) còn ghi hierarchical clipping ngoài basic appearance profile trong khi source đã có geometric clips. Cần bổ sung profile clipping riêng có gate, không tuyên bố group effects đầy đủ hoặc dùng wording historical làm bằng chứng runtime.

## 4. Hợp đồng đích

### 4.1 Hierarchy và ownership

- Một object có đúng một owner: layer root hoặc một Group. Mask là child có vai trò đặc biệt, không đồng thời là layer root.
- Chặn cycle/multi-parent/ID mất/mask ngoài children/mask tự trỏ container trước publish snapshot và nhận proposal.
- Shared read-only hierarchy resolver/index đặt trong core/document, trả parent chain, owning layer, world transform và inherited visible/locked. Vector/render/automation dùng cùng interface; core không phụ thuộc desktop.
- Index gắn document/epoch/page/revision và lifetime snapshot; không giữ raw pointer qua snapshot mới, không scan toàn map hay allocate descendants mỗi mouse move.
- Mask dùng path/fill rule thật; bbox chỉ broad phase. Text frame lấy glyph geometry từ typography. Image không tự được dùng alpha silhouette như geometric frame.
- Empty frame là container còn mask, không có content. Nếu giữ is_empty_frame trong storage thì writer phải duy trì/validate theo children, tránh hai nguồn sự thật.

### 4.2 Query và selection scope

Tên API dưới là đề xuất, ý nghĩa bắt buộc:

~~~text
SceneQueryContext
  document/epoch/page + snapshot revision
  scope: Document | GroupContents | PowerClipContents | FrameGeometry | DirectContents
  owner path + mask policy + inherited editability + clip visibility policy
HitResult
  object ID + owner path + local-to-world + quality + snapshot stamp
SelectionContext
  selected roots + scope + world-space bounds
~~~

- Document scope chọn outermost editable container theo painter order; clipped content phải qua clip stack, không nhận vùng bị ẩn ngoài frame.
- Edit Contents query trong active scope; mask/owner/object ngoài scope không là content target. Nested group được chọn như group ở cấp hiện tại; thao tác rõ để vào cấp con.
- Select Contents chọn content roots trong normal view và giữ DirectContents context để transform; không alias Edit. Alt-click chọn riêng content đang hiển thị bên trong clip.
- Một kết quả query dùng cho select + begin gesture. Controller nhận validated target/context hoặc tự sở hữu trọn query; shell không hit rồi gọi lần hit khác.
- Bounds chung cho handles/property fields/layout/snap/culling; phân biệt frame/content/source/painted/visible bounds.
- Point/region/hover/double-click/Layers/keyboard/snap cùng tôn trọng scope và locks. Indexed query phải so được với exhaustive oracle.

### 4.3 Transform và Lock Contents

P là tổng ancestors của object, C là local matrix, D là world delta:

~~~text
W = P * C
C_new = inverse(P) * D * P * C
~~~

Dùng cùng quy tắc cho drag, numeric X/Y/W/H, rotate/flip, align/distribute, node/frame edit. Không áp delta lên cả parent và descendants khi renderer đã truyền parent matrix. Selection chứa ancestor và child phải normalize thành roots.

Điểm pointer của Node/Frame phải đổi bằng inverse(P*C) từ world về source-local; snap/handles hiển thị qua P*C theo chiều ngược lại. Guard invertibility theo contract geometry hiện có, không dùng phép chia determinant thiếu kiểm tra.

- Content edit không di chuyển mask/container; khóa liên kết không cấm sửa riêng content.
- Lock on: đổi outer group/container matrix một lần, local content giữ nguyên.
- Lock off và move frame: content world giữ nguyên. Với group matrix G/G_new, bù content roots bằng C_new = inverse(G_new) * G * C; mask theo group mới. Không bù từng descendant lần nữa.
- Resize/rotate frame unlocked dùng cùng chính sách giữ world content như quyết định PikDraw, cần so riêng với Corel trước khi claim vendor parity. Edit Frame chỉ sửa frame geometry.
- Fit tính trong frame/container basis, không lấy world bbox rồi áp như local. Center chỉ translation; Fit min scale; Fill max scale; Stretch hai scale độc lập. CurrentPosition không mutate geometry.
- Nonuniform/reflection/singular cases có kiểm tra rõ. Không reset/recenter khi reselect/reopen/Finish.

### 4.4 Commands, Undo và lifecycle

- Command đích: CreateClip, AddToClip/Reparent, TransformScopedObjects, SetClipProperties, ExtractClipContents, ReleaseClip, ConvertToEmptyFrame, CopyClipContents. Ưu tiên mở rộng command hiện có khi đúng semantics.
- Mỗi user operation một transaction. Make + center, Place + reparent + optional fit phải Undo một lần; lỗi rollback ownership/page roots/frame/content.
- Make v1 yêu cầu cùng owner/page để convention topmost xác định được. Place/Add nhận content từ nhiều owner trong cùng active page, lưu owner/order cũ cho Undo và đổi local matrix để giữ world position. Cross-page target bị disable rõ cho tới khi có semantic page mapping riêng.
- Extract reparent content ra owner ngoài ở đúng painter order và world transform; direct child có C_parent = G * C. Giữ frame thành empty clip.
- Release bỏ quan hệ clip, trả frame/content ra owner ngoài. Hai action/label/test riêng.
- Add/paste/duplicate/create/delete trong scope cập nhật object và ownership cùng command. Không apply_mutation ngoài transaction để vá hierarchy.
- Copy Contents cấp IDs mới cho cây clone, giữ source asset refs/metadata và shared immutable backing; không sửa nguồn hoặc nhân bản toàn bitmap.
- Copy Contents From mặc định append vào target frame, giữ content đã có và frame geometry. Không thay/xóa nội dung cũ ngầm; nếu bổ sung Replace phải là mode tường minh có transaction/test riêng.
- Enter/Finish chỉ đổi view/selection, không tăng revision/dirty/Undo. Mỗi edit bên trong vẫn là Undo unit bình thường, không giữ transaction mở cả phiên.
- Session có document ID/epoch/page, ancestor chain, active mode, selection restore và generation. Revision mới revalidate chain, không thoát chỉ vì content vừa sửa hợp lệ.
- Tab/page switch, close/new/reopen, Undo xóa owner, automation mutation phải cancel/rebind đúng identity. Session là view state của tab/document; không mang ID sang document khác, không serialize như document data.
- Esc hủy numeric/tool/drag draft trước; Esc tiếp theo thoát một cấp. Finish giải quyết draft theo tool policy; input sai không tự commit.
- Pointer move chỉ cập nhật transient preview, không tăng document revision mỗi event. Mouse-up/Apply mới commit; cancel trả lại selection/preview/budget và không thêm Undo.

### 4.5 Edit view và render

- Truyền transient view context qua ViewportRenderer/backend; không đổi clip flags/source snapshot hoặc move object để giả isolation.
- Active pass hiển thị toàn content; frame wireframe, không pick như content. Context ngoài scope dim ở pass riêng, không wash bbox từng object che sai artwork.
- Nested edit giữ ancestor transforms. Editable pass bypass clipping của active owner và ancestor để sửa được content ngoài frame; descendant clips còn hoạt động tới khi vào cấp đó. Đây là view policy PikDraw.
- Không vẽ active subtree hai lần. Scope/path/generation/clip policy phải nằm trong render/cache/damage/culling keys, kể cả document revision không đổi.
- CPU/D2D hardware/WARP cùng semantic fixtures. Export/print dùng normal context; reveal view không lọt output.
- Giới hạn scratch/offscreen theo visible ROI và shared render budget; không cấp một full-canvas buffer cho từng child/clip level hoặc tạo GDI wash resources cho từng object mỗi frame. Admission failure phải báo incomplete đúng phạm vi và giữ source.
- Geometric clip không tự bật effects trên Group/Image hoặc đổi clip/effect/opacity order. C9 cần profile/order và oracle riêng; unsupported có diagnostic.

## 5. Feature UI spec

Một action/state provider điều khiển mọi bề mặt. “Đã khai báo” dưới đây chỉ nghĩa constant có trong source.

| Menu chính: Object > PowerClip | Semantic action | Hành vi |
| --- | --- | --- |
| Place Inside Frame… | action.object.powerclip.place_inside — đã khai báo | Chọn content, target-pick với hover frame hợp lệ; click đúng frame ID, Esc hủy. Nhận frame đã có clip để thêm. |
| Make from Selection | action.object.powerclip.make — đã khai báo | Topmost eligible selected object làm frame theo convention; kiểm ownership; không dựa selection vector order. |
| Edit Contents / Finish Editing | action.object.powerclip.edit / finish — đã khai báo | Vào/thoát session + render context; Finish làm việc cả khi selection rỗng. |
| Select Contents | action.object.powerclip.select_contents — mới | Select content roots với DirectContents context; normal clipped view. |
| Edit Frame | action.object.powerclip.edit_frame — mới | Target mask source; Node/transform trong frame context, không move content world. |
| Extract Contents | action.object.powerclip.extract — mới | Đưa content ra và giữ empty frame; Extract Selected chỉ bật khi partial context hợp lệ. |
| Release Clip | action.object.powerclip.release — đã khai báo | Bỏ clip; không dùng label Extract cho cùng handler. |
| Lock Contents to Frame | action.object.powerclip.lock — đã khai báo | Toggle liên kết transform; không sửa object locked flag. |
| Center / Fit / Fill / Stretch | action.object.powerclip.fit_center / fit_prop / fill_prop / stretch — đã khai báo | Chung command/enable state, disabled khi empty/singular/unsupported. |
| Create Empty Frame / Convert to Regular Object | action.object.powerclip.create_empty_frame / remove_frame — mới | Bỏ frame type chỉ khi empty; frame có content dùng Release riêng. Không xóa frame shape/content ngầm. |
| Copy Contents From… | action.object.powerclip.copy_from — mới | Pick frame nguồn; append cây clone, giữ content đích/asset refs; Esc hủy. |
| Properties… | Tái dùng action mở Properties | Focus PowerClip section, không tạo panel khác. |

**Property bar:** frame selection ưu tiên Edit Contents, Lock, Fit; phụ vào overflow. Session active có breadcrumb owner path + Finish độc lập selection. Child X/Y/W/H/angle theo world units trang. Edit không bị đẩy ra ngoài cửa sổ sau hàng transform.

**Properties > PowerClip:** frame name/type/count read-only; Edit/Finish/Select/Edit Frame; liên kết lock; preserve fill/stroke; Fit; empty-frame display; Extract/Release/Copy. Child selection có owner path và Select Frame. Multi/mixed không lấy frame đầu tiên làm đại diện. Dock đóng vẫn có đường focus; không tự bật lại.

**Context menu:** chung actions; đang sửa luôn có Finish kể cả click khoảng trống/nested clip. Không để nhánh if PowerClip che mất Finish ở else-if.

**Layers:** role Frame/Contents rõ; row selection dùng chung scope resolver; double-click mở đúng owner chain; mask không lẫn content selection.

**Canvas:** thay mini-toolbar lặp paint/hit và emoji sau khi fixed bar/Properties đạt parity. Nếu giữ affordance tùy chọn, dùng chung layout/hit/focus descriptor, không tự tính rect hai lần.

**Keyboard:** Enter/double-click vào frame; Esc theo draft precedence; Finish/breadcrumb thoát một cấp, Document thoát toàn chain. Giữ Ctrl-selection hiện có; Ctrl-click kiểu Corel chỉ qua preset khi không xung đột. Alt-direct selection phải qua input routing gate.

**Icons:** SVG đề xuất powerclip-edit.svg, powerclip-finish.svg, powerclip-frame.svg, powerclip-extract.svg, powerclip-lock.svg dưới apps/desktop/resources/icons; cùng viewBox/stroke/theme và provenance/license. Tái dùng icon cùng nghĩa; không emoji hoặc brand artwork.

**Label/help:** English mặc định, Vietnamese qua catalog: Edit Contents/Sửa nội dung; Finish Editing/Kết thúc chỉnh sửa; Edit Frame/Sửa khung. Tooltip/focus help giải thích lock là liên kết. Status có owner và Esc behavior; accessible names/focus không phụ thuộc hover.

**Settings:** auto-center đề xuất mặc định “khi hoàn toàn ngoài khung”; luôn tôn trọng CurrentPosition. Empty-frame display on-screen là overlay, không tự in dấu X. Các lựa chọn “in/export marker” chỉ bật khi có output spec/test.

**Không áp dụng:** không thêm standard toolbar row, toolbox tool mới, panel hoặc dialog riêng cho Edit/Finish. Kiểm 1366×768 và 1920×1080, scale 100/125/150/200%, light/dark và Properties mở/đóng.

## 6. Thứ tự triển khai và owned paths

Triển khai trực tiếp source gốc, tuần tự; không subagent/worktree/task branch/lease. Đọc diff từng file dirty trước khi sửa, giữ local edits ngoài scope. File code tối đa 2.000 dòng; tách theo domain, không append vào file đã vượt ngưỡng.

### PC0 — Regression và baseline

**Paths:** desktop_shell_align_boolean.cpp; vector selection/gesture tests và fixtures hiện có.

- Tái hiện bằng canvas input: tạo/chọn clip từ canvas, bấm Edit nhìn thấy, kéo Image và Path, Finish, Undo.
- Ghi IDs/owner path/world matrix/frame path/raster source ref/hash/revision; source và binary/backend identity.
- Có fixtures group identity, transformed parent, nested clip. Rà diff query index/transform đang dirty để giữ cải tiến numeric/performance.
- PC-B01…15 có kết quả tái hiện hoặc ghi rõ source inference. Chưa sửa model hàng loạt.

**Gate:** AC-PC-01/02/03 có ca fail trước fix, không chỉ gọi API Enter.

### PC1 — Hierarchy, query và bounds chung

**Paths:** core/document hierarchy resolver/index mới và validation/snapshot interfaces; modules/vector selection, vector_controller.cpp, gestures/edit_target.hpp; tests tương ứng.

- Parent/owner/world transform/visibility/locks và validation gắn snapshot lifetime.
- Point/region/Image/Group/nested clip query theo context/painter order; geometric clip sau broad phase, kiểm lỗ/fill rule.
- Selection bounds/handles đúng basis; một validated hit vào controller.
- Snap/Layers/tool targets dùng cùng resolver. Unsupported có kết quả typed, không silent no-op.

**Gate:** AC-PC-02/04/12/14/24/25; indexed/exhaustive parity và path/text regression.

### PC2 — Transform commands và lifecycle PowerClip

**Paths:** core/document commands (đề xuất clip_commands và hierarchy transform helper); modules/vector gesture/shape transform adapter; tách PowerClip khỏi document_workflow_group_transform.cpp sang document_workflow_powerclip.cpp; document/vector tests.

- Sửa local/world contract cho move/resize/rotate/flip/numeric/align; normalize selection roots, parent chỉ áp một lần.
- Lock liên kết có consumer thật; content edit vẫn được; singular/locked/stale reject trước mutation.
- Tách Make/Place/Add/Extract/Release/empty/copy; giữ z-order/coordinates/IDs; auto-center cùng Undo.
- Create/paste/delete/reparent trong scope đi transaction; không raw mutation ngoài writer.
- Bảo vệ authoring source rectangle/corners/contour; không bake matrix làm đổi semantics ngầm.

**Gate:** AC-PC-03/04/06/07/08/09/10/11/15/16/17.

### PC3 — Session và edit rendering

**Paths:** desktop tools/powerclip_edit_session.{hpp,cpp} mới; isolation/selection/shortcuts/canvas; ViewportRenderer view options; D2D/CPU clip/culling/cache/damage; renderer tests.

- State machine Outside → TargetPicking/Contents/Frame/DirectContents; nested chain và lifecycle identity hợp lệ.
- Reveal active contents + frame outline bằng view context, không mutate document.
- Scope-aware handles/drafts/cancel/stale adoption; cache invalidate khi mode đổi không có document revision.
- CPU/D2D/WARP parity; normal export không nhận view state.

**Gate:** AC-PC-05/12/13/16/17/23/24.

### PC4 — Actions và UI thống nhất

**Paths:** action IDs/dispatcher/registration; native menu/context/paint/chrome/pointer/selection paint/isolation/dock files; presenter PowerClip nếu cần; resources/locales/help; desktop shell/action tests.

- Một state provider và layout descriptor cho paint/hit/tooltip/focus/automation inspection.
- Fixed Edit/Finish + overflow + Properties; empty selection/nested/empty/window hẹp vẫn có đường ra.
- Menu/context/keyboard/Layers qua semantic actions; xóa alias và phép tính rect lặp khi parity đạt.
- SVG icons, scopes/mixed/disabled thật; command lỗi không báo success.

**Gate:** AC-PC-01/12/14/22/26 và ma trận layout mục 5.

### PC5 — Native storage, interop và clipping profile

**Paths:** model validation/native storage/commands tests; C5/C8/C9 phần liên quan; SVG importer/worker/writer, CDR adapter, host proposal validation; capability/README/feature docs.

- Source có PowerClip format từ native v10, writer hiện v19. Reuse schema nếu không đổi persistent fields; chỉ chọn version mới/migration sau rà nguồn thực, không hardcode version đã dùng.
- Save/recovery giữ mask/ownership/order/transforms/lock/preserve/fit/empty/source metadata; session transient.
- Imported clip giữ preserve-frame-appearance gốc, không tự thêm fill/stroke vào mask nguồn.
- C8 coverage theo representation; C9 geometric clip profile/order có oracle. Native editor pass không mở capability toàn CDR/PDF.
- Artistic Text frame provider kiểm Unicode/font/glyph; thiếu font/layout trả diagnostic và giữ source.

**Gate:** AC-PC-18/19/20/21/23/24.

### PC6 — Nghiệm thu và cập nhật sản phẩm

**Paths:** suites đã chạm, canonical app/worker staging, feature docs/README; project-state snapshot sau evidence.

- Chạy toàn AC trên binary sau sửa; giải quyết P0/P1 của workflow chính.
- Dùng native/CDR thật tại thư mục người dùng chỉ định. Không đặt thêm trần file/corpus để bỏ mẫu khó; safeguards parser/work/message có lý do vẫn giữ và báo variant cụ thể.
- Save → close → reopen → edit child → Undo/Redo → export. Output khi Outside/Contents/Frame phải như nhau.
- Dọn outputs/logs dùng một lần; giữ regression. Báo commands/backend/fixture IDs/case chưa chạy; không coi build pass là UX/fidelity/vendor parity.

## 7. Tương thích tài nguyên vector và đầu ra

Theo [hướng dẫn vector](../architecture/vector-resource-interoperability.md), mỗi format/representation có adapter riêng đưa vào model trung lập; sau normalize dùng chung command/edit/render.

| Nguồn / version / representation | Hiện trạng source | Gate và giới hạn claim |
| --- | --- | --- |
| Native v10…v19 geometric clip; legacy trước clip | Group/ClipProperties persist; writer v19. | Cấu trúc + pixels + edit + reopen; legacy defaults/migration đúng. Không chỉ đổi số version trong C5. |
| CDR RIFF/ZIP được pinned libcdr 0.1.9 + librevenge 0.0.6 chấp nhận, SVG representation | cdr-libcdr-svg-v2 giữ geometric path masks reader phát ra. Ghi version family theo fixture thực. | Import → canvas Edit → native reopen; giữ provenance. Không claim full Corel authoring/lens/mesh hoặc CDR export/round-trip. |
| SVG geometric clipPath subset | Importer tạo Path mask + Group; writer có clip branch. | Units/fill rule/nested affine/frame appearance, mở output bằng renderer độc lập. Mask/filter/external references ngoài subset phải báo. |
| PDF / AI PDF-compatible qua PDFium | Editable clipping còn được capability báo chưa bảo toàn đầy đủ. | Adapter/loss report riêng; native PowerClip fix không mở gate này. Không suy AI private authoring từ PDF view. |
| EPS native path / converted PDF | Clipping/appearance chưa giữ đầy đủ. | Không giả editable clip bằng bbox/thumbnail; report đúng representation. |
| DXF subset | Không có PowerClip semantic tương đương trong import hiện tại. | Group/transform regression nếu resolver đổi; không suy production từ clipped canvas. |
| PNG/JPEG/TIFF/ImageObject làm content | Raster placement/source metadata và shared backing hiện có. | Move/fit/copy giữ asset refs/ICC/original, không rewrite pixel. Import ảnh GiB là roadmap riêng. |

**Production:** vùng thấy trên màn hình không tự thành đường CNC/cut. Không trim geometry hoặc tạo đường cắt ngầm. Geometric export/raster output/source-format round-trip có gate riêng; edit-mode reveal view không là output authority.

## 8. Tiêu chí nghiệm thu

Mỗi hàng có Given/When/Then/Verify. Hiện là tiêu chí tương lai; khi triển khai ghi Pass/Fail/Chưa chạy với evidence.

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-PC-01 | Clip native chưa select, dock mở/đóng | Click frame trên canvas, Edit từ bar/menu/context | Vào đúng owner, Edit/Finish reachable không cần select ID qua test API | Pointer/action consumer + native control geometry/focus |
| AC-PC-02 | Path có lỗ, Path/Text/Image/Group content chồng nhau | Click/Alt-click/region/hover theo scope | Đúng painter order; không pick mask/outside/hidden region; ảnh/group chọn được | Exhaustive/indexed oracle + shell input |
| AC-PC-03 | Child trong Edit Contents | Kéo 20×10 mm, numeric nudge | World delta đúng, frame giữ, một Undo/Redo | Matrix/geometry assertions và actual pointer consumer |
| AC-PC-04 | Ancestors scale/rotate/reflect, clip nested | Drag/resize/rotate/align child rồi frame | P*C đúng, không double delta, handles/fields khớp pixels | Independent matrix oracle, identity/nonidentity fixtures |
| AC-PC-05 | Content có chi tiết ngoài frame | Enter, pick/kéo chi tiết ngoài, Finish | Edit thấy/chọn toàn active content, Finish clip lại; Enter/Finish không đổi revision | Pixel ROI + outside hit + source hash |
| AC-PC-06 | Content không object-locked | Move frame lock on/off rồi edit content | On đi cùng một lần, Off world content giữ; content vẫn edit | World matrices + toggle/move Undo; resize/rotate policy riêng |
| AC-PC-07 | Nhiều content, frame rotated/aspect khác | Center/Fit/Fill/Stretch/CurrentPosition | Đúng translation/min/max/two-scale theo basis; CurrentPosition no mutation | Independent numeric/aspect oracle + refs |
| AC-PC-08 | Content trên frame về z-order, có nhiều target | Place hover/click đúng frame; Esc lượt khác | Frame ID đúng, ownership một nơi; cancel không mutate | Target-pick shell + transaction |
| AC-PC-09 | Selection cùng owner, auto-center policies | Make → Undo một lần → Redo | Z-order/position đúng setting; CurrentPosition không center; không lịch sử nửa chừng | Command history + source/world snapshots |
| AC-PC-10 | Frame đã có content, nguồn có nested group/shared image | Add/Copy Contents From → Undo | Không thay nhầm frame; clone IDs mới, source giữ, asset refs shared | Tree/IDs/source/ref count |
| AC-PC-11 | Clip nested affine, nhiều content | Extract rồi Undo; Release lượt khác | Extract giữ empty frame; Release trả frame/content; world/z-order giữ | Commands + pixel + IDs, hai handlers riêng |
| AC-PC-12 | Clip trong group trong clip | Enter/double-click/breadcrumb/Finish/Esc | Chain hợp lệ, đúng cấp, không lặp ancestor/unrelated group | Desktop input/session/scope |
| AC-PC-13 | Session/draft active, hai tab/page | Switch/close/new/reopen; Undo xóa owner | Không stale target/ID leak; reset/rebind đúng identity | Lifecycle/async barriers + epoch assertions |
| AC-PC-14 | Ancestor/layer/object locked/hidden; empty frame | Edit/select/drag/fit/Edit Frame | Guards thống nhất; empty không auto-select mask như content | Action states + hierarchy permissions |
| AC-PC-15 | Scope active, owner transformed | Create text/shape, paste image, duplicate/delete → Undo | Owner đúng, không duplicate layer root/double-render, Undo toàn bộ | Roots/children/validation/pixels |
| AC-PC-16 | Path/compound/text frame đủ gate | Edit Frame Node/numeric → Finish | Chỉ frame đổi, content world giữ, source/IDs giữ | Node/tool consumer + geometry/font/pixels |
| AC-PC-17 | Drag/numeric/text/node draft, có external revision | Esc/Finish/Apply/stale commit | Hủy draft trước thoát, một gesture một Undo, stale không mutate | Gesture/writer safety + shell events |
| AC-PC-18 | Nested clip, lock/fit/preserve/empty, ICC/text | Save/recovery/close/reopen rồi edit | Hierarchy/world/metadata/IDs giữ; session transient; saved R/R+1 đúng | Storage fidelity + native workflow/hash |
| AC-PC-19 | CDR reader phát geometric masks, version đã ghi | Import → canvas Edit child → Finish → reopen | Native semantics đúng; layer/path/raster refs giữ; loss khác riêng | Adapter + desktop + source/independent ROI oracle |
| AC-PC-20 | Compound/nested clip affine/frame appearance | SVG export Outside và đang Edit, mở độc lập | Output giống nhau, clip/coordinates đúng subset, không reveal view | Writer/parser + independent pixels |
| AC-PC-21 | PDF/AI/EPS/CDR alpha mask/lens chưa map | Import/edit/export request | Diagnostic/loss đúng representation; không silent convert hoặc full capability | Capability/proposal/report assertions |
| AC-PC-22 | Width/DPI/theme mục 5; rỗng/mixed/dock đóng | Edit/Finish/Properties/overflow qua chuột/phím | Paint=hit=focus, không invisible hit; Finish luôn có, label rõ | Native semantic bounds + actual UI từng cấu hình |
| AC-PC-23 | Alpha edges/holes/nested/ICC/supported effects | Normal/Edit/Finish, zoom/pan trên CPU/hardware/WARP | Không seam/double paint/mất stroke/alpha/stale cache | Pixel oracle/backend diagnostics; chưa chạy ghi rõ |
| AC-PC-24 | Cycle/multi-parent/dangling mask/singular/stale hit | Command/import/open/async adoption | Reject trước publish, không recursion hang/partial doc | Host/model validators/malicious fixture |
| AC-PC-25 | 10k scene, clip nhiều nội dung/nested | Pick/drag/Enter/Finish/pan liên tục | Không scan/allocate toàn tree mỗi move; query parity; memory ổn định | Median/p95/allocations trên máy reference; target query p95 <16.6 ms cần đo |
| AC-PC-26 | Native và CDR geometric subset đủ gate | Place → Edit kéo/scale → frame edit → lock/fit → Finish → Undo/Redo → save/close/reopen → export | Workflow thực, world/source/IDs đúng, không direct API demo | Canonical app + structure/pixel/source evidence |

## 9. Kết quả triển khai và nghiệm thu hiện tại

Trạng thái bên dưới phản ánh source/build ngày 22/10/2026. “Một phần” nghĩa là implementation và regression liên quan đã có, nhưng chưa đủ toàn bộ fixture/oracle/runtime steps của acceptance case; không dùng trạng thái đó để tuyên bố parity với CorelDRAW hay Illustrator.

| AC | Kết quả | Bằng chứng và phần còn thiếu |
| --- | --- | --- |
| AC-PC-01 | Một phần | Native regression click canvas vào vùng mask trống và chọn đúng clip; Edit/Finish qua chuột chạy trên 2 kích thước × 4 DPI × 2 theme × 2 ngôn ngữ. Menu/context dùng cùng action state nhưng chưa chạy đủ mọi đường chọn/mở trong một workflow end-to-end. |
| AC-PC-02 | Một phần | Scope-aware point/region/hit, Group/Image và clip visibility có regression; index so với exhaustive đạt 200/200. Chưa có pixel/hit oracle riêng cho mọi lỗ compound path và painter-order chồng lấn. |
| AC-PC-03 | Một phần | Pointer thật kéo Path và Image bên trong clip, một Undo khôi phục; transform matrix nested có assertion. Numeric nudge và resize/rotate child chưa chạy trong cùng UI workflow. |
| AC-PC-04 | Một phần | Có parent rotation, nested clip và world-space transform regression. Chưa phủ đủ ancestor scale/reflection, handles và independent matrix oracle. |
| AC-PC-05 | Một phần | CPU/D2D/WARP có normal/edit/finish pixel checks; hit và drag được khi nội dung nằm ngoài frame. Chưa so source hash và chuỗi zoom/pan/nhiều nested view. |
| AC-PC-06 | Một phần | Command test kiểm tra unlocked giữ world position và locked đi cùng frame theo delta; UI cho phép sửa child khi lock bật. Policy khi resize/rotate frame và Undo của toàn chuỗi còn thiếu gate riêng. |
| AC-PC-07 | Đạt phần semantics command | Center/Fit/Fill/Stretch/Current Position có assertion kích thước/vị trí, kể cả Current Position không biến đổi geometry. Chưa đối chiếu bộ tham chiếu vendor và rotated-frame oracle độc lập. |
| AC-PC-08 | Một phần | Place target picker, hover/highlight, click và Esc được nối vào shell; command cập nhật owner atomically. Chưa có ma trận nhiều target/selection bất thường đầy đủ. |
| AC-PC-09 | Một phần | Make/Place là command transaction, Undo/Redo và auto-center khi content hoàn toàn ngoài frame có regression. Chưa đo hết painter order/policy khi trộn nhiều owner. |
| AC-PC-10 | Đạt phần clone/ownership | Copy Contents cấp ID mới, source giữ nguyên, world position giữ, Undo/Redo và shared image backing có test. Chưa có kiểm kê refcount/asset lifetime sau đóng tài liệu. |
| AC-PC-11 | Một phần | Extract giữ empty frame và world position; Release trả frame/content với world transform. Pixel/z-order oracle và Undo riêng cho Release chưa chạy trọn. |
| AC-PC-12 | Đạt phần session | Nested clip Enter/Finish theo từng cấp, breadcrumb và page navigation invalidation có native-shell regression. Close/reopen nhiều tab khi draft đang chạy chưa phủ đủ. |
| AC-PC-13 | Một phần | Owner bị xóa qua Undo và chuyển page làm session cũ invalid; tab switch/close/new/reopen và async stale result chưa chạy đủ tổ hợp. |
| AC-PC-14 | Một phần | Hierarchy guards kiểm tra inherited visible/locked và frame/content tách riêng; còn thiếu state-matrix cho mọi empty/mixed/layer-locked action. |
| AC-PC-15 | Một phần | Scoped add, duplicate, delete/Undo, SVG paste và Image drag có test; chưa chạy đủ Text creation/paste, mọi tool và transformed nested owner trong một lượt. |
| AC-PC-16 | Một phần | Frame Path và Text glyph geometry có provider riêng; Node tool nhận world/local transform. Chưa có runtime frame-node/numeric edit cùng chứng cứ giữ content/source hash. |
| AC-PC-17 | Một phần | Esc hủy draft trước khi thoát, gesture revision/page stamp và writer stale guards có regression. Chưa phủ mọi race giữa external edit, text/node draft và finish. |
| AC-PC-18 | Một phần | Native storage save/reopen giữ clip flags, mask ID, transform và Image pixels/source. Recovery được kiểm tra riêng; chưa chạy chuỗi recovery → edit → export với hash nguồn. |
| AC-PC-19 | Chưa xác minh | `test_pikdraw_desktop_import` đọc corpus thật gồm 4 CDR; cả 4 không có clipping group. Chưa có CDR geometric-PowerClip specimen/version để xác nhận import-edit-roundtrip. |
| AC-PC-20 | Một phần | SVG nested/affine clip, fill rule, frame appearance và roundtrip structure/bounds có test; chưa có renderer độc lập so pixel khi Outside và Edit. Raster nhúng vẫn trả Unsupported. |
| AC-PC-21 | Một phần | Adapter giữ profile/capability và một số representation không hỗ trợ trả lỗi rõ; chưa có fixture thật cho alpha mask/lens từ từng PDF/AI/EPS/CDR phiên bản. |
| AC-PC-22 | Một phần | 32 cấu hình size/DPI/theme/language kiểm layout và hit Edit/Finish thật; dock hidden/mixed/overflow/Properties focus ở mọi cấu hình chưa đủ. |
| AC-PC-23 | Một phần | Pixel tests chạy CPU, D2D hardware và forced WARP trên ca màu đơn giản; chưa có fixture alpha edge, hole, ICC/effect phức tạp, nhiều zoom/pan/device loss. |
| AC-PC-24 | Một phần | Cycle/multi-owner reject và hierarchy/storage validation có regression; chưa chạy đầy đủ malformed IPC/import, dangling mask và stale async adoption fixtures. |
| AC-PC-25 | Một phần | 10k object indexed/exhaustive parity 200/200; Debug median 0.0309 ms, p95 0.0522 ms trong lượt này. Chưa đo allocations, sustained drag/pan, memory stability hoặc Release trên máy tham chiếu. |
| AC-PC-26 | Chưa đóng | Canonical app có screenshot normal/edit; desktop test kéo child, native commands/storage và SVG export được kiểm riêng. Chưa có một workflow UI xuyên suốt close/reopen/export trên CDR geometric specimen. |

**Build/test đã chạy:** gói tích hợp 20 target chỉ định, CTest chọn 22 case và pass 22/22 trong 220.53 giây; `test_pikdraw_desktop_import` mất 190.85 giây. Sau khi chỉnh hit-routing để click canvas dùng cùng scope-aware picker, `test_pikdraw_desktop_shell` chạy lại và pass; `test_pikdraw_desktop_powerclip` alias pass 1/1. Command suite được mở rộng Center/Fit/Fill/Stretch/Current Position và locked movement, chạy lại pass 1/1. Selection target pass: 10k indexed pick median 0.0309 ms, p95 0.0522 ms (Debug), exhaustive parity 200/200. Các lần chạy riêng sau gói tích hợp kiểm lại clone backing, vector gestures, recovery, shell và PowerClip.

Executable chuẩn `build/win-dev/apps/desktop/pikdraw.exe` đã link thành công trước lần chỉnh hit-routing cuối. Lần relink source cuối bị Windows từ chối ghi vì PID 13032 đang chạy chính executable này trong cửa sổ người dùng; không cưỡng bức đóng tiến trình để tránh mất tài liệu chưa lưu. Runtime screenshot normal/edit trước thay đổi hit-routing xác nhận Edit Contents, Finish Editing, breadcrumb và Objects Properties; desktop-shell regression xác nhận input source mới, nhưng cần relink executable chuẩn sau khi cửa sổ đó đóng. Screenshot chỉ kiểm tra giao diện render, không thay cho toàn bộ AC-PC-26.

## 10. Lệnh kiểm tra khi triển khai

Mở rộng suites hiện có. desktop_shell_align_boolean.cpp hiện khoảng 1.120 dòng; tách PowerClip domain khi cần cohesion hoặc gần 2.000 dòng và đăng ký vào target hiện có. Không nhét phần lớn logic vào NativeWindow.

Các lệnh dự kiến sau sửa source, **chưa chạy trong lượt plan**:

~~~powershell
# Hierarchy/commands/storage
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_commands,test_pikdraw_document_storage,test_pikdraw_document_storage_fidelity -TestRegex '^test_pikdraw_document_(commands|storage|storage_fidelity)$'

# Query/gesture
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_vector_selection,test_pikdraw_editor_selection,test_pikdraw_vector_gestures -TestRegex '^test_pikdraw_(vector_selection|editor_selection|vector_gestures)$'

# CPU/D2D pixels/culling
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_rendering_pixels,test_pikdraw_rendering_rasterizer,test_pikdraw_rendering_culling -TestRegex '^test_pikdraw_rendering_(pixels|rasterizer|culling)$'

# UI/actions/desktop import
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_desktop_actions,test_pikdraw_desktop_import -TestRegex '^test_pikdraw_(desktop_shell|desktop_actions|desktop_import)$'

# Interop đúng subset
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_interop_svg_adapter,test_pikdraw_interop_svg_exporter,test_pikdraw_interop_cdr_worker_adapter -TestRegex '^test_pikdraw_interop_(svg_adapter|svg_exporter|cdr_worker_adapter)$'

# Canonical runtime
.\tooling\native\build.ps1 -Target pikdraw
~~~

Giữ shared build lock. Không dùng test binary cũ làm evidence sau edit. Common ownership/storage/command/contract đổi phải mở rộng writer/automation/import/recovery regressions theo consumer thực.

Corpus người dùng chỉ dùng local, ghi fixture ID/hash/version khi có quyền; không đưa bytes/nội dung khách hàng vào docs/Git. Báo cáo mới có run ID/vị trí riêng, không overwrite evidence cũ chỉ để chạy lại.

## 11. Điều kiện kết thúc

- PC-B01…15 có kết luận/regression; P0/P1 không còn chặn workflow chính.
- AC-PC-01…26 có kết quả thực; thiếu UI/backend/fixture ghi chưa chạy, không dùng để công bố capability.
- Chọn clip từ canvas, Edit rồi kéo được ảnh/chữ/path bên trong, sửa frame riêng, Finish cả khi selection rỗng.
- World transform áp đúng một lần; lock/fit/extract/release/empty/Undo rõ; save/reopen/export giữ source/clipping.
- Docs/capability nói đúng geometric subset đã kiểm, không claim vendor parity/toàn CDR/production từ build pass.

Thêm một nút Edit, bỏ guard gesture, gọi Enter qua test API hoặc vẽ mini-toolbar chưa đủ hoàn thành đợt này.
