# Đo hiệu năng effects trên software renderer · 2026-10-17

## Kết luận

Đã có benchmark có thể chạy lại cho source-only, Gaussian Blur, Drop Shadow, stack Blur+Shadow và 64 object nhỏ có Blur. Một bounded output cache đã được thêm để tránh lấy mẫu và blend hai lần trên vùng đích nhỏ/vừa. Các phép đo Release mới vẫn vượt mốc 33.3 ms p50 dùng trong kế hoạch software interaction; kết quả vì vậy **không đạt gate tương tác thời gian thực**.

Một lượt 40 mẫu tổng hợp trước tối ưu có process CPU chỉ chiếm 58–90% một lõi ở Shadow/stack/multi-object; các latency của lượt đó bị nhiễu bởi process khác. Blur chạy riêng trước tối ưu có process CPU 97% một lõi và p50/p95 78/125 ms. Ba lượt Blur 40 mẫu sau tối ưu đạt 55/70, 77/100 và 58/94 ms với CPU 95–100% của một lõi. Chúng gợi ý giảm latency so với một baseline trước đó, nhưng một baseline và ba lần lặp không khóa được clock/thermal; đây chưa phải gate hiệu năng ổn định. Không dùng các lượt bị contention hoặc profile chưa đo để đặt budget sản phẩm.

## Phương pháp

- Cấu hình Release, renderer CPU `SoftwareRasterizer`, viewport 640×480.
- Mỗi scenario chạy trong process riêng. Mỗi process có một cold render, ba warmup và 20 hoặc 40 render mẫu; p50/p95 dùng nearest-rank từ `steady_clock`. `GetProcessTimes` ghi tỷ lệ CPU của process so với một lõi trong các mẫu.
- Scene gồm hình chữ nhật Path 224×160 mm cho một object hoặc 64 Path 18×18 mm; Blur sigma 1.5 mm, Shadow offset +8/−6 mm với sigma 1.5 mm, stack Blur 1 mm + Shadow 1.5 mm. Multi-object dùng Blur 0.8 mm.
- Process private/working-set delta lấy từ `GetProcessMemoryInfo` so với baseline sau khi tạo document/target, rồi so với peak trong process. Đây là process-level growth, **không phải peak scratch chính xác**.
- Mỗi lượt kiểm tra `render()` thành công, số object được dựng và không có error diagnostic; checksum ngăn bỏ qua kết quả.
- CPU: Intel Core i7-3537U @ 2.00 GHz. GPU/present interval, input-to-visible latency, temperature/clock/power profile và DPI không được đo.

## Kết quả

| Scenario | Object | Release 20 mẫu p50 / p95 (ms) | Process CPU / một lõi | Private / working-set peak delta |
| --- | ---: | ---: | ---: | ---: |
| source_only | 1 | 7.788 / 9.567 | 98.5% | 0 / 12 KiB |
| blur_single | 1 | 71.603 / 142.091 | 94.9% | 5.73 / 5.67 MiB |
| shadow_single | 1 | 76.972 / 100.097 | 97.2% | 7.20 / 7.13 MiB |
| blur_shadow_stack | 1 | 88.612 / 99.524 | 99.7% | 7.79 / 7.72 MiB |
| many_blurred | 64 | 114.551 / 125.302 | 99.9% | 0.84 / 0.83 MiB |

Blur chạy riêng 40 mẫu đạt p50/p95 78.020/124.646 ms với 97.2% CPU một lõi và private/working-set delta khoảng 5.73/5.67 MiB. Trong lượt 40 mẫu tổng hợp bị tải ngoài, CPU share của Blur/Shadow/stack/64 objects lần lượt là 89.8/58.0/69.9/69.0%; p50 tương ứng là 90.965/196.825/213.253/262.341 ms. Dữ liệu đó minh họa contention, không phải mức renderer trong điều kiện chuẩn.

### Sau bounded output cache

Compositor giữ nguyên preflight toàn vùng trước pixel write. Với vùng đích tối đa 262,144 pixel, nó cache source pixel (Normal) hoặc pixel đã blend (mode khác), tối đa 4 MiB theo 16 byte/pixel. Vùng lớn hơn giữ đường hai pass cũ để giới hạn scratch. CTest Release `test_pikdraw_rendering_effect_compositor` passed 1/1; regression so sánh từng pixel Multiply với blend operator và kiểm tra backdrop lỗi không ghi dở dang.

| Scenario | Release 20 mẫu p50 / p95 (ms) | Process CPU / một lõi | Private / working-set peak delta |
| --- | ---: | ---: | ---: |
| source_only | 9.707 / 13.637 | 81.5% | 0 / 12 KiB |
| blur_single | 85.999 / 94.510 | 100.9% | 5.73 / 5.67 MiB |
| shadow_single | 71.759 / 87.278 | 100.6% | 7.21 / 7.14 MiB |
| blur_shadow_stack | 74.533 / 86.342 | 100.2% | 7.79 / 7.72 MiB |
| many_blurred | 89.902 / 101.815 | 100.2% | 1.02 / 0.78 MiB |

Ba lượt Blur 40 mẫu độc lập sau output cache: p50/p95 55.225/70.376, 76.644/99.569 và 58.215/94.216 ms, với process CPU 95.2–100.2% của một lõi. Median giữa ba lượt là 58.215/94.216 ms. Kết quả này thấp hơn baseline trước cache 78.020/124.646 ms, nhưng tải/clock/thermal chưa được khóa nên không dùng để công bố phần trăm tăng tốc ổn định. Mọi lượt Blur vẫn không đạt 33.3 ms.

### Sau convolution đối xứng SSE2

Đường compositor hiện dùng implementation SSE2 ở file riêng; Gaussian scalar hiện hữu vẫn là reference. Kernel chuẩn hóa bằng double rồi lưu trọng số float, gom hai mẫu đối xứng trước phép nhân SIMD, và giữ input validation, quota, cancellation polling, zero ngoài surface, không renormalize biên cùng publish output nguyên tử. Regression so từng pixel ngẫu nhiên với scalar trên nhiều kích thước/sigma đạt sai số tối đa `4e-6`; checksum benchmark trước/sau giữ nguyên.

| Lượt Release Blur 40 mẫu | p50 / p95 (ms) | CPU của process / một lõi | Private / working-set peak delta |
| --- | ---: | ---: | ---: |
| Scalar baseline 1 | 204.323 / 255.089 | 99.2% | 5.73 / 5.67 MiB |
| Scalar baseline 2 | 204.783 / 266.125 | 99.1% | 5.73 / 5.67 MiB |
| Scalar baseline 3 | 208.355 / 279.261 | 99.1% | 5.73 / 5.67 MiB |
| SSE2 run 1 | 50.641 / 58.444 | 99.8% | 5.76 / 5.70 MiB |
| SSE2 run 2 | 50.517 / 57.856 | 100.4% | 5.73 / 5.67 MiB |
| SSE2 run 3 | 53.867 / 66.547 | 97.7% | 5.73 / 5.67 MiB |

Median of the three runs is 50.641/58.444 ms, approximately 75% lower p50 and 78% lower p95 than this same-session scalar baseline. All runs used Intel Core i7-3537U, Release, 640×480, the same benchmark scene, 40 samples, and checksum `14946842818742444328`. This is a measured CPU improvement on one machine, not a passed product gate: p50 still exceeds 33.3 ms, scratch allocation is not directly measured, and GPU/present/input latency, clock/thermal state, Text, DPI and a second machine remain untested. Full Debug and Release CTest each passed 79/79 after the new target was registered; the focused rendering Release suite also passed 11/11.

### Sau AVX runtime dispatch

The AVX translation unit is compiled with a per-source AVX flag; the baseline dispatcher checks CPU AVX plus OS-enabled XMM/YMM state before calling it. Debug parity ran with AVX enabled on the same i7-3537U and passed scalar tolerance, edge-energy, premultiplied bounds, cancellation, and atomic-output checks. The checksum remained `14946842818742444328`.

| Release Blur run, 40 samples | p50 / p95 (ms) | Process CPU / one core | Private / working-set peak delta |
| --- | ---: | ---: | ---: |
| AVX first run after relink | 74.145 / 90.722 | 94.6% | 5.74 / 5.68 MiB |
| AVX repeated run 1 | 52.690 / 67.021 | 96.9% | 5.73 / 5.67 MiB |
| AVX repeated run 2 | 52.763 / 64.889 | 99.0% | 5.73 / 5.67 MiB |
| AVX repeated run 3 | 57.730 / 97.154 | 90.1% | 5.74 / 5.68 MiB |

The repeated-run median is 52.763/67.021 ms. Against the three SSE2 runs above, this is about 4% slower at p50 and 15% slower at p95 on this Ivy Bridge laptop; the first post-link run was slower again. AVX proves safe dispatch and numerical parity, but does not improve this host and does not meet the 33.3 ms interaction target. Its value on newer CPUs is unmeasured. Further CPU optimization must be measured against the SSE2 path, preserve the Gaussian v1 boundary contract, and include a scratch-allocation measurement before any product gate is considered passed.

### Thử nghiệm convolution hai lane — không giữ

Đã thử chia dải output theo hàng: một lane trên caller và tối đa một helper dùng chung toàn process. Callback hủy chỉ được gọi trên caller; caller poll trong khi chờ helper, helper quan sát cờ atomic; các lane ghi dải hàng rời nhau và output vẫn chỉ swap khi cả hai trục thành công. Debug parity/cancellation trên surface lớn qua focused CTest 2/2 trước lượt đối chứng. AVX được runtime dispatch bật trên i7-3537U.

Đối chứng Release dùng cùng runner/scene 640×480, 40 mẫu, checksum `14946842818742444328`. Lượt tuần tự được build với nhánh parallel tạm vô hiệu; sau đo, source trở lại baseline AVX/SSE2 trước thử nghiệm. Đây không phải clock/thermal-controlled benchmark: process CPU share dao động mạnh, do đó kết quả chỉ dùng để quyết định có lợi ích đủ rõ để giữ hay không.

| Đường chạy | p50 / p95 ms, 3 lượt | CPU p50 ms | CPU share / một lõi | Median giữa 3 lượt p50 / p95 ms |
| --- | --- | ---: | --- | ---: |
| Hai lane | 547.779/1931.361; 202.335/478.219; 147.802/244.515 | 78.125 mỗi lượt | 10.3–47.7% | 202.335 / 478.219 |
| Tuần tự control | 146.605/189.802; 202.757/1197.394; 183.516/331.486 | 62.500–78.125 | 19.2–52.9% | 183.516 / 331.486 |

Checksum và process-memory delta tương đương; CPU p50 median không giảm. Hai-lane median p50 cao hơn khoảng 10% và p95 cao hơn khoảng 44% so với serial control, trong khi tải máy không ổn định. Không có bằng chứng tăng tốc hữu ích, nên đã loại bỏ worker dispatch, worker slot, row-range AVX interface và test chỉ dành cho parallel path; implementation serial đã kiểm chứng được giữ nguyên. Không tính stack/runtime của thread như scratch renderer; process-level peak vẫn không phải đo scratch chính xác. Bước tiếp theo là profiling một thuật toán convolution ít phép tính/ít copy hơn dưới tải được ghi nhận, không bật worker song song theo mặc định.

### Baseline Blur sau thử nghiệm SIMD — 17/10/2026

Sau khi hoàn nguyên thử nghiệm bỏ kiểm tra biên khỏi kernel, chạy lại đúng scenario `blur_single` bằng Release, 640×480, 40 mẫu mỗi process trên Intel Core i7-3537U. Ba lần giữ cùng checksum `14946842818742444328`; CPU tổng của máy được đọc khoảng 10% trước lượt đo, còn process benchmark dùng 95.3–99.4% một lõi. Đây là một cửa sổ đo nhẹ hơn các lượt thử song song trước, nhưng không khóa xung nhịp, nhiệt độ hay nguồn điện.

| Release Blur, 40 mẫu | p50 / p95 (ms) | CPU process / một lõi | Private / working-set peak delta | Checksum |
| --- | ---: | ---: | ---: | ---: |
| Lượt 1 | 48.538 / 66.612 | 97.7% | 5.73 / 5.67 MiB | `14946842818742444328` |
| Lượt 2 | 51.152 / 77.374 | 95.3% | 5.73 / 5.67 MiB | `14946842818742444328` |
| Lượt 3 | 49.607 / 73.942 | 99.4% | 5.73 / 5.67 MiB | `14946842818742444328` |

Median của ba lượt là 49.607/73.942 ms p50/p95. Kết quả xác nhận baseline serial trên máy này còn vượt mục tiêu p50 33.3 ms; nó không chứng minh tăng tốc hay đóng gate phát hành. Process peak delta vẫn không đo scratch của renderer riêng lẻ.

### Capacity buffer Blur trong render path — 17/10/2026

Thêm telemetry vào Gaussian SIMD, object compositor và `RenderMetric`. Trường `peak_gaussian_blur_scratch_capacity_bytes` đếm tổng capacity của vector đang sống trong lúc một node Gaussian Blur chạy: source isolation, filter input/output và staging/kernel buffers nội bộ. Số này không gồm allocator metadata, target/backdrop, document geometry hoặc scratch của Gaussian scalar được Drop Shadow gọi bên trong. `source_only` và `shadow_single` báo 0; vì vậy đây là phép đo cho node Blur tường minh, không phải tổng scratch mọi effect.

Focused Release build của Gaussian SIMD, compositor và benchmark thành công; CTest Gaussian SIMD + effect compositor đạt 2/2. Ba lượt Release Blur 40 mẫu trên i7-3537U đều giữ checksum `14946842818742444328` và báo capacity `5,923,920` bytes (5.65 MiB). Private/working-set peak process là `6,012,928`/`5,947,392` bytes; đây là đại lượng riêng, không cộng hoặc thay thế cho capacity buffer.

| Blur Release 40 mẫu, sau thêm telemetry | p50 / p95 (ms) | CPU máy được lấy mẫu trước lượt | CPU process / một lõi | Gaussian buffer capacity (bytes) |
| --- | ---: | ---: | ---: | ---: |
| Lượt 1 | 76.819 / 101.371 | 2% | 95.9% | 5,923,920 |
| Lượt 2 | 55.048 / 67.820 | 11% | 100.3% | 5,923,920 |
| Lượt 3 | 47.772 / 55.574 | 6% | 99.0% | 5,923,920 |

Median p50/p95 là 55.048/67.820 ms. Biến thiên thời gian vẫn đáng kể và p50 vẫn cao hơn 33.3 ms. Telemetry cho thấy riêng node Blur dùng khoảng 5.65 MiB vector capacity trên fixture này; nó không chứng nhận giới hạn RAM toàn renderer, không đo allocator/OS resident pages và không bao phủ Drop Shadow.

### Capacity buffer Drop Shadow và kiểm tra parity — 17/10/2026

Drop Shadow hiện dùng Gaussian SIMD dispatch đã có kiểm thử parity. `peak_drop_shadow_scratch_capacity_bytes` theo dõi capacity vector đang sống trong nhánh Shadow, gồm source/current surface ở compositor, alpha conversion, vectors Gaussian tạm và staged shadow output. Nó loại allocator metadata, caller target/backdrop và process memory; đây là metric capacity của vector, không phải RSS hay mức tăng bộ nhớ tiến trình. `source_only` báo 0 cho cả Blur và Shadow.

Release build bốn target Drop Shadow, compositor, Gaussian SIMD và benchmark hoàn tất 59/59 bước. CTest `test_pikdraw_rendering_drop_shadow`, `test_pikdraw_rendering_effect_compositor`, `test_pikdraw_rendering_gaussian_blur_simd` và `test_pikdraw_effect_draft` đạt 4/4; ca effect draft xác nhận kích hoạt lại Shadow chỉnh node hiện có, không nhân đôi ID hay revision. Trên i7-3537U, 640×480, `shadow_single` giữ checksum `16534785165175396944` ở cả ba lượt 40 mẫu và báo `9,334,080` bytes capacity (8.90 MiB). Process private/working-set peak delta dao động rất ít quanh `7,553,024`/`7,483,392` bytes; không cộng đại lượng này với vector capacity vì hai phép đo có phạm vi khác nhau.

| Shadow Release 40 mẫu | p50 / p95 (ms) | CPU process / một lõi | Shadow vector capacity (bytes) | Checksum |
| --- | ---: | ---: | ---: | --- |
| Lượt 1 | 141.209 / 197.505 | 56.2% | 9,334,080 | `16534785165175396944` |
| Lượt 2 | 125.667 / 175.482 | 67.5% | 9,334,080 | `16534785165175396944` |
| Lượt 3 | 56.370 / 142.963 | 92.8% | 9,334,080 | `16534785165175396944` |

Median giữa ba lượt là 125.667/175.482 ms, nhưng tải CPU khác nhau đáng kể và p95 dao động rộng; đây chưa phải phép so sánh có kiểm soát với baseline scalar cũ `62.067/84.367` ms (CPU 99.9%). Vì vậy chưa kết luận SIMD làm nhanh/chậm và chưa tuyên bố scratch đã giảm so với scalar: trước đây Shadow chưa có metric riêng tương đương. Một lượt 60 mẫu khác báo 121.958/144.498 ms, CPU 74.4%; không gộp với median 40 mẫu. Cần chạy A/B cùng binary fixture, kiểm soát tải/clock và đo scalar baseline bằng cùng metric trước khi đặt gate hiệu năng hoặc scratch.

### Kiểm tra tích hợp mới nhất

Tại thời điểm thử nghiệm serial-control, full Debug CTest ghi 77/80: paste-bounds tại `test_desktop_shell.cpp:2574` và hai COM/MCP semantic-catalog checks (`test_pikdraw_com_dispatch`, `test_pikdraw_com_runtime_e2e`) thất bại; Gaussian SIMD, compositor và effect-draft cases vẫn qua. Kết quả này đã được supersede: sau khi source trở lại baseline serial AVX/SSE2, lượt mặc định `tooling/native/build.ps1 -Test` hoàn tất build và full Debug CTest đạt 80/80, 0 lỗi lúc 19:00–19:01 ngày 2026-10-17. Full-suite rerun bằng `tooling/native/build.ps1 -TestsOnly` lúc 19:07–19:08 cũng đạt 80/80; `build/win-dev/Testing/Temporary/LastTest.log` ghi đủ 80 pass, 0 fail. Hai lượt gồm storage, culling, Gaussian/SIMD, Shadow, render warning, effect draft, desktop shell, COM và interop. Full Release 79/79 là kết quả trước đó, chưa chạy lại trên snapshot mới. Test pass không đóng cổng hiệu năng 33.3 ms, đo scratch chính xác, OS UI/DPI/accessibility hoặc fidelity định dạng ngoài.

Private-memory delta đổi sang MiB bằng 1 MiB = 1,048,576 bytes. Lượt CPU-bận 20 mẫu cho thấy effects tăng đáng kể thời gian so với source-only. Peak process của 64 object nhỏ thấp hơn single-object lớn vì mỗi effect surface nhỏ được giải phóng trước object kế tiếp; số này không đại diện RAM toàn app khi cache/panel khác hoạt động.

## Giới hạn và bước tiếp theo

- Timing phụ thuộc tải máy; chưa chốt ngưỡng/p95 phát hành từ một máy. Lượt 20 mẫu CPU-bận và Blur 40 mẫu riêng là cảnh báo CPU bound, chưa phải ma trận nghiệm thu.
- Probe đo document render CPU, không đo render submit, Windows compositor, monitor present, GPU, pointer-to-visible, cancel latency hoặc interactive preview.
- Chưa có Text outlines, transform stress, ICC/CMYK, object phủ viewport lớn hơn, offscreen shadow, stack sâu, cold-cache matrix hay hardware thứ hai.
- AVX runtime-dispatch and numerical parity are implemented, but the measured repeated-run median (52.8/67.0 ms p50/p95) is slightly slower than SSE2 on this i7-3537U and remains over 33.3 ms. The bounded two-lane experiment showed no useful win and has been removed. Next, profile a lower-work or lower-copy convolution path against the same Release fixture under recorded load; retain SSE2 fallback and check cancellation, allocation bounds and exact edge policy. Measure clock/load/power and renderer-owned scratch separately. Output sampling cache and SSE2 already have regressions; no evidence supports relaxing synchronous-preview limits or marking the p95 gate passed.

## Chạy lại

```powershell
.\tooling\performance\run_effects_benchmark.ps1 -Configuration Release -Iterations 20
# Binary đã build:
.\tooling\performance\run_effects_benchmark.ps1 -Configuration Release -Iterations 40 -SkipBuild
```

Benchmark target không được đăng ký trong CTest mặc định. Runner build target tường minh dưới `.agent/native-build.lock`, tránh biên dịch toàn bộ desktop shell/test khi phần không liên quan đang lỗi.
