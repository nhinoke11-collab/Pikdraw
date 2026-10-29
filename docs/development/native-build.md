# Build/test native và phối hợp giữa các task

Quy tắc workflow duy nhất ở [AGENTS](../../AGENTS.md). Trang này là hướng dẫn sử dụng runner;
các ví dụ có ngày trong roadmap/audit là ngữ cảnh lịch sử, không thay cú pháp hiện hành.

Source: C:/Users/CK ART STAGE/Downloads/PikDraw.
Một build tree: build/win-dev. App: build/win-dev/apps/desktop/pikdraw.exe.
Không tạo worktree, nhánh hay build tree riêng để tránh chờ khóa.
Runner cần Python 3.10+; env.ps1 tự tìm, có thể chỉ định bằng PIKDRAW_PYTHON.

## Build đúng phạm vi

~~~powershell
# Auto: giữ Debug/Release đã configure; build tree mới bắt đầu bằng Debug.
.\tooling\native\build.ps1

# Chỉ biên dịch và chạy test của target. Không cần lặp tên trong regex.
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_storage

# Nhiều target: dùng array PowerShell khi gọi trực tiếp script.
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_effect_draft','test_pikdraw_effect_render_warning')

# Chỉ build target benchmark; không gọi CTest.
.\tooling\native\build.ps1 -Test -TestTarget benchmark_pikdraw_rendering_effects -BuildOnly

# Full suite là yêu cầu tường minh, gồm build all và CTest.
.\tooling\native\build.ps1 -Test -AllTests

# Đổi cấu hình là chủ động; Debug/Release cùng dùng một build tree.
.\tooling\native\build.ps1 -Configuration Release
~~~

`-Test` không có target hoặc `-AllTests` bị từ chối trước khi gửi hàng đợi.
Các task sửa thường ngày giữ Auto; gom kiểm tra Release thành một đợt thay vì đảo cấu hình qua lại.
`-TestTarget` vừa giới hạn biên dịch vừa giới hạn test:
ưu tiên CTest trùng tên target; nếu không có, tìm test theo tên executable trong CTest.
Khi một executable có nhiều test, ưu tiên test chính trùng tên, tránh lặp các alias focused.
Không tìm được mapping thì dừng và yêu cầu `-TestRegex`; không tự rơi về full suite.

`-TestRegex` chọn tên CTest, vẫn cần `-TestTarget` để biết phần phải build. Ví dụ:

~~~powershell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_rendering_rasterizer -TestRegex '^test_pikdraw_swap_chain_benchmark$' -VerboseTests
~~~

`-TestsOnly -TestRegex '<regex>'` hoặc `-TestsOnly -AllTests` chỉ chạy binary đã có.
Nó kiểm tra cấu hình nhưng không xác nhận binary đã phản ánh source vừa sửa.
Sau sửa code, dùng `-Test` để Ninja cập nhật dependency trước khi test.
Không kết hợp `-TestsOnly` với `-Submit`, `-Clean` hoặc `-Reconfigure`.
`-VerboseTests` giữ output CTest trong lượt chạy; output tạm vẫn được dọn khi thành công.

## Nhiều task dùng chung hàng đợi

~~~powershell
# Chỉ đọc owner, các yêu cầu gần nhất, target, phase, lỗi và thời gian từng bước.
.\tooling\native\build.ps1 -Status

# Gửi rồi tiếp tục làm việc độc lập; runner in một mã 32 ký tự hex.
.\tooling\native\build.ps1 -Submit -Test -TestTarget test_pikdraw_document_storage

# Thay chuỗi bên dưới bằng mã yêu cầu đã nhận, không gửi lại cùng việc.
.\tooling\native\build.ps1 -Status -RequestId '<ma-yeu-cau>'
.\tooling\native\build.ps1 -Wait -RequestId '<ma-yeu-cau>' -LockWaitSeconds 60
# Hủy yêu cầu còn trong hàng đợi nếu task không cần nó nữa.
.\tooling\native\build.ps1 -Cancel -RequestId '<ma-yeu-cau-dang-cho>'
~~~

Mỗi yêu cầu có worker nền giữ môi trường của chính caller; các worker xếp FIFO.
Chỉ một worker giữ .agent/native-build.lock để configure/build/test.
Runner cũ hoặc cleaner đang giữ khóa vẫn được tôn trọng. Khóa trống/không rõ owner không tự bị xóa.
Khóa PID đã chết chỉ được thu hồi sau khi không còn CMake/Ninja/compiler/linker/CTest hoạt động;
không suy ra worker con đã chết chỉ từ PID cha.

Lệnh thường chờ hàng đợi tối đa `-LockWaitSeconds` (mặc định 5 giây); khi đã bắt đầu thì theo dõi tới kết quả.
Nếu hết thời gian chờ, PowerShell báo **still queued**, yêu cầu vẫn tiếp tục nền.
Dùng mã đó với `-Status/-Wait`; không resubmit. `-Submit` trả ngay sau khi gửi.
Đóng cửa sổ caller không hủy yêu cầu đã gửi. Worker chờ khóa tối đa một giờ rồi báo lỗi.
Không dùng `-Force/-NoLock`: các bypass cũ bị từ chối.
`-Cancel` chỉ gửi yêu cầu hủy việc đang chờ; xem `-Status` để xác nhận `cancelled`.
Nếu worker đã bắt đầu, nó tiếp tục chạy; lệnh này không kill compiler hoặc app.

State nhỏ dùng chung ở .agent/native-build, gồm mã, owner, phase, source digest, test và kết quả.
Đây là metadata vận hành tự động, không là task board/lease/handoff; không ghi environment/token vào file.
Các agent đọc cùng state để phối hợp, không bắt buộc nhắn từng chat hay cần một chat điều phối sống liên tục.

Yêu cầu **giống nhau và chồng thời gian** có thể dùng một kết quả thành công nếu source, cấu hình,
toolchain, environment và hash binary còn khớp; Ninja dry-run phải không còn việc cần làm.
Source digest tính local edits, file mới và file bị xóa, không chỉ Git HEAD.
Code đổi trong lượt chạy làm kết quả thành `stale`, không xác nhận source hiện tại.
Yêu cầu được xếp hàng có thể chạy trên source mới hơn lúc gửi; receipt ghi digest thực lúc chạy.

Không dùng cache test lịch sử: yêu cầu gửi sau khi lượt trước đã hoàn thành sẽ kiểm tra lại.
Không gộp test RUN_SERIAL, fixture lifecycle, UI/E2E/benchmark, nhãn windows/gpu/isolated-import,
hoặc test có corpus ngoài repo qua biến PIKDRAW_*_DIR/PATH/FILE/ROOT.
Dùng `-NoReuse` nếu cần lượt riêng. Không gộp custom/package target; các tác vụ staging vẫn do Ninja quyết định.
State `interrupted` chỉ nói worker đã thoát, không xác nhận compiler con hay binary đã hoàn thành.

## Build lỗi và chạy tiếp

~~~powershell
# Retry từ đúng yêu cầu lỗi; không clean, không lấy LastTestsFailed dùng chung của task khác.
.\tooling\native\build.ps1 -RetryFailed '<ma-yeu-cau-loi>'
.\tooling\native\build.ps1 -Submit -RetryFailed '<ma-yeu-cau-loi>'
~~~

Sửa nguyên nhân rồi retry. Ninja giữ object/cache và chỉ làm phần mất hiệu lực;
lỗi linker thường chỉ cần link lại khi các input không đổi.
Lỗi CTest có JUnit sẽ lưu danh sách test lỗi trong receipt riêng; retry build lại dependency rồi chạy nhóm lỗi đó.
Nếu chưa có danh sách lỗi (configure/build/crash), retry giữ phạm vi yêu cầu trước.
Retry không tự lặp vô hạn, không tự sửa code hoặc bỏ test lỗi. Retry subset không được báo thành full suite pass.
Worker bị ngắt cần kiểm tra owner/lock rồi gửi lại; không tự xóa khóa. Retry TestsOnly bị từ chối vì không thể build source fix.
Retry giữ cấu hình/scope trước, có thể ghi đè `-Configuration`, `-Jobs`, `-TestJobs` và `-VerboseTests`.

`-Clean` chỉ dùng khi thực sự cần bỏ incremental output; không là cách mặc định chữa lỗi.
`-Reconfigure` chỉ tái sinh build graph, không xóa object. CMake/Ninja tự kiểm tra CMakeLists/dependency khi build.
App đang mở có thể khóa linker: lưu tài liệu và đóng app khi cần; runner không tự kill PikDraw.

## Mức song song và giới hạn bằng chứng

`-Jobs N` điều chỉnh compile/link. `-Jobs 0` tự chọn theo CPU và RAM đang trống, tối thiểu 1,
ước tính 640 MiB/job sau khi chừa 256 MiB; máy từ 4 CPU logic chừa một CPU cho desktop.
Ví dụ 4 CPU, RAM trống 2,1 GiB chọn 2 job; RAM trống khoảng 3,5 GiB chọn 3 job.
Đây là heuristic để tránh mặc định chạy tuần tự, không là cam kết peak RAM hay tốc độ.
`-TestJobs N` điều chỉnh CTest, mặc định 1; tăng với subset đã biết độc lập.
CTest tiếp tục tôn trọng RUN_SERIAL/resource locks của suite.
Ninja giới hạn một bước link/archive tại một thời điểm qua pool pikdraw_link; compile vẫn song song.
PCH đã bật, chỉ chứa STL/OS headers ổn định; không hứa thời gian build cố định.

Receipt có thời gian configure/build/discovery/test để tìm nút thắt. Queue bỏ việc trùng và scope hẹp giảm công việc;
một build tree vẫn phải tuần tự hóa các writer. Build/test pass không chứng minh FPS, fidelity import, UX hay máy CNC.
Status/log ghi cấu hình trước/sau, cảnh báo khi đổi cấu hình, số job, CPU/RAM và số bước đã hoàn thành.
Số bước Ninja gồm PCH, compile, archive, link và staging, không phải số file có cùng chi phí.
Không suy ETA từ tỷ lệ bước hoặc tốc độ file/phút; bước cuối có thể tốn nhiều thời gian hơn.
Sửa -Jobs/chính sách chỉ áp dụng khi khởi động lượt Ninja mới; không đổi -j của process đang chạy.

## Output và cleanup

Output mới ở build/win-dev/test-output/<pid>-<request-id>, có marker .pikdraw-test-run.
CTest nhận PIKDRAW_TEST_OUTPUT_DIR; test/probe mới dùng thư mục đó hoặc temp riêng, RAII/finally tự dọn.
Build log/JUnit/output của lượt thành công được xóa; lượt lỗi/stale giữ để đọc rồi dọn.
Receipt JSON nhỏ không thay log chẩn đoán; giữ tối đa 128 kết quả đã được dọn output.

~~~powershell
.\tooling\native\clean-test-artifacts.ps1
.\tooling\native\clean-test-artifacts.ps1 -Apply -RunId '<pid>-<request-id>'
~~~

Cleaner không chạy Apply khi có build lock. Nó chỉ xóa output được xác minh, không xóa source/fixture,
toolchain, object/cache, binary canonical, backup/recovery hay tài liệu khách hàng.
Không lưu dump/screenshot/file thiết kế test trong source hoặc docs.
Giữ cả PCH (*.pch/*.gch), object (*.obj/*.o), thư viện (*.a/*.lib), test executable, CMakeCache,
.ninja_log và .ninja_deps. Chúng là cache biên dịch, kể cả executable có tên bắt đầu bằng test_.
Giữ CTestCostData.txt để CTest dùng lịch sử thời gian khi sắp lịch test song song.
Không dọn bằng cách quét extension trên toàn build tree. Mất PCH làm nhiều consumer phải compile lại
dù source và cấu hình không đổi; runner báo số PCH từng được build nhưng nay bị thiếu trong log/Status.
Runner dùng CMake/Ninja đã ghi trong CMakeCache nếu chúng còn tồn tại, tránh đổi công cụ giữa các task.

## Wrapper và package

tooling/pikdraw.ps1 là alias tiện gọi, không là một build system khác.
dev.ps1 -Build cũng giữ cấu hình Auto. check.ps1 -Suite unit/all chạy full suite tường minh;
e2e chọn target của mình, không dùng binary cũ để báo source mới đã pass.
tooling/native/package.ps1 -Configuration Release theo [Windows package](windows-package.md),
dùng target CPack dưới khóa chung. Package không được gộp/reuse như test thường.

Workflow cũ lặp ở ADR 0007 đã bỏ. Nội dung thay thế là AGENTS và trang này;
không khôi phục task create/claim/lease, context packet hoặc chỉ dẫn worktree cũ.
