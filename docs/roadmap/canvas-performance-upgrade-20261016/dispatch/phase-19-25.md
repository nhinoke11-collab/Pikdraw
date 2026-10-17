# Gói dispatch P19–P25

Ngày 2026-10-17. Đây là đề xuất task/path cho coordinator; chưa tạo task triển khai và chưa có phase nào được đánh dấu đạt. Các path mới là candidate, cần kiểm lại lease ngay trước khi tạo/claim. Không dùng status của roadmap/catalog làm bằng chứng phase đã hoàn tất.

## Gate nền và lịch tối đa 3 agent

Trước khi giao P19–P25, coordinator xác minh evidence/consumer acceptance của **P03, P04, P05, P09, P10, P11, P13, P15, P16, P17, P18**. Cần thêm trực tiếp **P06** cho P25, **P12** cho P24, và các nhánh **P15A/P16A** cho P19. P22 nằm trong phạm vi này, nhưng phải đóng trước P23/P24. P00 hoặc dispatch document không thay cho gate runtime. Nếu dependency chưa được nghiệm thu thì chỉ giao phần chuẩn bị không đụng contract/source, hoặc chờ; không hạ gate.

| Wave | Tối đa 3 agent | Điều kiện và thứ tự |
|---|---|---|
| 1 | P19 + P22 | Sau gate nền. Disjoint paths; P19 chỉ dựng backend/resource seam, P22 khóa semantics độc lập. |
| 2 | P20 + P23 | Sau P19; P23 cũng cần P22. P20 còn chờ P01 present correlation và lease Pen P07. |
| 3 | P21 | Sau P20 và P09; xếp sau P23 vì cùng sửa `core/rendering/CMakeLists.txt`. |
| 4 | P24 + P25 | P24 cần P23/P12/P13; P25 cần P04/P06/P09/P10. Hai task dùng CMake khác nhau. P25 chỉ bắt đầu sau khi đã rà soát diff kế thừa và P07 nhả app CMake. |

P20 có thể tiến hành ngay sau khi P07 nhả lease, không cần đợi P23. Nếu P01 không chứng minh được frame đã hiển thị tương ứng với input, P20 có thể hoàn thành phần wiring nhưng **không** đóng gate present/FPS. Không báo FPS từ GPU submit, `WM_PAINT` hay `BitBlt` return.

## Child task candidates

Các dependency dưới đây là gate evidence, không chỉ là code đã tồn tại. Mỗi task claim trọn path trong một hàng; các task dùng chung CMake/composition phải chạy tuần tự như wave trên.

| Task | Dependencies / acceptance chính | Exact owned paths đề xuất |
|---|---|---|
| **P19 GPU backend/resource ownership** | P18, P11, P13, P15, P15A, P16A; xác minh P04/P05/P17 seam và asset lifetime. Pass: reference corpus vector/bitmap/group/color; GPU handle chỉ giải phóng trên owner đúng; generation reset, upload/cache bounded, không readback full frame. | `core/rendering/src/gpu/gpu_backend.hpp`; `core/rendering/src/gpu/gpu_backend.cpp`; `core/rendering/src/gpu/gpu_resources.hpp` *(path mới đề xuất)*; `core/rendering/src/gpu/gpu_resources.cpp`; `core/rendering/src/renderer.cpp`; `core/rendering/CMakeLists.txt` |
| **P20 native present/frame pacing** | P19, P10 và trace/correlation P01. Pass: input-to-displayed-frame join thật; resize/DPI/minimize/restore/sleep/monitor/focus/IME; idle không busy-render; software fallback không chặn input. | `apps/desktop/src/shell/native_window_events.cpp`; `apps/desktop/src/runtime/native_presenter.hpp`; `apps/desktop/src/runtime/native_presenter.cpp`; `apps/desktop/src/runtime/composition_root.cpp`; `apps/desktop/CMakeLists.txt` |
| **P21 backend policy/recovery** | P20, P09; kiểm soát qua P04/P18. Pass: forced-software/init-fail/loss/OOM; generation stale bị loại, source còn nguyên, retry hữu hạn và diagnostics bounded. Không hứa tự phục hồi driver crash trong process. | `core/rendering/include/pikdraw/rendering/backend_policy.hpp`; `core/rendering/src/gpu/backend_policy.cpp`; `core/rendering/CMakeLists.txt`; `apps/desktop/src/runtime/composition_root.cpp` |
| **P22 EffectStack semantics — coordinator** | P03, P05, P11. Pass: producer/consumer vectors thống nhất units/sigma/order/transform/clip/isolation/alpha/color, bounds/ROI hữu hạn, unknown-required và undo/migration policy; không serialize backend filter hoặc flatten source. | `core/document/include/pikdraw/document/effects/effect_stack.hpp`; `contracts/c3-rendering.md`; `contracts/c4-color-typography.md`; `contracts/c5-storage.md` |
| **P23 Gaussian blur reference + GPU adapter** | P19, P22; có P13/P17 tile/cache behavior. Pass: CPU reference impulse/step/transparent edges/anisotropic sigma; GPU so cùng oracle; tile seam, scratch/cancel bounded, preview/refinement stale-safe. | `core/rendering/src/effects/gaussian_blur.hpp`; `core/rendering/src/effects/gaussian_blur.cpp`; `core/rendering/CMakeLists.txt` |
| **P24 shadow/effect graph cache** | P23, P12, P13; semantics kế thừa P22. Pass: ROI/halo truyền qua stack, shadow ngoài source bounds, group opacity đúng một lần, invalidate edit/delete không ghost/seam, cache/memory bounded trên S5. | `core/rendering/src/effects/drop_shadow.cpp`; `core/rendering/src/effects/effect_graph.cpp`; `core/rendering/src/effects/effect_cache.cpp`; `core/rendering/CMakeLists.txt` |
| **P25 async Boolean/offset workflow** | P06, P09, P10; yêu cầu JobToken/stale policy từ P03/P04. Pass: 100/1k/10k trace tách ack/cancel/compute/commit; edit/undo/close giữa job bị reject; một atomic commit/undo và cùng geometry/source semantics. | `apps/desktop/src/workflows/document_workflow.cpp`; `apps/desktop/src/runtime/geometry_jobs.hpp`; `apps/desktop/src/runtime/geometry_jobs.cpp`; `apps/desktop/CMakeLists.txt`; `core/document/src/async/proposal_validator.cpp` |

## Blocker và ranh giới cần coordinator khóa

- **Lease đang chặn:** `pen-implementation-p07-shell-20261016` (`session-pen-p07-ui-20261017`) đang giữ `apps/desktop/src/shell/native_window_events.cpp`, `apps/desktop/src/runtime/composition_root.cpp` và `apps/desktop/CMakeLists.txt`. Không claim P20/P21/P25 trên các path này cho tới khi lease được nhả; không sửa thay owner. Các task dispatch P01–P18 hiện chỉ giữ file dispatch khác, không giao quyền runtime.
- **Diff kế thừa cần bảo toàn:** `apps/desktop/src/workflows/document_workflow.cpp` hiện có diff 174 dòng thêm/16 dòng xóa; `core/document/src/async/proposal_validator.cpp` có diff 2 dòng. Task-board scan không thấy lease active trên hai path, nhưng chưa xác định owner/ý nghĩa diff. Coordinator phải lập baseline/đối chiếu trước khi giao P25; không reset hoặc ghi đè.
- **Backend:** P18 phải quyết định go/no-go bằng revision/build/license/capability evidence trên máy thật. Agent P19 không tự chọn Skia/ANGLE, API GPU hay đổi stack. Coordinator/P04 khóa seam inject, owner của context/surface, format/alpha/color, generation và CPU source recovery; chọn theo capabilities, không theo hãng GPU. P20 owns HWND/UI; render owner owns GPU surface/resources.
- **Effects:** P22 khóa sigma theo đơn vị nguồn, transform/nonuniform scale, thứ tự clip/effect/opacity, isolation, premultiplied alpha/color space, finite kernel support/tail tolerance và ROI propagation. P23 CPU reference là oracle cho adapter; P24 không reorder graph để batch GPU nếu đổi kết quả. Preview downsample không đổi source và export không đọc preview cache.
- Nếu P21/P25 thêm Settings/status UI, task riêng phải ghi feature spec theo UI standard; lựa chọn UI không thuộc gói dispatch này. Không phát sinh thay đổi registry/shared API ngoài task được scope và consumer review.

## Kiểm tra gói

Chỉ đối chiếu roadmap/catalog, module instructions, source paths, working-tree diff và task leases. Không sửa runtime, build hoặc test. Gói này là kế hoạch giao việc, không phải bằng chứng GPU/effect/FPS đã hoạt động.
