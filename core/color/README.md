# PikDraw Core Color

**Status**: Implemented for Milestone M1 (`ps04-color`).  
**Authority**: `contracts/c4-color-typography.md` (Contract C4).  
**Dependencies**: None.

## Feature Index

| Feature | Public Headers (`include/pikdraw/color/`) | Implementation (`src/`) | Tests (`tests/`) |
|---|---|---|---|
| **Values** | `values/color_space.hpp`, `values/color_value.hpp` | `values/color_value.cpp` | `values/test_color_values.cpp` |
| **Profiles** | `profiles/profile_id.hpp`, `profiles/profile_registry.hpp` | `profiles/profile_registry.cpp` | `profiles/test_profiles.cpp` |
| **Preview** | `preview/color_transform.hpp`, `preview/preview_cache.hpp` | `preview/color_transform.cpp`, `preview/preview_cache.cpp` | `preview/test_preview.cpp` |
| **Composite** | `color.hpp` | N/A | N/A |

## Build and Test
```powershell
cmake --preset win-dev
cmake --build --preset win-dev --target test_pikdraw_color
ctest --preset win-dev -R pikdraw_color_tests --output-on-failure
```
