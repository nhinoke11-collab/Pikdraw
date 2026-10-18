# PikDraw Native Tooling

Cung cấp công cụ phát triển, biên dịch và kiểm thử trực tiếp cho PikDraw.

## 1. CLI điều phối nhanh (`tooling/pikdraw.ps1`)

Entry point duy nhất cho các tác vụ thường ngày:

- **Kiểm tra môi trường / sức khỏe repo:**
  ```powershell
  .\tooling\pikdraw.ps1 doctor
  ```
- **Biên dịch ứng dụng (Native Win32 C++20):**
  ```powershell
  .\tooling\pikdraw.ps1 build
  ```
- **Chạy kiểm thử C++:**
  ```powershell
  .\tooling\pikdraw.ps1 test
  .\tooling\pikdraw.ps1 test -TestTarget test_svg_dxf_import
  ```
- **Chạy kiểm tra kiến trúc / hợp đồng:**
  ```powershell
  .\tooling\pikdraw.ps1 check -Suite architecture
  ```
- **Khởi chạy ứng dụng:**
  ```powershell
  .\tooling\pikdraw.ps1 run
  ```
- **Đóng gói bản phát hành:**
  ```powershell
  .\tooling\pikdraw.ps1 package
  ```

## 2. Các script thực thi native (`tooling/native/`)

- `build.ps1`: Trình chạy build native với chính sách single build tree (`build/win-dev`) và khóa build an toàn `.agent/native-build.lock`.
- `check.ps1`: Trình kiểm tra tự động kiến trúc (architecture gates), fixtures và contracts.
- `env.ps1`: Tự động tìm kiếm toolchain cục bộ (Python 3.10+, CMake, Ninja, Clang, MSVC).
- `dev.ps1`: Khởi tạo môi trường phát triển Win32.
- `package.ps1`: Đóng gói Release desktop, MCP sidecar và COM local server.
