# Contract C7: Actions, UI Development Loop, and Localization

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `modules/desktop`
Consumers: `modules/vector`, `modules/raster`, `modules/production`, `modules/plugin_host`

---

## 1. Stable Action Identifiers

All user-facing commands, shortcuts, menus, and toolbar buttons map to hierarchical stable string identifiers:

```
action.<domain>.<verb>[.<qualifier>]
```

### Core Action Catalog
- **File Domain (CorelDRAW Workflow Reference)**:
  - `action.file.new` (Ctrl+N)
  - `action.file.new_from_template`
  - `action.file.open` (Ctrl+O)
  - `action.file.open_cloud`
  - `action.file.open_recent`
  - `action.file.close` (Ctrl+W)
  - `action.file.close_all`
  - `action.file.save` (Ctrl+S)
  - `action.file.save_as` (Ctrl+Shift+S)
  - `action.file.save_cloud`
  - `action.file.save_template`
  - `action.file.revert`
  - `action.file.acquire_image`
  - `action.file.import` (Ctrl+I)
  - `action.file.export` (Ctrl+E)
  - `action.file.export_for`
  - `action.file.publish_pdf`
  - `action.file.print` (Ctrl+P)
  - `action.file.print_preview`
  - `action.file.properties`
  - `action.file.exit` (Alt+F4)
- **Home & Navigation Domain**:
  - `action.view.home` (Trang chủ / Home Screen) (Alt+H)
- **Edit Domain**:
  - `action.edit.undo`
  - `action.edit.redo`
  - `action.edit.cut`
  - `action.edit.copy`
  - `action.edit.paste`
  - `action.edit.delete`
  - `action.edit.select_all`
- **View Domain**:
  - `action.view.zoom_in`
  - `action.view.zoom_out`
  - `action.view.zoom_100`
  - `action.view.fit_all`
  - `action.view.reload_ui` (Development mode)
- **Tool Domain**:
  - `action.tool.select`
  - `action.tool.node_edit`
  - `action.tool.pen`
  - `action.tool.rectangle`
  - `action.tool.ellipse`
  - `action.tool.text`
- **Object Domain**:
  - `action.object.group`
  - `action.object.ungroup`
  - `action.object.to_curves`
  - `action.object.boolean_union`
  - `action.object.boolean_difference`

---

## 2. Development-Time UI Reload Loop

To enable rapid UI iteration without long recompilations:
- **Separation of View and Presenter**:
  - Views are defined in `.ui` forms and styled via QSS (`themes/*.qss`).
  - Presenters and Document Controllers bind to the View interface via generation-guarded signal/slot connections.
- **Reload UI Mechanism (`action.view.reload_ui` / `F5`)**:
  1. Re-parses `.ui` XML and `.qss` style sheets from disk.
  2. Constructs the new widget tree in memory.
  3. Swaps the central layout and reconnects presenters with incremented `ui_generation`.
  4. Active document state, undo history, and viewport camera parameters are 100% preserved.
- **Safety**:
  - Development-time loose-file loading is compiled out in Release builds; production builds package verified binary Qt resources (`.rcc`).

---

## 3. Localization and Multilingual Support

- **Catalog Structure**: Key-value JSON translation files located in `locales/`:
  - `locales/en_US.json` (English - Base reference)
  - `locales/vi_VN.json` (Vietnamese - Primary user locale)
- **Key Hierarchy**: Keys strictly mirror action and UI component domains (e.g. `"action.file.save": "Lưu tệp"`).
- **Runtime Language Switching**: Changing language re-translates existing action titles and tooltip labels dynamically without restarting the application.

---

## 4. Visual Design Tokens & Shell Geometry Contract

All UI components and QSS stylesheets in `apps/desktop/` must adhere to the 2026 canonical design reference: [docs/architecture/ui-reference/ui_design_2026.html](../docs/architecture/ui-reference/ui_design_2026.html).

- **Shell Dimensions**:
  - `topbar_height`: 38px
  - `context_ribbon_height`: 34px
  - `tool_strip_width`: 48px – 56px
  - `inspector_dock_width`: 280px – 320px
  - `status_bar_height`: 26px
- **Cyber Dark Color Tokens**:
  - `color_bg_app`: `#070b10`
  - `color_bg_header`: `#0c131d`
  - `color_bg_ribbon`: `#0e1622`
  - `color_bg_dock`: `#0c131d`
  - `color_border_tech`: `rgba(255, 255, 255, 0.08)` / `#1d2636`
  - `color_accent_cyber`: `#00f2c3` (hover: `#00d2aa`, light: `#00a884`, dim: `rgba(0, 242, 195, 0.12)`)
  - `color_text_primary`: `#e2e8f0` / `#ffffff`
  - `color_text_muted`: `rgba(255, 255, 255, 0.6)`
  - `canvas_grid_dot`: `rgba(255, 255, 255, 0.14)` on `#070c12`
- **Typography**:
  - `font_family_ui`: `Plus Jakarta Sans`, `Segoe UI`, sans-serif
  - `font_family_numeric`: `JetBrains Mono`, monospace (with tabular figures)

