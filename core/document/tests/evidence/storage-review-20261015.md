# Kiểm chứng sửa native storage — 2026-10-15

Base: `995f27b3fa0f74e9e15f4a3859228670bf61c9b2`. Đây là kiểm chứng module trên worktree
storage, chưa phải bản tích hợp main hoặc nghiệm thu C5/M1. Hash source được ghi riêng trong
`storage-source-hashes.json`; log CTest cuối ở `storage-ctest-debug.txt`.

## Thay đổi đã kiểm chứng

Codec prototype v2 giữ source CMYK/spot/gray, alpha/overprint, binary64, fill rule,
font identity/Unicode, transform, layer/group/order và document ID. Reader kiểm tra checksum
cả header/payload, length/count/tag/structure; không cấp phát theo count ngoài budget.
V1 lossy bị từ chối rõ ràng. File save dùng exclusive temp, OS flush, atomic replace;
lỗi không xóa file nguồn. Recovery xác minh ID trong container trước lookup/cleanup.
Mở lại tạo lifecycle mới, giữ nội dung editable/undo và trạng thái saved đúng revision.

## Kết quả thực tế

- LLVM-MinGW Clang 23.1.3, C++20 Debug; CMake build toàn bộ: exit 0.
- CTest toàn worktree trước bổ sung diagnostics cuối: **22/22 pass**, gồm harness gate.
- CTest cuối `-R '^test_pikdraw_document'`: **7/7 pass**, 3.59 giây.
- `-Wall -Wextra -Wpedantic -Werror -fsyntax-only`: **8 translation units pass**,
  gồm document.cpp, 6 storage .cpp và test_storage_fidelity.cpp.
- `pikdraw.ps1 doctor`: ok=true, 15 modules, không có issue. `git diff --check`: pass.

Fixture mới kiểm source properties chính xác sau serialize/load, sửa/undo sau reopen,
saved-epoch collision, CRC32 known answer, header corruption, mọi prefix truncation,
null input, count lớn có checksum hợp lệ, ba fault stages AfterWrite/BeforeFlush/BeforeReplace,
giữ bytes file cũ, không rò temp, đường dẫn tiếng Việt, ID có `../` và ID dài 512 byte,
checkpoint mismatch và lỗi thư mục có diagnostic.

## Các lần thất bại được giữ trong đánh giá

Lần gọi executable/CTest trong shell chưa nạp LLVM DLL PATH trả `0xc0000135`; không tính là
test pass. Chạy lại sau `Get-PikDrawToolchain` đã nạp đúng runtime mới có kết quả phía trên.
Fixture checkpoint ban đầu thất bại với filename hex toàn bộ ID: target 253 ký tự, temp suffix
thêm khoảng 40 ký tự trong working directory CTest này. Lookup hint cố định cùng kiểm tra ID
trong container sửa trường hợp đó; không chứng minh mọi đường dẫn dài tùy ý trên Windows.

## Giới hạn còn lại

Chưa chạy Release/MSVC hoặc consumer tests trên merged SHA; cần chạy ở bước tích hợp.
Chưa có mất điện thực, recovery index/quota/scheduler/UI, assets/ICC bindings/migration schema C5,
external parser containment, benchmark hoặc ảnh giao diện. POSIX file I/O chưa được chạy trên máy này.
Không dùng các kết quả trên để tuyên bố hoàn thành M1.
