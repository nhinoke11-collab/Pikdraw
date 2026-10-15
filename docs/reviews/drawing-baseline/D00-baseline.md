# D00 — Baseline kiểm kê cho roadmap công cụ vẽ

Ngày kiểm tra: 2026-10-15  
Người kiểm tra: Codex coordinator  
Trạng thái: đã kiểm tra độc lập; chọn baseline code cho workstream, chưa tích hợp vào `main`.

## Quyết định baseline

Dùng commit `970273dc35a98cf9fdf4f7e8f3d3f48b5c9fd836` trên nhánh
`agent/codex/editor-pick-style-delivery-20261015` làm baseline code sạch cho các task drawing kế tiếp.
Commit này là hậu duệ của `ed5bd9c4f0dcb9b8c8bef749863e052c3d299419` và chứa 10 commit sau base đó,
bao gồm các sửa document/vector/storage/rendering cần cho pick, selection và style. Roadmap nằm trên
nhánh tài liệu `agent/codex/drawing-tools-plan-20261015` ở `5d65dba97585633760839803e8f68e03c9a5c1fa`.
Hai phần có thể được ghép trên một nhánh tích hợp sạch vì phần roadmap chỉ thêm tài liệu; chưa ghép vào
checkout `main`.

Checkout `main` hiện có 11 file desktop đã sửa nhưng chưa commit, một thư mục `cdr/` chưa theo dõi và
các ảnh tham khảo UI chưa theo dõi. Tôi không đọc nội dung `cdr/`, không sửa, xóa, stash hoặc commit các
thay đổi này. Nhiệm vụ tích hợp cuối phải giữ chúng và giải quyết phần desktop trùng nhau trước khi
đưa kết quả vào `main`.

## Năng lực đã xác nhận trên baseline code

Nhánh `970273d` có đường thao tác native cho pick theo hình học, resize handle, Window selection trái
sang phải, Crossing selection phải sang trái, multi-select, move/resize có preview và một bước undo,
hủy bằng Escape/capture loss/stale revision, chỉnh fill/stroke/opacity/độ dày, bảng màu RGB/CMYK,
lưu/mở native v2 và preview ICC trên Windows. Các kiểm tra consumer bao gồm hit geometry, input native,
pixel renderer, dữ liệu lưu/mở lại và undo/redo.

Các ảnh kiểm chứng đã có trong worktree của nhánh:

- `build/editor-evidence-final/pick-style-dark.png`
- `build/editor-evidence-final/pick-style-light.png`
- `build/editor-evidence-final/window-selection.png`
- `build/editor-evidence-final/crossing-selection.png`

Ảnh được tạo từ đường paint của app native và có hash trong
`apps/desktop/tests/evidence/editor-tools-20261015/artifact-sha256.json`. Đây chưa phải bằng chứng
Windows present/input-to-visible hay ảnh chụp desktop OS.

## Kiểm tra tôi vừa chạy lại

Chạy từ worktree `codex-editor-pick-style-delivery-20261015` sau khi nạp MSVC x64 Developer Shell.
Source manifest có 92 file; cả 92 SHA-256 đều khớp với source checkout.

| Lệnh/kiểm tra | Kết quả |
| --- | --- |
| `cmake --build --preset win-msvc-debug --target pikdraw --parallel 2` | Pass; Ninja báo không có việc cần build |
| `ctest --test-dir build/win-msvc-debug --output-on-failure` | 33/33 pass, 42.68 giây |
| `cmake --build --preset win-msvc-release --target pikdraw --parallel 2` | Pass; Ninja báo không có việc cần build |
| `ctest --test-dir build/win-msvc-release --output-on-failure -R 'icc_preview|storage_fidelity|rendering_pixels|editor_selection|editor_tools'` | 5/5 pass, 1.32 giây |
| SHA-256 executable Release | `d064e50ebbe1a57016f7f6d64de6af29288fe7993411bdfef8c6b48ee55c4578` |
| SHA-256 ảnh dark pick/style | `89012b61c3a8021a7dce65dc409c61686bb3dd2262d30ac7a51710c136466da5` |

Ảnh và build hash đầy đủ nằm trong manifest của nhánh. Kết quả CTest xác nhận các binary hiện có trên
nhánh này; build báo `no work to do`, tức không có source mới được biên dịch trong lần chạy này.

## Khoảng trống còn lại so với roadmap

Baseline chưa có Pen/Node editor, Polyline liên tục, Pencil/Curvature, ToolSession chung, numeric HUD,
snap masks và profile phím, Ortho/Polar, bắt tâm đồng tâm, polygon đều có tham số, Trim/Extend/Fillet/
Chamfer, transform/array chính xác, đo kích thước liên kết hay route 2D. Geometry Boolean/Offset hiện
chưa là kernel topology đầy đủ. ICC hiện là preview cục bộ; chưa có soft proof, gán/nhúng profile tài
liệu hoặc xuất PDF/X. Nhóm và text bounds còn giới hạn.

Báo cáo benchmark của nhánh ghi software CPU source redraw p95 khoảng 36.9–41.6 ms cho corpus
1.000 path/101.000 path elements. Đây chưa đạt 60 FPS; chưa đo present interval, input-to-visible,
p99 60 giây, peak RAM hoặc ma trận Intel/AMD/RTX. Không dùng kết quả này làm chứng nhận hiệu năng.

## Trạng thái nguồn sự thật

`docs/START.md` còn câu “chưa có editor”, trong khi `docs/project-state.json` mô tả M1 complete nhưng
đồng thời liệt kê “No runnable graphics editor”. Hai tài liệu này không đồng nhất và không thay thế
kiểm thử executable. Không cập nhật trạng thái dự án trong D00; chỉ cập nhật ở task tích hợp sau khi
code được hợp nhất và kiểm chứng lại.

## Bước tiếp theo

Ghép roadmap và báo cáo này lên một nhánh coordinator sạch bắt đầu từ `970273d`, rồi tạo D01 để khóa
semantics path/node, numeric input, snap, modifiers, shape parameters, storage/versioning và consumer
vectors. Các task runtime tiếp tục trên nhánh sạch đó. Trước tích hợp cuối, coordinator phải bảo toàn
11 file desktop đang sửa ở `main`, ghép thủ công phần UI chồng lấn và chạy lại acceptance trên đúng
merged SHA. Không đánh dấu kế hoạch hoặc tính năng khác là đã triển khai chỉ dựa trên task catalog.
