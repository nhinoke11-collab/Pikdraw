# ADR 0012 — Gradient là fill paint có version

Ngày: 17/10/2026. Trạng thái: **Proposed — chỉ đặc tả, chưa triển khai hoặc bật capability**.
Plan: [Gradient](../roadmap/gradient-20261017.md), [UI](../design/features/gradient.md),
[AC](../quality/gradient-acceptance.md). Kế thừa [C5](../../contracts/c5-storage.md),
[C9](../../contracts/c9-appearance-effects.md), [document runtime](../architecture/document-runtime.md).

## Bối cảnh

PathObject hiện có fill ColorValue và Appearance/effects; renderer tô một màu trong covered spans.
Native v4 lưu source color và Appearance, chưa có Gradient record. Cần sửa Gradient như tài nguyên
vector editable, đồng bộ preview/command/save/render và không biến nó thành dữ liệu riêng của UI/importer.
Stack runtime vẫn C++20/Win32/GDI; không dùng task này để đổi graphics toolkit.

## Quyết định đề xuất

1. Gradient là paint của Path fill, không là node Gaussian/Shadow hay bitmap bake. V1 có Linear và
   Radial centered ellipse, stop/midpoint/spread/source color có semantics version 1.0 của PikDraw.
   Công thức/mapping được pin trong plan; tên tương tự ứng dụng khác không chứng minh tương đương.
2. Migration bridge giữ solid ColorValue hiện có và thêm immutable nullable GradientPaint pointer.
   `effective_fill_paint()` là authority theo has_fill → gradient → solid; remembered solid không dùng
   làm visual fallback khi renderer/exporter không hỗ trợ Gradient. G09 audit tất cả consumer trước enable.
3. Source gradient_to_object affine binary64 và stop IDs ổn định; snapshots chia sẻ immutable value;
   commands batch atomic/single writer. Clone object remap paint/stop IDs; Undo giữ IDs.
4. Renderer dùng một prepared sampler và cùng màu/alpha semantics cho direct, float scratch,
   Properties ramp và canvas preview. UI/interop không có renderer mutable riêng. Preserve nguồn
   CMYK/spot; session preview sRGB không phải print/separation proof.
5. Native v5 có required paint records/version và vẫn đọc v2/v3/v4. Không ghi extension Gradient vào
   v4 mà bản cũ có thể bỏ qua. Old reader reject v5 rõ; native save không hứa AI/CDR source round-trip.
6. Import từng format có adapter riêng → canonical paint sau validation C8/C9; V1 SVG subset có
   gates riêng, PDF/AI/CDR/EPS chưa có adapter vẫn unsupported. Unknown không fallback solid.

## Phương án đã xét

- **Dùng filter node hoặc rasterize ngay:** không giữ được stop/geometry editability và rối thứ tự
  fill opacity/effect bounds; không chọn làm representation nguồn.
- **Thay toàn bộ fill/stroke/Text bằng tagged PaintSource ngay:** model tổng quát hơn, nhưng mở rộng
  consumer/migration ngoài scope đầu tiên. Hoãn; bridge phải có accessor và consumer audit bắt buộc,
  không chấp nhận hai authority mâu thuẫn.
- **GDI GradientFill làm renderer riêng:** không bao phủ tự nhiên stop/midpoint/ellipse/ICC semantics
  và float scratch hiện có; không chọn làm authority. Tối ưu sau phải qua cùng scalar oracle.
- **Ghi Gradient trong v4 bằng solid fallback:** rủi ro silent data loss khi bản cũ mở/lưu; không chọn.

## Hệ quả và điều kiện chấp nhận

Bridge có thêm quy tắc ownership, clone và validation; source format version mới làm bản cũ không mở
file mới. Đổi lại source data không biến mất âm thầm. Không bật UI trước storage/renderer/clone/style
audit. G00 phải cập nhật C5/C7/C9 trong task có scope, nhận consumer review của document/rendering/
vector/interop/desktop và pin giới hạn/vectors; G01–G10 cung cấp evidence theo đúng source snapshot.

ADR này chưa sửa C5/C9 runtime, không chứng nhận fidelity/UX/performance và không cho phép task
planning bắt đầu triển khai. Chuyển Accepted khi G00 hoàn tất review hợp đồng và ghi evidence;
triển khai/qualification vẫn được báo riêng theo mốc native và SVG.
