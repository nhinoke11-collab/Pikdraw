# Contract C7: Actions, UI Development Loop, and Localization

Version: 1.1.0
Status: M0 action IDs remain locked; drawing-tool actions and context bindings proposed pending consumer review
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

## 4. UI organization and visual guidance (documentation update 2026-10-16)

The design authority is [ADR 0008](../docs/decisions/0008-editor-ui-organization.md),
[UI/UX standard](../docs/design/ui-ux-standard.md) and [workspace layout](../docs/architecture/workspace-layout.md).
The former mandatory Cyber HTML palette, fonts and fixed-pixel shell table are superseded as visual guidance.
They are not new document/plugin/IPC fields; this documentation update does not change runtime tokens or geometry storage.

Before UI implementation, complete the [feature UI template](../docs/design/feature-ui-template.md): primary menu,
action IDs, toolbar/toolbox placement, contextual property bar, Properties sections, field scopes/states,
SVG icons, concise labels, tooltip/focus help, status hints and usage help. Equivalent entry points share
actions and values; opening a panel remains distinct from editing document properties.
Use the workspace's DIP-based density guidance and the app's shared theme resources, not per-feature styling.
Historical HTML/SVG mockups are references only and are not evidence of a working native UI.

This is a design/manual-review policy update. Action IDs/version, localization bindings, modifier precedence,
document/source units and sections 1–3/5 remain unchanged. No runtime adoption or automated UX checker is claimed;
implementation and any machine-consumed token/API change require their own scope, consumer review and validation.

---

## 5. Drawing actions, snap controls, and modifier profiles (extension 1.1)

New stable action IDs are added without claiming that a runtime handler exists:

- Tools: `action.tool.line`, `action.tool.polyline`, `action.tool.pencil`, `action.tool.curvature`, and `action.tool.polygon`.
- Snap and direction: `action.snap.toggle`, `action.snap.open_config`, `action.snap.select_all`, `action.snap.clear_all`, `action.snap.direction.free`, `action.snap.direction.ortho`, and `action.snap.direction.polar`.
- Precision/session: `action.precision.hud.toggle`, `action.pen.finish`, `action.pen.cancel_draft`, `action.shape.from_center`, `action.shape.keep_center`, and `action.preference.input_profile.select`.
- Individual snap options use `action.snap.option.<group>.<option>` with the option IDs in `contracts/drawing/semantics.json`. Unknown options are rejected rather than silently accepted.

Snap masks, direction parameters, angle lists, modifier profiles, and per-tool overrides are user/workspace preferences and do not dirty or revise a document. Committed guides, construction geometry, and shape parameters are document data and follow C2 commands/undo. The master snap action only enables/disables automatic candidate acquisition; it preserves child masks and never disables numeric locks, explicit shape constraints, Ortho, Polar, or the active gesture modifier.

Continuous modifier behavior is resolved by focus scope, active tool, pointer phase, then exact chord. Text controls and IME composition own their keys before the canvas; AltGr and OS-reserved chords are not interpreted as PikDraw modifiers. A chord performs one named action or one declared composite, not two overlapping actions. The action descriptor records context and conflict policy; no global handler may guess from only the key code.

Default profile semantics are stable: PikDraw/Illustrator-inspired uses Shift to constrain angle/aspect, Alt to draw a rectangle/ellipse from center, and Shift+Alt for both. Corel-inspired uses Ctrl to constrain, Shift for center, and Ctrl+Shift for both. Alt breaks Bézier handles in both profiles; with the constrain key it both constrains the handle angle and breaks the opposite handle. Temporary Node in an active Pen session uses Ctrl in the PikDraw/Illustrator-inspired profiles and held F2 in Corel-inspired. Shift+click adds/removes nodes in Node. Holding S in the canvas bypasses automatic snap only when no reserved command chord consumes S; it does not bypass numeric or geometric constraints. Space+drag and middle-button drag pan.

F3 is the proposed default for the snap-master action. Other function-key defaults are not locked by this contract. The current native UI advertises F10 for Node, Shift+F2 for Zoom to Selection, and F8 for Text, while the current key handler does not implement all of those advertised bindings. D09 must reconcile the action map and product labels before assigning F-keys to Ortho/Polar/HUD. These actions remain available through named menu/command entries and user-remappable bindings. In particular, Corel Ctrl+Shift center-constrained drawing and the existing Ctrl+Shift+S Save As chord must be tested as a conflict vector; the reserved Save As chord wins when its terminal S key is pressed, so S-bypass is inactive for that exact chord. The user can turn snap off with the master action before the gesture.
