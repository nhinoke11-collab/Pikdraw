# PikDraw interop

Trạng thái: mới có prototype giới hạn, chưa phải hỗ trợ toàn bộ định dạng.

Responsibility:
- Ma trận capability: chỉ SVG tĩnh theo subset có giới hạn được nhập/xuất; DXF ASCII 2D theo `dxf-entities-v1` chỉ được nhập. PDF, AI, CDR, PNG và JPEG hiện bị từ chối rõ ràng; không suy ra hỗ trợ từ IPC hoặc preview.
- Fidelity Loss Reports (`capabilities/format_capabilities.hpp`): detailed tracking of unsupported attributes (filters, masks, scripts).
- SVG Importer (`adapters/svg_importer.hpp`): nhập subset SVG tĩnh đã chặn giới hạn, tạo path/text và chẩn đoán nội dung không thể bảo toàn.
- DXF Importer (`adapters/dxf_importer.hpp`): chỉ nhập hồ sơ ASCII 2D có đơn vị/entity được hỗ trợ; nguồn ngoài hồ sơ bị từ chối.
- SVG Exporter (`writers/svg_exporter.hpp`): xuất subset SVG; không bảo đảm giữ cấu trúc/thuộc tính ngoài capability đã công bố.

Dependencies: `core/document`, `core/geometry`, `core/color`, `core/typography`. Zero UI dependencies.
Các adapter là prototype theo subset và cần kiểm chứng fixture trước khi dùng dữ liệu sản xuất. Kiểm tra hiện có gồm capability, SVG, DXF và worker; kết quả harness không thay thế kiểm thử round-trip theo nguồn thực tế.
