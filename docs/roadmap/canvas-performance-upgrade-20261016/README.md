# Kế hoạch nâng cấp hiệu năng canvas PikDraw

Ngày: 2026-10-16. Đây là kế hoạch nâng cấp; các task P00 về inventory, khôi phục test và refresh một số contract đã chạy. P00d ghi nhận Debug CTest 63/63 trên snapshot tích hợp của task đó; P00c validation đang review và chưa có kết quả Release. Kết quả cũ không tự đại diện cho working tree hiện tại.
Owner kế hoạch: `canvas-performance-upgrade-plan-20261016`. Source kiểm tra: HEAD `11512046d23fac08059ecd37382179283b4530a0` cộng working tree hiện có. Không dùng HEAD đơn lẻ làm định danh binary/source đã sửa.

## 1. Mục tiêu và ranh giới cam kết

Ưu tiên phản hồi khi thiết kế: chọn, kéo, pan/zoom, nhập thông số, gọi lệnh, undo/redo, mở/lưu tài liệu. Vector nhiều node, bitmap lớn, transparency, clip, shadow và blur phải được xử lý bằng dữ liệu dẫn xuất có giới hạn bộ nhớ, công việc nền có thể hủy, và renderer chỉ làm phần cần nhìn thấy.

Không thể cam kết mọi file, mọi hiệu ứng, mọi phần cứng luôn 60 FPS ở chất lượng đầy đủ. Cam kết nghiệm thu theo **máy + corpus + kích thước viewport + mức preview**. Khi vượt ngân sách, app phải tiếp tục nhận input, dùng preview đã công bố và hoàn thiện hình sau khi ngừng thao tác. Không mất source, không tự flatten tài liệu, không âm thầm tắt hiệu ứng. Hoàn thành tính toán nặng có thể mất thời gian; phản hồi bắt đầu/hủy lệnh phải nhanh.

Mục tiêu chính: GPU đã qua kiểm chứng đạt p95 present interval ≤16,7 ms và input-to-visible p95 ≤50 ms tại 1080p/60 Hz; software fallback đạt ≤33,3 ms và ≤100 ms trên corpus được công bố. Đây là **mục tiêu**, chưa là kết quả. Chi tiết ở [acceptance.md](acceptance.md).

## 2. Bằng chứng hiện tại và khoảng trống

| Quan sát từ source/evidence ngày 16/10 | Hệ quả cho kế hoạch |
|---|---|
| `NativeWindow` render đồng bộ khi `WM_PAINT`; camera đổi làm mất cache cả canvas | Tách input, render, present; thêm mailbox và damage/tile cache |
| `SpatialCuller` quét tuyến tính; selection có đường scan và flatten path | Index theo snapshot; broad phase và narrow phase có ngân sách |
| PersistentObjectMap đã là AVL bất biến, `all()` trả view | Giữ cấu trúc hiện có; không viết lại document store chỉ vì tên hàm `all()` |
| `ImageObject::pixels_rgba` là vector inline; sửa object có nguy cơ copy asset lớn | Tách asset bất biến dùng chung khỏi transform/mask/effect metadata |
| Source codec hiện **v3**, đọc v2/v3; header payload length 32-bit; tổng file giới hạn 256 MiB | Kế hoạch native v4 phải có random access, nguồn chunked, migration v2/v3; tài liệu v2 cũ chưa phản ánh source |
| `SaveRecoveryService::load` đọc cả file; serialize tạo payload rồi container | Đo peak memory và chuyển dần sang stream/chunk; không chỉ nâng size limit |
| Software renderer trả unsupported cho Image/Group; Text đã gọi outline thật nếu build có DirectWrite, còn bounds heuristic/cache cần xử lý | P11A/P15A/P16A hoàn thiện group/bitmap và tái dùng typography hiện có trước khi công bố tối ưu cả scene |
| Live build Debug: 20 lượt chụp + cửa sổ idle; chưa có input/present trace | Không dùng thời gian khởi chạy/chụp 338–382 ms làm FPS |
| Bằng chứng Release cũ: 1.000 path/101.000 elements, source redraw p95 35,42–57,05 ms | Baseline lịch sử, cần đo lại binary và workload hiện tại |
| P00b khôi phục 72 file test/helper; P00d ghi nhận Debug CTest 63/63 trên snapshot của task | P00c vẫn review, Release chưa chạy; kiểm tra source/revision trước khi dùng lại kết quả |

Nguồn đối chiếu: [audit](../../reviews/canvas-performance-audit-20261016.md), [runtime report](../../../artifacts/evidence/canvas-performance-runtime-20261016/runtime-evaluation-vi.md), [runtime architecture](../../architecture/document-runtime.md), [ADR 0002](../../decisions/0002-runtime-and-hardware.md). Thông tin RAM phần cứng trong báo cáo cũ bị nhầm đơn vị: 8.452.857.856 byte ≈ **7,87 GiB / 8,45 GB**, không phải 8,45 GiB.

## 3. Bộ tài liệu giao agent

1. [architecture.md](architecture.md): luồng runtime, contracts, thuật toán, bộ nhớ, phần cứng và native v4.
2. [acceptance.md](acceptance.md): corpus, traces, ngưỡng, phương pháp đo và gate chống sai số.
3. [tasks.md](tasks.md): từng task nhỏ, deps, file được sửa, các bước và kết quả phải chứng minh.
4. [tasks.json](tasks.json): cùng catalog dạng dữ liệu để coordinator giao việc; **không phải task board đang chạy hay scheduler**.
5. [agent-runbook.md](agent-runbook.md): prompt dùng ngay, kiểm soát scope, build, review, handoff và xử lý fail.

## 4. Thứ tự triển khai

| Đợt | Task | Kết quả cần đạt trước khi đi tiếp |
|---|---|---|
| A — đo được và khóa semantics | P00–P04 | Baseline Release có trace input/present; semantic vectors và fixtures độc lập; quyết định dependencies |
| B — dọn đường phản hồi và tài nguyên | P05–P10 | Asset không bị copy khi transform; bounds/index đúng revision; event loop không chờ việc nặng |
| C — giảm công việc mỗi frame | P11–P17, P11A/P15A/P16A | Scene/display list, group traversal, bitmap render, text cache, damage, tiles, preview gesture, path LOD/cache và software fast path |
| D — GPU và nền tảng correctness | P18–P21 | Backend qua spike/capabilities; present thực; bitmap/text/group qua fidelity gate; phục hồi đã đo |
| E — hiệu ứng và lệnh nặng | P22–P26 | Shadow/blur không phá source; effect graph có ROI/cache; async geometry; giao diện cùng action/undo |
| F — file lớn và phục hồi | P27–P32 | Chốt format candidate, streaming/random access, migration, journal và save/load nền |
| G — giới hạn tài nguyên và nghiệm thu | P33–P37 | Adaptive quality, soak, fault matrix, hồi quy trên build tích hợp, rollout có rollback |

Đợt là trình tự đọc; dependency chính xác nằm trong catalog. Một số việc như format spike có thể nghiên cứu sớm sau P04/P05; không mở implementation song song chỉ dựa vào bảng này. Mặc định chạy tuần tự. Khi được giao song song rõ ràng, coordinator kiểm deps/leases và chỉ mở các file không chồng lấn.

**Đường ưu tiên giá trị sớm:** P00→P01/P02→P03/P04→P06/P07/P08→P10/P11/P12/P13→P14. Đo lại pan/zoom/drag trước khi làm hiệu ứng mới. Giữ bitmap/source correctness là gate riêng, không trì hoãn đến cuối. P18 spike GPU có thể bác bỏ candidate; không viết hàng loạt adapter trước khi spike qua.

## 5. Chia scope và quyền quyết định

Chia task theo kết quả người dùng, ranh giới code và cách review; không áp trần số file cố định. Agent có thể đề xuất API, schema hoặc cách tổ chức trong scope. Thay đổi contract dùng chung, định dạng lưu, threading hoặc ngân sách tài nguyên cần review consumer và evidence phù hợp. Tách task khi giúp review, giảm xung đột hoặc cho phép tích hợp độc lập; không tách chỉ để đạt một con số. Các nhãn trong catalog mô tả loại công việc, không xếp hạng năng lực agent.
## 6. Điều kiện phát hành

Chỉ công bố mức tải đã đo; xuất bảng từng máy, từng cảnh và preview mode. Phải có dữ liệu raw, ảnh đối chiếu, source snapshot, binary hash, backend/driver và build type. Không đạt một gate thì ghi fail và task sửa; không che bằng average, cảnh nhẹ hoặc FPS submission. File cũ không bị overwrite bằng format mới trước khi migration/round-trip/durability qua kiểm tra.

Đây vẫn là kế hoạch, không phải chứng cứ hiệu năng. P00 evidence nêu trên chỉ áp dụng cho snapshot đã kiểm tra; chưa có số đo input-to-present mới chứng minh đạt các mục tiêu ở đây.

