# PikDraw selection and scale verification — 2026-10-16

Verified live in the canonical `build/win-dev/apps/desktop/pikdraw.exe` using an unsaved demo document. No source files were changed.

- `02-left-to-right-window.png`: left-to-right marquee fully encloses the rectangle; the rectangle is selected and the separate line remains unselected (Window behavior).
- `03-right-to-left-crossing.png`: right-to-left marquee overlaps only part of the rectangle; it is still selected (Crossing behavior).
- `09-corner-drag-result.png`: dragging from just inside the lower-right selection handle changes W/H from 94.03 × 63.69 mm to 105.55 × 77.34 mm; scale works. Starting exactly on the outer edge missed once, so the handle target could use clearer affordance/tolerance.
- `10-after-undo-original-scale.png`: Ctrl+Z restored the temporary demo geometry; the document remains unsaved.

Direct numeric W/H entry was not confirmed in this run: the custom input path activated Windows text services and did not apply a new size. This does not affect the successful handle-drag scale result.
