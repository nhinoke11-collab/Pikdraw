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
- **File Domain**:
  - `action.file.new`
  - `action.file.open`
  - `action.file.save`
  - `action.file.save_as`
  - `action.file.export`
  - `action.file.close`
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
