# PikDraw automation service

This module exposes typed, UI-independent application operations to automation adapters.
It is not an MCP server, COM server, transport, authorization system, or proof of installed
client compatibility. External callers must enter through a host adapter that authenticates
the principal and dispatches calls onto the owning document writer thread.

Document mutation uses PikDraw document commands, strict epoch/revision checks and undo.
Rectangle creation keeps the original `x_mm`, `y_mm`, `width_mm`, `height_mm` contract. It also
accepts numeric `x`, `y`, `width`, `height` with an explicit `length_unit` (`mm`, `cm`, `m`,
`in`, `ft`, `yd`, `pt`, or `pc`); `px` is rejected because the request has no DPI basis. Batch
create operations accept the same typed-unit form for rectangle, ellipse, text position, and
line/polyline points. Transform translation/pivot, duplicate offsets, and style stroke width also
accept typed numeric lengths when paired with `length_unit`. All geometry is normalized to canonical
millimeters before entering the document writer. The typed form cannot be mixed with explicit
`*_mm` fields. Layer targeting currently operates on layer index; the document model schema v25 assigns stable LayerIds which will be supported across all automation query targets. Capability descriptors report only handlers in this service. Read results,
text measurement, safe-margin validation, and legacy numeric fields remain explicitly expressed in
millimeters in this service. The desktop `document.create` adapter accepts either the legacy mm page
fields or a typed width/height plus physical `length_unit`, then validates canonical mm before it
opens a tab; pixel and mixed-unit requests are rejected.
