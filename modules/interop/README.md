# PikDraw interop

Status: Implemented in Milestone M1 (ps11-interop-core).

Responsibility:
- Format Capabilities Matrix (`capabilities/format_capabilities.hpp`): supported formats (SVG, PNG, JPEG) and explicit unsupported handling (PDF, DXF, CDR, AI) without silent flattening.
- Fidelity Loss Reports (`capabilities/format_capabilities.hpp`): detailed tracking of unsupported attributes (filters, masks, scripts).
- SVG Importer (`adapters/svg_importer.hpp`): parses SVG subset into PathObject and TextObject (supporting Vietnamese Unicode text), with real dimensions and fidelity diagnostics.
- SVG Exporter (`writers/svg_exporter.hpp`): serializes DocumentSnapshot to standard SVG with real mm units, path verbs, colors, and fonts.

Dependencies: `core/document`, `core/geometry`, `core/color`, `core/typography`. Zero UI dependencies.
Tests: 3 test suites passing (`test_pikdraw_interop_capabilities`, `test_pikdraw_interop_svg_adapter`, `test_pikdraw_interop_svg_exporter`).
