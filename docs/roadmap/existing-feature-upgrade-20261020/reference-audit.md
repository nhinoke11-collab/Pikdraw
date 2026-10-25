# Đối chiếu source tham khảo cho nâng cấp PikDraw

Ngày khảo sát: 20/10/2026. Tài liệu bổ trợ cho [kế hoạch chính](../existing-feature-upgrade-20261020.md).

Đã lấy metadata/cây source GitHub của Graphite, Penpot, LibreCAD và Krita; Inkscape lấy HEAD trực tiếp từ GitLab chính thức. Tải 28 file code và 5 file license vào thư mục tạm riêng, đọc các entry point và đoạn liên quan tới thuật toán/UI/lifecycle; không build/chạy code upstream. Tải source không đồng nghĩa audit từng dòng, benchmark hoặc chứng nhận độ đúng của upstream.

Không dùng stars để quyết định thuật toán. Mỗi ý tưởng phải trả lời: PikDraw đang có gì, thiếu bằng chứng nào, sửa ở đâu và oracle nào chứng minh cải thiện.

## 1. Phiên bản cố định và phạm vi đọc

| Repo / ref tại lúc khảo sát | Nguồn chính | Phần đọc | Vai trò đối với PikDraw |
| --- | --- | --- | --- |
| Graphite `49711cd37591bbdccce9ecaac6eae98de65893bf` | [commit](https://github.com/GraphiteEditor/Graphite/tree/49711cd37591bbdccce9ecaac6eae98de65893bf) | Pen/Select FSM; graph executor/runtime; layout handler; NumberInput; MIT license | Session, typed fields, execution identity và derived evaluation |
| Penpot `a5dac030735d21da63a9c0dd54de7e5702c98248` | [commit](https://github.com/penpot/penpot/tree/a5dac030735d21da63a9c0dd54de7e5702c98248) | Undo; transforms; numeric input; layers; geometry transforms; changes builder; license | Preview/commit lifecycle, mixed values và đồng bộ dữ liệu UI |
| Inkscape `14580157691ffedfe745ebf919252a8cf3883711` | [GitLab commit](https://gitlab.com/inkscape/inkscape/-/tree/14580157691ffedfe745ebf919252a8cf3883711) | Pen/Node tools; selected style; drawing/drawing item; COPYING | Semantics vector, source-vs-ink bounds, invalidation và style update |
| LibreCAD `2af4873798fe5e2b4119720c351fc66835cad5bc` | [commit](https://github.com/LibreCAD/LibreCAD/tree/2af4873798fe5e2b4119720c351fc66835cad5bc) | Snapper; modification; math; dimension base action; layer widget; license | Precision/snapping/dimension và CAD interaction |
| Krita `da7c64398edcb3c2fb9cf1815f8ee06987df5301` | [GitHub mirror commit](https://github.com/KDE/krita/tree/da7c64398edcb3c2fb9cf1815f8ee06987df5301) | Update scheduler; strokes queue; async merger; canvas; node model; KoToolManager; COPYING | Job scheduling, dirty updates, layer projections và tool context |

Krita upstream chính ở [KDE Invent](https://invent.kde.org/graphics/krita); ref trên là source mirror đã tải. Inkscape GitHub chỉ trỏ tới GitLab, không dùng landing repo đó làm mã nguồn engine.

## 2. Graphite: học cách biểu diễn thao tác và kết quả

Nguồn code đã đối chiếu:

- [Pen tool](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/editor/src/messages/tool/tool_messages/pen_tool.rs), [Select tool](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/editor/src/messages/tool/tool_messages/select_tool.rs).
- [Graph executor](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/editor/src/node_graph_executor.rs), [runtime](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/editor/src/node_graph_executor/runtime.rs).
- [Layout handler](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/editor/src/messages/layout/layout_message_handler.rs), [NumberInput](https://github.com/GraphiteEditor/Graphite/blob/49711cd37591bbdccce9ecaac6eae98de65893bf/frontend/src/components/widgets/inputs/NumberInput.svelte).

Điểm có căn cứ: Pen dùng FSM và thông điệp Abort/transition; executor mang execution_id/request/response, document context, pending execution và graph hash. NumberInput có undefined/mixed hiển thị dạng gạch, unit/range/step, typed text, drag state và start/commit history transaction.

**Áp dụng:** U02 tách field domain + session; U05 dùng execution identity, coalesce derived preview và cache theo dependency. Widget chỉ gửi typed intent; source authority vẫn thuộc document commands của PikDraw.

**Không suy diễn:** comment NodeRuntime tại ref này nói phần runtime còn dùng một JS stack riêng trên main JS thread và tách thread là hướng hoàn thiện. Message passing/execution IDs không chứng minh chạy đa luồng hoặc đạt độ trễ mong muốn. [Trang tính năng chính thức](https://graphite.art/features/) vẫn mô tả alpha và roadmap; không đưa feature roadmap vào inventory đã chạy.

Không chuyển mô hình mọi đối tượng PikDraw sang graph hoặc lấy renderer/file format Graphite làm authority. Appearance stack hiện có chỉ cần evaluation/cache/dependency tốt hơn. Gradient vẫn là paint server theo C9, không đổi thành effect chỉ vì Graphite dùng node.

## 3. Penpot: preview, mixed state và thao tác theo transaction

Nguồn code đã đối chiếu:

- [Undo](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/frontend/src/app/main/data/workspace/undo.cljs).
- [Transforms](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/frontend/src/app/main/data/workspace/transforms.cljs).
- [Numeric input](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/frontend/src/app/main/ui/components/numeric_input.cljs).
- [Layers](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/frontend/src/app/main/ui/workspace/sidebar/layers.cljs).
- [Geometry transforms](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/common/src/app/common/geom/shapes/transforms.cljc), [changes builder](https://github.com/penpot/penpot/blob/a5dac030735d21da63a9c0dd54de7e5702c98248/common/src/app/common/files/changes_builder.cljc).

Undo phân biệt entry/transaction/group; transforms có sample preview không ghi Undo và commit endpoint cuối thành một bước. Numeric input phân biệt :multiple, giá trị hiển thị, parse/range/integer/step và focus/blur.

**Áp dụng:** một gesture một transaction, cancel/no-op không tạo Undo; field mixed không là zero; ngăn số đang nhập đột nhiên cập nhật sang target khác. Layers và Properties là projections cùng context, có stable semantic identity.

Không chép timeout 20 giây, hạn mức Undo 50 hoặc hành vi auto-commit-on-unmount thành policy PikDraw. Chính sách draft của PikDraw phải an toàn khi selection/tab/page đổi; không dùng model cộng tác/web stack của Penpot để thay single writer hiện có.

## 4. Inkscape: invalidation và semantics vector

Nguồn code đã đối chiếu:

- [Pen](https://gitlab.com/inkscape/inkscape/-/blob/14580157691ffedfe745ebf919252a8cf3883711/src/ui/tools/pen-tool.cpp), [Node](https://gitlab.com/inkscape/inkscape/-/blob/14580157691ffedfe745ebf919252a8cf3883711/src/ui/tools/node-tool.cpp).
- [Selected style](https://gitlab.com/inkscape/inkscape/-/blob/14580157691ffedfe745ebf919252a8cf3883711/src/ui/widget/selected-style.cpp).
- [Drawing](https://gitlab.com/inkscape/inkscape/-/blob/14580157691ffedfe745ebf919252a8cf3883711/src/display/drawing.cpp), [Drawing item](https://gitlab.com/inkscape/inkscape/-/blob/14580157691ffedfe745ebf919252a8cf3883711/src/display/drawing-item.cpp).

DrawingItem thể hiện state flags, propagation, geometry bbox/drawbox, effect/filter/clip bounds và cache policy theo chi phí. SelectedStyle kiểm style-modified flags, chặn callback vòng lặp và gộp Undo cho opacity.

**Áp dụng:** giữ source bounds riêng ink/effect bounds; dirty propagation có dependency; style update không kéo theo redraw mọi panel; sửa opacity liên tục dùng một session/merge key có target. Pen/Node so cả state transitions và cách giữ bản nháp, không chỉ so icon.

Cache threshold/heuristic Inkscape chỉ là tham khảo; số cụ thể không mang sang D2D. Bộ renderer GTK/Cairo và document SVG không thay C++/Win32/document trung lập của PikDraw. Pixel/oracle hoặc imported SVG của một bộ app không tự chứng minh xuất CDR/AI/DXF đúng.

Boolean đường cong của Inkscape cần đọc sâu thêm engine phụ trợ trước quyết định dùng mã: [lib2geom README](https://gitlab.com/inkscape/lib2geom/-/blob/master/README.md) tự ghi giới hạn degenerate cases. Không hứa exact Boolean chỉ từ tên thư viện.

## 5. LibreCAD: snapping và scope chính xác

Nguồn code đã đối chiếu:

- [Snapper](https://github.com/LibreCAD/LibreCAD/blob/2af4873798fe5e2b4119720c351fc66835cad5bc/librecad/src/lib/actions/rs_snapper.cpp).
- [Modification](https://github.com/LibreCAD/LibreCAD/blob/2af4873798fe5e2b4119720c351fc66835cad5bc/librecad/src/lib/modification/rs_modification.cpp), [math](https://github.com/LibreCAD/LibreCAD/blob/2af4873798fe5e2b4119720c351fc66835cad5bc/librecad/src/lib/math/rs_math.cpp).
- [Dimension base action](https://github.com/LibreCAD/LibreCAD/blob/2af4873798fe5e2b4119720c351fc66835cad5bc/librecad/src/actions/drawing/draw/dimensions/rs_actiondimension.cpp).
- [Layer widget](https://github.com/LibreCAD/LibreCAD/blob/2af4873798fe5e2b4119720c351fc66835cad5bc/librecad/src/ui/dock_widgets/layer_widget/qg_layerwidget.cpp).

Snapper phân biệt endpoint/intersection/center/grid/on-entity, range và trạng thái bắt điểm. Dimension có dimension data/style và options/preview context.

**Áp dụng:** candidate priorities, DIP snap radius, exact source coordinate và visual hint dùng cùng kết quả; dimension dựa đơn vị thật, display scale riêng. Cải tiến offset/trim phải giữ topology và kết quả edit/Undo, không chỉ line preview.

Không chép RS entity model, Qt widgets/global settings hoặc mặc định snap distance của LibreCAD. PikDraw đã có SnapSourceIndex và candidate ranking; cần đo và cải thiện điểm yếu trước khi thay thuật toán.

## 6. Krita: scheduler, cập nhật cục bộ và layer model

Nguồn code đã đối chiếu:

- [Update scheduler](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/image/kis_update_scheduler.cpp), [strokes queue](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/image/kis_strokes_queue.cpp).
- [Async merger](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/image/kis_async_merger.cpp), [canvas](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/ui/canvas/kis_canvas2.cpp).
- [Node model](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/ui/kis_node_model.cpp), [tool manager](https://github.com/KDE/krita/blob/da7c64398edcb3c2fb9cf1815f8ee06987df5301/libs/flake/KoToolManager.cpp).

Scheduler có update queue/strokes queue, balancing, thread limits, barrier/lock và cancel. Node model dùng update queue/compressor, persistent model index và thumbnail cache.

**Áp dụng:** ngân sách chung cho tác vụ nền, batch cancellation, partial dirty projection và layer-row update theo identity. Canvas interaction phải có ưu tiên nhưng save/export không starvation.

Không sao chép thread/barrier toàn engine: PikDraw vẫn single-writer; worker nhận snapshot immutable và proposal. Update compressor 100 ms ở node model Krita không phải mục tiêu độ trễ cho canvas của PikDraw. Raster authoring mới không thuộc scope dù Krita có nhiều tính năng.

## 7. Thuật toán bổ trợ cần spike trước khi chọn

[Clipper2](https://github.com/AngusJohnson/Clipper2) có C++ polygon Boolean/offset, BSL-1.0. [API C++](https://github.com/AngusJohnson/Clipper2/blob/main/CPP/Clipper2Lib/include/clipper2/clipper.h) phân biệt Paths64/PathsD/fill rule/precision. Đây cũng là ứng viên đã nêu trong ADR 0001 của PikDraw, chưa là dependency được tích hợp trong đợt này.

U03 cần so backend hiện tại, ứng viên Clipper2 và curve backend nếu cần:

| Tiêu chí | Gate |
| --- | --- |
| Rectangle cũ, polygon lõm, holes, touch/coincident và winding | Geometry oracle độc lập, không dùng chính output thư viện làm expected |
| Curve flatten/quantization | Sai số tổng theo mm, checked range/local origin, không dùng precision mặc định không đọc nghĩa |
| Source/editability | Giữ analytical source khi chưa conversion; converted result có semantics và tolerance rõ |
| Cancellation/budget | Giới hạn input/work/scratch có lỗi rõ; không truncate path rồi báo success |
| Compiler/determinism | Lặp cùng input trên toolchain/build được hỗ trợ; không mặc định int64 đồng nghĩa mọi compiler cho kết quả tuyệt đối giống nhau |
| Dependency | Commit/hash/license/transitive notices và CMake target riêng; không để type upstream lọt vào public document schema |

## 8. Quy tắc mã, icon và license

Đợt này chỉ đọc source và viết kế hoạch, không đưa mã hoặc asset upstream vào PikDraw.

| Repo | Kết quả đọc license | Cách sử dụng trong kế hoạch |
| --- | --- | --- |
| Graphite | MIT hoặc Apache-2.0 theo từng file/repo notice | Tham khảo thuật toán; nếu tích hợp phải pin và giữ notices/dependency audit |
| Penpot | MPL-2.0 | Học cấu trúc UX/state; reuse source cần tuân thủ license của file |
| Inkscape | COPYING: phần lớn source GPL-2.0-or-later, từng file có ngoại lệ; binary tổng thể GPL-3.0-or-later | Học semantics/algorithms; không copy source hoặc icon khi chưa giải quyết license cụ thể |
| LibreCAD | LICENSE ghi GPLv2 và thành phần bên thứ ba riêng | Tham khảo CAD behavior; source reuse có gate riêng |
| Krita | COPYING GPLv3; từng thư viện/file có thể có notice khác | Tham khảo scheduler/model; không giả mọi file/asset cùng một license |
| Clipper2 bổ trợ | Repo ghi BSL-1.0 | Chỉ là ứng viên U03; pin audit trước khi thêm |

Không lấy logo/icon thương hiệu hoặc giao diện nguyên mẫu của upstream làm bộ nhận diện PikDraw. Dùng icon SVG hiện có và icon mới cùng hệ viewBox/stroke/provenance. Không đóng gói font/fixture từ upstream khi chưa rõ quyền.

## 9. Kết quả khảo sát và giới hạn

Quyết định cụ thể đã đưa vào U01–U10: typed bindings/edit sessions; action authority; robust geometry có sai số; index neutral; bounded scheduler; dependency/dirty caches; native layout/focus model; layer projection; format fidelity gates.

Đợt này không chứng minh repo nào nhanh hơn PikDraw trên cùng máy/file, không đánh giá usability qua việc tự dùng upstream và không xác nhận tính tương đương vendor. Các benchmark/UI/file compatibility phải chạy ở từng lát implementation. Ref/path ở trên đủ để lấy lại đúng bản.

Việc dọn source tham khảo trong thư mục Temp riêng đã được thử bằng xóa thư mục có kiểm tra path và xóa từng file đã tải; cả hai bị kiểm duyệt tự động từ chối với `blocked by policy`. File tải tạm còn ngoài repo; không đưa source upstream vào Git/PikDraw.
