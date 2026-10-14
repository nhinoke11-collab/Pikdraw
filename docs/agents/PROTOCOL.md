# Giao thức phối hợp agent PikDraw

## Một bộ quy tắc, nhiều nhà cung cấp
Các agent Codex, Claude, Gemini hoặc nhà cung cấp khác đọc cùng AGENTS.md và task packet.
Vai trò được gán theo công việc, không theo tên model:
coordinator chốt phạm vi/contract; implementer sửa module; reviewer kiểm chứng độc lập; integrator hợp nhất.

Adapter không phải API client. Harness hiện là CLI/bảng task cục bộ. Nó chưa tự gọi model,
truyền message qua mạng hoặc xác thực các agent từ xa.

## Cổng giao tiếp hiện tại
Trên cùng máy, mọi worktree của cùng repository dùng bảng `.agent/tasks/` tại checkout chính.
Dùng harness để create/claim/update/release và xuất handoff; không ghi trực tiếp task JSON.
Ngoại lệ phục hồi metadata do coordinator xử lý được mô tả ở WORKTREES.md; không dùng ngoại lệ để mở rộng quyền task đang chạy.
Bảng dùng khóa loại trừ và lease owner. Hai task có owned paths chồng lấn không được cùng claim.
Không chiếm lease theo thời gian; coordinator phải xác nhận task đã dừng trước khi giải phóng.

Một task truyền đủ: ID, module, mục tiêu, base revision, owned paths, owner, trạng thái, evidence,
summary và next action. Handoff bổ sung head revision, changed paths, decisions, checks và open questions.
Bản handoff cần dẫn tới commit/worktree và artifact; không chuyển nguyên lịch sử chat.

Ví dụ bàn giao trong worktree của task sau khi commit:

```sh
python tooling/agent_harness.py handoff --task-id vector-offset --owner codex-session-01 --head HEAD --summary "Contour prototype có undo" --test "Geometry fixture và undo regression đã chạy: pass" --next-step "Reviewer kiểm tra sai số và source-preserving behavior"
```

Handoff CLI xuất payload task và ghi artifact cục bộ. Schema envelope `contracts/agent-message.schema.json`
là đặc tả transport tương lai; harness không triển khai JSON Schema validator tổng quát.

## Vòng đời
`todo -> in_progress -> review -> done`; `blocked` dùng khi có blocker được ghi rõ.
Sau handoff, implementer giao quyền review; done chỉ dùng sau tích hợp/kiểm chứng.
Tái mở task đã done phải là quyết định rõ ràng; thông thường tạo task sửa lỗi mới.

## Liên máy / nhà cung cấp khác
Xuất handoff JSON và chuyển các commit qua Git remote được người dùng chỉ định.
Máy nhận phải fetch đúng revision và kiểm tra evidence; runtime board cục bộ không tự đồng bộ.
Transport HTTP/MCP, auth, signature, queue và retry là module tương lai, chưa được triển khai.
Không gán tên provider trong task như bằng chứng một dịch vụ đã kết nối hoặc một agent đã chạy.

## Hợp đồng thay đổi
Shared contracts có task riêng sở hữu `contracts/`, reviewer của module tiêu thụ và ADR khi breaking change.
Schema/API có version; host tương lai phải kiểm tra payload trước khi áp dụng.
Message không tự cấp quyền; coordinator và công cụ vẫn kiểm tra owned paths, base revision và trạng thái.

## Evidence tối thiểu
- Lệnh kiểm tra, kết quả thực tế và đường dẫn artifact nhỏ.
- Những gì chưa kiểm chứng hoặc bị giảm khả năng chỉnh sửa.
- Bước tiếp theo đủ để phiên mới tiếp tục mà không đọc lại chat.
Không ghi test passed nếu chưa chạy; không gắn file chứa bí mật hoặc dữ liệu khách hàng riêng.
