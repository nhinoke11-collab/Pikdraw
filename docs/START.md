# Bắt đầu làm việc với PikDraw

1. Đọc [AGENTS](../AGENTS.md), xem Git status và [module registry](../registry/modules.json).
2. Đọc [trạng thái hiện hành](project-state.json), rồi mở source/module và contract đúng yêu cầu.
3. Sửa trực tiếp source gốc, giữ các local edits. Agent tự quyết định lựa chọn thường lệ trong scope.
4. Build/test target liên quan qua tooling/native/build.ps1; nhiều task xem -Status, dùng -Submit/-Wait
   theo [hướng dẫn build](development/native-build.md), báo kết quả và dọn output tạm.
5. Cập nhật tài liệu khi hành vi hoặc giới hạn thực thay đổi. Không cần task board/lease/handoff.

Source: C:/Users/CK ART STAGE/Downloads/PikDraw.
App: build/win-dev/apps/desktop/pikdraw.exe.

| Cần làm | Đọc tiếp |
| --- | --- |
| Hiểu source/runtime | [Kiến trúc](architecture/architecture.md), [tổ chức code](development/code-organization.md) |
| Tối ưu lag/FPS, file lớn, MCP | [Kế hoạch hiệu năng/storage/automation](roadmap/performance-storage-automation.md) |
| Lưu/mở .pr, nén ảnh và asset | [Định dạng PR](architecture/native-pr-format.md), [C5](../contracts/c5-storage.md) |
| Kết nối agent vào app | [Quickstart MCP](development/automation-quickstart.md), [MCP](../apps/mcp-server/README.md), [C10 automation](../contracts/c10-agent-automation.md) |
| Build và dọn output | [Native build](development/native-build.md), [nghiệm thu](quality/feature-acceptance.md) |
| UI hoặc import vector | Chuẩn UI/UX và vector-resource-interoperability trong [chỉ mục](README.md) |

Mọi agent dùng cùng workflow trong AGENTS.md. Đọc tài liệu theo nhu cầu; không nạp toàn bộ roadmap.
