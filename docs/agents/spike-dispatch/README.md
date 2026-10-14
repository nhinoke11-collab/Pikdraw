# Điều hành ba spike native

Người dùng đã giao leader Codex điều phối Antigravity **Gemini 3.8 Flash (High)**, code A/B/C song song, nghiệm thu và tiếp tục catalog khi gates thực sự đạt.

Model được CLI local liệt kê: `gemini-3.8-flash-high`; không thay model hoặc gán provider metadata làm bằng chứng đã gọi agent. Task board/handoff giữ trạng thái thực; leader giữ root build, contracts và integration.

| Task | Owned paths | Target |
| --- | --- | --- |
| spike-a-viewport-20261015 | core/rendering; apps/desktop | pikdraw_spike_a_viewport |
| spike-b-document-20261015 | core/geometry; core/document | pikdraw_spike_b_tests |
| spike-c-interop-20261015 | modules/interop; platform/workers | pikdraw_spike_c_tests |

Mỗi lane có worktree/owner riêng, test/report trong module sở hữu, output dưới out/. Root CMake có thể configure lane checkout ngoài bằng `-DPIKDRAW_SPIKE_SOURCE_ROOT=<lane path>`, với đúng A/B/C flag. Điều này cho phép task branch giữ diff trong scope mà vẫn dùng cùng build/contracts revision của leader.

Đọc [foundation contract](../../../contracts/spikes/README.md) và [gate spike](../../roadmap/foundation-spikes.md). Consumer review contract trước implementation; kết quả chưa đo không pass. Code đi qua reviewer khác người viết và checks trên merged SHA trước done.

Chuyển phase: leader kiểm gate A/B/C, nhận ps00-accept-spikes rồi ps01/ps02 và các task ready trong [catalog](../../roadmap/post-spike-tasks.json). Thiếu máy/corpus/toolchain/containment phải ghi blocker, không hạ gate để tự chuyển.

CLI tham chiếu: [headless](https://www.antigravity.google/docs/cli/headless/), [auth](https://www.antigravity.google/docs/cli/install/). CLI profile/keyring tự quản credentials; không đọc/xuất token hoặc đưa private logs vào Git.
