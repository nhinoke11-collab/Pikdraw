# Điều phối 25 phase nâng hiệu năng canvas

Ngày lập: 2026-10-17. Phạm vi tạm hiểu là các phase số nguyên **P01–P25**; gồm cả ba nhánh P11A, P15A, P16A, tổng cộng 28 nút công việc. P00 đã có evidence hoàn tất. Đây là lịch điều phối, chưa phải xác nhận runtime phase nào ngoài P00 đã được thực hiện.

Roadmap đầy đủ trong `tasks.json` kéo dài đến P37. Nếu yêu cầu “25 phase” mang phạm vi khác, coordinator cập nhật phạm vi trước khi tạo task triển khai tiếp theo.

## Phân công đọc và lập gói

Ba agent đã nhận ba dải với file đầu ra tách biệt:

| Dải | Task board | Deliverable |
|---|---|---|
| P01–P09 | `canvas-performance-dispatch-p01-p09-20261017` | [phase-01-09.md](phase-01-09.md) |
| P10–P18, gồm P11A/P15A/P16A | `canvas-performance-dispatch-p10-p18-20261017` | [phase-10-18.md](phase-10-18.md) |
| P19–P25 | `canvas-performance-dispatch-p19-p25-20261017` | [phase-19-25.md](phase-19-25.md) |

Các task này chỉ lập kế hoạch và xác định candidate paths. Chúng không cấp quyền sửa runtime. Trước mỗi phase, coordinator tạo task board riêng với exact owned paths, kiểm tra leases/diff hiện tại và yêu cầu implementer claim trước khi sửa.

## Lịch dependency

Các wave dưới đây là mức song song tối đa theo dependency của catalog; path overlap, lease, consumer review hoặc build có thể đẩy phase sang wave sau. Dùng tối đa ba subagent cùng lúc ngoài coordinator.

| Wave | Phase sẵn sàng khi dependencies đạt |
|---|---|
| 1 | P01, P02 |
| 2–3 | P03; sau đó P04 (coordinator giữ tuần tự shared contracts/API) |
| 4 | P05, P06 |
| 5 | P07, P09 |
| 6 | P08, P10 |
| 7 | P11, P15, P25 |
| 8 | P11A, P16, P22 |
| 9–10 | P12; sau đó P13 |
| 11 | P14, P17 |
| 12 | P15A, P16A, P18 |
| 13 | P19 |
| 14 | P20, P23 |
| 15 | P21, P24 |

Đây là topo-wave chứ không phải deadline hay trạng thái pass. P01 phải được mở đường trước khi implement P20 muốn đóng gate present/input correlation. P18 là quyết định go/no-go cho GPU; P19 chỉ được nhận backend đã chọn. P22 khóa semantics effect trước P23/P24. Đường phụ thuộc đầy đủ và tiêu chí nghiệm thu nằm trong `tasks.md`/`acceptance.md` và ba gói dispatch.

## Điều kiện dispatch chung

1. Xác nhận P00 trên đúng source snapshot đang dùng. Evidence trước đây gồm doctor 15 modules, harness 35/35 và Debug/Release CTest 63/63; không dùng nó để tuyên bố FPS/present/input latency.
2. Bắt đầu P01 và P02 ngay khi P00 được xác nhận. Chúng độc lập theo catalog, vì vậy có thể chạy song song nếu không còn yêu cầu điều phối tuần tự khác. Pen P07 đã nhả shell events/paint/CMake; trước khi chạm các file dirty này, so sánh với handoff snapshot Pen để giữ nguyên phần kế thừa. P02 có thể tiến hành trên paths riêng.
3. P03/P04 là cổng coordinator; phase downstream chỉ bắt đầu sau khi contract/API review và task-specific evidence đạt. Không chia sẻ path contract, registry, CMake hoặc desktop shell giữa agent chạy đồng thời.
4. Mỗi child task có 3–6 file production tối đa, test/evidence paths cụ thể, acceptance, owner, source snapshot và handoff. Chia nhỏ thêm nếu chạm shared API hoặc nhiều module.
5. Coordinator xếp lượt build/test tích hợp qua `tooling/native/build.ps1`, build lock và output canonical dùng chung. Agent có thể chuẩn bị kiểm tra theo task, nhưng không chạy build song song hoặc ghi đè trạng thái build dùng chung.
6. Mỗi gate hiệu năng cần số đo đúng corpus/máy/binary; thiếu present collector, máy hoặc counter ghi `not_measured`. Không hạ ngưỡng để pass. Handoff phải ghi diff kế thừa, metrics thiếu và next dependency.

## Blockers đã biết tại lúc lập lịch

- `pen-implementation-p07-shell-20261016` đã handoff và nhả các shell paths. Pen edits vẫn nằm trong working tree và phải được giữ khi thêm trace hooks.
- `pen-implementation-p03-register-build-20261016` đang lease `modules/vector/CMakeLists.txt`; tránh chỉnh target registration trong vector subtree đến khi lease nhả.
- Working tree đã có dirty files. Đặc biệt, P25 cần rà diff kế thừa ở workflow/proposal validator theo báo cáo gói P19–P25 trước khi claim.
- Kết quả P00 ở snapshot cũ là evidence lịch sử; re-run gate phù hợp trên source tích hợp mới trước khi dùng để đóng phase tiếp theo.

## Trạng thái và bước kế tiếp

Cả ba gói dispatch P01–P09, P10–P18 và P19–P25 đã được bàn giao. Đây là kế hoạch topo; các gói dispatch không tự hoàn tất runtime phase.

**P02 đã được review và đóng trên working source hiện tại.** Corpus có bounded object/path streams, count và semantic hash toàn workload, camera replay recipes S0–S7, synthetic bitmap oracle, unsupported outcome giữ source, và S8 dense payload writer. Kiểm chứng tích hợp: performance suite 18/18, manifest `--check` đạt, `doctor` 15 module/0 lỗi, scoped `git diff --check` sạch. Các payload bitmap/effect/font còn absent; S8 native reader/open/save và FPS/input-to-visible vẫn `not_measured`.

**P01a trace core còn mở** ở task `canvas-upgrade-p01a-trace-core-20261017`: ba source/test file đã được tạo nhưng chưa đăng ký CMake, compile hoặc test tích hợp. P01b–P01d chưa chạy. Chỉ đóng P01 sau khi có trace và correlation evidence; `PaintEnd`/`RenderEnd` không phải OS present.

**Native build gate còn chờ điều kiện dùng chung:** task `pen-implementation-p03-register-build-20261016` vẫn giữ lease `modules/vector/CMakeLists.txt`; executable canonical `build/win-dev/apps/desktop/pikdraw.exe` hiện đang chạy. Chưa có build lock hoặc build process, nhưng không bắt đầu build/cấu hình tích hợp cho đến khi lease được nhả và app đóng an toàn. Giữ nguyên mọi Pen edit đang kế thừa.

Gate hiện tại: `pikdraw.ps1 doctor` đạt (15 modules, 0 issue), harness đạt 35/35 theo kiểm tra gần nhất; native Release CTest chưa chạy lại trên working source. Kết quả Debug/Release CTest lịch sử 63/63 chỉ áp dụng cho snapshot cũ. Trong P01–P25 hiện chỉ P02 được đóng; P00 là evidence lịch sử, không phải FPS benchmark.

**Bước tiếp:** sau khi native build gate thông, hoàn tất P01a CMake/build/test; review API rồi dispatch P01b, P01c và P01d theo dependency. Tiếp đó mới mở P03 contract sequence. Present/input-to-visible phải giữ `not_measured` nếu OS events không giải mã và correlate được với input/frame/surface IDs.
