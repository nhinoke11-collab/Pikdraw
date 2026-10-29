# Quy tắc làm việc PikDraw

Đây là nguồn duy nhất cho workflow của Codex, Claude và các agent khác trong repo.
CLAUDE.md/GEMINI.md chỉ dẫn tới file này; tài liệu feature và roadmap không được đặt workflow khác.

## Source và build

- Làm trực tiếp tại source gốc C:/Users/CK ART STAGE/Downloads/PikDraw. Không tạo worktree, clone hoặc nhánh task.
- Thực hiện yêu cầu trực tiếp; không yêu cầu task create/claim/handoff/lease hay nạp context packet hành chính.
- Đọc Git status trước khi sửa. Giữ local edits; không reset, stash, clean hoặc ghi đè code của người dùng.
- Native build/test qua tooling/native/build.ps1, dùng chung khóa .agent/native-build.lock và build/win-dev.
  App duy nhất: build/win-dev/apps/desktop/pikdraw.exe. Không xóa khóa của tiến trình đang chạy.
- Build giữ cấu hình hiện có theo mặc định. Chọn test theo target; full suite phải dùng -AllTests.
  Nhiều task dùng -Status/-Submit và mã yêu cầu chung; yêu cầu đang chờ thì theo dõi -Wait, không gửi lại.
  Hàng đợi là tự động, không thêm thủ tục claim/lease. Cú pháp và retry xem docs/development/native-build.md.
- Trong vòng sửa thường dùng Auto; gom kiểm tra Release thành một đợt, tránh đảo Debug/Release theo từng task.
  Tiến độ Ninja là số bước khác chi phí, không suy phần trăm thời gian hoặc ETA từ số file/phút.
- Chọn target/test đúng phạm vi. Commit chỉ phần đã hoàn thành và kiểm chứng khi có thể tách khỏi local edits khác.

## Kiến trúc và dữ liệu

- registry/modules.json quyết định dependency. Core/module không phụ thuộc apps/desktop.
- Runtime: C++20/Win32, Direct2D 1.1/D3D11 hardware hoặc WARP, đọc frame về BGRA rồi GDI trình bày.
  SoftwareRasterizer là fallback/oracle. Qt/Skia chưa phải runtime; không bắt buộc đổi framework để thêm module.
- Document writes qua command/transaction và một logical writer. Giữ IDs, Undo/Redo, đơn vị thật,
  Unicode, màu/profile, metadata và revision. Snapshot/asset bất biến; async kiểm epoch/revision/generation trước commit.
- Importer/plugin output là dữ liệu chưa tin cậy: kiểm schema, capability, kích thước, số lượng và nội dung trước áp dụng.
- Tính toán nặng phải có cancellation, quota và progress; không chờ worker/network/AI trong UI event loop.
- File thiết kế chỉ dùng .pr. Extension không cấp capability hoặc xác nhận schema.
  Payload schema v23 dùng container v23 dưới 1 MiB hoặc container chunk v24 với STORE/DEFLATE.
  Schema v24 deduplicate encoded source và buffer RGBA8/RGBA16 trong container chunk v25;
  schema v25 lưu LayerId/group/role trong container chunk v26; reader nhận v2–v26.
  Không còn trần toàn file 256 MiB hoặc quota cứng
  100k object/1m path element; wire widths và memory quota vẫn giới hạn dữ liệu đọc.
  Streaming trực tiếp, index và lazy assets trong docs/architecture/native-pr-format.md chưa triển khai.
- Mỗi file code tối đa 2000 dòng, khuyến nghị 300–1800. Tách theo domain trước khi thêm tính năng vào file quá dài.
  Xem docs/development/code-organization.md.

## Đọc tài liệu theo việc

Bắt đầu bằng docs/START.md và registry/module liên quan; chỉ mở contract/feature cần dùng.
docs/project-state.json là snapshot hiện hành, tối đa 8000 byte, không phải nhật ký.
docs/README.md chỉ mục nguồn hiện tại; roadmap/ADR cũ chỉ dùng làm ngữ cảnh, phải đối chiếu source.

Với vector PDF/AI/CDR/EPS/SVG/DXF, đọc docs/architecture/vector-resource-interoperability.md và C8/C9.
Plan nêu adapter theo format/version/representation, đưa vào document trung lập rồi dùng chung semantics
đã kiểm chứng. Báo riêng render, editability, provenance, source export round-trip và production.
Nêu định dạng không áp dụng; không ép tất cả qua một parser làm mất nghĩa.

Với UI, đọc docs/design/ui-ux-standard.md và docs/architecture/workspace-layout.md.
Spec ghi menu/action ID, toolbox/toolbar, property bar/Properties, field scopes/states, SVG icon,
label, tooltip/focus help, status hint và hướng dẫn dùng; mục không áp dụng ghi rõ.
Dùng bề mặt hiện có, agent tự quyết định lựa chọn thường lệ và tiếp tục triển khai.

## Test và dọn output bắt buộc

- Ưu tiên mở rộng regression test của module. Test có giá trị tiếp tục ở trong suite; không xóa cả tests/.
- Probe/source dùng một lần, ảnh chụp, fixture tự sinh, file thiết kế test, dump, trace và log tạm phải xóa
  sau khi đã đọc và báo kết quả, kể cả lượt test thất bại. Sửa test để tự dọn khi thoát/exception.
- Output mới đặt trong build/win-dev/test-output/<run-id> hoặc thư mục tạm riêng có owner;
  dùng biến PIKDRAW_TEST_OUTPUT_DIR khi runner cung cấp. Không tạo output test ở root/source/docs.
- Runner dọn thư mục output của lượt CTest thành công. Khi thất bại đọc log cần thiết, sửa lỗi và
  dọn phần còn lại bằng tooling/native/clean-test-artifacts.ps1. Không giữ dump lặp làm tài liệu.
- Chỉ xóa path đã xác minh nằm trong workspace/output riêng và đã xác định là generated.
  Không xóa fixture dùng lại, test đã đăng ký, source, toolchain, build cache/binary canonical,
  dữ liệu khách hàng, backup hay recovery của người dùng.
  PCH (*.pch/*.gch), object (*.obj/*.o), thư viện (*.a/*.lib), test executable, CMakeCache,
  .ninja_log và .ninja_deps là build cache cần giữ, không phải output test tạm.
  Dọn output bằng clean-test-artifacts.ps1; không quét/xóa theo extension trên toàn build tree.
- Báo trực tiếp lệnh, kết quả, paths thay đổi và giới hạn. Không tạo handoff/snapshot trung gian;
  không suy ra FPS/UX/fidelity/containment từ build hoặc test không đo các thuộc tính đó.
- Tài liệu hướng người dùng viết tiếng Việt; contract kỹ thuật có thể dùng tiếng Anh.
