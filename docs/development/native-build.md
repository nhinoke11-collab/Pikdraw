# Build native dùng chung cho mọi phiên

## Đường dẫn duy nhất

Source: `C:/Users/CK ART STAGE/Downloads/PikDraw`.
Build/cache: `build/win-dev`.
App: `build/win-dev/apps/desktop/pikdraw.exe`.

Từ 16/10/2026 chỉ có preset `win-dev`. Mọi phiên build ghi đè đúng app này;
không tạo `pikdraw_app.exe`, `pikdraw-latest.exe`, app có ngày giờ hoặc build tree khác.
CMake từ chối linked worktree và thư mục build khác. Test/worker executable là công cụ
kiểm thử/runtime riêng, không phải phiên bản app để người dùng mở.

## Lệnh cho agent

```powershell
# App tăng dần, dùng chung output:
.\tooling\native\build.ps1
# Release ghi đè cùng output, không tạo thư mục khác:
.\tooling\native\build.ps1 -Configuration Release
# Toàn bộ targets và CTest (khi nguồn test có mặt):
.\tooling\native\build.ps1 -Test
# Chỉ chạy CTest đã build, giữ cùng lock (khi nguồn test có mặt):
.\tooling\native\check.ps1 -Suite unit
# Mở đúng app canonical:
.\tooling\native\dev.ps1 -Mode Editor
```

Runner giữ khóa độc quyền `.agent/native-build.lock` qua configure/build/test;
phiên thứ hai phải chờ phiên trước xong rồi chạy lại. Không xóa khóa của owner đang chạy.
Sau crash, xác minh owner đã dừng trước khi phục hồi khóa. Không đổi tên hoặc copy exe
để né app đang mở; đóng app theo yêu cầu của phiên đang dùng rồi build lại cùng path.
Runner fail khi configure/build/test lỗi và giải phóng khóa; không báo pass khi không có test.
Direct CMake có thể bỏ qua khóa nên agent phải dùng runner.

## Build tăng dần và phạm vi rebuild

Lệnh mặc định cấu hình preset `win-dev`, sau đó gọi `cmake --build` cho target `pikdraw`;
nó không chạy `clean` hay `--clean-first`. Ninja so sánh input, lệnh build và dependency graph:
source/header mới hoặc thay đổi được biên dịch lại cùng những consumer phụ thuộc thực sự bị ảnh
hưởng; output còn hợp lệ được tái sử dụng. Đổi header dùng chung, compiler/flags/preset, thiếu output
hoặc thay dependency có thể khiến nhiều target phải build lại. `-Clean` là lựa chọn có chủ ý, không
dùng cho mỗi feature/fix.

Sau một build thành công, nếu không đổi source, cấu hình hay toolchain, lần chạy tiếp theo phải báo
không có việc build hoặc không chạy compile/link edge. Nếu vẫn chạy nhiều target, kiểm tra input,
timestamp và dependency log trước khi lặp lại. Đặc biệt, `ninja: warning: premature end of file;
recovering` báo dependency log bị phục hồi; ở Ninja 1.13.2 tình trạng này có thể làm build lặp lại
nhiều target dù source không đổi. Khi thấy cảnh báo này, ghi lại phiên bản Ninja/CMake và output,
đánh dấu lượt build là recovery chứ không phải bằng chứng incremental, rồi coordinator xử lý metadata
dưới shared lock trước khi xác nhận no-op. Không xóa cả build tree hoặc xóa app để né vấn đề.
Xem [Ninja issue #2703](https://github.com/ninja-build/ninja/issues/2703) về deps-log recovery.

`-Clean` dùng target clean trong cùng build tree. Không tự xóa/copy sang build tree mới.
Đổi compiler cần được coordinator xử lý tuần tự trong cùng cache/output sau kiểm kê;
không trộn cache toolchain khác hoặc tự tạo preset phụ. Debug không là evidence FPS Release.

## Môi trường và giới hạn

C++20, CMake >= 3.25, Ninja và Python >= 3.10. `tooling/native/env.ps1` dò công cụ
cục bộ/PATH và `.agent/toolchains`; không tự cài dependency. Giữ toolchain/checksum/license
trong evidence. Runner dùng compiler đã cấu hình trong cache, không tự chuyển backend.

Đã có editor Win32/GDI; Qt preview vẫn là kế hoạch. Build pass không nghiệm thu fidelity,
editability, graphics performance hoặc OS containment. Module chỉ đăng ký khi có implementation;
không dùng stub để vượt gate. Xem `docs/project-state.json` để biết giới hạn thực tế.

Sau lượt kiểm cuối ngày 16/10/2026, theo yêu cầu người dùng mã nguồn test đã được xóa;
CTest không khả dụng cho tới khi mã test được khôi phục. Build mặc định chỉ tạo app.
