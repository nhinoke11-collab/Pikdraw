# Contract C4: Color Models, Overprint, and Typography

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `core/color`, `core/typography`
Consumers: `core/document`, `core/rendering`, `modules/production`, `modules/interop`

---

## 1. Color Models and Preservation

PikDraw preserves source color specifications without premature flattening or lossy conversion to sRGB.

### Supported Color Spaces
1. **RGB**:
   - `r, g, b: double` in range $[0.0, 1.0]$ (or normalized 16-bit integers).
   - Profile: `sRGB` (default) or `Display P3`.
2. **CMYK** (Production & Print):
   - `c, m, y, k: double` in range $[0.0, 1.0]$ ($0\% \dots 100\%$).
   - Standard ICC Profile tagging (e.g. `ISO Coated v2 300% (ECI)`, `U.S. Web Coated (SWOP) v2`).
3. **Spot Color (Custom Separation)**:
   - `spot_name: string` (e.g. `"PANTONE 185 C"`, `"CutContour"`, `"Crease"`).
   - `tint: double` in range $[0.0, 1.0]$ ($0\% \dots 100\%$).
   - `alternate_cmyk: CMYKColor` or `alternate_rgb: RGBColor` for on-screen preview rendering only.
4. **Overprint Semantics**:
   - `overprint_fill: bool`
   - `overprint_stroke: bool`
   - Overprint flags are strictly preserved in the document model and honored during production PDF/X and separation exports. Screen preview may simulate overprint via ICC blend transforms.

---

## 2. Typography and Font Handling

### Source Unicode Integrity
- **Encoding**: Strict UTF-8 Unicode throughout all text models and serializations.
- **Vietnamese Language Support**: Full support for precomposed and decomposed Vietnamese diacritics (NFC/NFD normalization handling). Caret navigation and text selections must operate on valid grapheme cluster boundaries.

### Font Identity
Text runs define font identity through:
- `family_name: string` (e.g. `"Roboto"`, `"Noto Sans"`)
- `postscript_name: string` (unique font identifier)
- `weight: uint16_t` ($100 \dots 900$, standard OpenType weights)
- `italic: bool`
- `font_size_pt: double` (points, $1\text{ pt} = \frac{25.4}{72}\text{ mm} \approx 0.352778\text{ mm}$)

### Source Text vs. Outlines
- **Editability First**: Text remains editable character sequences accompanied by font attributes.
- **No Automatic Outline Flattening**: Fonts are never silently converted to curves during save, load, or preview operations. Conversion to outlines (`TextToPathCommand`) is only triggered upon explicit user command.
- **Missing Font Fallback Policy**: If a font is absent on the local machine, the text run retains its full original font descriptor, renders with a deterministic substitute/fallback font, and emits a non-destructive missing-font warning flag.
