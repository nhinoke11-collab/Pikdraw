# ADR 0010 — Cổng nghiệp vụ cho AI agent, MCP và COM

Ngày: 17/10/2026. Trạng thái: **Implementation in progress; release acceptance open**.
Task nguồn: `ai-agent-mcp-com-plan-20261017`; consumer tasks `ai-automation-a05-20261017`,
`ai-agent-settings-ui-runtime-20261017`, `ai-agent-a06-com-20261017`.

## Vấn đề

AI agent cần đọc tài liệu, tạo và sửa thiết kế, kiểm tra kết quả, lưu/mở lại bằng
dữ liệu có cấu trúc. Tự động hóa qua tọa độ màn hình không đáp ứng yêu cầu này.
Runtime được đối chiếu là C++20/Win32/GDI; không đợi chuyển Qt/Skia để làm automation.

## Quyết định đề xuất

1. Xây API nghiệp vụ typed, dùng chung command/transaction, validation và snapshot
   với editor. Không gọi trực tiếp handler chuột, HWND, menu hoặc sửa model từ RPC thread.
2. MCP là giao diện chính cho AI agent. Bản đầu dùng sidecar `pikdraw-mcp.exe`
   qua stdio; sidecar kết nối đúng instance desktop qua named pipe nội bộ có xác thực.
   Named pipe không được quảng bá là transport MCP chuẩn cho client.
3. COM là adapter Windows local-server bổ sung, dùng cùng MCP tool definitions/service
   và policy. ProgID `PikDraw.Automation.1`; user đăng ký/gỡ qua HKCU. Không bắt mọi MCP
   client đi qua COM. COM ở đây là Component Object Model, không phải cổng serial COM.
4. Giữ một writer logic trên application thread, strict revision/epoch/generation,
   atomic batch, IDs ổn định và một Undo unit cho mỗi batch thành công.
5. Đọc trạng thái và kiểm chứng bằng query/snapshot/preflight có version. Render buffer
   có thể dùng làm oracle tự động; không bắt agent chụp cửa sổ hoặc nhìn ảnh để thao tác.
6. Capability chỉ công bố hàng đã có handler và evidence phù hợp. Action ID hoặc
   declaration trong header không tự chứng minh automation, format fidelity hay production.

Chi tiết: [C10](../../contracts/c10-agent-automation.md) và
[kế hoạch triển khai](../roadmap/ai-agent-mcp-com-20261017.md).

## Ranh giới phụ thuộc

`apps/desktop` compose service và các core/module hiện hữu. API DTO/descriptor không
chứa HWND/GDI/parser types. Lớp adapter platform/COM không phụ thuộc ngược vào desktop;
desktop inject interface/dispatcher. SDK MCP và COM type library không đi vào core.
`apps/com-server` phụ thuộc MCP adapter để dùng chung tool dispatch, session và authorization;
ứng dụng vẫn chỉ có một document writer. Nó không link desktop UI vào COM process và không
gọi trực tiếp command handlers.

## Lựa chọn đã cân nhắc

| Lựa chọn | Kết luận |
| --- | --- |
| MCP gọi lại mọi click/menu | Không giải quyết semantic targeting, tham số và kiểm chứng |
| Mọi kết nối bắt buộc qua COM | Thêm registration/marshalling và phụ thuộc Windows vào MCP |
| MCP server chạy trong editor | Có thể làm, nhưng tăng coupling SDK/network và rủi ro parser trong process editor |
| Sidecar + service chung + COM adapter | Chọn cho spike; phải đo latency, lifecycle và chi phí đóng gói |
| HTTP mở ngay từ đầu | Hoãn; local stdio đáp ứng luồng chính, HTTP có gate auth/Origin riêng |

## Hệ quả và điều kiện nhận

Cần tách dần workflow đang phụ thuộc global selection sang lời gọi có target IDs;
không bọc một loạt hàm hiện hữu rồi gọi đó là atomic batch. Sidecar làm tăng một binary
và protocol IPC cần versioning. Crash sidecar không được làm mất tài liệu hoặc lặp edit.

ADR chuyển Accepted sau consumer review document/vector/rendering/interop/desktop/workers,
spike end-to-end trên app chuẩn và kiểm concurrency/retry/revoke. COM source đã có
independent STA/MTA activation/tool smoke và test register/unregister trong HKCU; release
acceptance vẫn cần COM parity khi apply/receipt/revoke, package/installed executable và
kiểm nhiều client trong ứng dụng. Không mở DCOM/remote activation. Các gate và stop
conditions ở roadmap là bắt buộc.
