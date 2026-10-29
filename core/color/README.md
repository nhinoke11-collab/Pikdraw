# PikDraw Core Color

## ICC preview Windows — 2026-10-25

`preview/icc_preview.hpp` nạp file ICC/ICM thực tế, kiểm RGB/CMYK đúng slot, tạo transform
Windows Color Management về profile RGB của màn hình đang dùng trong desktop; API không truyền
đích màn hình vẫn mặc định sRGB hệ thống. Desktop đọc association bằng `GetICMProfileW`, nạp
profile đã kiểm tra và dựng lại transform khi cửa sổ chuyển monitor hoặc Windows báo đổi display.
Nếu profile monitor không đọc được, desktop dùng sRGB hệ thống làm fallback.
Không đổi `ColorValue` nguồn. Giữ alpha riêng, cache tối đa 4.096 màu preview; lỗi chuyển màu
được đếm để desktop báo fallback. Không có CMYK ICC thì dùng preview gần đúng hiện có.

Desktop có Assign/Convert ICC cho trang và bitmap theo scope hiện được hỗ trợ. Proof Colors dùng
profile CMYK đang chọn làm target; menu Color Profiles lưu một trong bốn rendering intent ở tùy
chọn v2 và vẫn đọc tùy chọn v1. `supports_proofing` chỉ bật intent nếu Windows CMM tạo được cả
RGB→CMYK proof và CMYK→display transforms. Khi intent đang bật mà transform không khả dụng,
preview tăng failure counter và không lặng lẽ chuyển sang đường RGB thường. Gamut warning là
projection CMM trên màn hình, không phải phép đo vật lý. BPC vẫn không khả dụng trên Windows CMM;
Overprint Preview và PDF/X cũng chưa được triển khai.

Dependency Windows: `mscms` của hệ điều hành. Không đóng gói lại profile hệ thống.
`test_pikdraw_icc_preview` xác minh sRGB, sai model/file, CMYK SWOP thực nếu đã cài,
giữ channels/alpha và so với công thức preview để chứng minh đã đi qua ICC engine.
Đây chưa phải kiểm chuẩn màn hình bằng thiết bị, document ICC binding đầy đủ, PDF/X, BPC
hay proof giấy/mực.

`ProfileColorTransform::convert_process16le` chuyển mẫu process RGB/RGBA hoặc
CMYK/CMYKA interleaved U16LE theo hai ICC đã bind; RGB↔CMYK đổi đúng số channel,
alpha giữ nguyên, và mỗi lệnh xử lý tối đa 8.192 pixel mỗi batch với callback hủy/
tiến độ. Destination chỉ được công bố khi caller nhận thành công vì hủy giữa các
batch có thể để lại phần output đã ghi. Regression dùng profile sRGB và RSWOP cài
trên Windows, so pixel path với `ColorValue` scalar, giữ alpha và kiểm hủy/layout.
API này được dùng cho Assign/Convert process paints và bitmap RGB↔CMYK trong phạm vi workflow
hiện có; native CMYK raster editing, Gray/Lab và BPC chưa có. Export CMYK16 TIFF là một output
bitmap riêng và không khẳng định plate/PDF-X parity.

Chuyển màu không tự áp giới hạn TAC khi chưa có output condition. `clamp_tac` và tham số TAC
trong API conversion cần giới hạn tường minh 100%..400%; preflight/command sửa TAC theo profile
đầu ra chưa được nối vào desktop.
Black Generation cũng không tự chọn Pure K/Rich Black cho mọi paint; policy phải được caller
chọn rõ để không tự đặt overprint cho cả fill và stroke.

**Status**: Implemented for Milestone M1 (`ps04-color`).  
**Authority**: `contracts/c4-color-typography.md` (Contract C4).  
**Dependencies**: None.

## Feature Index

| Feature | Public Headers (`include/pikdraw/color/`) | Implementation (`src/`) | Tests (`tests/`) |
|---|---|---|---|
| **Values** | `values/color_space.hpp`, `values/color_value.hpp` | `values/color_value.cpp` | `values/test_color_values.cpp` |
| **Profiles** | `profiles/profile_id.hpp`, `profiles/profile_resource.hpp`, `profiles/profile_registry.hpp` | `profiles/profile_resource.cpp`, `profiles/profile_registry.cpp` | `profiles/test_profiles.cpp` |
| **Preview** | `preview/color_transform.hpp`, `preview/preview_cache.hpp` | `preview/color_transform.cpp`, `preview/preview_cache.cpp` | `preview/test_preview.cpp` |
| **ICC process transform** | `preview/profile_color_transform.hpp` | `preview/profile_color_transform.cpp` | `preview/test_icc_preview.cpp` |
| **Composite** | `color.hpp` | N/A | N/A |

`ProfileResource` copies an ICC byte payload into immutable ownership, applies a 16 MiB quota,
checks header/color-space/tag-table bounds, and computes the identity digest from the bytes with
Windows CNG. The resource API does not accept document-provided filesystem paths. Other platforms
currently report hash support unavailable; profile-byte loading is therefore Windows-only until
a reviewed portable digest provider is added. The registry can resolve a loaded resource by its
digest; document/page/paint bindings and native storage migration are supported in schema v15+.

## Build and Test

Xem [Native Build](../../docs/development/native-build.md):

```powershell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_color
```
