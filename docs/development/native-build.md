# Build native dùng chung cho mọi phiên

## Đường dẫn duy nhất

Source: `C:/Users/Administrator/Downloads/PikDraw`.
Build/cache: `build/win-dev`.
App: `build/win-dev/apps/desktop/pikdraw.exe`.

Chỉ có một preset `win-dev` (`PIKDRAW_BUILD_TESTS=ON` sẵn trong cache). Mọi phiên build ghi đè đúng app này;
không tạo `pikdraw_app.exe`, `pikdraw-latest.exe`, app có ngày giờ hoặc build tree khác.
CMake từ chối linked worktree và thư mục build khác. Test/worker executable là công cụ
kiểm thử/runtime riêng, không phải phiên bản app để người dùng mở.

## Lệnh cho agent (Ưu tiên build đúng target mục tiêu)

```powershell
# Build incremental chỉ app pikdraw.exe (bỏ qua reconfigure khi cache đã khớp):
.\tooling\native\build.ps1
# Build và chạy đúng 1 target test vừa sửa (nhanh nhất, tránh link lại toàn bộ suite):
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell -TestRegex '^test_pikdraw_desktop_shell$'
# Chỉ chạy CTest đã build với regex mục tiêu:
.\tooling\native\build.ps1 -TestsOnly -TestRegex '^test_pikdraw_rendering_gaussian_blur$'
# Mở đúng app canonical:
.\tooling\native\dev.ps1 -Mode Editor
```

Runner giữ khóa `.agent/native-build.lock` kèm PID và **tự động xếp hàng chờ (tối đa 120 giây)** nếu phiên khác đang build, hoặc tự thu hồi khóa nếu tiến trình cũ đã thoát. Không tự ý xóa khóa của tiến trình đang chạy hoặc đổi tên exe để né file lock.
Không lật qua lại giữa `Debug` và `Release` trên `build/win-dev` trừ khi được giao đo benchmark Release, vì đổi cấu hình trên generator đơn cấu hình sẽ khiến Ninja biên dịch lại toàn bộ dự án.

## Build tăng dần và phạm vi rebuild

Lệnh mặc định kiểm tra `build/win-dev/CMakeCache.txt` và `build.ninja`: nếu đã khớp `Configuration` và `PIKDRAW_BUILD_TESTS=ON`, runner bỏ qua bước chạy lại `cmake --preset win-dev` và gọi thẳng `cmake --build` cho target yêu cầu.
Chỉ truyền `-TestTarget <tên_target>` của module đang sửa; không gọi `build.ps1 -Test` không tham số trong vòng lặp sửa lỗi vì lệnh đó sẽ link lại toàn bộ `all` targets.

## Môi trường và giới hạn

C++20, CMake >= 3.25, Ninja và Python >= 3.10. `tooling/native/env.ps1` dò công cụ
cục bộ/PATH và `.agent/toolchains`; không tự cài dependency.
Đã có editor Win32/GDI; Qt/Skia vẫn là kế hoạch kiến trúc. Build pass không nghiệm thu fidelity,
editability, graphics performance hoặc OS containment. Xem `docs/project-state.json` để biết trạng thái hiện tại.
