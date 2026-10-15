# PikDraw Desktop Application Shell

**Status**: Planned in M1 (`ps05-native-preview` and `ps13-vertical-slice`).  
**Visual Reference**: [docs/architecture/ui-reference/ui_design_2026.html](../../docs/architecture/ui-reference/ui_design_2026.html) and [docs/architecture/workspace-layout.md](../../docs/architecture/workspace-layout.md).

## Responsibility
Desktop shell host, Qt dock layouts, 4 workspace switcher (Vector, In ấn, Thi công, 3D), action routing, and accessibility.

## Visual Design System & 2026 Standards
All desktop UI implementation must strictly match the 2026 Cyber Design System:
- **Canonical Design**: `docs/architecture/ui-reference/ui_design_2026.html` (Dark mode) and `ui_light_mode.html` (Light mode).
- **Core Geometry**:
  - Topbar: fixed 38px (`PK PRO 2026` logo gradient, file title with green save pulse, menus, Spotlight command search `Ctrl+K` / `⌘K`, pill workspace switcher, quick theme toggle, settings button, export button).
  - Context Tool Ribbon: fixed 34px (target indicator, geometry inputs `X`, `Y`, lock ratio, `W`, `H` mm in tabular font).
  - Tool Strip: width 48–56px with Cyber Teal `#00f2c3` active state and glow.
  - Inspector Dock: width 280–320px (contextual properties and layers/assets).
  - Status Bar: height 26px (selection count, mouse coordinates mm, color profile, preflight check status, zoom slider).
- **Colors**:
  - Cyber Dark: Base `#070b10`, Panels `#0c131d` / `#0e1622`, Hairline borders `rgba(255, 255, 255, 0.08)`, Accent `#00f2c3`.
  - Light Mode: Base `#ffffff` / `#f8fafc`, Panels `#e8ecf2`, Accent `#00876a`.
- **Typography**: `Plus Jakarta Sans` for UI labels; `JetBrains Mono` for numeric values.

## Development Loop (Reload UI / F5)
- Views are authored as `.ui` forms in `ui/` and styled with `.qss` files in `themes/`.
- Pressing `F5` in development mode reloads forms and stylesheets instantly without recompiling C++.
- Presentation logic is decoupled from View widgets according to Contract C7 and ADR 0004.
