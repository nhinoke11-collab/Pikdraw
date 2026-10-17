# Phân rã triển khai và giao việc

**Tất cả P00–P16 đang planned.** Catalog này không tạo lease, không khởi động agent hoặc
cho phép sửa các path nêu dưới đây ngay trong task tài liệu. Task board mới là quyền làm việc.
Đọc [UI spec](ui-spec.md), [behavior/precision](behavior-and-precision.md) và
[acceptance](acceptance.md) cùng [tasks.json](tasks.json) trước mỗi gói.

## 1. Trình tự và các mốc

| Gói | Phụ thuộc | Công việc cụ thể và đầu ra | Điều kiện nhận | Công sức dự kiến |
| --- | --- | --- | --- | --- |
| P00 Baseline/prerequisites | — | Chụp HEAD+dirty hash, kiểm board/leases, compiler/app hash; dùng inventory test của canvas P00a; lập capability matrix có thật | P00b khôi phục test sources; P00d ghi nhận Debug CTest 63/63 trên snapshot tích hợp, P00c còn review và Release chưa chạy | Hoàn tất theo task board |
| P01 Semantic/input review | P00 | Khóa vector X/Y/world/frame, handle modes/zero, drag threshold, Esc/history, transform, F3/profile migration; review geometry/document/desktop/storage/rendering/interop; contract/ADR chỉ sửa khi cần với scope riêng | Consumers thống nhất expected results, không chỉ JSON schema; ghi version/migration | 1–2 ngày |
| P02 Consumer oracles | P01 | Dùng test sources đã khôi phục; thêm analytic vectors, lifecycle traces và quotas cho Pen; đăng ký CMake qua task build phù hợp; baseline trước implementation | Oracle độc lập được review, baseline lỗi/thiếu giữ rõ | 1–2 ngày |
| P03 Pen draft core | P02 | Thêm Pen controller, append Line/Cubic, start/confirm/finish/cancel, local history; editable source builder, resume token/base revision | AC01/04/08; preview không sửa doc; finish một transaction | 2–3 ngày |
| P04 Handles/resume | P03 | Corner/Cusp/Smooth/Symmetric, outgoing/incoming, convert line/curve, Alt/constrain, Adjust Handles; close/resume hai đầu, reverse mapping | AC02/03/04; không zero smooth; không đổi style/source ngoài target | 2–3 ngày |
| P05 Numeric Pen | P04 | Reuse D06; L/θ, XY/ΔXY, X/Y angle reference adapter, locks/repeat, H/θ, pointer/IME focus; conflict solver nhỏ | AC05/06/07/09; @polar giữ nghĩa Frame X; numeric không bị snap ghi đè | 2–3 ngày |
| P06 Draft lifecycle/recovery | P05 | Focus/capture/pan/tab/undo/stale, bounded history/chunk checkpoints; save/recovery provenance và quota; không full serialize mỗi point | AC08/09/10/15; late proposal luôn bị chặn; recovery có fault traces | 1,5–3 ngày |
| P07 Native Pen surfaces | P06 | Register action/menu/toolbox, SVG assets, property bar/Properties/HUD cùng model, enable state, keyboard/profile migration, labels/help EN/VI | AC01–09/12/13; Pen thực sự có trên executable chuẩn và drawing hoạt động | 2–3 ngày |
| P08 Gate A | P07 | Integrated snapshot build, analytic+native regression, thao tác click/drag/numeric/focus, save/reopen, ảnh UI và benchmark khai báo hardware/corpus | AC01–10/12–15 trong scope A pass; giao Pen+numeric usable; Node còn pending | 1–2 ngày |
| P09 Node interaction | P08 | Select/multi/window/crossing/lasso, move/nudge/XY/delta, handle numeric, selection mapping, transformed objects | AC02/06/11/12; multi XY không collapse; hidden/locked không sửa | 2–3 ngày |
| P10 Topology commands | P09 | Bổ sung command thiếu sau audit: exact split, explicit delete, line↔cubic, reverse/open/break/close/join bridge/merge mapping | AC03/04/11; no ID reuse, nguồn đúng, atomic undo; restrictions có lý do | 2–3 ngày |
| P11 Native Node surfaces | P10 | Nối Node action/menu/toolbox/Properties/context/help, markers, keyboard, Mixed và unsupported states; auto add/delete opt-in dùng cùng commands | AC11/12/13, doubleclick không duplicate, không auto delete vì hover | 1–2 ngày |
| P12 Gate B | P11 | Integrated Pen+Node regression/roundtrip/metadata, source/renderer/hit/export consumers và ảnh/native traces; cập nhật coverage cụ thể | AC01–15 trong scope B; Pen/Node cơ bản mới được gọi complete | 1–2 ngày |
| P13 Construction frames | P12 | World X/Y và previous tangent đầy đủ; frame quay/gốc tùy ý, transform-inverse, validation/persistence của frame preference/source theo scope đã review | AC06/16; không đổi @/# grammar âm thầm; singular/zero tangent diagnostic | 1,5–2,5 ngày |
| P14 Advanced snap | P13 | Substeps có gate riêng: nearest/t-midpoint → intersections → tangent/perpendicular → parallel/extension/tracking; half-length chỉ khi có bounded inverse length oracle | AC17; capability riêng, nhiều nghiệm chọn nhánh, quota/stale/index; chưa đạt giữ disabled | 3–6 ngày |
| P15 Gate C | P14 | Kiểm analytic relations/framed input và adverse cases, stress local queries, branch UI và persistence source không bị flatten | AC16/17/14/15; chỉ công bố snap subset đã pass | 1–2 ngày |
| P16 Delivery/state | P12, P15 | Review exact integrated snapshot, build output chuẩn, evidence hashes, acceptance matrix, project-state/roadmap/help và hướng dẫn mở app; ghi hạn chế | AC18; không ghi Pen complete nếu gate A/B thiếu; advanced partial báo từng loại | 0,5–1 ngày |

Ước lượng khoảng **25–43,5 ngày công** cho cả A/B/C, gồm kiểm tra nhưng chưa tính chờ
toolchain/quyền phục hồi test hoặc sửa nền tảng ngoài scope. Không là cam kết lịch.
P14 có thể tách task nhỏ theo capability; không dùng việc chờ mốc C để trì hoãn giao mốc A/B.
Sau mỗi gate A/B, tạo handoff và cập nhật project state bằng task có claim trước khi nhận mốc kế.
P16 không thay các lần bàn giao A/B. Bằng chứng P00/P00d áp dụng cho source snapshot của các task đó; kiểm lại revision trước khi dùng cho delivery khác.

## 2. Ownership và thay đổi dùng chung

`tasks.json` ghi `proposed_owned_paths`; **chưa là lease**. Trước claim, đọc module AGENTS
và chuyển thành danh sách file/thư mục hẹp thực tế. Không claim toàn `apps/desktop` nếu chỉ sửa
action registration. Path hiện chưa tồn tại là đề xuất, kiểm convention trước khi tạo.

- Vector: `modules/vector/.../tools/pen`, `.../tools/node`; ToolSession/input/numeric/snap dùng chung
  chỉ sửa theo task con đã claim, không fork parser hay snap engine riêng cho Pen.
- Document: reuse `editable_path.hpp`, `path_commands.hpp` và implementation; command thiếu được task
  document riêng, review consumers. Native v3 không bị bump chỉ vì thêm một tool; chỉ bump nếu source mới cần.
- Desktop: `native_window.cpp`, events/shortcuts/menu/paint/header là hot paths của nhiều phiên;
  claim và sửa **tuần tự**, tách feature presenter/controller; tránh nhét toàn Pen vào file shell lớn.
- Render/storage/interop: chỉ sửa adapter nếu có regression consumer chứng minh cần; scoped task,
  không nhân scope topology thành redesign renderer/file formats.
- Contracts/CMake/action registry/localization catalogs/project-state/shared gate fixtures: serialized,
  coordinator nhận lại từng task; review semantic vectors với consumers trước tích hợp.
- Test restoration phối hợp task inventory đang có; không đụng path đang leased và không coi việc
  có inventory là quyền khôi phục toàn bộ 72 file đã xóa. Nếu authorization chưa rõ, chỉ chặn phần phụ thuộc.

Không chạy delegate nếu chưa có yêu cầu rõ từ người dùng/chỉ dẫn áp dụng. Nếu sau này được phép,
same source + disjoint paths + semantic vectors; build vẫn tuần tự theo lock chung.

## 3. Prompt mẫu cho từng task triển khai

```text
Thực hiện đúng gói Pxx trong docs/roadmap/pen-tool-20261016/tasks.json.
Đọc AGENTS.md, START, project-state, task board và pikdraw-session; claim paths trước sửa.
Xác minh prerequisites/gate và snapshot hiện tại, đọc module AGENTS + bounded refs liên quan.
Dùng ui-spec và behavior-and-precision làm quyết định đích; đối chiếu contract đang hiệu lực.
Ghi expected semantic vectors và AC tương ứng trước code, giữ inherited dirty work.
Triển khai scope task, UI/help/localization đi cùng feature. Không thêm panel tự phát.
Build qua tooling/native/build.ps1; chỉ chạy tests khi source cần thiết đã được phục hồi hợp lệ.
Giữ gate/oracle độc lập; ghi native traces, exact source/exe hash, giới hạn và next action.
Không tự commit, tạo branch/worktree, dispatch agent hoặc đánh dấu task kế tiếp đã làm.
```

## 4. Quy trình build, gate và dừng an toàn

1. Khóa baseline/hashes + dirty paths; source chung có thể đổi sau HEAD nên không báo chỉ SHA.
2. Review UI/contract delta ở scope được claim; không tự sửa control đang dùng bởi task khác.
3. Sau khôi phục source test cần thiết mới chạy suites được liệt kê từ cấu hình thực tế;
   baseline harness `doctor` và `python -m unittest discover -s tests/harness -v` khi source có.
4. App và tests dùng `tooling/native/build.ps1`; không gọi CMake/Ninja trực tiếp ngoài lock.
   App duy nhất `build/win-dev/apps/desktop/pikdraw.exe`; không copy exe đổi tên để né lock.
   Đọc script help/cấu hình hiện tại trước chọn Debug/Release/Test flags.
5. Build mặc định incremental, không routine Clean. Nếu log báo recovering dependency log,
   lưu log và chẩn đoán trước lần build tiếp. App đang giữ exe là lỗi cần xử lý, không đổi output.
6. Run native/manual evidence trên đúng exe/hash vừa build. Cần Debug và Release regressions vì
   baseline từng có Release-only lỗi document/export; không lấy Debug xanh để lấp ô Release.
7. Handoff source baseline/result snapshot, actual paths, commands/results, screenshot/trace/hashes,
   AC pass/fail/not_run, unresolved limits và next step. Gate chỉ nhận bằng chứng tích hợp.

Nếu test/lifecycle/storage/contract prerequisite thiếu, gói tương ứng bị blocked có lý do;
các gói không phụ thuộc mới được tiếp tục. Không bỏ test để hoàn thành danh sách, không ghi
performance/fidelity/CNC acceptance từ compile hoặc doctor. Scope hiện tại là lập kế hoạch,
không thực thi quy trình build/khôi phục source test kể trên.


