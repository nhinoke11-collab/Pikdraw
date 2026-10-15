# Thực thi ba spike — 15/10/2026

## Mốc mới nhất — tiếp tục sau GitHub publication

Main hiện ở `8f792202f63f44c3797a845b31986a126b7eed5b`, Apache-2.0; source spike vẫn trên task branches riêng. Quyền Git/mạng/CMake đã dùng được; các đoạn lịch sử dưới ghi giới hạn quyền của lượt trước, không phải blocker hiện hành.

- B: private paged COW root/index và SourcePath shared segments/cached bounds; iterator giữ root, empty-chunk pruning, native restore single publish. **26/26 native tests qua**, gồm 20.000 random map operations với oracle và history bound 500 qua Undo/Redo.
- Release benchmark trên i7-3537U 2C/4T, RAM visible 8.254.744 KiB, Windows 10 Pro 19045, Balanced: cùng 20.000 empty path objects metadata + một path 100.001 segments, 64 transforms/65 retained roots. Baseline p95 25,3186 ms/private retained 582.148.096 bytes; candidate p95 0,0636 ms/private retained 20.619.264 bytes. Cleanup 181,184 → 7,7696 ms. Một baseline/final run; không có renderer/UI/corpus production hoặc acceptance threshold.
- C: **8/8 local protocol/process groups qua**. Live AppContainer đã chạy đủ năm probes với positive controls: bốn explicit OS denials; mạng timeout 10060. Full test **exit 1**, containment gate **fail**. Không quy timeout hoặc worker crash thành permission pass. Test-only event whitelist và SEH shim cho strict invalid-handle fault; không nới quyền OS/global firewall.
- Gemini 3.8 Flash High đã review B và C độc lập qua CLI. Các lần code generation đầu timeout/empty SUCCESS không được nhận là delivery; một request bị từ chối vì `--effort` không hỗ trợ cho model có mức High trong tên, đã retry cùng model và bỏ tham số đó. Leader viết implementation, xử lý findings và kiểm thử; không tuyên bố nghiệm thu độc lập sau toàn bộ sửa.
- Findings B: xử lý iterator ownership, prune chunks, restore single publication và ràng buộc mapped value. History không unbounded: tổng hai stacks đã bị giới hạn bởi transfer; regression 500/500 xác nhận. Dirty policy bảo thủ là giới hạn UX, không đổi sang content identity chưa có contract.
- Findings C: sửa positive handle control, fail-launch cleanup, Job handle release, trustee type và allocation guard. Response budget vốn được ValidateHeader kiểm; thêm explicit check cho dễ audit. Cancellation drain có rủi ro kernel hang còn mở; không bỏ wait trên stack OVERLAPPED vì sẽ tạo UAF.

Artifacts nhỏ có thể review trong branches: `core/document/reports/cow-measurement-20261015.json`, `platform/workers/reports/live-containment-20261015.json`. Raw compiler/run/input hashes trong ignored out của các lane. Chưa có Qt/Skia viewport/hardware evidence, importer corpus/license/fidelity hoặc full containment acceptance; không mở post-spike phase.

## Lịch sử lượt đầu (trước publication)

Trạng thái: **bản nháp native đã build/test một phần; chưa spike nào được nghiệm thu**. Chưa dispatch implementation sau spike. Main vẫn ở `655b5ab6b4b635c06ed7be3b798a20b88db4b5be` và sạch; runtime source nằm trong bốn task worktree, chưa tích hợp.

## Kết quả thực tế

| Nhánh | Đã kiểm chứng | Gate còn thiếu |
| --- | --- | --- |
| A | C++20 camera-relative math, transform, buffer bounds và fixtures nhiều/vài path trên 100.000 điểm; native tests qua | Qt/Skia chưa build; raster oracle, text/image/effects, GPU/software present/input, recovery, memory và Intel/AMD onboard/RTX chưa đo |
| B | **21/21 native tests**; 10.000 commit seed 42 với oracle trạng thái riêng; immutable snapshots, reader/writer stress, atomic rejection, undo/redo, Unicode save/reopen và fault trước replace | Map/index vẫn copy O(n), path clone còn deep copy; chưa đo prepare/commit/retention/cleanup; corpus topology/units và kill/checkpoint/durability còn thiếu; chưa sanitizer hoặc review nghiệm thu độc lập |
| C | **8/8 nhóm kiểm tra cục bộ**; codec bounded, actual child ping/pong, unsupported import, stalled-write deadline, truncated-response exit | Không có parser thật/corpus 20–30 file; chưa import-edit-save-reopen-export, audit license hoặc live AppContainer canaries; quota enforcement chưa được chứng minh |
| Điều phối | Doctor ok, 15 modules; **7/7 leader control tests**; harness 27 tests: **26 qua, 1 skip** do thiếu quyền symlink; scope/header dependency audit qua | Chưa có integrated SHA cho source mới hoặc consumer/independent acceptance review |

Các build native dùng Clang C++20 Debug, static linking, `-Wall -Wextra -Wpedantic -Werror`. Không suy FPS hoặc latency production từ thời gian chạy test. Trace B có tối đa 64 object sống và giữ 10 snapshot; không phải benchmark 100.000 object.

## Phản biện đã chuyển thành sửa mã

- Chặn external mutable alias sau Insert; reader lấy root bằng atomic shared_ptr operations. Không cam kết lock-free. Transaction chỉ publish root/index sau khi toàn bộ batch thành công; lỗi giữa batch rollback.
- Proposal kiểm cả document ID, epoch theo lifetime, revision, generation, cancellation, null/duplicate/unknown IDs. Proposal là một undo unit; Undo/Redo luôn tăng revision. Load phục hồi revision và không cấp lại ID đang tồn tại.
- Curve approximation không làm mất zero-chord loop hoặc collinear overshoot; Close được giữ, multi-contour/số không hợp lệ/budget hết bị từ chối. Rectangle subset kiểm số hữu hạn; lỗi Boolean không xóa output cũ. General path Boolean vẫn chưa hỗ trợ.
- Native decoder có bound checks trước đọc byte/count, checksum IEEE CRC32, giới hạn file/payload; save dùng sibling temp, stream flush + FlushFileBuffers rồi replace. Python struct/zlib kiểm envelope độc lập. Chưa có bằng chứng mất điện/journal/checkpoint.
- Parent pipe chuyển sang overlapped I/O: deadline bao gồm write lẫn read. Timeout/response sai kết thúc worker scope. Launch fail closed khi AppContainer/attribute/job assignment thất bại; không xóa profile có từ trước. Stderr không trộn binary stdout.
- Bỏ fake import tạo hình từ input bất kỳ, khai báo SVG support, corpus/hash giả và bảo đảm license không có cơ sở. Mọi adapter hiện bị từ chối rõ. Full test C trả **exit 1** khi năm denied canaries chưa chạy; protocol-only pass không đóng containment gate.
- A kiểm tràn pixel-buffer và miền tọa độ, inverse hữu hạn; UI báo lỗi preview, có buffer cap 64 MiB. Skia path projection áp transform trước camera; text/stroke transform chưa hỗ trợ bị từ chối. Device-loss không nhận mã giả làm kết quả đo.

## Agent và provenance

Antigravity CLI **1.3.5** đã kết nối model thật `gemini-3.8-flash-high`. Ba source-generation lane chạy riêng; lần đầu RunCommand bị headless từ chối dù CLI trả SUCCESS. Leader không nhận empty/denied SUCCESS, không đổi global policy, chuyển sang generation thuần rồi tự review/apply/build.

CLI có lần trả `structured_output` cũ/khác JSON trong response. Leader đã đối chiếu, chọn ứng viên response đầu tiên có source tương ứng và ghi `source_selection` vào ignored review-candidate.json; không last-wins/dedupe âm thầm. Control validator nay chặn hai output bất đồng trước khi chọn source. Source model đã được leader sửa đáng kể; đây chưa là independent acceptance review sau sửa.

Các tiến trình Gemini đã kết thúc; không có agent đang chạy nền hoặc network scheduler đã triển khai. `registry/team.json` trong leader worktree là metadata của phiên, không phải bằng chứng tự động hóa transport.

## Máy và giới hạn môi trường

- Windows 10 Pro 10.0.19045, RAM 7,87 GiB; Intel HD Graphics 4000 driver 10.18.10.4252 và AMD Radeon HD 8500M/8700M driver 27.20.1034.6. AMD này là GPU rời, không thay mẫu AMD onboard. Không có RTX evidence.
- Cache riêng có LLVM-MinGW **20261006 / Clang 23.1.3**, CMake 3.31.10, Ninja 1.13.2. Compiler archive SHA256: `317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a`.
- Profile quyền hiện tại chỉ cho ghi workspace/temp; `.git` chỉ đọc, network bị hạn chế. CMake cache bị Access denied; compiler vẫn chạy được nên đã dùng direct compiler runner trên source được phép. Không sao chép executable bị chặn, đổi ACL hoặc thêm Git safe.directory để vượt kiểm soát.
- Linked checkout Git báo dubious ownership; audit đọc blob metadata từ primary repo được phép và so byte source trực tiếp, chấp nhận LF/CRLF theo checkout. Không đổi trust. Đây là scope/header scan, không thay kiểm link/integration.
- Chưa có Qt/Skia SDK tương thích; LLVM-MinGW libc++ không được link lẫn prebuild MSVC/MinGW libstdc++. AppContainer profile/ACL mutations ngoài workspace chưa được chạy trong profile này.

## Đường dẫn tiếp tục

| Task | Worktree | Phạm vi |
| --- | --- | --- |
| Leader | `.worktrees/codex-spike-leader-bootstrap-20261015` | Root build/contracts, runners, kiểm gate và báo cáo |
| A | `.worktrees/gemini-spike-a-viewport-20261015` | core/rendering, apps/desktop |
| B | `.worktrees/gemini-spike-b-document-20261015` | core/geometry, core/document |
| C | `.worktrees/gemini-spike-c-interop-20261015` | modules/interop, platform/workers |

Mỗi lane giữ `out/spikes/{a,b,c}-direct/report.json`, build/test logs, SHA256 executable và hash toàn bộ C++/headers đã compile. C có worker hash và full-gate-probe.json; B có independent-native-envelope.json. Leader giữ review-audit.json, foundation-draft-receipt.json và foundation-gate-check.json trong out/spikes. Receipt hiện bị chặn đúng thiết kế: thiếu integrated SHA/reviewer và mandatory evidence, không phải ps00 đã pass.

Chạy từ checkout chính, dùng Python thực tế thay alias Python nếu cần:

```powershell
$py = 'C:/Users/CK ART STAGE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$runner = '.worktrees/codex-spike-leader-bootstrap-20261015/tooling/spikes/direct_native.py'
& $py -X utf8 $runner --lane B --source-root .worktrees/gemini-spike-b-document-20261015 --toolchain-root .agent/toolchains/llvm-mingw-20261006-ucrt-x86_64
```

Thay lane/source-root tương ứng để chạy A math hoặc C local protocol/process tests. `--mode test` chỉ nhận binary khớp source/header hashes của lần build trước. A command này không build Qt/Skia. Test C mặc định, không `--protocol-only`, hiện phải thất bại tại containment chưa đo.

## Bước tiếp theo bắt buộc

1. B mở rộng benchmark sang nhiều path thực, topology/units và tải renderer/UI kết hợp; bổ sung async schedule, kill/fault recovery và sanitizer. Giữ oracle độc lập, lặp phép đo trước khi đặt ngưỡng.
2. A cần toolchain Qt/Skia tương thích ABI, build GUI/raster thật, rồi đo present/input/recovery/memory trên Intel onboard 8 GB, AMD onboard 8 GB và RTX theo `foundation-spikes.md`.
3. C cần chẩn đoán nguyên nhân network timeout để chứng minh explicit denial, kiểm tra resource quotas, bổ sung fixtures hợp pháp cùng parser/import-edit-save-reopen-export và license/fidelity report. Giữ parser/plugin untrusted tắt tới acceptance.
4. Sau các sửa, mỗi lane cần independent review và formal handoff. Tích hợp rồi chạy lại source, consumer vectors và quality gates trên cùng integrated SHA; hiện tại chưa có integrated runtime SHA.
5. Chỉ sau khi mọi mandatory gate được reviewer chấp nhận mới hoàn thành `ps00-accept-spikes` và dispatch catalog sau spike. Main/project-state vẫn ghi runtime là chưa triển khai.
