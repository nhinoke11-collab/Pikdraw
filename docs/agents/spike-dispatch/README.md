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

## Adapter thực tế của phiên này

CLI 1.3.5 xác nhận model nhưng tài khoản không có mode auto; RunCommand bị headless từ chối.
Leader giữ policy, chuyển agent sang **tạo source thuần, không gọi tools**, với contract/module
instructions được host cung cấp. Gemini trả JSON files; leader kiểm schema/scope, review nội dung,
áp dụng vào lane worktree rồi chạy native build/test; commit/handoff cần quyền Git ghi.
Provider SUCCESS có response rỗng hoặc denied_actions không được nhận là source delivery.

Đây là code generation qua model thật và build/test do leader thực hiện, không phải agent có
quyền shell trực tiếp. Không dùng dangerously-skip-permissions hoặc đổi settings toàn cục.
Gemini code generation đã kết thúc; leader nhận lease review và sửa mã. Evidence ghi rõ
vai trò từng bên; không coi review trước khi leader sửa là nghiệm thu độc lập sau sửa.

Native runner: `tooling/spikes/native.ps1` ở worktree leader, `-Lane B/C -SourceRoot <lane>`.
`-Lane A -EntryPoint RenderMath` chỉ kiểm math, không đóng viewport gate. Compiler portable
LLVM-MinGW 20261006/Clang 23.1.3, CMake 3.31.10, Ninja 1.13.2 trong shared .agent/toolchains;
không tương thích C++ ABI với thư viện MSVC/MinGW libstdc++ khác, cần toolchain Qt/Skia phù hợp.

`spike_control.py inspect-generation` chỉ kiểm shape/scope, chưa tự áp dụng hoặc nghiệm thu code.
`check-foundation` chặn receipts thiếu review độc lập, merged SHA, artifact hash hoặc gate pass.
Receipt do leader ghi sau kiểm chứng thực tế; không nhận receipt tự công bố của feature agent.

Profile quyền mới chặn CMake cache và ghi `.git`. `direct_native.py` dùng compiler còn được
phép chạy, source lists rõ và `--toolchain-root` tường minh; không thay Git trust/ACL hoặc
sao chép executable bị chặn. Runner kiểm hash source/header/binary trước test và phát hiện
source đổi giữa build/test. A chỉ là math, C chạy protocol/process smoke trusted-only.

Ba review drafts đang partial, chưa tích hợp. Xem [báo cáo thực thi](../../reports/spike-execution-20261015.md)
để tiếp tục. Không có agent đang chạy nền; harness/catalog chưa phải scheduler.
