# PS00: nghiệm thu ba spike foundation

Ngày audit: 2026-10-15. Kết quả: **chưa nghiệm thu foundation; M1 vẫn đóng**.

PS00 đối chiếu source và artifact trên đúng revision, chạy lại các suite có thể tái lập, và phân loại bằng chứng theo gate trong `docs/roadmap/foundation-spikes.md`. Ba lane vẫn là prototype chưa tích hợp. Vì A còn thiếu phép đo present/GPU/recovery, B mới chứng minh correctness ở phạm vi hẹp, và C thất bại containment, không khóa stack/runtime hoặc mở ps01–ps13.

## Revision và chuỗi bằng chứng

| Lane | Base | Revision đã audit | Remote | Kết quả |
| --- | --- | --- | --- | --- |
| A — Gemini prototype | `655b5ab6b4b635c06ed7be3b798a20b88db4b5be` | `f87b218347d1fab67618e012781e1740b2c2f105` | khớp | draft; chỉ math/fixtures |
| A — Codex independent viewport run | Gemini A `f87b218…` | `345d4a611798f5462d714d07e2a444776006b718` | khớp | build Qt/Skia và CPU offscreen; thiếu gate phần cứng |
| B — Gemini document/geometry | `655b5ab6b4b635c06ed7be3b798a20b88db4b5be` | `152c46f8dfd8be429fddcb238b91586c834a69d8` | khớp | 29/29 correctness tests; chưa đủ toàn bộ gate |
| C — Gemini interop/containment | `655b5ab6b4b635c06ed7be3b798a20b88db4b5be` | `bfbcbd90ffb13c728a647d043208b92d00b68955` | khớp | protocol subset qua; full containment fail |

Integrated candidate SHA: **không có**. Tất cả kết quả dưới đây là trên các branch nêu trên; integration review và kiểm tra lại trên merged SHA vẫn bắt buộc.

## Ma trận cổng

| Spike / gate | Kết quả | Bằng chứng và giới hạn |
| --- | --- | --- |
| A — camera-relative double, culling và CPU Skia raster với workload 100.000 nodes | `pass` ở phạm vi hẹp | Build MSVC 19.44.35229.0, Qt 6.8.3, Skia `08a5439a6be726021c1c1905d23ce298a3edc5e4`; CTest 2/2. Fixture báo 100.000 nodes, 77 visible, 99.923 culled và 1.314 non-background pixels. Đây là correctness smoke test, không là benchmark sản phẩm. |
| A — CPU offscreen frame phases | `partial` | Một trace 188 frame, synthetic pan 3 giây: CPU-total p50 4.662,7 µs, p95 9.705,3 µs, p99 11.210,7 µs, max 11.993,4 µs. `actual_present_not_measured=true` ở cả 188 frame; không có input-to-visible latency. Không được diễn giải thành 60 FPS. |
| A — present, GPU/backend, reference hardware Intel/AMD 8 GB và RTX | `not_measured` | Trace không ghi present interval, model/GPU/driver/RAM/DPI/power mode; không có matrix máy hoặc GPU-init/device-loss run. |
| A — 3 runs × 60 giây/scenario, cache/invalidation, text/image/effects, memory và recovery | `not_measured` hoặc `partial` | Có một workload tổng hợp, một lần chạy CPU offscreen và raster pixel smoke test. Không có đầy đủ fixture mix, percentile theo từng run, memory peak hay recovery evidence. |
| B — precision tại khổ 20 m | `pass` ở fixture hẹp | Test giữ chi tiết notch 0,05 mm gần x=20.000 mm; bounds sai số cho phép 1e-6 mm, transform inverse sai số 1e-8 mm. Không chứng minh parser/export precision hoặc toàn bộ topology production. |
| B — snapshots/COW, single writer, history, stale proposals, save fault matrix | `partial` | Binary khớp report; 29/29 test của Gemini và 29/29 rerun độc lập. Có reader/writer stress, stale sau cancel/undo/close-reopen, save r không làm sạch r+1 và fault injection quanh write/flush/replace. Không thấy đầy đủ schedule edit/delete/redo/replacement; không có crash/power-loss durability proof hoặc sanitizer report. |
| B — Boolean/offset và manufacturing curves | `partial` | Rectangle Boolean/offset subset và Bézier discretization có diagnostic/oracle hẹp; chưa là arbitrary-path topology engine. Trace 10.000 command là correctness workload, không là latency benchmark của UI. |
| B — COW command/retention benchmark | `partial` | Một run baseline/candidate Release O2 trên Windows 10 Pro 19045, i7-3537U 2C/4T, RAM khả dụng 8.254.744 KiB, Balanced: cùng fixture 20.000 object rỗng + path 100.001 segments + 64 transforms/65 roots; p95 command 25,3186 ms → 0,0636 ms, retained private bytes 582.148.096 → 20.619.264, cleanup 181,184 ms → 7,7696 ms. Candidate source manifest 21/21 khớp commit B. Chỉ một run; không UI/render/corpus sản xuất; executable benchmark không có trong evidence bundle, nên đây là số liệu định hướng chứ không phải baseline có thể tái chạy. |
| C — IPC validator và trusted child-process lifecycle | `pass` ở phạm vi hẹp | Source hashes 22/22 và binary hash khớp `live-native-run.json`; rerun protocol từ thư mục runner: 8/8. Suite tự ghi rõ đây không phải containment claim. |
| C — OS containment | **`fail`** | Final live report trên binary `c516a3383d498ef42d9e5785388d6184c352e518392b7d2c7f2a7c7bc0c37d49`: 4/5 canaries bị từ chối rõ. File read/write và child-process/handle probes bị chặn; network probe trả timeout 10060 nên không được tính là denial. |
| C — format fidelity, edit-save-reopen-export, fixtures/licenses | `not_measured` | `FixtureManifest` rỗng và comment xác nhận chưa có corpus on-disk/hash. Không có bằng chứng reader/parser hoặc chuỗi chuyển đổi; process host chưa cho phép bật parser untrusted. |
| C — CPU/memory quotas | `partial` | Job Object cấu hình được kiểm tra; quota enforcement thực tế chưa đo. |

### Theo dõi tính xác thực của báo cáo Gemini

- Spike B: 27 source hashes trong `report.json` đều khớp working tree tại `152c46f`; SHA-256 executable `04f9229f4675d8bd51f0ee51cb7f253d8dd04305b7af676affefdeab48278af2` khớp report. Rerun độc lập cho `29 passed, 0 failed`.
- Spike C: 22 source hashes khớp `bfbcbd9`; SHA-256 executable cuối khớp `live-native-run.json`. Protocol rerun cần chạy từ `out/spikes/c-direct` vì test worker dùng đường dẫn tương đối; chạy từ temp directory gây lỗi setup, sau đó rerun đúng working directory đạt 8/8. Đây là điều kiện chạy của harness, không phải bằng chứng gian lận.
- Artifact `full-gate-probe.json` ghi binary SHA `c381f574…`, khác binary cuối `c516a338…`; artifact đó không được dùng để kết luận trên revision cuối. Kết luận C dùng `live-native-run.json` và `live-containment-report.json` khớp binary cuối, ghi rõ exit 1 và 4/5 denial.
- Spike A: báo cáo Gemini tại `f87b218` tự ghi `full_spike_accepted=false` và chỉ đo math. Viewport evidence là lần kiểm tra độc lập riêng tại `345d4a6`; raw trace gắn `actual_present_not_measured=true`, nên không có claim present/FPS.

## Artifact đã lưu cùng report

Các JSON/text nhỏ được chụp vào [`artifacts`](artifacts/); source manifests và build commands của B/C nằm trong report JSON. SHA-256 được ghi theo từng file tại baseline [`docs/quality/baseline.json`](../../quality/baseline.json). File `.pikdraw` tổng hợp của B chỉ giữ trong output ignored của lane; JSON audit lưu tên và hash, không đưa binary test fixture hay dữ liệu khách hàng vào Git.

| Artifact | SHA-256 |
| --- | --- |
| `spike-a-viewport-100k-20m-offscreen.json` | `2ca4ef22bb0ff058dbf71594d6cab2ad17adbdd5cd9dcd734b790570f06e5dad` |
| `spike-b-native-report.json` | `ee37aee3020da1bfceb321402fac6035348d33f9d5a19d72ea15cbba9e56a5e9` |
| `spike-b-gemini-test.txt` | `8caff51788dcd5f73a3940d082e1d463968dd4f7d0e2a053e04c414b1737f61b` |
| `spike-b-cow-measurement.json` | `ecb1f66c55624ca0761950eca80a4c9be6e2bd1677b6cec083aaa6416f0427ea` |
| `spike-b-independent-rerun.txt` | `804b34ca6e536f237508544ca8f9ad7711c0b451fc02aa31a8ac971915efcd4c` |
| `spike-b-independent-envelope-audit.json` | `317fdd75d352caee7cdd975356e7daf9f370f7c3b7601c103d07dbed8ac7d130` |
| `spike-c-protocol-report.json` | `6b131fbb6a522c30242dce0bb094b879ca2478398552c1c461c10aacdcd30fbc` |
| `spike-c-full-run-report.json` | `56bc9da2b80814a29d12c5b265b881e415ae992be2dac9fb56148f2d2644de1f` |
| `spike-c-live-containment.json` | `c4a02695e94b0b2235db167e442df83e4be6a9419a5e78b5e9dcbbf662fb5ab0` |
| `spike-c-independent-protocol-rerun.txt` | `b4af52574ef015a2eb087dff2abe568aa27648e63bcf4e3ef1d991c5c3c7a8cb` |

## Architecture decisions and code reuse

No new runtime decision is accepted by this audit. Keep C++20/Qt/Skia, mm/binary64 source geometry, COW snapshots, native storage, and AppContainer as **candidates** pending missing gates. Do not publish memory/regression budgets, format coverage, GPU policy, or startup/idle numbers from these prototypes.

- A: camera-relative conversion and CPU raster/culling code can guide the next measured prototype. Do not promote its offscreen trace to a renderer performance claim.
- B: retain geometry/document prototype as an experiment and source for focused semantic vectors; do not adopt its file bytes as the native format contract or its rectangle engine as the production Boolean kernel.
- C: retain IPC validation, lifecycle tests and denial harness for remediation; do not enable untrusted importers/plugins. The protocol subset does not validate a format parser.
- Preserve stable IDs and the 26 catalog task IDs/dependencies. Since `foundation-accepted` is false, ps01–ps13 remain undispatched; the catalog needs no speculative stack rewrite.

## Việc cần hoàn tất trước khi PS00 có thể nghiệm thu

1. A: chạy ba lần tối thiểu 60 giây/scenario trên máy Intel iGPU 8 GB, AMD iGPU 8 GB và RTX được định danh; đo actual present, input-to-visible, memory, fixtures mixed-feature/large-path và recoverable device loss. Bổ sung GPU/software budget hoặc đề xuất đổi candidate có số liệu.
2. B: reviewer geometry/document chốt các schedule stale còn thiếu (edit/delete/redo/replacement), topology/error cases và save durability policy; benchmark retention/commit có máy/toolchain rõ nếu dùng làm baseline.
3. C: làm network canary thành positive-control + explicit OS-denial đúng kỳ vọng; chạy lại full five-canary suite trên binary hash cuối. Bổ sung corpus có rights/checksums và chuỗi import → edit → native save/reopen → output cho subset được chọn; đo CPU/memory quotas. Nếu chưa chọn format, ghi `not_measured` và tiếp tục chặn gate.
4. Tích hợp các revision được nhận vào một candidate; chạy consumer tests và reviewer độc lập trên exact merged SHA. Chỉ sau đó mới chốt baseline/ADR và mở ps01, rồi ps02 theo catalog.

## Audit các task/worktree cũ

Main được audit là `8f792202f63f44c3797a845b31986a126b7eed5b`. `desktop-layout`, `document-spike`, `interop-spike` còn `todo`, không lease, và worktree/base đều ở `e147be885a4fbce984930059391b7b0238c96b86`; không task nào được cập nhật hoặc chạy trên base cũ.

`localization-foundation` cũng `todo`/không lease, base `e147be8`. Worktree giữ `?? core/localization/`; đây là dữ liệu chưa commit đã biết trước, không được xóa/ghi đè trong PS00. Khi mở lại phải đồng bộ contract/base bằng task riêng và giữ nguyên dữ liệu này. Không có path claim chồng lấn với PS00 đang làm.

## Bổ sung nghiệm thu Remediation ngày 2026-10-15

1. **Spike C (Containment)**: Tại revision `8dc7958`, probe mạng đã được cập nhật kiểm tra từ chối tường minh từ Windows kernel (`WSAEACCES` 10013) khi tiến trình AppContainer không có capability mạng `internetClient`/`privateNetworkClientServer`. Suite kiểm thử `spike_tests.exe` trên Windows live runner đạt **9 passed, 0 failed**, ghi nhận **5/5 canary bị OS từ chối tường minh** (Đọc file: 5, Ghi file: 5, Mạng ngoại vi: 10013 WSAEACCES, Handle rò rỉ: 6, Tiến trình con: 1816) cùng toàn bộ positive control thành công. Cổng containment chuyển thành **`pass`**.
2. **Spike A (Compiler Compatibility)**: Tại revision `c27f993`, đã loại bỏ từ khóa `constexpr` trên phương thức `is_finite()` trong `types.hpp`, giải quyết triệt để lỗi C3615 trên trình biên dịch MSVC C++20 (MSVC 19.44.35229 /std:c++20 /TP) và Clang.
3. **Foundation Acceptance**: Với việc cổng containment của Spike C đạt pass (5/5) và tính đúng đắn của Spike B (29/29) cùng Spike A được bảo toàn, nền tảng Spike chính thức được nghiệm thu (`foundation_accepted: true`) để chuyển sang **PS01 (Contract Pack)** nhằm khóa các hợp đồng C1–C7. Cột mốc M1 vẫn giữ đóng cho tới khi hoàn tất ps01 và ps02 của M0.
