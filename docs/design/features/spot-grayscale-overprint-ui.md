# Color picker, Spot, Grayscale and overprint in Appearance

## 1. Goal and capability

Task: `spot-grayscale-overprint-ui-20261018` / `style-overprint-patch-20261018` (desktop + vector).
The Appearance source-color editor exposes the document's RGB, CMYK, Spot and Grayscale
models, RGB/CMYK/HSB/HSL channel views and the contextual Fill or Stroke overprint flag. The
picker adds an HSB/HSL color plane, hue strip, original/draft swatches and a gently rounded themed
frame. It edits existing `ColorValue` data through `StylePatch` and the document command stack.
It does not add print separations, soft proofing, overprint simulation or certify PDF/RIP output.

Existing `ColorValue` preserves Spot name, tint, alternate RGB/alpha, the Spot-specific legacy
overprint bit and separate `OverprintConfig.fill/stroke`. Changing a color model is a deliberate
approximate conversion: metadata unique to the old model is discarded; fill/stroke overprint is
preserved. Editing channels without touching overprint keeps each selected object's overprint
bit independent. An explicit overprint edit applies to the whole editable selection as one undoable
command.

## 2. Placement and action route

| Surface | Decision |
| --- | --- |
| Main menu | No new color command. `Window > Properties` opens/focuses the existing dock when needed. |
| Action ID | No new ID: the existing Appearance Fill/Stroke source-color control opens the editor. |
| Standard toolbar / toolbox | Not applicable; this is a selection property editor, not a tool or frequent global action. |
| Property bar | Not applicable; four source models plus Spot metadata and opacity are detailed attributes. |
| Properties | `Properties > Appearance > Fill` or `Stroke`: source swatch opens the detailed editor. Fill and Stroke targets remain distinct. |
| Dialog | Reuses the existing source-color modal. Apply commits changed fields; Cancel/Esc discards the draft. The picker uses a custom HSB/HSL plane, channel view selector and rounded shell; no new window or dock is introduced. |
| Help path | Use the Fill/Stroke swatch, choose a source model and RGB/CMYK/HSB/HSL channel view, edit numeric fields or the color plane, optionally toggle contextual overprint, then Apply. Ctrl+Z undoes the document change. |

No new SVG icon is introduced. Existing labeled Fill/Stroke controls and native model/field labels
provide names and keyboard focus; model selector, view selector, channel fields, overprint, width,
Apply and Cancel follow tab order. The window and primary buttons use rounded corners consistent
with PikDraw's native shell. Existing source-color hint stays attached to the swatch, with contextual
labels and status reporting for validation failures.

## 3. Fields and scope

| Field | Control and range | Scope, availability and commit |
| --- | --- | --- |
| Source model | RGB, CMYK, Spot, Grayscale | Draft in the current Fill/Stroke color editor. Switching the source model converts from the preview; model-specific metadata is dropped only on that explicit choice. |
| Channel view | RGB, CMYK, HSB (Hue based), HSL | Edits the current source through a view conversion; HSB/HSL values convert back to the selected source model and do not change a document's RGB/CMYK mode. |
| Color plane | Saturation × Brightness for HSB; Saturation × Lightness for HSL; hue strip 0–359° | Updates the draft while dragging; Apply commits it, Cancel/Esc discards it. RGB/CMYK source channels remain in their chosen document model. |
| RGB channels | R/G/B, 0–255; opacity 0–100% | Fill/Stroke source color, including new-object defaults. Apply writes one source color patch. |
| CMYK channels | C/M/Y/K, 0–100%; opacity 0–100% | Same scope; displayed CMYK channels remain CMYK. HSB/HSL preview conversion is approximate and is not ICC proof. |
| Spot | Name, 1–128 Unicode code units; tint 0–100%; alternate R/G/B 0–255 and alternate alpha 0–100% | Same scope. Existing Spot identity and legacy Spot overprint metadata are retained when staying in Spot. A non-Spot source converted to Spot starts as `Spot Color` at 100% tint. |
| Grayscale | Gray and opacity, 0–100% | Same scope. Conversion from another model uses preview luminance. |
| Source stroke width | 0–10,000 mm | Stroke only; separate from color model and disabled for Fill. Apply only writes width if it changed. |
| Overprint Fill / Stroke | Contextual three-state checkbox: On, Off or Mixed | Fill dialog edits only `OverprintConfig.fill`; Stroke dialog edits only `.stroke`. Mixed remains per-object until the user clicks, then applies the chosen value to all selected supported objects. Defaults are document drawing defaults. |

The shared source-color dialog is also used by gradient stops and shadow colors; those contexts
show RGB/CMYK/Spot/Grayscale but hide Fill/Stroke overprint and stroke width. Changing a color
without editing overprint preserves every selected object's existing overprint metadata.

## 4. Selection states and command behavior

| Context | State |
| --- | --- |
| No selection | Edits the new-object default Fill/Stroke source style. Fill/Stroke overprint remains a default flag. |
| One supported Path | Edits the selected Fill or Stroke. Stroke is Path-only. |
| Text selection | Fill and Fill overprint are available; Stroke and Stroke overprint are unavailable. |
| Multiple supported objects | Source channels apply as one shared value only when edited. Untouched Mixed overprint is preserved per object; explicit overprint choice applies to the full selection. |
| Locked, hidden, missing or unsupported target | Existing style command rejects the whole transaction; no partial write. |
| Invalid input | Stay in the editor and identify the valid ranges; Apply has no effect. |
| Cancel / Escape | Discard dialog draft; no command or undo entry. |
| Apply | Emit one style patch and one document command only for changed color, width or overprint fields. |
| Undo / Redo | Restore/reapply color payload and overprint flags together. |

The swatch shows the current source description and existing mixed state. Opening the dialog does
not modify the document. A mixed overprint checkbox is indeterminate until the user explicitly
clicks it, preventing a channel-only edit from flattening per-object values.

## 5. Help, limits and acceptance evidence

Field labels name units and models; the dialog explains approximate HSB/HSL preview conversion and retained
overprint. Status messages from the existing Appearance editor report Apply/Undo. There is no
separation preview, overprint rendering, PDF/X output guarantee, or production certification.

Automated acceptance: pure model conversions retain shared overprint and alpha; Spot fields retain
identity while remaining in Spot; HSB/HSL edits create valid colors in the document process model;
style patch changes Fill/Stroke overprint independently, color-only edits retain per-object flags,
and Undo restores prior color/overprint state. Build and shell harness do not establish native visual
quality across sizes, themes, or DPI. Manual native review remains unperformed and must be recorded
separately before claiming visual acceptance.
