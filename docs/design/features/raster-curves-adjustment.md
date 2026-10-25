# Chỉnh Curves ảnh raster

Action action.bitmap.adjust.curves được mở từ Bitmaps > Curves… khi chọn đúng một ảnh có nguồn
được giữ lại, chưa bị khóa và đã có mẫu sRGB được giải nghĩa. Apply ghi hoặc cập nhật một
RasterCurvesParams trong graph ảnh qua SetRasterEditNodeCommand; cùng node ID được dùng khi mở
lại, lệnh có Undo/Redo, còn Cancel không đổi document.

V1 dùng năm điểm X cố định tại 0%, 25%, 50%, 75% và 100%. Người dùng nhập từng điểm đầu ra
trong 0–100% và chọn RGB tổng hợp hoặc riêng R/G/B. Evaluator nội suy tuyến tính trên giá trị
sRGB mã hóa, giữ alpha, chạy trên RGBA8 và RGBA16LE, có hủy công việc và tuân thủ memory budget.
Đây là tone curve RGB; không chỉnh trực tiếp separation CMYK, Gray hoặc Lab.

Trường có label và unit phần trăm, tab order từ Channel qua năm output point rồi Apply/Cancel.
Hộp thoại ghi rõ nội suy tuyến tính và alpha được giữ. Status hint sau Apply nêu Curves đã cập
nhật, preview đang render và Ctrl+Z để hoàn tác. Không dùng icon SVG riêng vì đây là mục menu,
không có toolbox/bar/properties control cho V1; các điểm kéo trực tiếp trên đồ thị và chỉnh sâu
trong Properties vẫn ở roadmap.

Trong .pr, Curves được giới thiệu ở graph schema v6 và cần algorithm version 1; graph hiện hành
là v8, readers v1–v8 tiếp tục đọc/migrate, còn loại node không hỗ trợ phải báo lỗi. Hash preview bao gồm
channel và mọi điểm output. TIFF export áp node graph trước khi chuyển sang output profile.

Acceptance đã chạy:

- U8: chỉnh riêng Red, giữ Green/Blue và alpha.
- U16: mẫu kề nhau quanh điểm điều khiển vẫn cho output khác nhau.
- Identity: buffer giữ nguyên bit-for-bit; điểm ngoài [0, 1] bị từ chối.
- .pr: graph v8 giữ channel, năm số, ID và algorithm version qua save/reopen.
- Menu/action: ID, nhãn EN/VI và routing test pass.

Chưa xác nhận thao tác cửa sổ native trực quan; V1 chưa có đồ thị tương tác, preview trong dialog,
mask, curves parametric/spline, picker điểm từ ảnh hoặc channel CMYK.
