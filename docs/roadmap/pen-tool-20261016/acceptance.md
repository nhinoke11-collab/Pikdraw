# Ma trận nghiệm thu Pen/Node

**Tất cả ca dưới đây chưa chạy cho Pen.** Đây là expected results/oracles cần triển khai
và ghi evidence, không phải báo cáo pass. Nguồn test chung đang thiếu phải giải quyết qua P00/P02.
Expected values dựa analytic/contract; không lấy ảnh render hoặc chính implementation làm oracle số học.

## 1. Các nhóm acceptance

| ID | Ca kiểm và expected result | Evidence / mốc |
| --- | --- | --- |
| AC01 | Từ Tools/menu/toolbox/shortcut đã đăng ký đều vào cùng Pen; 3 click tạo 2 Line; drag tạo Cubic; mixed path có source verbs thật, không chỉ polyline nhìn cong | Source commands + native pointer trace; A |
| AC02 | Corner/Cusp/Smooth/Symmetric, Alt và constrain đúng profile/phase, H/θ đúng. Smooth G1, Symmetric hai handle bằng nhau. Zero/absent handle không serialize invalid mode | Analytic vectors + native keydown/up/mouse; A/B |
| AC03 | Close tại node đầu không tạo duplicate/zero segment; mở/đóng/reverse/resume hai đầu giữ hình, ID mapping, fill rule/style. Closed curve hai anchor không bị quy tắc Polyline bác sai | Topology assertions, shape samples/derivative và save/reopen; A/B |
| AC04 | Draft confirm/undo/redo/backspace/cancel/Enter/Esc/doubleclick đúng phase; 1 finish = 1 revision/undo unit; cancel/resume-cancel = zero document change | Revision/history trace + actual WM double-click sequence; A/B |
| AC05 | L/θ, #XY, @ΔXY, @L<θ, units/expressions/locale parse đúng; góc view Y không đổi nghĩa @polar | Numeric vectors ở phần 2 + native typing, Tab/Enter; A |
| AC06 | World X/Y, delta signs/quadrants, mirror/rotation/nonuniform scale, view đổi không đổi world; inverse transform đúng, singular từ chối | Source mm assertions + transform oracles; A/B/C theo capability |
| AC07 | Lock L, lock θ, cả hai, repeat opt-in; snap/Ortho/Polar/modifier conflict không sửa số; L+ΔX hai nhánh, vô nghiệm báo rõ | Source endpoint + conflict UI traces; A |
| AC08 | Edit/delete/undo/redo/close-reopen/cancel/config/camera change khi job muộn: stale candidate/commit từ chối; không ghi nhầm tab hoặc half-commit | Deterministic schedule/fault traces; A/B/C |
| AC09 | Native focus vào property/IME/text không gọi tool; deg/rad không kích E/R; AltGr/chord reserved/key-repeat, capture loss/release ngoài cửa sổ; pan/zoom giữ draft/locks | Actual keyboard event routing và native lifecycle traces; A/B |
| AC10 | V3 roundtrip giữ Object/Subpath/Node/Segment IDs, source L/C/Q/arc tồn tại, Unicode/name, RGB/CMYK/spot/ICC/overprint, winding/style/transforms; fail save giữ bản cũ; checkpoint có provenance | Native codec + writer fault + save/reopen fixtures; A/B |
| AC11 | Node click/multi/window/crossing/lasso, move/nudge/numeric, split t=.25/.5/.75, delete explicit, convert/join/break/reverse; locked/hidden read-only không sửa | Independent split vectors, undo/ID map, native Node trace; B |
| AC12 | Empty/single/multi/Mixed/locked/invalid/stale states rõ; target Draft/Selection đúng; bar/Properties/HUD đồng bộ; không dồn multi XY về node đầu; narrow overflow vẫn nhập được | Native screenshots + keyboard-only controls; A/B |
| AC13 | EN mặc định/VI catalog đổi không sửa artwork; SVG/icon names/focus help/status/help đủ; 1366×768/1920×1080, light/dark, DPI có dữ liệu; keyboard không cần hover | Screenshots + accessibility/focus/name logs; A/B |
| AC14 | CPU/memory/latency đo cùng hardware/corpus/seeds, median/p95/max và raw trace; không dùng screenshot time/hidden paint làm FPS. Overlay không full redraw source mỗi mouse event | Runtime trace; A/B/C |
| AC15 | Long path/history/recovery quotas; giới hạn giữ draft hợp lệ, không O(n²) serialize mỗi pointer; checkpoint fault/cancel/base mismatch không ghi đè source | Allocation/bytes/work trace + fault injection; A/B/C |
| AC16 | Frame origin/rotation, previous tangent từ line/cubic, zero tangent có diagnostic, #world không đổi; đổi reference không xoay hướng lock đã có | Analytic frame/tangent vectors + native field trace; C |
| AC17 | Nearest/t-midpoint/half-length phân biệt; tangent/perpendicular/intersection/parallel từng capability có oracle, nhiều nghiệm/none/degenerate/transform/stale xử lý đúng, disabled kinds không chạy query | Geometry oracle + branch/cancel/quota trace; C |
| AC18 | File/path ownership và dependency đúng, source/exe khớp snapshot tích hợp, Debug/Release relevant suites, help/state/coverage đúng, app chuẩn mở được, unresolved limits báo rõ | Handoff có hashes + commands + AC matrix; A/B/C delivery |

AC06 ở mốc A kiểm world/mapping/viewport, B thêm node edit transformed, C thêm frame/previous tangent.
AC10 không yêu cầu tạo mới arc/quad bằng Pen; yêu cầu giữ segment cũ khi resume/edit phần được hỗ trợ.
AC11 không ép triển khai arbitrary refit/delete hoặc join khác style; subset bị giới hạn phải disable/nêu rõ,
không nhận silently destructive. Từng mức coverage phải nằm trong delivery matrix.

## 2. Vector số học tối thiểu

Các số làm tròn dưới đây là để đọc; fixture dùng giá trị analytic double và tolerance đã khóa P01.
World Y lên, góc CCW. Trừ khi nói khác, anchor A=(10,20) mm và active frame world.

| Vector | Input | Expected endpoint/kết quả |
| --- | --- | --- |
| N01 | L=100, World X, θ=0° | (110,20) |
| N02 | L=100, World X, θ=90° | (10,120) |
| N03 | L=100, World Y, θ=30° | (−40,106.60254037844386) |
| N04 | L=100, World Y, θ=−30° | (60,106.60254037844386) |
| N05 | L=250, World X, θ=30° | (226.50635094610965,145) |
| N06 | `@20;15` | (30,35) |
| N07 | `#120;80` | (120,80), không phụ thuộc anchor/frame |
| N08 | `@100<0`, UI reference World Y | (110,20); UI reference không đổi grammar |
| N09 | frame +X quay 90°, `@20;15` | (−5,40) |
| N10 | frame +X quay 90°, `@100<0` | (10,120) |
| N11 | frame quay 90°, `#120;80` | (120,80) world |
| N12 | L expression `(100mm+20mm)/2`, θ=0 | L=60, endpoint (70,20) |
| N13 | L=`1in`, θ=`1.5707963267948966rad` | (10,45.4) |
| N14 | locale decimal comma `@1,5cm;2,5mm` | (25,22.5); ở locale dot input comma từ chối |
| N15 | L=100, ΔX=60 | ΔY=±80, endpoints (70,100)/(70,−60); chọn nhánh rõ |
| N16 | L=100, ΔX=120 | Vô nghiệm; giữ draft/text, zero command |
| N17 | L=100, θx=0, locks XY=(10,120) | Conflict; không tự ưu tiên ghi đè driver |
| N18 | L=100 θx=30, snap target lệch khỏi numeric ray | Giữ numeric endpoint, không snap lệch; hint target bị loại |
| N19 | `1mm+1deg`, `1/0`, NaN/Inf, bytes>256, grouping | Diagnostic đúng offset; không thêm node/revision |
| N20 | L=−1 hoặc line L=0 | Không commit; delta âm vẫn hợp lệ nếu endpoint khác |
| N21 | Node A=(10,20), Out H=30 θx=90° | Out=(10,50); symmetric In=(10,−10); smooth giữ độ dài In riêng |
| N22 | H=0 khi node Smooth | Không serialize Smooth invalid; yêu cầu mode Cusp/remove theo thiết kế |
| N23 | T=translate(100,50)*scale(2,3), world node=(120,80) | Source node=(10,10); numeric move không reset T |
| N24 | Đổi reference X→Y khi θworld=30° locked | θdisplay=300°; endpoint/hướng giữ nguyên |
| N25 | Tọa độ 20.000 mm và gần biên ±1.000.000 mm | Numeric không round theo display; vượt biên source/world bị từ chối |

Split oracle độc lập tối thiểu: cubic P0=(0,0), P1=(0,100), P2=(100,100), P3=(100,0).
Tại t=.5, điểm chung=(50,75); nửa trái controls (0,50),(25,75), nửa phải controls
(75,75),(100,50). Kiểm parameter mapping và đạo hàm, không chỉ so bitmap.
Thêm cubic collinear overshoot, cusp, very short handles, mirrored/nonuniform transformed fixtures.

## 3. Hiệu năng và quota đề xuất

Corpus cố định: empty; 1.000 mixed paths khoảng 100.000 elements; một draft 1.000/10.000
segments; stress một path 100.000 elements cho editing/hit/snap, không tạo widget từng node.
P02 ghi machine/compiler/build/config/camera/DPI và cold/warm. Fixtures synthetic, không dữ liệu khách hàng.

Mục tiêu để đo và điều chỉnh có review: overlay computation p95 ≤4 ms trên corpus chuẩn;
input-to-visible p95 ≤100 ms ở software interaction; frame p95 ≤33,3 ms khi policy tương ứng
đã chọn. Đây là target chưa đo, không hứa 60 FPS hoặc bắt renderer hiện tại đạt bằng cách giảm fidelity.
Nếu baseline source redraw vẫn vượt budget thì giao Pen với limitation cụ thể, performance gate
fail/pending chứ không ghi pass. Đo source redraw tách overlay/present/input latency.

Theo dõi draft/history/checkpoint bytes so với quotas ToolSession; trace 1.000→10.000 segments
phải chỉ ra append không copy toàn draft trên mỗi move, checkpoint không tăng tổng work bậc hai.
Memory max thêm cho phiên phải có baseline+giới hạn explicit; không tùy ý tăng quotas để pass stress.
Quotas/job timeout hit báo pending/error và giữ source; disabled snap không làm query nặng.

## 4. Gói evidence bàn giao

Mỗi mốc có source baseline/result hash, exact paths, exe hash/build config, command logs/exit,
AC IDs với `pass/fail/not_run/unsupported`, fixture expected+actual, ảnh native, key/pointer/focus traces,
raw perf data và hardware; nêu reviewer nếu có. Không đánh dấu unsupported thành pass.
Gate A/B cần mọi yêu cầu bắt buộc của subset đã công bố pass; advanced giữ disabled nếu chưa đạt.
Không suy ra hỗ trợ AI/CDR export, CAM hay độ chính xác máy CNC từ kết quả Pen.

Hướng dẫn nghiệm thu tay tối thiểu: mở đúng `build/win-dev/apps/desktop/pikdraw.exe` →
Tools > Pen → mixed line/cubic → numeric 250 mm/30° → pan/focus/undo → close/resume →
save/reopen → Tools > Node (sau mốc B) → split/move/Hθ → undo/reopen.
Người dùng phải tìm thấy tool qua menu lẫn toolbox; không chấp nhận chỉ chạy fixture headless.
