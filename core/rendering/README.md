# Rendering — bản nháp spike A

Đã có primitives mm/binary64, camera trừ origin trước khi chuyển float, fixtures nhiều path/vài path trên 100.000 điểm, culling tuyến tính và trace CPU có giới hạn. Native math tests đã chạy; đây chưa là benchmark viewport.

`ExperimentalSceneSnapshot` là dữ liệu thử riêng, chưa phải immutable document contract dùng chung. `world_bounds_mm` phải là bounds thế giới đã tính từ transform; bộ dựng scene chịu trách nhiệm tính đúng. Không dùng renderer types làm storage/plugin API.

`PIKDRAW_ENABLE_SKIA=ON` yêu cầu thư viện Skia thật và API `SkSurfaces::WrapPixels`; chưa pin/build/kiểm thử Qt/Skia tại phiên này. Không có target console thay thế viewport. Các prebuild MSVC hoặc MinGW libstdc++ không được trộn với LLVM-MinGW libc++.

Raster backend mới hỗ trợ path; từ chối text và stroke có scale/shear/rotation thay vì im lặng bỏ nội dung. Chưa có image, gradient/filter/compositing oracle, GPU backend, R-tree/cache hoặc hit-test. Recovery trả chưa đo/không hỗ trợ; không dùng mã lỗi giả làm bằng chứng device-loss.

Giới hạn tọa độ khảo sát camera là ±10.000.000 mm. Native tests kiểm lỗi NaN/Inf, giá trị hữu hạn quá miền, inverse và tràn kích thước pixel buffer. Tracer chỉ có CPU/submission; actual present/input-to-visible/memory và hardware matrix đều chưa đo. Không đóng gate A.
