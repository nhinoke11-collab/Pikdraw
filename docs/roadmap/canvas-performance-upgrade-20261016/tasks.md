# Catalog task triển khai

Trạng thái tất cả: **planned, chưa dispatch**. Có 41 task P00–P37 và P11A/P15A/P16A (ba card bổ sung ở cuối). Các task có `Chia nhỏ` phải được coordinator tách thêm trước khi giao model nhỏ. Dependency là gate evidence, không chỉ code đã tồn tại. Xem [runbook](agent-runbook.md) và [architecture](architecture.md).

Mọi task có common requirements: same-source lease; bounded context; semantic invariants; native runner; tests thật sau restoration; raw evidence + source/binary hash; review độc lập. Exact tests/evidence/CMake paths do coordinator cấp trước claim. Path chưa tồn tại là proposed.

## P00 — Khóa source baseline và phục hồi hạ tầng test có scope

- Vai trò: `coordinator`; module: `harness`.
- Dependencies: không có.
- Kết quả: Inventory source/test bị xóa, map target→test source và kế hoạch phục hồi có scope trước mọi native gate.
- Production paths đề xuất: `docs/quality/canvas-test-inventory.md`; `CMakeLists.txt`.
- Chia nhỏ: P00a inventory docs; P00b các restoration task exact paths từ inventory; P00c CMake integration. CMakeLists chỉ sửa nếu inventory chứng minh cần.

Các bước:

1. Ghi source snapshot, dirty/untracked/deleted files và binary hash; đọc board/runner, không reset.
2. Đối chiếu git/evidence 63/63 để liệt kê exact test files cần phục hồi cho document/render/vector/storage/workers/harness; mỗi nhóm restoration là subtask lease riêng.
3. Khôi phục trong các subtasks đã cấp đường dẫn; coordinator ghép CMake targets tuần tự; ghi tồn tại và lệnh chạy thực.

Bằng chứng nghiệm thu:

- Doctor actual dependency/ownership; harness test chỉ sau khi source có thật.
- Smoke build Debug/Release qua runner; phát hiện assertion bị disable hoặc missing tests.
- Fail nếu chỉ cài placeholder tests hoặc dùng kết quả 63/63 cũ làm pass mới.

## P01 — Thu timestamps input, render và present thật

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P00`.
- Kết quả: Trace event/frame IDs có clock thống nhất, report metric thiếu rõ.
- Production paths đề xuất: `apps/desktop/src/performance/canvas_trace.cpp`; `apps/desktop/src/performance/canvas_trace.hpp`; `apps/desktop/src/shell/native_window_events.cpp`; `core/rendering/include/pikdraw/rendering/backend.hpp`.
- Chia nhỏ: P01a trace hooks; P01b external collector tại tooling/performance theo lease mới; P01c run evidence.

Các bước:

1. Thêm ring buffer bounded bật/tắt, QPC timestamp input/dequeue/paint/render; dump nền ngoài UI.
2. Nối present collector phù hợp backend, kiểm PID/surface/frame correlation; counter CPU/GPU/memory/I/O.
3. Chạy binary Release canonical; ghi build hash/metadata và overhead instrumentation on/off.

Bằng chứng nghiệm thu:

- Ảnh frame-ID hoặc marker trace chứng minh input→display mapping; không lấy paint return làm present.
- 3 lượt on/off; overhead mục tiêu <3%; overflow log có count.
- Báo p50/p95/p99/max và missing reason; native automation thiếu thì không đóng input gate.

## P02 — Corpus deterministic và oracles độc lập

- Vai trò: `implementer`; module: `contracts`.
- Dependencies: `P00`.
- Kết quả: Manifest S0–S8 có counts/hash/seed, oracle geometry/color/hierarchy và traces.
- Production paths đề xuất: `docs/quality/canvas-corpus-v1.json`; `docs/quality/reference-fixtures/canvas-semantics.json`; `tooling/performance/canvas_fixture_manifest.py`.
- Chia nhỏ: Generator/render reference chia subtasks theo module; không giao toàn S0–S8 cùng lúc.

Các bước:

1. Khóa workloads từ acceptance; mỗi scene có sparse/all-overlap và camera traces.
2. Tạo generator/fixtures trong test subtree được lease riêng, hash geometry/assets/font rights; chọn reference renderer/analytic shapes.
3. Khai báo supported subset và stress; goldens do reviewer tạo/duyệt độc lập implementation.

Bằng chứng nghiệm thu:

- Cùng seed tái tạo cùng semantic hash; counts/megapixels khớp manifest.
- Fixture catches winding/alpha/group/bitmap unsupported; không giảm workload để pass.
- S8 actual bytes/chunks có entropy và >32-bit offset, không dùng file sparse thay workload.

## P03 — Chốt budgets, semantics và metric contracts

- Vai trò: `coordinator`; module: `contracts`.
- Dependencies: `P01`, `P02`.
- Kết quả: Contracts versioned cho ChangeSet/frame/asset/jobs, thresholds và semantic vectors được consumer review.
- Production paths đề xuất: `contracts/c2-document.md`; `contracts/c3-rendering.md`; `contracts/c5-storage.md`; `contracts/c6-workers.md`; `docs/quality/canvas-performance-gates.json`.
- Chia nhỏ: Chia P03a frame/change set; P03b asset/residency; P03c metrics+gates, review tuần tự shared contracts.

Các bước:

1. Đọc baseline mới; chốt workload/hardware envelope và ngưỡng proposed hoặc ghi fail, không hạ ngưỡng để làm đẹp.
2. Khóa lifetime/order/stale semantics và fields của các DTO trong architecture; color/alpha policy có producer-consumer vectors.
3. Phân biệt source, derived preview, runtime residency; ghi decisions/ADR mới bằng task path riêng nếu đổi kiến trúc.

Bằng chứng nghiệm thu:

- Consumer document/render/vector/storage/desktop đọc cùng vectors.
- Test swap revisions, undo, close/reopen, DPI/profile/backend generation.
- Không có contract renderer types đi vào file/plugin source.

## P04 — Khóa dependency placement và API seam

- Vai trò: `coordinator`; module: `contracts`.
- Dependencies: `P03`.
- Kết quả: Interfaces và integration map cho injection provider/executor, tránh dependency vòng.
- Production paths đề xuất: `registry/modules.json`; `contracts/c3-rendering.md`; `core/document/include/pikdraw/document/assets/asset_provider.hpp`; `core/rendering/include/pikdraw/rendering/frame_pipeline.hpp`.
- Chia nhỏ: Headers/API trước, wiring CMake sau; không implement engine trong task này.

Các bước:

1. Đặt source DTO/provider interface trung lập ở document; renderer consume qua injection, raster sản xuất assets.
2. Khóa ownership render/context, UI/HDC, worker publish; thiết kế adapter cũ→mới có flag.
3. Liệt kê consumers cần migrate và CMake edits; registry chỉ đổi khi direction có lý do được review.

Bằng chứng nghiệm thu:

- Dependency graph không module→UI, document→render, rendering→raster/workers.
- Consumer compile probes và lifetime/stale tests.
- Không bỏ old renderer path trước khi G2 qua.

## P05 — Asset bất biến dùng chung thay pixel inline

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P04`.
- Kết quả: Image metadata dùng AssetRef; adapter v2/v3 giữ khả năng đọc/ghi subset cũ.
- Production paths đề xuất: `core/document/include/pikdraw/document/model/object.hpp`; `core/document/include/pikdraw/document/assets/image_asset.hpp`; `core/document/src/assets/image_asset.cpp`; `core/document/src/storage/object_codec.cpp`.
- Chia nhỏ: P05a asset type; P05b model/codec; P05c workflow/renderer consumer migration, từng task cấp exact paths.

Các bước:

1. Tách immutable source pixels/original bytes khỏi transform/crop/clip.
2. Dedup theo content identity; pin asset lifetime theo snapshot; clone metadata không copy pixels.
3. Migrate validation/legacy codec consumers qua subtasks có scope, giữ full-data legacy path đến khi v4 có writer.

Bằng chứng nghiệm thu:

- Duplicate/transform ảnh lớn 100 lần không nhân 100 bản pixel; measure allocations.
- Undo/redo và save sau xóa object vẫn có asset sống theo snapshot.
- V2/v3 image round-trip giữ bytes/metadata và controlled error cho source chưa hỗ trợ.

## P06 — ChangeSet và visual bounds có revision

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P04`.
- Kết quả: Publication gồm root+change set; bounds cũ/mới đầy đủ.
- Production paths đề xuất: `core/document/include/pikdraw/document/snapshots/change_set.hpp`; `core/document/src/document.cpp`; `core/document/src/commands/transaction_writer.cpp`; `core/document/include/pikdraw/document/snapshots/document_snapshot.hpp`.
- Chia nhỏ: Bounds geometry helpers tại core/geometry theo subtask lease nếu cần; không nhét render types vào document.

Các bước:

1. Khai báo geometry/style/transform/order/asset changes; capture before/after IDs/bounds.
2. Compute bounds nặng ngoài publication; snapshot pin bảo đảm lifetime.
3. Undo/redo cũng tạo revision mới và change set; coalesce transaction không drop deletes.

Bằng chứng nghiệm thu:

- Move/delete/undo invalidate vùng cũ và mới; ID reused không nhận stale changes.
- Nonuniform scale/miter/group/clip và later effect extent có vectors.
- Commit p95≤2 ms mục tiêu; đo prepare/validation/publication riêng.

## P07 — Index không gian theo snapshot với delta

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P06`.
- Kết quả: Packed R-tree + bounded delta/tombstones, query đúng snapshot.
- Production paths đề xuất: `core/document/include/pikdraw/document/spatial/spatial_index.hpp`; `core/document/src/spatial/spatial_index.cpp`.

Các bước:

1. Bulk build immutable R-tree nền; leaf IDs/source+visual bounds/order metadata.
2. Maintain delta insert/update/remove đến khi replacement index có đúng base.
3. Expose bounded query và stats; pathological candidates không được drop.

Bằng chứng nghiệm thu:

- So indexed result với linear oracle S1/S2/S3, random edits/undo.
- Không miss object mới, deleted không xuất hiện khi bulk job trả muộn.
- Đo query/build/update/memory và all-overlap worst case; không chỉ sparse benchmark.

## P08 — Tích hợp index vào culling, hit-test và snap

- Vai trò: `implementer`; module: `vector`.
- Dependencies: `P07`.
- Kết quả: Culling/pick/snap dùng cùng revisioned scene metadata, narrow phase chính xác.
- Production paths đề xuất: `core/rendering/include/pikdraw/rendering/culling.hpp`; `modules/vector/src/selection/hit_test.cpp`; `modules/vector/src/selection/region_selection.cpp`; `modules/vector/src/precision/snap/snap_index.cpp`.
- Chia nhỏ: Chia render culler và vector selection/snap thành consumer subtasks tuần tự.

Các bước:

1. Thay linear broad phase bằng index adapter, giữ render/selection order.
2. Giữ exact source geometry narrow phase và DIP tolerance; giới hạn work với pending result semantics.
3. Đo point/window/crossing/snap riêng, hỗ trợ giant path acceleration qua task P16.

Bằng chứng nghiệm thu:

- Indexed vs linear hit/topmost/region oracle; locked/hidden/clipped/group cases.
- No stale snap sau delete/undo/zoom; selection không dùng mesh LOD.
- Latency gates S1/S2 và trường hợp overlap có diagnostic/pending đúng.

## P09 — Memory governor và lifetime accounting

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P05`, `P06`.
- Kết quả: Shared budget/reservations cho source, history, assets, jobs và cache adapters.
- Production paths đề xuất: `core/document/include/pikdraw/document/resources/resource_budget.hpp`; `core/document/src/resources/resource_budget.cpp`; `core/document/include/pikdraw/document/history/undo_history.hpp`.
- Chia nhỏ: P09a governor; P09b history/cleanup consumers, CMake wiring riêng.

Các bước:

1. Reserve bytes trước allocation/job; count live/pinned/retired bytes và worker quotas.
2. Thiết lập pressure policy/lease caps; cleanup ngoài UI, GPU disposal qua render owner interface.
3. History eviction có policy minh bạch; không silently drop user history/source.

Bằng chứng nghiệm thu:

- Inject caps 64/128/256 MiB; reservations không overflow, pinned không bị evict.
- Undo/save/export snapshot pin vẫn hợp lệ sau current delete.
- 100 vòng edit/undo/cleanup plateau theo acceptance; đo cleanup stalls.

## P10 — Mailbox render và scheduler ưu tiên tương tác

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P04`, `P06`, `P09`.
- Kết quả: UI input tách khỏi render nặng, latest view mailbox bounded.
- Production paths đề xuất: `apps/desktop/src/runtime/frame_scheduler.hpp`; `apps/desktop/src/runtime/frame_scheduler.cpp`; `apps/desktop/src/runtime/composition_root.cpp`; `apps/desktop/src/shell/native_window_events.cpp`.
- Chia nhỏ: P10a scheduler unit; P10b shell integration; giữ old flag rollback.

Các bước:

1. UI cập nhật view/tool, enqueue latest render request; document commands giữ thứ tự.
2. Render owner/CPU workers tạo completed frame; UI fallback chỉ present hoàn chỉnh.
3. Worker priority/cancel token; giới hạn 1 pending frame+1 in-flight, cap compute tổng.

Bằng chứng nghiệm thu:

- UI không wait render/I/O/fence; stress resize/close/undo khi job còn chạy.
- Down/up/Escape/IME không mất do coalesce; drawing sample fidelity.
- Input latency S1/S2, queue depth/stale age không tăng theo thời gian.

## P11 — Retained scene và dependency versions

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P05`, `P06`, `P10`.
- Kết quả: Display list/subtree cache theo source dependency.
- Production paths đề xuất: `core/rendering/include/pikdraw/rendering/scene/render_scene.hpp`; `core/rendering/src/scene/render_scene.cpp`; `core/rendering/src/renderer.cpp`.

Các bước:

1. Build scene từ snapshot; giữ layer/group composite order và root lease.
2. Apply change set incremental, giữ geometry/style/transform/effect versions riêng.
3. Expose damage dependencies và immutable draw records; not global revision cache miss every edit.

Bằng chứng nghiệm thu:

- Single-object edit không rebuild unrelated scene; count/cache stats.
- Group opacity/order/clip output bằng baseline/reference.
- Stale scene builder trả muộn không thay newer scene.

## P12 — Damage propagation đúng theo composite

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P11`, `P11A`.
- Kết quả: Dirty regions bao phủ old/new bounds và các dependency đọc nền.
- Production paths đề xuất: `core/rendering/src/scene/damage_tracker.cpp`; `core/rendering/include/pikdraw/rendering/scene/damage_tracker.hpp`.

Các bước:

1. Union before/after visual bounds, expand stroke/effect ROI.
2. Propagate hierarchy/order/clip/backdrop dependency đến composite boundary.
3. Coverage threshold full redraw theo measured cost; giữ oracle full-frame.

Bằng chứng nghiệm thu:

- Delete/move/effect parameter/order/clip không để pixel cũ.
- Compare damage render với full render, random edit sequences.
- Counter dirty coverage/false-positive area; correctness quan trọng hơn minimum rect.

## P13 — Tile cache có halo và budget

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P09`, `P12`.
- Kết quả: Tiles 256/512 px thử nghiệm, eviction bounded và priority visible.
- Production paths đề xuất: `core/rendering/include/pikdraw/rendering/cache/tile_cache.hpp`; `core/rendering/src/cache/tile_cache.cpp`; `core/rendering/src/cache/tile_scheduler.cpp`.

Các bước:

1. Implement keys theo scene deps/scale/DPI/profile/quality/backend generation.
2. Allocate tile+halo, render ROI, crop compose; dirty invalidate chính xác.
3. LRU bytes/cost, pin visible frame; reserve trước enqueue; benchmark cỡ tile.

Bằng chứng nghiệm thu:

- Seams/alpha/clip/large stroke và nested effects vectors placeholder tới P24.
- Cache hit/miss và memory pressure không evict đang dùng, no thrash loop.
- Small edit không redraw whole viewport; full coverage case không chậm hơn baseline vô lý.

## P14 — Pan/zoom và transform preview rồi refine

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P08`, `P10`, `P13`.
- Kết quả: Pan/zoom/drag phản hồi bằng tiles/preview; commit một lần.
- Production paths đề xuất: `apps/desktop/src/shell/native_window_canvas.cpp`; `apps/desktop/src/shell/native_window_selection.cpp`; `apps/desktop/src/runtime/interaction_preview.cpp`; `apps/desktop/src/runtime/interaction_preview.hpp`.

Các bước:

1. Reproject cached content khi camera đổi, render exposed tiles theo priority.
2. Preview selected subtree đúng order, overlay cùng displayed camera; không vẽ trùng source.
3. Settle generation/refine scheduling; pointer-up transaction, Escape cancel.

Bằng chứng nghiệm thu:

- S1/S2 displayed-frame+input trace đạt G1, coverage/age recorded.
- Rotate/scale/group transparency/clip correctness; no stale result sau cancel.
- Một gesture một undo; source coordinates không đổi bởi preview.

## P15 — Bitmap decode/mip/tiles bất đồng bộ

- Vai trò: `implementer`; module: `raster`.
- Dependencies: `P05`, `P09`, `P10`.
- Kết quả: Asset provider có visible tile/mip residency và decode queue bounded.
- Production paths đề xuất: `modules/raster/include/pikdraw/raster/image_pipeline.hpp`; `modules/raster/src/image_pipeline.cpp`; `modules/raster/src/mip_cache.cpp`.
- Chia nhỏ: P15a provider/mip; P15b worker decoder adapter ở platform/workers có lease riêng + C6 vectors; P15c integration.

Các bước:

1. Giữ original bytes, decode metadata trước; ROI codec-supported hoặc scanline disk cache.
2. Mip pyramid theo color/alpha policy; requests visible ưu tiên và cancel token.
3. Decode third-party parsers trong worker process qua adapter task riêng; không bypass containment.

Bằng chứng nghiệm thu:

- S4 không full decode mọi ảnh; allocation peak trong quota, pressure/cancel.
- ICC/orientation/crop/transparent edges đúng; missing source diagnostic.
- 100 asset instances share content; decode failure không crash/half-commit host.

## P16 — Path LOD/cache và giant-path narrow phase

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P08`, `P11`.
- Kết quả: Adaptive flatten/tessellation cache và segment acceleration giữ source.
- Production paths đề xuất: `core/rendering/src/path_adapter/authoring_path_adapter.cpp`; `core/rendering/src/raster/path_flatten.cpp`; `core/rendering/src/cache/path_cache.cpp`; `modules/vector/src/selection/hit_geometry.cpp`.
- Chia nhỏ: Render cache và vector narrow phase là hai subtasks; topology changes cần geometry review.

Các bước:

1. Screen-space tolerance theo scale bucket, subtract double origin trước float.
2. Giữ winding/holes/open stroke và analytic source; giant path chunking không đổi fill.
3. Source segment hierarchy phục vụ narrow phase; cache bounded bằng governor.

Bằng chứng nghiệm thu:

- S3 100k/1m segments, far origin, self-intersections/reflection.
- Semantic hash/node IDs không đổi qua zoom/LOD/save.
- Settled edge error/tolerance và hit tests theo exact oracle; no arbitrary truncation.

## P17 — Software raster giảm copy và parallel tiles

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P13`, `P16`.
- Kết quả: CPU fallback native pixel format, reusable buffers, tile workers bounded.
- Production paths đề xuất: `core/rendering/src/software_rasterizer.cpp`; `core/rendering/include/pikdraw/rendering/frame_target.hpp`; `core/rendering/src/raster/path_paint.cpp`; `apps/desktop/src/shell/native_window_canvas.cpp`.
- Chia nhỏ: Target layout contract review trước consumer changes; SIMD chỉ sau profiler.

Các bước:

1. Thêm explicit channel/alpha layout target, adapter BGRA nếu phù hợp; bỏ swizzle thừa.
2. Reuse scratch/surface pool theo budget; tiles độc lập, composition giữ order.
3. SIMD optional runtime feature probe và scalar reference; test concurrency 1/2/N capped.

Bằng chứng nghiệm thu:

- RGBA/BGRA alpha/endian/stride negative and boundary vectors.
- S1/S2 software present/input budgets, compare before-after same hash metadata.
- No per-frame full buffer allocations khi warm; worker concurrency không starve UI.

## P18 — Spike GPU candidate và quyết định backend

- Vai trò: `coordinator`; module: `rendering`.
- Dependencies: `P03`, `P04`, `P17`.
- Kết quả: ADR candidate pin + capabilities/performance matrix, chọn go/no-go.
- Production paths đề xuất: `docs/decisions/canvas-gpu-candidate.md`; `core/rendering/src/gpu/gpu_probe.cpp`; `core/rendering/src/gpu/gpu_probe.hpp`.
- Chia nhỏ: Build/vendor config cần exact paths lease do coordinator cấp sau inventory; không tải dependencies không pin.

Các bước:

1. Thử Skia Ganesh+ANGLE/D3D11 qua facade trên native surface; inventory revision/compiler/license.
2. Đo S1/S4/S5 subset supported và shader/color/alpha/text/filter correctness.
3. So software/candidate; nếu fail ghi nguyên nhân chọn alternate bằng ADR, không mở rộng implementation hàng loạt.

Bằng chứng nghiệm thu:

- Present capture thực, GPU/VRAM/driver compatibility trên available machines.
- Init failure/device-loss smoke; không suy support từ nhãn GPU/API docs.
- Ghi missing hardware và corpus, chưa coi qualification toàn bộ.

## P19 — GPU backend và resource ownership

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P18`, `P11`, `P13`, `P15`, `P15A`, `P16A`.
- Kết quả: Backend được chọn render scene/tiles/assets, context owner và generation đúng.
- Production paths đề xuất: `core/rendering/src/gpu/gpu_backend.cpp`; `core/rendering/src/gpu/gpu_backend.hpp`; `core/rendering/src/gpu/gpu_resources.cpp`; `core/rendering/src/renderer.cpp`.

Các bước:

1. Implement facade adapter theo candidate ADR; immutable draw commands.
2. Upload assets từng batch, fence lifetime, bounded atlas/cache; không readback whole frame.
3. Handle resize/device loss/generation và CPU fallback dispatch theo policy.

Bằng chứng nghiệm thu:

- GPU vs CPU/reference corpus bitmap/vector/group/color.
- Resources giải phóng đúng owner; close/tabs/reset không UAF/leak.
- Frame/queue/GPU timing residency S1/S4, actual uploads bounded.

## P20 — Native present tích hợp và frame pacing

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P19`, `P10`.
- Kết quả: Hiển thị GPU trực tiếp trên window, CPU fallback không chặn input.
- Production paths đề xuất: `apps/desktop/src/shell/native_window_events.cpp`; `apps/desktop/src/runtime/native_presenter.cpp`; `apps/desktop/src/runtime/native_presenter.hpp`; `apps/desktop/src/runtime/composition_root.cpp`.

Các bước:

1. Wire native surface/swap lifecycle; HWND owned UI, GPU owned render contract.
2. Vsync/pacing và queue depth theo caps; không busy loop khi idle/minimized.
3. Overlay/UI compositor đồng bộ displayed camera và frame generation.

Bằng chứng nghiệm thu:

- Displayed present/input correlation P01, không chỉ GPU submit FPS.
- DPI/resize/minimize/sleep/monitor switch/focus/IME.
- G2 timing S1/S2; idle không render vô hạn.

## P21 — Capability fallback và device failure policy

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P20`, `P09`.
- Kết quả: Mode chọn theo probe, recoverable loss fallback và diagnostics bounded.
- Production paths đề xuất: `core/rendering/src/gpu/backend_policy.cpp`; `core/rendering/include/pikdraw/rendering/backend_policy.hpp`; `apps/desktop/src/runtime/composition_root.cpp`.

Các bước:

1. Collect capabilities/driver/build, explicit force software, one-shot recovery.
2. Invalidate GPU generation, giữ CPU/source assets, reconstruct software resources.
3. Fatal driver policy/restart safe mode có evidence; nếu yêu cầu isolation cần coordinator spike riêng.

Bằng chứng nghiệm thu:

- Inject init failure/loss/OOM; no source loss, stale handles rejected.
- Software forced runs, graphics state survives tabs and save.
- Không báo seamless fatal driver recovery chưa đo; bounded retries.

## P22 — Khóa EffectStack source và color/ROI semantics

- Vai trò: `coordinator`; module: `document`.
- Dependencies: `P03`, `P05`, `P11`.
- Kết quả: Effect schema Gaussian Blur/DropShadow source và reference vectors.
- Production paths đề xuất: `core/document/include/pikdraw/document/effects/effect_stack.hpp`; `contracts/c3-rendering.md`; `contracts/c4-color-typography.md`; `contracts/c5-storage.md`.

Các bước:

1. Định nghĩa units/sigma/order/transform/clip/isolation/premul/color policies.
2. Bounds/ROI lan ngược qua graph và finite kernel support/tail tolerance.
3. Unknown required effects, serialization migration và non-destructive undo contract.

Bằng chứng nghiệm thu:

- Analytic/reference sigma=0/large, offsets, nonuniform scale, group alpha.
- Producer UI/document/render/storage cùng effect vectors.
- No SkImageFilter serialized/source flatten.

## P23 — Gaussian blur CPU reference và GPU adapter

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P19`, `P22`.
- Kết quả: Blur theo semantics, CPU reference separable và GPU adapter quality levels.
- Production paths đề xuất: `core/rendering/src/effects/gaussian_blur.cpp`; `core/rendering/src/effects/gaussian_blur.hpp`.

Các bước:

1. Implement finite-support two-pass reference; halo/edge modes explicit.
2. GPU/filter adapter pin; large sigma preview downsample with declared quality.
3. Budget scratch/tiles and cancel at bounded units; settled full-quality path.

Bằng chứng nghiệm thu:

- Compare reference trên impulse/step/transparent color/edges/anisotropic sigma.
- S5 CPU/GPU timing; tile seam diff với full frame.
- Preview không sửa sigma source, quality refinement không nhận stale parameters.

## P24 — Drop shadow và effect graph cache/ROI

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P23`, `P12`, `P13`.
- Kết quả: Shadow alpha/tint/offset đúng và effect nodes cached theo dependency.
- Production paths đề xuất: `core/rendering/src/effects/drop_shadow.cpp`; `core/rendering/src/effects/effect_graph.cpp`; `core/rendering/src/effects/effect_cache.cpp`.

Các bước:

1. Alpha mask→blur→tint→offset→composite; reuse mask/blur khi chỉ đổi tint/offset hợp lệ.
2. Graph ROI backward propagation, halo stack, clip/isolation/premul order.
3. Invalidate dependency/background, latest generation previews and refinement.

Bằng chứng nghiệm thu:

- Nested effects across tiles, shadows outside original bounds, group opacity only once.
- Color/offset-only updates không rerun unrelated expensive nodes.
- S5 100 effect nodes, memory limit, no seam/ghost after edit/delete.

## P25 — Lệnh geometry nặng bất đồng bộ có cancel

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P06`, `P09`, `P10`.
- Kết quả: Boolean/offset chuẩn bị nền, single atomic commit và strict stale rejection.
- Production paths đề xuất: `apps/desktop/src/workflows/document_workflow.cpp`; `apps/desktop/src/runtime/geometry_jobs.cpp`; `apps/desktop/src/runtime/geometry_jobs.hpp`; `core/document/src/async/proposal_validator.cpp`.
- Chia nhỏ: Nếu workflow monolith cần refactor, tách prepare job và shell integration thành subtasks scope nhỏ.

Các bước:

1. Đo synchronous hotspots; move prepare/compute/validation lớn vào bounded jobs.
2. Token doc/epoch/base/generation, cancellation và progress; UI ack nhanh.
3. Writer rechecks then publishes root/history/change set once; large cleanup ngoài UI.

Bằng chứng nghiệm thu:

- 100/1k/10k path command traces: ack/cancel/compute/commit riêng.
- Edit/undo/delete/close giữa job: không half-apply, reject stale.
- Same geometry/topology/source semantics và undo oracle baseline.

## P26 — UI preview/effect/performance theo chuẩn

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P14`, `P21`, `P24`, `P25`.
- Kết quả: Phiếu UI hoàn chỉnh rồi action bindings/properties/status dùng surfaces sẵn.
- Production paths đề xuất: `docs/design/features/canvas-performance-effects.md`; `apps/desktop/src/shell/native_window_menus.hpp`; `apps/desktop/src/shell/native_window_shortcuts.cpp`; `apps/desktop/src/shell/native_window_settings.cpp`.
- Chia nhỏ: Các file settings/locales/icons/properties thật phải xác nhận và cấp lease từ inventory; card không cho sửa path ngoài danh sách. Chia 26a spec, 26b view prefs, 26c effect properties.

Các bước:

1. Inventory action IDs, actual settings/properties files; hoàn thiện template từ architecture section 10.
2. Implement Auto/Full view quality, Performance preferences, Blur/Shadow properties và job status/cancel.
3. Mixed selection, draft slider preview, one undo, catalog labels/icons/help, focus/IME.

Bằng chứng nghiệm thu:

- Keyboard/mouse path cùng action ID; no-selection/locked/mixed/busy/error matrix.
- Dark/light, 100/150/200% DPI, narrow UI; source hash không đổi khi quality mode đổi.
- Effect edit undo/redo/cancel, parameters persisted sau P32; chưa có writer không claim round-trip.

## P27 — Spike format v4: chunked container đối chứng SQLite

- Vai trò: `coordinator`; module: `document`.
- Dependencies: `P03`, `P05`, `P22`.
- Kết quả: ADR format/layout, numeric bounds, migration và durability policy khóa.
- Production paths đề xuất: `docs/decisions/native-v4-container.md`; `contracts/c5-storage.md`; `docs/quality/native-v4-layout-vectors.json`.
- Chia nhỏ: P27a spec; P27b two bounded prototypes in assigned tests; P27c decision. Temporary prototype paths được cấp riêng, không alternate app binary.

Các bước:

1. Implement bounded prototypes bằng subtasks riêng; so chunked candidate vs SQLite trên S8 cold/warm/read/save/cancel.
2. Measure metadata-first open, streaming peak, unchanged chunks, copy portability/WAL and failure points.
3. Chọn một format và chunk size/codec/version/required flags; lock endian/64-bit offset/CRC/hash layout và golden byte fixtures.

Bằng chứng nghiệm thu:

- >4 GiB offset test, overlap/overflow/decompress bomb/unknown required version.
- Source semantic fidelity và file portability, extra memory/I/O amplification.
- ADR go/no-go dựa raw numbers, không đổi v3 layout in-place.

## P28 — V4 chunk I/O và streaming reader/writer

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P27`.
- Kết quả: Storage primitive theo format ADR, bounded random read và streaming encode.
- Production paths đề xuất: `core/document/src/storage/v4/chunk_io.cpp`; `core/document/src/storage/v4/chunk_io.hpp`; `core/document/src/storage/v4/container_directory.cpp`; `core/document/include/pikdraw/document/storage/v4_container.hpp`.

Các bước:

1. Parse header/directory có size/count/offset checks trước allocation.
2. Read ranges/decompress chunks with per-chunk cap/checksum, cancel/job budget.
3. Stream encode/copy unchanged source chunks, no giant payload vectors.

Bằng chứng nghiệm thu:

- Golden byte fixtures + fuzz bounded malformed ranges/CRC/codecs.
- Streaming scratch≤64 MiB target; >4 GiB offsets round-trip.
- Error partial files không được marked success; orphan temporary handled safely.

## P29 — Lazy source pages, asset hydration và object registry

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P28`, `P05`, `P07`, `P09`.
- Kết quả: Metadata-first document view với immutable source leases và pending commands.
- Production paths đề xuất: `core/document/include/pikdraw/document/storage/source_residency.hpp`; `core/document/src/storage/source_residency.cpp`; `core/document/src/storage/v4/object_pages.cpp`.
- Chia nhỏ: Residency contract đã khóa P04/P27; thay snapshot API cần consumer migration subtasks.

Các bước:

1. Load manifest/IDs/order/bounds metadata, page fetch visible prioritized.
2. Hydrate source/assets bằng generation token, pin chunks, verify authoritative data.
3. Commands cần nguồn trả Pending; snapshot/export/save đọc pinned consistent source, no mixed revisions.

Bằng chứng nghiệm thu:

- S8 first preview/editable/full-ready timestamps; no full hydrate on open.
- Close/reopen/save during hydrate, stale responses rejected.
- Source semantic hash same eager load; directory hint corrupt không lừa production bounds.

## P30 — Migration v2/v3 sang v4 và unsupported policy

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P28`, `P29`.
- Kết quả: Read legacy giữ nguyên, Save As v4 có semantic round-trip.
- Production paths đề xuất: `core/document/src/storage/v4/legacy_migration.cpp`; `core/document/src/storage/v4/legacy_migration.hpp`; `core/document/src/storage/native_format.cpp`.

Các bước:

1. Map objects/path IDs/font/colors/assets/effects to v4; preserve original bytes khi có.
2. Write temporary, reopen validate before success; original legacy không overwrite mặc định.
3. Unknown required feature refuse/read-only; optional opaque preservation; downsave capability report.

Bằng chứng nghiệm thu:

- V2/v3 fixtures multi-object/group/path/color/image exact semantic compare.
- Save/read fail leaves original intact; invalid/unsupported legacy rejected.
- No generated new node IDs for existing v3 authoring source, no silent flatten.

## P31 — Journal/checkpoint/durability và bounded recovery

- Vai trò: `implementer`; module: `document`.
- Dependencies: `P28`, `P30`.
- Kết quả: Recovery có commit records/flush policy và streaming atomic save.
- Production paths đề xuất: `core/document/src/storage/v4/recovery_journal.cpp`; `core/document/src/storage/v4/checkpoint.cpp`; `core/document/src/storage/file_io.cpp`; `core/document/src/storage/save_recovery.cpp`.
- Chia nhỏ: I/O primitives và journal separate tasks; reviewer owns independent fault oracle.

Các bước:

1. Pin snapshot, temp same-volume streaming save, flush/validate/replace; revision dirty rule.
2. Journal sequence/base hash/checksum/commit marker, batch drafts, bounded checkpoint/compaction.
3. Nếu append-only incremental save không qua crash oracle, giữ temp-replace baseline; không sửa format tùy ý.

Bằng chứng nghiệm thu:

- Kill/fault every write/flush/replace/checkpoint boundary, truncated tail.
- Old or new valid state; save r never clears r+1; missing disk/permissions/quota.
- Writer locks, journal retention, compaction no deleted pinned chunks; measured durability window.

## P32 — Open/save/import/export nền và asset linkage

- Vai trò: `implementer`; module: `desktop`.
- Dependencies: `P29`, `P30`, `P31`, `P25`, `P26`.
- Kết quả: UI file operations asynchronous với progress/cancel, consistent source output.
- Production paths đề xuất: `apps/desktop/src/shell/native_window_files.cpp`; `apps/desktop/src/workflows/document_workflow.cpp`; `apps/desktop/src/runtime/document_io_jobs.cpp`; `apps/desktop/src/runtime/document_io_jobs.hpp`.
- Chia nhỏ: Import/export adapter changes ngoài desktop dùng subtasks đúng module; fidelity claims chỉ cho fixtures actual.

Các bước:

1. Open metadata-first, preserve active document until new source valid; cancel hydration safely.
2. Save/export fixed snapshot, background encode; never use low-quality preview tiles for output.
3. Self-contained default, linked asset opt-in provenance/missing relink; source rasterization/export unsupported reports.

Bằng chứng nghiệm thu:

- Edit while save r, close/cancel/reopen, no dirty-state loss.
- S8 first visible/editable/save CPU/memory/I/O and UI latency under load.
- Effects/assets/source semantic round-trip; export doesn't silently drop unsupported effects.

## P33 — Adaptive quality và hardware budget policy

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P17`, `P21`, `P24`, `P32`.
- Kết quả: Policy phản hồi dựa frame/memory pressure với hysteresis và refinement bounded.
- Production paths đề xuất: `core/rendering/src/quality/quality_controller.cpp`; `core/rendering/include/pikdraw/rendering/quality_controller.hpp`; `core/rendering/src/gpu/memory_budget_probe.cpp`.

Các bước:

1. Use recent frame/input deadlines, CPU/GPU budget and pressure, không theo brand.
2. Giảm prefetch/background resolution trước selected-object precision; reserve shared RAM accounting.
3. Hysteresis tránh rung chất lượng; idle refinement deadlines và progress/cancel.

Bằng chứng nghiệm thu:

- Artificial slow GPU/CPU, pressure 64/128/256 MiB cache, iGPU UMA accounting.
- No unbounded LOD decay; coverage/age/settled thresholds S1–S5.
- Auto/Full toggle không mutate source/export, thermal profile recorded.

## P34 — Full feature correctness và consumer semantic audit

- Vai trò: `reviewer`; module: `contracts`.
- Dependencies: `P16`, `P20`, `P24`, `P30`, `P32`, `P33`.
- Kết quả: Independent review G1–G4 + actual dependency/path checks.
- Production paths đề xuất: `docs/reviews/canvas-upgrade-correctness.md`.
- Chia nhỏ: Nếu foundation text/group/bitmap chưa đủ, coordinator tách fixes theo exact source paths; G2/G3 chưa pass.

Các bước:

1. Compare source snapshot/diffs and contracts; run shared vectors + goldens độc lập.
2. Audit real image/text/group rendering; unresolved foundation gaps cần correction tasks owned paths riêng trước pass.
3. Validate edit→undo→save→reopen→export, stale jobs and color/profile/source IDs.

Bằng chứng nghiệm thu:

- Không accepting unsupported demo vì exit 0; ảnh bitmap thật và glyph outlines phải hiện đúng.
- No circular dependencies, ownership escapes, full-frame copies hidden.
- Mỗi fail có minimal fixture và task sửa; reviewer không tự nới oracle.

## P35 — Hardware matrix, pressure và recovery qualification

- Vai trò: `reviewer`; module: `rendering`.
- Dependencies: `P34`.
- Kết quả: Máy/driver/backend/quality matrix với raw frame/input/resource evidence.
- Production paths đề xuất: `docs/reports/canvas-upgrade-hardware.md`.

Các bước:

1. Chạy S0–S6 3×60s trên Intel/AMD iGPU 8GB và dGPU available; forced software.
2. Run device-loss/init fail/OOM/sleep/DPI/resize/multiple tabs.
3. Measure CPU/GPU/private/shared RAM/VRAM/I/O; missing machine explicit.

Bằng chứng nghiệm thu:

- Absolute performance gates và regression vs same-machine baseline.
- No FPS derived from CPU time; verify displayed/input matching.
- Recovery preserve source, bounded retries; fatal failure policy honest.

## P36 — Soak, file faults và starvation audit

- Vai trò: `reviewer`; module: `document`.
- Dependencies: `P34`, `P35`.
- Kết quả: 30 phút/2 giờ interactive soak, S7/S8 pressure/fault results.
- Production paths đề xuất: `docs/reports/canvas-upgrade-soak-storage.md`.

Các bước:

1. Replay edit/undo/open/close/save/effect traces thật; measure live retention/queues.
2. Fault injection file writes/journal/checkpoint/worker crashes/cancel races.
3. Audit cold/warm file performance, memory plateau, recovery window and interleaved saves.

Bằng chứng nghiệm thu:

- No OOM/hang/source loss; per-run p99 stalls/coverage/quality reported.
- Durability old/new source oracle, corrupt file reject bounded.
- No leaks beyond declared plateau; instrumentation overhead accounted.

## P37 — Integrated release gate, flags và rollout

- Vai trò: `coordinator`; module: `desktop`.
- Dependencies: `P35`, `P36`.
- Kết quả: G5 report trên canonical binary tích hợp, default flags chỉ bật phần đã qualify.
- Production paths đề xuất: `docs/project-state.json`; `docs/reports/canvas-upgrade-release.md`.
- Chia nhỏ: CMake/default feature flag edits nếu cần dùng task exact paths riêng; docs status chỉ sau validation.

Các bước:

1. Freeze source snapshot, rebuild Release canonical incremental qua lock, run final relevant gates.
2. Record exact hashes/configs/dependencies/unsupported matrix; default flags rollout theo machine envelope.
3. Update project-state implemented có evidence; rollback rendering giữ source, v4 migration vẫn bảo toàn originals.

Bằng chứng nghiệm thu:

- No stale binary evidence; smoke and final integrated benchmarks on shipped config.
- Every task/dependency accepted or explicit scope exclusion, G0–G5 status.
- Missing hardware/effects/present không được tuyên bố general no-lag.

## P11A — Hierarchical group traversal và composite correctness

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P11`.
- Kết quả: Group transform/order/visibility/clip traversal thực, không trả unsupported cho subset đã khóa.
- Production paths đề xuất: `core/rendering/src/scene/group_traversal.cpp`; `core/rendering/src/scene/group_traversal.hpp`; `core/rendering/src/software_rasterizer.cpp`.
- Chia nhỏ: Nếu cần thêm group opacity/clip source schema, coordinator cấp subtask C2/C5+object model trước consumer; task traversal không tự thêm field.

Các bước:

1. Duyệt hierarchy có depth/cycle validation, compose transforms đúng thứ tự và emit ordered draw records.
2. Group isolation/opacity theo schema được P03 khóa; source model chưa hỗ trợ field phải có migration task, không giả có sẵn.
3. Reuse retained scene, bounds gồm descendants; loại trừ double-render child nằm trong group.

Bằng chứng nghiệm thu:

- Nested group depth 8, reflection/nonuniform transform, hidden/locked ancestors, clipping và duplicate references.
- CPU output so independent flattened-reference fixture mà source vẫn group editable.
- Selected group move/undo/culling không mất hoặc vẽ đôi child.

## P15A — Bitmap raster thật và sampling/ICC/clip

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P15`, `P17`, `P11A`.
- Kết quả: Software renderer vẽ ImageObject qua asset tiles; demo ảnh thật không còn placeholder unsupported trong subset.
- Production paths đề xuất: `core/rendering/src/raster/image_paint.cpp`; `core/rendering/src/raster/image_paint.hpp`; `core/rendering/src/software_rasterizer.cpp`.

Các bước:

1. Inverse-transform sample visible tiles/mips qua AssetProvider được inject, không decode trong paint.
2. Implement sampling/alpha/ICC/orientation/crop/clip theo vectors; pending tile có trạng thái và retry generation.
3. Map legacy inline image adapter và asset mới; GPU P19 dùng cùng semantic fixtures.

Bằng chứng nghiệm thu:

- Actual bitmap reference pixels, rotated/cropped images, transparent edge halos và ICC test.
- S4 resident memory bounded, missing assets có diagnostic đúng; không biến pending thành source mất.
- Demo vector-bitmap hiển thị nội dung đã hỗ trợ và test tiled-vs-full image diff.

## P16A — Cache text layout/outlines và bounds theo glyph thật

- Vai trò: `implementer`; module: `rendering`.
- Dependencies: `P16`, `P17`, `P11A`.
- Kết quả: Tái dùng DirectWrite shaper/outlines hiện có, cache đúng font/source và dùng bounds thật cho render/index.
- Production paths đề xuất: `core/rendering/src/cache/text_cache.cpp`; `core/rendering/src/cache/text_cache.hpp`; `core/rendering/src/software_rasterizer.cpp`; `core/document/src/spatial/text_bounds.cpp`.
- Chia nhỏ: Text bounds provider placement phải qua P04 injection semantics; không import rendering vào document.

Các bước:

1. Xác minh PIKDRAW_TYPOGRAPHY_REAL_OUTLINES của actual build; reuse TextLayout/text_to_path_outlines hiện có.
2. Cache layout/outlines theo text/font/features/size, tách transform; chuyển Y-axis tại boundary đúng một lần.
3. Text visual bounds cập nhật index đúng revision; font fallback warning không rewrite source font/text.

Bằng chứng nghiệm thu:

- S6 NFC/NFD tiếng Việt, glyph O holes, combining clusters, alignment cases đã supported.
- Zoom/drag không shape lại toàn text khi source/font không đổi; eviction/font changes invalidate.
- Unsupported RTL/mixed-font/justify theo typography contract báo rõ; không quảng cáo full typography.


