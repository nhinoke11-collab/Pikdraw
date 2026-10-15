# PikDraw Desktop Application Shell

**Status**: Implemented in Milestone M1 (`ps05-native-preview`).  
**Visual Reference**: [docs/architecture/ui-reference/ui_design_2026.html](../../docs/architecture/ui-reference/ui_design_2026.html) and [docs/architecture/workspace-layout.md](../../docs/architecture/workspace-layout.md).

## Responsibility
Desktop shell host, dock layouts, 4-workspace switcher (Vector, In ấn, Thi công, 3D), action routing, Cyber Dark themes, and dynamic F5 UI reload.

## Visual Design System & 2026 Standards
All desktop UI implementation strictly adheres to the 2026 Cyber Design System:
- **Canonical Design**: `docs/architecture/ui-reference/ui_design_2026.html` (Dark mode) and `ui_light_mode.html` (Light mode).
- **Core Geometry**:
  - Topbar: fixed 38px (`PK PRO 2026` logo gradient, menus, Spotlight search `Ctrl+K`, workspace switcher, theme toggle).
  - Context Tool Ribbon: fixed 34px (target indicator, tabular geometry inputs `X`, `Y`, lock ratio, `W`, `H` mm).
  - Tool Strip: width 48–56px with Cyber Teal `#00f2c3` active state and glow.
  - Inspector Dock: width 280–320px (contextual properties, layers, style).
  - Status Bar: height 26px (selection count, cursor coordinates in mm, color profile, preflight check status, zoom slider).
- **Colors**:
  - Cyber Dark: Base `#070b10`, Panels `#0c131d` / `#0e1622`, Hairline borders `#1d2636`, Accent `#00f2c3`.
  - Light Mode: Base `#f8fafc`, Panels `#ffffff` / `#e8ecf2`, Accent `#00876a`.
- **Typography**: `Plus Jakarta Sans` for UI labels; `JetBrains Mono` for numeric values.

## Feature Map & Code Organization
| Feature Group | Public API (`include/pikdraw/desktop/`) | Implementation (`src/`) | Unit Tests (`tests/`) |
| :--- | :--- | :--- | :--- |
| **Shell Geometry** | `shell/shell_geometry.hpp` | `shell/shell_geometry.cpp` | `tests/test_desktop_shell.cpp` |
| **Workspace State** | `shell/workspace_state.hpp` | `shell/workspace_state.cpp` | `tests/test_desktop_shell.cpp` |
| **Status Bar Model** | `shell/status_bar_model.hpp` | `shell/status_bar_model.cpp` | `tests/test_desktop_shell.cpp` |
| **Action Dispatcher**| `actions/action_id.hpp`, `action_dispatcher.hpp` | `actions/action_dispatcher.cpp` | `tests/test_desktop_actions.cpp` |
| **Presenters** | `presenters/tool_strip_presenter.hpp`, `ribbon_presenter.hpp`, `inspector_presenter.hpp` | `presenters/*.cpp` | `tests/test_desktop_shell.cpp` |
| **Theme Tokens** | `themes/theme_tokens.hpp` | `themes/theme_tokens.cpp` | `tests/test_desktop_shell.cpp` |
| **UI Reload** | `development/ui_reload_controller.hpp` | `development/ui_reload_controller.cpp` | `tests/test_desktop_reload.cpp` |
| **Desktop Facade** | `shell/desktop_window.hpp` | `shell/desktop_window.cpp`, `main.cpp` | `tests/test_desktop_shell.cpp` |

## Development Loop (Reload UI / F5)
- Views are authored as declarative panel definitions in `ui/panels/` and styled with `.qss` in `themes/cyber_dark.qss`.
- Pressing `F5` in development mode triggers `UIReloadController::reload()`, re-parsing stylesheets and forms instantly with allowlist security validation and rollback mechanics on malformed input.
- Presentation logic is decoupled from View widgets according to Contract C7 and ADR 0004.
