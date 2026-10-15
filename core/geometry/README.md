# Geometry — bản nháp spike B

Source curves và transforms dùng mm/binary64. Manufacturing approximation tách khỏi source; không lấy tolerance viewport làm tolerance dao.

Đã có affine transforms, bounds bảo thủ theo control polygon, Boolean/offset hình chữ nhật analytic. General path Boolean trả diagnostic chưa hỗ trợ; chưa có Clipper2/CGAL, topology holes/self-intersections và error budget toàn pipeline được nghiệm thu.

Cubic subdivision kiểm khoảng cách control points tới đoạn chord hữu hạn, giữ được zero-chord loop và collinear overshoot; không âm thầm chấp nhận khi hết depth/point budget. Single-contour approximation giữ Close, từ chối nhiều contour trong một polyline và tolerance ngoài miền [1e-9, 1e6] mm. Giới hạn input ±10.000.000 mm. Không coi khoảng sai số khảo sát này là cam kết CNC.

Native tests có vectors 20 m/reflection/nonuniform transform, source preservation, rectangle subset và oracle analytic cho loop. Còn thiếu corpus production cho tangent/near-contact/holes/slivers/degenerate và đơn vị/parser boundary. Các API vẫn là prototype private, chưa khóa storage/plugin contracts.

SourcePath nay chỉ cho đọc `segments()`; append/set/reserve là các mutator có kiểm miền, không trả reference mutable. Các bản copy chia sẻ storage; edit detach, bounds control polygon được giữ cùng storage và cập nhật khi edit. Giới hạn prototype 250.000 segments/path. Reserve/point edit có thể copy storage khi shared; cần đo và prepare ngoài UI với path lớn. Không coi COW value là thread-safe khi hai thread cùng sửa một SourcePath instance.
