# PikDraw Core Typography

**Status**: Implemented for Milestone M1 (`ps06-typography`).  
**Authority**: `contracts/c1-geometry.md`, `contracts/c4-color-typography.md`.  
**Dependencies**: `core/geometry`.

## Feature Index

| Feature | Public Headers (`include/pikdraw/typography/`) | Implementation (`src/`) | Tests (`tests/`) |
|---|---|---|---|
| **Shaping** | `shaping/font_identity.hpp`, `shaping/font_resolver.hpp`, `shaping/shaper.hpp` | `shaping/font_identity.cpp`, `shaping/font_resolver.cpp`, `shaping/shaper.cpp` | `shaping/test_shaping.cpp` |
| **Layout** | `layout/text_layout.hpp`, `layout/caret_navigation.hpp`, `layout/glyph_outline.hpp` | `layout/text_layout.cpp`, `layout/caret_navigation.cpp`, `layout/glyph_outline.cpp` | `layout/test_layout.cpp` |
| **Composite** | `typography.hpp` | N/A | N/A |

## Build and Test
```powershell
cmake --preset win-dev
cmake --build --preset win-dev --target test_pikdraw_typography
ctest --preset win-dev -R pikdraw_typography_tests --output-on-failure
```
