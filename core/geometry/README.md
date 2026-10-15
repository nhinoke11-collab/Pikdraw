# PikDraw Core Geometry

**Status:** Có nền tảng hình học M1. D02 bổ sung evaluator/arc/transform/flattening và truy vấn curve; chưa chứng minh khả năng vẽ ArcTo xuyên suốt giao diện, renderer hay mọi định dạng nhập/xuất.
**Authority:** `contracts/c1-geometry.md` (Contract C1).
**Dependencies:** Không có (module nền tảng).

## Feature Index

| Feature | Public Headers (`include/pikdraw/geometry/`) | Implementation (`src/`) | Tests (`tests/`) |
|---|---|---|---|
| **Primitives** | `primitives/point.hpp`, `primitives/bounds.hpp`, `primitives/transform.hpp`, `primitives/validation.hpp` | `primitives/transform.cpp` | `primitives/test_primitives.cpp` |
| **Curves** | `curves/curve.hpp`, `curves/path.hpp` | `curves/curve.cpp`, `curves/curve_ops.cpp`, `curves/path.cpp` | `curves/test_curves.cpp`, `curves/test_curve_analytic.cpp` |
| **Flattening** | `flattening/flattening.hpp` | `flattening/flattening.cpp` | `curves/test_curve_analytic.cpp` |
| **Operations** | `operations/boolean.hpp`, `operations/offset.hpp` | `operations/boolean.cpp`, `operations/offset.cpp` | `operations/test_operations.cpp` |
| **Acceptance** | `geometry.hpp` | N/A | `acceptance/test_20m_billboard.cpp` |

## D02 file map

| File | Trách nhiệm | Consumer/test |
|---|---|---|
| `include/pikdraw/geometry/curves/curve.hpp` | Kiểu line/Bezier/analytic arc và API evaluate/split/length/query | Geometry consumers; `tests/curves/test_curve_analytic.cpp` |
| `src/curves/curve.cpp` | Resolve endpoint arc, analytic bounds, cubic extrema, affine arc encoding | `Path2D`; arc/precision tests |
| `src/curves/curve_ops.cpp` | Split, adaptive length, bounded nearest-point, line-line intersection | Vector/geometry consumers; query tests |
| `include/pikdraw/geometry/curves/path.hpp`, `src/curves/path.cpp` | Path source verbs, checked bounds/transform và compatibility wrappers | Document; path/renderer consumer tests |
| `include/pikdraw/geometry/flattening/flattening.hpp`, `src/flattening/flattening.cpp` | Flatten Bezier/arc, sai số, depth/output budget và diagnostics | Renderer adapter; flattening tests |
| `tests/curves/test_curve_analytic.cpp` | Arc oracle, affine, split, degenerate/large-coordinate, query và failure cases | Geometry CTest target |

## D02 đã kiểm chứng

- `ArcTo` được giải thành ellipse analytic và flatten theo sai số; hết subdivision budget trả `ToleranceNotMet`, hết output-point budget trả `WorkBudgetExceeded`, không trả polyline thiếu chất lượng như kết quả thành công.
- Affine map biến đổi tâm như point, hai trục như vector. Arc được mã hóa lại về dạng endpoint hiện có; reflection và shear có oracle lấy mẫu.
- Có split chính xác theo De Casteljau cho line/quadratic/cubic và split tham số cho ellipse arc; đo chiều dài adaptive trả sai số ước lượng hoặc `IterationLimit`.
- Nearest-point trên line là chính xác; curve dùng branch-and-bound từ bounds và trả giới hạn sai số hoặc `IterationLimit`. Giao đoạn thẳng hỗ trợ điểm, chồng lấp và input lỗi.
- `Path2D::bounds_checked` / `transformed_checked` trả status và chỉ số element lỗi. `bounds()` giữ tương thích bằng hull bảo thủ khi path lỗi; đó không phải xác nhận dữ liệu hợp lệ. `transformed()` ném exception nếu không thể giữ nguyên hình học.
- `Path2D::make_ellipse` vẫn tạo cubic tương thích các consumer hiện có. `ArcTo` rendering/import-export và giao cắt curve tổng quát chưa được tích hợp/kiểm chứng. Boolean/Offset hiện là prototype theo bounding rectangle, không phải CAD kernel hay bù dao.

## Build and Test

```powershell
.\tooling\native\env.ps1
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug --target test_pikdraw_geometry
ctest --test-dir build/win-msvc-debug -R pikdraw_geometry_tests --output-on-failure
```

Kết quả D02 phải ghi SHA và artifact sau khi chạy trên revision tích hợp. Một lần build/test của worktree chưa là bằng chứng tích hợp.
