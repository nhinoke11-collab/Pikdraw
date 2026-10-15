# Hướng dẫn Build Native và Cấu trúc CMake Presets

Tài liệu này hướng dẫn cách cấu hình, biên dịch tăng dần và chạy kiểm thử hệ thống native của PikDraw trên Windows.

---

## 1. Yêu cầu Môi trường & Toolchain

PikDraw là ứng dụng đồ họa vector C++20 chuyên nghiệp. Hệ thống build native yêu cầu các công cụ sau:

1. **Trình biên dịch C++20**:
   - **MSVC**: Visual Studio 2022 (MSVC 19.44+) hỗ trợ đầy đủ `/std:c++20`.
   - **Clang**: LLVM/Clang 18+ (hoặc bộ toolchain LLVM-MinGW UCRT được ghim trong repo).
2. **CMake**: Phiên bản 3.25 trở lên.
3. **Ninja**: Phiên bản 1.11 trở lên làm build generator.
4. **Python**: Phiên bản 3.10 trở lên dùng cho harness, architecture gates scanner và contract validator.

> [!NOTE]
> Script `tooling/native/env.ps1` tự động phát hiện Python, CMake, Ninja, MSVC và Clang từ cả PATH hệ thống lẫn thư mục `.agent/toolchains` cục bộ của dự án.

---

## 2. Danh sách CMake Presets (`CMakePresets.json`)

PikDraw sử dụng chuẩn `CMakePresets.json` (schema version 6) với thư mục build và cache tách biệt hoàn toàn theo từng worktree và cấu hình:

| Preset Name | Trình biên dịch | Build Type | Thư mục Build | Mục đích |
|---|---|---|---|---|
| `win-dev` | Clang / MSVC | Debug | `build/win-dev` | Cấu hình mặc định cho dev, bật CTest architecture & contract gates |
| `win-msvc-debug` | MSVC | Debug | `build/win-msvc-debug` | Kiểm tra tính tương thích và chẩn đoán với MSVC STL |
| `win-msvc-release` | MSVC | Release | `build/win-msvc-release` | Bản build tối ưu hóa hiệu năng release bằng MSVC |
| `win-clang-debug` | Clang | Debug | `build/win-clang-debug` | Kiểm tra với Clang UCRT |
| `win-clang-release` | Clang | Release | `build/win-clang-release` | Bản build tối ưu hóa hiệu năng release bằng Clang |

---

## 3. Quy trình Biên dịch và Chạy Kiểm thử

### 3.1. Cấu hình và Build bằng CMake / Ninja

```powershell
# Cấu hình preset win-dev (Debug)
cmake --preset win-dev

# Biên dịch toàn bộ các target đang hoạt động
cmake --build --preset win-dev

# Chạy toàn bộ test suites được đăng ký trong CTest
ctest --preset win-dev --output-on-failure
```

### 3.2. Sử dụng Bộ Công cụ Runner Dòng lệnh

PikDraw cung cấp các script PowerShell chuyên dụng tại `tooling/native/`:

#### Chạy kiểm tra kiến trúc và hợp đồng (`check.ps1`):
```powershell
# Chạy suite kiểm tra kiến trúc (cấm module-to-UI, cấm vòng, cấm path escape)
.\tooling\native\check.ps1 -Suite architecture

# Chạy suite kiểm tra hợp đồng C1-C7 và oracles
.\tooling\native\check.ps1 -Suite contract

# Chạy toàn bộ các suites
.\tooling\native\check.ps1 -Suite all
```

#### Chạy dev preview (`dev.ps1`):
```powershell
# Chạy shell preview trong giai đoạn phát triển UI
.\tooling\native\dev.ps1 -Mode Preview -Configuration Debug -Scenario basic

# Buộc dùng software rendering fallback để kiểm thử
.\tooling\native\dev.ps1 -Mode Preview -Configuration Debug -ForceSoftware
```

> [!IMPORTANT]
> Tại mốc M0 (Foundation Lock), ứng dụng shell C++ chưa được biên dịch (sẽ hoàn thiện tại M1 `ps05-native-preview` và `ps13-vertical-slice`). `dev.ps1` sẽ thông báo chẩn đoán rõ ràng và trả về mã lỗi 1 khi binary chưa có sẵn, tuyệt đối không dùng fake stub.

---

## 4. Cơ chế Đăng ký Module Tăng Dần (`cmake/PikDrawModules.cmake`)

Hệ thống build tuân thủ nghiêm ngặt nguyên tắc:
- Chỉ đăng ký module CMake khi module đó **thực sự có implementation** (`CMakeLists.txt` tồn tại trong thư mục module).
- Khi chuyển sang mốc M1+, cờ `PIKDRAW_REQUIRE_M1_TARGETS` được bật; nếu thiếu bất kỳ target bắt buộc nào (`pikdraw_geometry`, `pikdraw_color`, `pikdraw_document`, `pikdraw_rendering`, `pikdraw_desktop`), CMake sẽ báo lỗi dừng ngay lập tức.
- Tuyệt đối không dùng stub trả thành công giả để vượt qua gate.

---

## 5. Xử lý Lỗi Biên dịch Thường Gặp

1. **Không tìm thấy CMAKE_CXX_COMPILER**:
   - Chạy môi trường developer prompt của Visual Studio hoặc bảo đảm `tooling/native/env.ps1` có thể phát hiện Clang/MSVC.
2. **Lỗi cảnh báo compiler biến thành lỗi**:
   - `PIKDRAW_ENABLE_WARNINGS_AS_ERRORS` mặc định bật (`/WX` trên MSVC, `-Werror` trên Clang). Mã nguồn mới phải sửa triệt để cảnh báo trước khi commit.
3. **Lỗi phụ thuộc ngược vào UI**:
   - Nếu một class trong `core/` hoặc `modules/` include header từ `apps/desktop` hoặc Qt Widgets, `check.ps1 -Suite architecture` sẽ chặn lại ngay lập tức.
