# ADR 0002 — phần cứng, executors và khả năng phục hồi

Ngày gốc: 15/10/2026. Hợp nhất trạng thái hiện hành: 23/10/2026.
Trạng thái: nguyên tắc thiết kế được giữ; performance budgets cần đo trên runtime thực.

## Backend hiện tại

ViewportRenderer dùng Direct2D 1.1 / D3D11 hardware, chuyển sang WARP khi cần;
SoftwareRasterizer dùng làm CPU fallback/oracle. Frame hiện đi qua staging/readback và GDI.
Qt/Skia chưa là backend được phân phối. Không suy backend từ GPU brand hoặc shader model riêng lẻ.

## Quyết định tiếp tục áp dụng

- Render không sửa document; source geometry/production không phụ thuộc LOD hay chất lượng preview.
- UI tránh chờ parse/encode/geometry/save/network. Tách roles UI/writer/render owner/compute/worker
  theo ownership; số thread và executor topology quyết định bằng đo đạc và thread affinity thực.
- Một pending render cho state mới nhất; preview lỗi thời được bỏ, final gesture commit vẫn chính xác.
- Budget gồm cache CPU/GPU, assets, Undo/snapshots, worker IPC và scratch cùng lúc.
  iGPU chia sẻ RAM; số object hoặc kích thước vật lý không đủ mô tả chi phí.
- Từng mode hardware/WARP/software có corpus, latency và memory evidence riêng.
  Thiếu máy hoặc chưa đo phải ghi rõ, không dùng offscreen CPU timing làm visible FPS.
- Device loss có thể phục hồi: hủy device resources, rebuild từ snapshot/CPU assets theo lifecycle
  đã kiểm. Fatal driver crash/hang không được cam kết phục hồi liền mạch.
- Image uploads, path/tessellation, text layout và ICC caches có keys/bounds/eviction rõ.
  Không giải phóng source assets khi save/export/Undo còn giữ snapshot.
- Binary64 world/local coordinates; camera rebasing trước upload float. Sai số sản xuất theo
  C1 và machine profile, không lấy screen-pixel tolerance thay manufacturing tolerance.
- Job Object/resource limits không chứng minh containment. Canary filesystem/network/child
  process phải được kiểm theo sandbox/adapter; untrusted plugin chưa đạt gate thì chưa bật.

## Tích hợp và nghiệm thu

Đọc [AGENTS](../../AGENTS.md) và module registry; sửa trực tiếp, giữ local edits, shared files tuần tự.
Contract changes cần đối chiếu consumers, migrations và fixture semantics; build xanh không tự
chứng minh architecture hoặc tính tương thích định dạng.

Các gói measurement, scheduling, direct presentation, tile/LOD và startup lazy nằm trong
[kế hoạch hiệu năng/storage/MCP](../roadmap/performance-storage-automation.md).
[Quality gates](../quality/gates.md) quy định phạm vi bằng chứng.
Các raw snapshot/baseline prototype cũ không còn là nguồn trạng thái runtime.
