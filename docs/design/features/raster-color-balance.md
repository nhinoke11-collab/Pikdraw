# Color Balance for raster images

**Status:** graph/evaluator, menu action, modal editor, persistence, and inline Properties controls are implemented in source. Release regression and native visual acceptance are pending.

## Entry points and action contract

| Surface | Contract |
| --- | --- |
| Menu | `Bitmaps > Color Balance…` opens the draft dialog for one editable RGB image. |
| Action ID | `action.bitmap.adjust.color_balance`; enabled for one selected sRGB-preview bitmap with preserved source and editable document state. |
| Toolbar | Not applicable for this first menu action; it has five coupled controls and does not fit the frequent-action toolbar. |
| Toolbox/flyout | Not applicable; Color Balance has no canvas gesture. |
| Property bar | Not applicable to this first selected-object panel: the right Inspector dock owns the tonal-range selector and five coupled controls. The menu action remains available for modal editing. |
| Properties | Inspector dock shows Shadows/Midtones/Highlights, three signed percent fields, and Preserve Luminosity for one selected editable RGB bitmap. Fields update the same stable per-range node through document commands and support Undo. |
| SVG icon | Not applicable; Color Balance is exposed through a text menu action and direct Properties controls, with no icon-only control. |

## Fields and semantics

| Field / label | Type and range | Scope and state |
| --- | --- | --- |
| Tonal range / Dải sắc độ | Properties and dialog; Shadows, Midtones, Highlights | One bitmap; each band keeps an independent node and parameter set. |
| Cyan ↔ Red / Cyan ↔ Đỏ | Properties and dialog; finite percent, −100…+100 | One tonal-range node; positive moves toward red. |
| Magenta ↔ Green / Đỏ tía ↔ Lục | Properties and dialog; finite percent, −100…+100 | One tonal-range node; positive moves toward green. |
| Yellow ↔ Blue / Vàng ↔ Lam | Properties and dialog; finite percent, −100…+100 | One tonal-range node; positive moves toward blue. |
| Preserve luminosity / Bảo toàn độ sáng | Properties and dialog; Boolean, default on | Rescales adjusted linear-sRGB RGB values to retain source luminance when possible. |

The adjustment runs on linear-light sRGB RGB samples, preserves alpha, accepts RGBA8 and RGBA16LE, and blends through the node's supported adjustment mask. It does not convert the image to CMYK or edit process separations. The tonal weighting and axes are PikDraw semantics, not a compatibility claim for Photoshop/Corel sliders.

## Help and feedback

- Tooltip/focus help: “Shift the selected shadow, midtone, or highlight range along RGB/complementary color axes. Alpha is preserved.” / “Dịch vùng tối, trung gian hoặc vùng sáng theo các trục RGB/màu bù. Alpha được giữ nguyên.”
- Field focus help names the active complementary axis, its ±100% range, Enter/Esc behavior, and Ctrl+Z; range selection reports the active band. Inline apply/toggle reports the undo hint; dialog Apply reports preview rendering.
- Disabled reason: select one editable RGB sRGB-preview image with a preserved source; resolve an unapplied profile before editing.
- How to use: select an image and use the Inspector's Color Balance range selector and fields for immediate, undoable changes. `Bitmaps > Color Balance…` remains available for a multi-field draft; Cancel/Esc discards it. Each range has one stable node and can be revised from either surface; Ctrl+Z/Redo reverses/reapplies a command.

## Persistence and acceptance

Graph schema v17 appends a stable `ColorBalance` node kind. Earlier graph schemas retain their existing decoding behavior. Each tonal range has a stable node ID; applying again updates that range rather than appending a duplicate, and its mask and enabled state are retained. Storage and evaluator regressions cover parameter round-trip, mask behavior, range validation, alpha, and U8/U16 samples. Desktop workflow source also covers dialog ranges, inline Properties pointer dispatch, disabled-node edits, Undo/Redo, and `.pr` reopen. The final Release suite and native visual checks remain pending.
