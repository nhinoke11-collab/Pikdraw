# PikDraw Core Color

## ICC preview Windows — 2026-10-15

`preview/icc_preview.hpp` nạp file ICC/ICM thực tế, kiểm RGB/CMYK đúng slot, tạo transform
Windows Color Management về sRGB của hệ thống với Relative Colorimetric.
Không đổi `ColorValue` nguồn. Giữ alpha riêng, cache tối đa 4.096 màu preview; lỗi chuyển màu
được đếm để desktop báo fallback. Không có CMYK ICC thì dùng preview gần đúng hiện có.

Dependency Windows: `mscms` của hệ điều hành. Không đóng gói lại profile hệ thống.
`test_pikdraw_icc_preview` xác minh sRGB, sai model/file, CMYK SWOP thực nếu đã cài,
giữ channels/alpha và so với công thức preview để chứng minh đã đi qua ICC engine.
Đây chưa phải document ICC binding, PDF/X, BPC hay proof giấy/mực.

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
