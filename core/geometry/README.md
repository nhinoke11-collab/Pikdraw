# PikDraw Core Geometry

**Status**: Implemented for Milestone M1 (`ps03-geometry`).  
**Authority**: `contracts/c1-geometry.md` (Contract C1).  
**Dependencies**: None (Leaf foundational core module).

## Feature Index

| Feature | Public Headers (`include/pikdraw/geometry/`) | Implementation (`src/`) | Tests (`tests/`) |
|---|---|---|---|
| **Primitives** | `primitives/point.hpp`, `primitives/bounds.hpp`, `primitives/transform.hpp`, `primitives/validation.hpp` | `primitives/transform.cpp` | `primitives/test_primitives.cpp` |
| **Curves** | `curves/curve.hpp`, `curves/path.hpp` | `curves/curve.cpp`, `curves/path.cpp` | `curves/test_curves.cpp` |
| **Flattening** | `flattening/flattening.hpp` | `flattening/flattening.cpp` | `curves/test_curves.cpp` |
| **Operations** | `operations/boolean.hpp`, `operations/offset.hpp` | `operations/boolean.cpp`, `operations/offset.cpp` | `operations/test_operations.cpp` |
| **Acceptance** | `geometry.hpp` | N/A | `acceptance/test_20m_billboard.cpp` |

## Build and Test
```powershell
cmake --preset win-dev
cmake --build --preset win-dev --target test_pikdraw_geometry
ctest --preset win-dev -R pikdraw_geometry_tests --output-on-failure
```
