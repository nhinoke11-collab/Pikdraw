# Contract C4: Color Models, Overprint, and Typography

Version: 1.2.1 (documentation correction; no color/font API change)
Audited: 2026-10-24 against the working source.
Status: typed source colors, ICC resources/commands and Windows typography exist; print separations, complete Unicode conformance and vendor equivalence are not certified.
Producers: `core/color`, `core/typography`
Consumers: `core/document`, `core/rendering`, `modules/production`, `modules/interop`

---

## 1. Color Models and Preservation

The document retains typed authored colors. Import preservation depends on the adapter's C8 coverage; preview conversion does not grant fidelity to metadata the importer did not retain.

### Supported Color Spaces

1. **RGB**:
   - `r, g, b, a: double` in range $[0.0, 1.0]$; no alternative 16-bit channel layout in `ColorValue`.
   - Optional immutable `ProfileResource`; otherwise inherits page/legacy interpretation. A profile name alone does not prove an ICC transform is available.
2. **CMYK** (Production & Print):
   - `c, m, y, k, a: double` in range $[0.0, 1.0]$ ($0\% \dots 100\%$ for ink channels).
   - Standard ICC Profile tagging (e.g. `ISO Coated v2 300% (ECI)`, `U.S. Web Coated (SWOP) v2`).
3. **Spot Color (Custom Separation)**:
   - `spot_name: string` (e.g. `"PANTONE 185 C"`, `"CutContour"`, `"Crease"`).
   - `tint: double` in range $[0.0, 1.0]$ ($0\% \dots 100\%$).
   - Current `SpotColor` stores `alternate_rgb: RGBColor` and a legacy `overprint` bool; there is no `alternate_cmyk` member. Alternate RGB is preview data, not a certified spot separation.
4. **Overprint Semantics**:
   - `OverprintConfig::fill: bool`
   - `OverprintConfig::stroke: bool`
   - Current `ColorValue` has `OverprintConfig { fill, stroke }`. Native storage preserves authored metadata. PDF/X output, separations and screen overprint simulation require separate capability/evidence and must not be assumed available.

`GrayscaleColor { gray, a }` is also a current payload. RGB/CMYK/gray alpha is a separate channel; Spot uses its tint/alternate representation. Document/page working modes are RGB or CMYK. Assign-profile and convert-profile commands are distinct edits with Undo; an explicit paint profile can override the page binding. Missing/unsupported ICC paths need a declared diagnostic/fallback, not an exact-print claim.

---

## 2. Typography and Font Handling

### Source Unicode Integrity

- **Encoding**: Strict UTF-8 Unicode throughout all text models and serializations.
- **Vietnamese/cluster handling**: Windows shaping and the canvas editor handle UTF-8 text with cluster navigation. Current boundary code covers combining marks, variation selectors, emoji modifiers, ZWJ, regional pairs and CRLF through a bounded implementation; complete Unicode grapheme conformance is not established by that subset. There is no automatic NFC/NFD normalization contract: preserve source text unless an explicit edit/conversion says otherwise.

### Font Identity
Text runs define font identity through:

- `family_name: string` (e.g. `"Roboto"`, `"Noto Sans"`)
- `postscript_name: string` (unique font identifier)
- `weight: uint16_t` ($100 \dots 900$, standard OpenType weights)
- `italic: bool`
- `size_pt: double` (points, $1\text{ pt} = \frac{25.4}{72}\text{ mm} \approx 0.352778\text{ mm}$)

### Source Text vs. Outlines

- **Editability First**: Text remains editable character sequences accompanied by font attributes.
- **Outlines**: Rendering may derive glyph outlines without replacing `TextObject.text`. Persisted conversion to paths must be an explicit command/Undo operation. `text_to_path_outlines` is a derived geometry helper, not proof of a particular UI command or source-format text round-trip.
- **Missing fonts**: `FontResolver` retains requested identity separately from the resolved actual font and warning. On Windows a missing family falls back through Segoe UI/system font resolution. Installed fonts/OS affect the result; identical fallback across machines and universal embedded-font support are not guaranteed.

Text currently also carries alignment, decorations, frame/flow/path-binding and spacing fields. Their presence is not proof that every import adapter or exported format retains them; see C8 and the matching workflow.

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

## 4. Source authority and acceptance boundary

- [ColorValue](../core/color/include/pikdraw/color/values/color_value.hpp), [ICC resource](../core/color/include/pikdraw/color/profiles/profile_resource.hpp), [color commands](../core/document/src/commands/color_commands.cpp).
- [FontIdentity](../core/typography/include/pikdraw/typography/shaping/font_identity.hpp), [font resolution](../core/typography/src/shaping/font_resolver.cpp), [caret boundaries](../core/typography/src/layout/caret_navigation.cpp).
- [Text object fields](../core/document/include/pikdraw/document/model/object.hpp), [typography regression](../core/typography/tests/layout/test_layout.cpp), [color workflow regression](../apps/desktop/tests/test_document_color_workflow.cpp).

This is a source/documentation audit, not a new native test run. C9 preview equations remain the native semantic profile; CMYK/spot proofing, PDF/X, RIP/press output and exact vendor appearance require their own acceptance.
