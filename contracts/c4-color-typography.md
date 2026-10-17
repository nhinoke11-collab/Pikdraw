# Contract C4: Color Models, Overprint, and Typography

Version: 1.2.0
Status: Locked (M0 Foundation); native appearance preview color semantics are implemented for the C9 Path/Text profile, with print and vendor equivalence unverified
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

## 3. Appearance rendering color and alpha (native profile extension 1.2)

This extension describes the preview/render conversions used by the C9 native basic profile. It does not change authored color values, certify print separations, or claim color equivalence with Illustrator, Photoshop, or CorelDRAW. Current source and regression coverage exercise the native Path/Text rendering path; unsupported scopes and vendor-file mappings remain outside this status.

- Source ColorValue channels, alpha, ICC/profile identity, spot name/tint/alternate, and overprint flags remain the document authority. A render conversion is transient; it must not rewrite those values or their source metadata.
- Color alpha, fill opacity, stroke opacity, shadow opacity, and object opacity are separate factors. Source color alpha participates when the fill/stroke or shadow color is sampled. Fill/stroke opacity scales that component on the transparent SourceGraphic. Object opacity scales both premultiplied RGB and alpha of the complete effected surface once, including the shadow; it is not pushed down to each overlapping fill, stroke, or child.
- The v1 filter working space is linear sRGB (linear_srgb_v1). Convert a source color through its declared profile to encoded sRGB, decode the transfer function, safely unpremultiply when alpha is nonzero, and premultiply for the filter surface. Gaussian convolution operates on premultiplied linear-light RGB and alpha. Alpha is not gamma transformed. At alpha zero, scratch RGB is zero so hidden color cannot bleed into blur edges.
- The v1 blend working space is encoded sRGB (srgb_encoded_v1) for the six C9 modes. Blend functions operate on unassociated encoded color components; the compositing equations combine those results with source and backdrop alpha. Normal remains source-over. This named profile is a PikDraw semantic choice, not a claim that a vendor document or print workflow uses the same blend space.
- Drop Shadow's schema 1.1 `blend_mode` uses those same six equations for the shadow tint against the painter-order backdrop below the object. It is distinct from the object's blend mode: the shadow is blended first, the source graphic is then composited above it with the object mode, and object opacity interpolates the complete group result with the original backdrop. Shadow opacity and shadow-color alpha are applied to the shadow branch before its blend; authored ColorValue metadata is unchanged.
- Drop Shadow takes the filtered input alpha mask, multiplies it by the separate shadow opacity and the shadow ColorValue alpha, then applies the shadow color converted to the declared render space. It is placed below the input before object opacity and object blending.
- Conversions between premultiplied float scratch and the public straight-alpha RGBA8 target are explicit. Before applying or removing an sRGB transfer function, safely unpremultiply; after conversion, premultiply again if the next stage requires it. Zero-alpha target pixels have zero RGB.
- RGB preview of CMYK or spot colors is not a substitute for separations or overprint simulation. Missing, invalid, or unsupported profile data must produce a diagnostic and a declared fallback/capability outcome; it must not be silently relabeled as exact source rendering. No print-readiness claim follows from a successful screen render.
