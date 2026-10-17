# ADR 0009: Mô hình appearance/effect graph và fidelity nguồn

Ngày: 17/10/2026. Trạng thái: **Accepted for the native basic Path/Text profile; external-format mappings remain proposed or unsupported**.
Phạm vi: quyết định kiến trúc cho dữ liệu appearance, renderer, persistence và adapter AI/CDR. Native profile v1 đã được triển khai; không có capability import/export AI/CDR mới được mở.
Contract liên quan: [C9 Appearance/effects](../../contracts/c9-appearance-effects.md).

## Bối cảnh

Opacity, blend/transparency, blur, feather, shadow, masks và appearance stack có tên tương tự giữa Illustrator, Photoshop và CorelDRAW, nhưng có thể gắn vào object, fill, stroke, group hoặc layer với thứ tự và quy tắc compositing khác nhau. Một thuộc tính opacity đơn lẻ hay enum tên mode không thể biểu diễn đầy đủ các khác biệt này.

PikDraw cần một model có thể dùng chung giữa document, rendering, storage và importers. Các bộ chuyển đổi phải biết chính xác file nào, phiên bản nào, representation nào và nhóm tham số nào đã được kiểm chứng. Nếu không, việc mở file có thể giữ geometry nhưng làm mất appearance mà không báo.

Hiện `modules/interop/README.md` chỉ công bố SVG subset tĩnh và DXF ASCII 2D; PDF, AI, CDR, PNG và JPEG bị từ chối. Các hợp đồng fidelity C8 và roadmap rendering P22–P24 là đề xuất. Kế hoạch này không nâng mức hỗ trợ hiện tại.

## Quyết định đề xuất

1. Dùng appearance graph không destructive với node ID/type/schema version, scope, thứ tự và tham số có đơn vị/basis. Native Path/Text model, commands và v4 storage codec triển khai profile có giới hạn này; provenance nhập từ hãng và các scope rộng hơn chưa được triển khai.
2. Giữ model trung lập với hãng và backend. `core/document` sở hữu dữ liệu; renderer tính pixel theo snapshot; adapter ánh xạ nguồn theo version row; storage bảo toàn graph/provenance theo migration; desktop trình bày diagnostics và lựa chọn conversion.
3. Tách color alpha, fill opacity, stroke opacity, object opacity và group/layer compositing. Định nghĩa rõ stack order, isolation/pass-through/knockout, clipping, color space, alpha convention, bounds và undo semantics trước khi hỗ trợ từng node.
4. Bật từng feature/version/representation theo evidence. AI qua PDF-compatible representation được báo riêng với Illustrator authoring data. Mỗi CDR version family có catalog riêng. Import, editability, visual fidelity, production readiness và export-back là kết quả độc lập.
5. Không âm thầm bỏ, đổi Normal, flatten hay thay effect. Chỉ `reference_only` khi đường reference có appearance evidence; chỉ conversion sau lựa chọn rõ của người dùng; còn lại là unsupported/cancel. Lưu source payload không tự chứng minh rằng effect còn hiển thị đúng.
6. Chỉ công bố native AI/CDR save-back khi writer và reopen-in-source-app test đạt cho đúng version/feature matrix. Native PikDraw save/reopen là capability riêng.

### Native basic profile after C3–C5 alignment (Implemented; bounded scope)

The native profile is limited to Path and Text with real glyph outlines. It stores object/fill/stroke opacity, one object-level blend ID from normal/multiply/screen/overlay/darken/lighten, and an ordered single-input list of Gaussian Blur v1 and Drop Shadow v1. Group, Image, Layer, source masks/clips, isolation/pass-through/knockout, multiple fills/strokes, spread, and advanced effects remain outside the profile. Color alpha remains source ColorValue data.

Each node has a stable nonzero instance ID, stable type ID, schema version, enabled state, typed parameters, and order. Blur uses sigma X/Y in object-local mm. Shadow schema 1.0 uses local-mm offset and sigma X/Y, source ColorValue, and separate opacity; it defaults to Normal blend and spread zero. Shadow schema 1.1 adds a separate shadow blend mode from the six stable v1 blend IDs. A non-Normal shadow must be the last enabled node because it composites against the real painter-order backdrop; later enabled nodes are invalid and rejected, while disabled nodes after it are ignored. The profile caps storage at 256 nodes per object and 100,000 nodes per document, matching C5.

SourceGraphic is fill/stroke output on transparency; component opacity is applied there. Normal-mode ordered effects run in object-local coordinates, then the affine transform, object opacity once over the full premultiplied result, then object blend against the painter-order backdrop. A non-Normal Shadow is the terminal enabled effect: its shadow branch blends against the backdrop below the object, the source graphic composites above the shadow with the object blend, and object opacity interpolates the complete result once against the original backdrop. Viewport clipping is last. Blend functions use encoded sRGB; blur uses normalized separable linear-sRGB convolution, finite support ceil(4*sigma_px), transparent-zero exterior samples, and no boundary renormalization. Drop Shadow uses input alpha, blur, offset, tint and separate shadow opacity, then places the result below input. These are versioned PikDraw semantics, not assumed Illustrator/Corel equivalence.

C4 requires source RGB/CMYK/spot/gray channels, profiles, alpha, and overprint metadata to remain intact; conversions for the named preview spaces are transient. C3's bounded Path/Text renderer implements distinct source/effect/target bounds, checked per-request work and scratch limits, and incomplete/budget diagnostics. The current software path is synchronous. Aggregate multi-job reservations, measured process RSS under pressure, and tiled seam proof are not established and must not be inferred from the per-kernel limits.

The source codec writes native v4 and reads v2/v3/v4. Appearance schema 1.0 is stored with minimum-compatible version 4 and authoring-path schema 1.0. Effect node major schema 1 supports Gaussian Blur minor 0 and Drop Shadow minor 0/1; legacy Shadow minor 0 loads as Normal, while minor 1 appends a length-prefixed stable shadow blend ID. Loading v2/v3 supplies default opacity 1, Normal object blend, and no nodes; v2 also performs the existing editable-path/stable-ID migration. Opening never rewrites the file; explicit save writes v4. Older readers reject v4, and there is no v4-to-v3 writer. C5 2.2 documents the implemented v4 framing and field order. Independent byte fixtures cover representative effects, order, blend IDs, corruption, checksums, and v2/v3 migration; they do not certify every recovery or adversarial-input path.

The native Path/Text profile is implemented through document commands, software rendering, native v4 persistence, desktop Properties, and undo/redo. This ADR accepts those architecture choices for that bounded profile. AI, CDR, PDF and EPS appearance import/export are not thereby supported; SVG capability remains limited to its separately documented subset and rejects unrepresentable effects. Illustrator/CorelDRAW visual parity, editability, source-file round-trip, print output, direct OS interaction and async preview cancellation require separate evidence.

## Hệ quả

- Opacity is not only a UI field. The implemented native profile keeps separate alpha scopes, command/undo semantics, model/storage migration and renderer vectors; new group scopes require their own reference cases before implementation.
- Blur/shadow cần đơn vị, transform, edge/support, clipping, bounds/ROI, alpha/color semantics và budget; tên `radius` hoặc `blur` không đủ để chuyển đổi.
- Importer sẽ cần capability rows và per-effect loss report; parser không tự cấp quyền support. Reference render, editable mapping và native round trip cần gate tách biệt.
- Các hiệu ứng phức tạp như mesh, lens, pattern/texture transparency, masks, knockout và plugin effects có thể vẫn unsupported/reference-only cho tới khi có adapter/render/storage evidence.
- C9 và P22–P24 cần consumer review thống nhất trước khi bất kỳ API/schema chung nào được đưa vào code; C5 migration và C8 report/provenance không được mâu thuẫn.

## Phương án đã cân nhắc

- **Một scalar opacity dùng cho mọi scope:** quá thiếu thông tin; không phân biệt màu alpha, fill/stroke, object hoặc group compositing.
- **Generic effect name + JSON vendor parameters:** tên không đảm bảo semantics; dữ liệu tùy ý không có validation, renderer behavior, migration hay fidelity evidence.
- **Flatten toàn bộ file thành bitmap khi import:** có thể bảo toàn một preview trong vài trường hợp nhưng phá chỉnh sửa vector, cấu trúc và đường sản xuất; chỉ cho phép như conversion tường minh có thông số.
- **Coi PDF/SVG trung gian là biểu diễn authoring đầy đủ:** không đúng với artboards, layers, live effects, plugin data hoặc feature không có trong representation; report theo representation thực tế.

## Review và điều kiện chuyển trạng thái

C3/C4/C5 now record the native v4 appearance implementation, color/alpha semantics, effect bounds and per-request resource limits. BF01 byte fixtures and native Path/Text consumer paths exist. Remaining gates include aggregate memory-pressure measurement, direct OS interaction, async-preview cancellation, unimplemented group and mask scopes, and independent source-format fixtures. Current interop remains limited to the published SVG subset and DXF ASCII 2D import; PDF, AI, CDR and EPS remain unsupported. Contract/ADR acceptance does not certify Illustrator/CorelDRAW visual fidelity, editability, print readiness or source-format round-trip.
