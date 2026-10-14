# ADR 0001 — nền tảng kho mã và phạm vi quyết định stack

Ngày: 2026-10-14. Cập nhật sau thẩm định: 2026-10-15.
Trạng thái: **Accepted cho scaffold/harness và ranh giới dữ liệu; Proposed cho stack ứng dụng, thư viện và schema runtime**.

## Bối cảnh

PikDraw là desktop vector phục vụ quảng cáo, in và thi công; bốn workspace dùng chung một tài liệu. Kho mã có scaffold/harness và đặc tả, chưa có editor, renderer hoặc converter chạy được. Quyết định thiết kế không chứng minh năng lực runtime đã triển khai.

## Quyết định có hiệu lực

1. Dependency có hướng theo `registry/modules.json`; core không phụ thuộc UI, module chỉ dùng public API. Không tạo vòng qua header tiện ích hoặc re-export.
2. Một document model giữ stable IDs, đơn vị, đường nguồn, chữ chỉnh sửa được, metadata màu và tài nguyên. Mọi thay đổi qua command/transaction; render/export/workers chỉ đọc snapshot bất biến. Single writer/revision được mô tả ở [document/runtime](../architecture/document-runtime.md).
3. Geometry nguồn, approximation sản xuất và dữ liệu render là ba biểu diễn riêng. Tolerance hiển thị không quyết định đường CNC hoặc dữ liệu xuất.
4. Import fidelity, editability và source-format export là ba năng lực riêng. Tuyên bố phải gắn format/version/feature với fixture và báo cáo mất dữ liệu. Không âm thầm flatten để vượt gate.
5. Parsers bên thứ ba và plugin ngoài chạy ngoài process. Quyền filesystem/network và OS containment cần kiểm chứng riêng; untrusted plugins chỉ bật sau gate containment, trước đó là trusted-only.
6. Harness Python standard library cung cấp bảng task cục bộ, lease, structure/context/worktree/handoff checks. Agent-message JSON Schema là đặc tả; chưa có validator tổng quát, transport mạng hay client gọi model. Engine không phụ thuộc harness.
7. Worktree tách checkout, không phải sandbox. Harness kiểm tra claim chồng lấn và committed diff lúc handoff; chưa chặn mọi ghi file ở OS. Integration kiểm tra scope, dependency thực tế và semantics trên revision sau merge.
8. Tài liệu phân biệt implemented, specification, proposed và measured. Target hoặc thiết kế runtime không được ghi như tính năng ứng dụng đã chạy.

## Ứng viên stack

**C++20 + Qt + Skia là baseline cho spike**, chưa khóa Qt version, backend, geometry library, storage format hoặc giấy phép bản phân phối. Qt phục vụ shell/docking/input; Skia qua render facade; geometry/document không dùng kiểu renderer như schema lưu trữ công khai.

Rust + Vello/wgpu là ứng viên đối chiếu nếu evidence tốt hơn. Memory safety không chứng minh correctness hình học, consistency tài liệu hoặc fidelity file. Native vẫn có allocation, destructor, shader compilation, GPU synchronization và I/O stalls; không bảo đảm triệt tiêu micro-stutter.

Vòng đời dài dựa trên public boundaries nhỏ, dependency pin, build tái tạo, fixtures và chi phí nâng cấp. Không cam kết mười năm từ tên thư viện/benchmark marketing. Present Skia vào Qt và input/layout phải qua prototype; không tạo widget/scene item riêng cho mỗi control point.

Geometry nguồn đề xuất mm, IEEE-754 binary64 (`double`/`f64`) cho tọa độ và transform; kiểm chứng trước khi khóa schema. Clipper2 là ứng viên polygon clipping/offset với lượng tử hóa và coordinate-range checks; không gọi đó là Boolean Bézier exact hoặc full CAD kernel.

Màu nguồn giữ RGB/CMYK/spot, ICC, overprint và transparency theo phạm vi model. Chữ giữ Unicode, font identity, shaping/layout và liên kết outline. Preview RGB hoặc PDF nhìn đúng không chứng minh separations, PDF/X hoặc text editability.

## Phạm vi đầu tiên

Lát cắt đầu: mở → sửa vector/chữ → undo/redo → lưu/phục hồi native → xuất một đầu ra đã kiểm chứng. Bốn workspace chia sẻ dữ liệu; không triển khai bốn sản phẩm hoàn chỉnh cùng lúc.

- PDF/PDF-compatible AI: import tập tính năng có fixture; PDF representation không bảo đảm phục hồi Illustrator authoring data.
- CDR: import nghiên cứu theo version/feature với libcdr; **CDR export và AI native round-trip ngoài MVP**.
- SVG/PDF/tập DXF 2D: ứng viên trao đổi/đầu ra; mỗi output có gate riêng.
- EPS, AI không có PDF representation và SKP: khảo sát adapter riêng; không suy ra hỗ trợ từ parser PDF.
- CAD constraints, CAM/postprocessor G-code, nesting tối ưu, 3D phức tạp: mốc riêng. Extrusion preview không phải mô phỏng quang học đã kiểm chứng.

Tham khảo Inkscape/FreeCAD không đồng nghĩa chọn fork/embed. Tích hợp lõi ứng dụng hoàn chỉnh cần spike và đánh giá maintenance/license riêng.

## Bằng chứng trước khi chốt

| Quyết định | Bằng chứng bắt buộc |
| --- | --- |
| Shell/build/backend | Input/docking/present prototype, Windows package, CI build, dependency pin |
| Geometry/document | Units/precision/topology oracle, stale result rejection, transaction, undo/redo |
| Render/text/color | Mixed fixtures, pan/zoom/invalidation, GPU/software budgets và color/text reports |
| Persistence | Version/migration, revision-consistent save, durability policy, fault injection |
| Interop | Format/version/entity/effect matrix; visual/editable/export evidence riêng |
| RPC/containment | Compatibility, size/resource budgets, cancellation, OS permission denial tests |
| License/distribution | Inventory cả dependency bắc cầu, fixture/font rights và nghĩa vụ phân phối |

[Ba spike foundation](../roadmap/foundation-spikes.md) là điều kiện đi tiếp; [ADR 0002](0002-runtime-and-hardware.md) mô tả hardware/executors. Thiếu máy/corpus/quyền fixture phải ghi thiếu evidence. Đổi public contract cần task riêng, consumer review và migration; ADR này chưa sửa schema/API.

## Nguồn để tái kiểm chứng

- [SkScalar](https://skia.googlesource.com/skia/+/refs/heads/main/include/core/SkScalar.h): public scalar là float.
- [Vello](https://github.com/linebender/vello): phân biệt CPU/GPU/compute; pin revision khi đo.
- [Clipper2](https://www.angusj.com/clipper2/Docs/Overview.htm): integer nội bộ và coordinate range.
- [Adobe AI saving](https://helpx.adobe.com/illustrator/using/saving-artwork.html): PDF-compatible representation.
- [libcdr](https://github.com/LibreOffice/libcdr/blob/master/README), [PDFium license](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE), [Poppler](https://poppler.freedesktop.org/api/cpp/poppler-document_8h_source.html), [dxflib](https://www.qcad.org/fr/90-dxflib), [Qt license](https://doc.qt.io/qt-6/licensing.html): capability và license theo bản thực sự chọn.

Nguồn có thể thay đổi; evidence phải lưu dependency version/revision. Process riêng không tự loại bỏ nghĩa vụ giấy phép.
