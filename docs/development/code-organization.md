# Quy tắc tổ chức mã nguồn cho PikDraw

Trạng thái: quy tắc review cho code mới hoặc code được tiếp nhận từ spike. Bộ kiểm tra tự động là deliverable của ps02; tài liệu này không khẳng định checker đã chạy. Đọc cùng [AGENTS](../../AGENTS.md), [quality gates](../quality/gates.md) và [kế hoạch M1](../roadmap/m1-implementation.md).

## Mục tiêu và cách đếm

Tối ưu khả năng hiểu, sửa và kiểm chứng một tính năng trong context nhỏ. Giảm số dòng bằng cách loại trùng lặp và tách trách nhiệm; không minify, bỏ validation/comment cần thiết hoặc tạo abstraction chỉ để đạt số.

Metric dùng cho giới hạn file là **số dòng không rỗng** sau chuẩn hóa newline; tính cả comment, dấu ngoặc và preprocessor. Report thêm tổng dòng vật lý và loại file. Một dòng chỉ có whitespace là rỗng. Không đếm “logical LOC” bằng regex C++ và không bỏ comment để làm số đẹp.

| Loại file do người viết duy trì | Vùng nên nhắm tới, dòng không rỗng | Trần cần exception trước khi tích hợp |
| --- | --- | --- |
| Public header C/C++ | 80–180 | 300 |
| Implementation C/C++, private header, Rust | 150–350 | 500 |
| Entry point / composition root | 80–180 | 250 |
| Python, PowerShell và script công cụ | 150–300 | 400 |
| Unit/contract/integration test source | 150–300 | 500 |
| Form nguồn Qt `.ui`, một panel/dialog | 150–350 | 600 |
| QSS theo nhóm component/theme | 80–180 | 300 |
| CMake theo target/module | 60–150 | 250 |

Vùng nên nhắm tới không phải số tối thiểu. Một API 40 dòng rõ ràng là hợp lệ. Public header chỉ chứa API/types cần thiết; template implementation cần thiết vẫn tính vào trần. Một hàm nên dưới 40 dòng không rỗng; vượt 60 dòng cần reviewer ghi lý do giữ nguyên hoặc tách theo bước nghiệp vụ. Checker ps02 bắt buộc đếm file; kiểm tra hàm bằng review hoặc AST tool phù hợp, không tuyên bố đã kiểm bằng regex thiếu chính xác.

Phân loại do gate owner khóa, agent không tự chọn cap lớn hơn: generated output trong manifest được loại; runtime entry/composition exact paths dùng cap 250; source dưới test paths dùng cap 500; public include dùng cap 300; còn lại theo loại file trong bảng. Private implementation header và `.inc` dùng cap implementation. Source extension mới phải được reviewer phân loại, không bị bỏ qua chỉ vì chưa có rule. `main.cpp` runtime chỉ bootstrap; helper executor có behavior riêng là implementation, không cần ép mọi file `src/runtime/` thành composition root.

## Chia theo tính năng và trách nhiệm

- Một file có một trách nhiệm chính hoặc một nhóm type nhỏ gắn chặt. Tên nói được nhiệm vụ: `proposal_validator.cpp`, `save_coordinator.cpp`, `selection_tool.cpp`.
- Trong module, nhóm theo feature: `include/pikdraw/<module>/<feature>/`, `src/<feature>/`, `tests/<feature>/`. Public/private boundary theo ps01; file map M1 là gợi ý, không ép di chuyển ABI/header đã được M0 khóa.
- Facade chỉ định tuyến và giữ lifecycle; tách model, commands, snapshots, history, persistence, codecs, UI presenters thành nhóm riêng. Không dồn vào `document.cpp`, `main_window.cpp`, `engine.hpp` hoặc `spike_tests.cpp`.
- Dùng lại behavior đã kiểm chứng; tách implementation khỏi adapter Qt/Skia/OS. Một helper có thể ở cùng file nếu chỉ dùng tại đó. Chỉ đưa thành module dùng chung khi có consumer thật và dependency hợp lệ.
- Không đặt code không liên quan vào `utils`, `common`, `manager` chung. Không tách máy móc thành `part1.cpp`, `part2.cpp` hoặc chuỗi `#include .inc` để lách cap.
- Không tạo hàng loạt file trống, wrapper chuyển tiếp hoặc một file cho mỗi getter. Nếu tách xong chỉ còn các mảnh vài dòng không có ranh giới kiểm thử/API/lifecycle riêng, ghép lại nhóm có quan hệ.
- Format bình thường, mỗi câu lệnh rõ ràng; cấm gộp nhiều statements/nhánh trên một dòng để giảm LOC. Không dùng macro/code generation để giấu code viết tay khỏi kiểm tra.

Ví dụ chia document; chỉ tạo phần có behavior thực sự:

```text
core/document/
  include/pikdraw/document/
    model/object_id.hpp
    model/path_object.hpp
    commands/transaction.hpp
    snapshots/document_snapshot.hpp
    async/job_proposal.hpp
    storage/native_format.hpp
  src/
    commands/transaction_writer.cpp
    snapshots/snapshot_publisher.cpp
    history/undo_history.cpp
    async/proposal_validator.cpp
    storage/native_reader.cpp
    storage/native_writer.cpp
    storage/save_coordinator.cpp
  tests/
    commands/transaction_atomicity_tests.cpp
    async/stale_proposal_tests.cpp
    storage/save_fault_tests.cpp
```

Ví dụ desktop: `main.cpp` chỉ bootstrap; `runtime/` tạo services/executors; `shell/` quản lý cửa sổ; `presenters/` nối view với commands; `ui/panels/` chứa form; `development/` quản lý Reload UI. Core không include Qt để rút ngắn adapter.

## Giữ context nhỏ cho agent

Mỗi module README có bảng feature → public API → implementation → tests, lệnh build/test, dependencies và known limits. Agent đọc task, entry checkpoint và đúng nhóm feature; không đổ toàn bộ catalog, engine hoặc mọi test vào context.

Trước code, ghi file map dự kiến trong task update: file nào tạo/sửa, responsibility, consumer và tests. Khi một file gần vùng trần, chia theo behavior ngay trong checkpoint; đừng đợi end-of-task mới chuyển cơ học hàng nghìn dòng. Handoff kèm file map thực tế, size report và exception còn hiệu lực.

Không di chuyển file thuộc task khác để đạt cap. Rename giữ semantics/consumer includes và chịu ownership check như edit. Root CMake/registry/shared contracts/gates do coordinator hoặc task sở hữu sửa; không cho nhiều agent đồng thời chạm một file đăng ký target.

## Legacy, generated data và exceptions

- File cũ không đổi không buộc refactor chỉ vì M1 bắt đầu. ps02 chụp baseline legacy theo path/revision/metric; thay đổi nhỏ cần exception có phạm vi, không tăng size tùy ý.
- Prototype spike được nâng thành production code phải áp dụng tổ chức file trong task module tiếp nhận. Refactor giữ oracle và behavior; không nâng prototype nguyên khối rồi gọi là hoàn tất.
- Compiler output như moc/uic và dependency vendored nguyên bản được loại bằng manifest path/generator/provenance do gate owner kiểm soát. Marker “generated” tự thêm không cấp exception. Form nguồn `.ui` do Designer tạo nhưng được đội dự án duy trì vẫn chịu giới hạn.
- Fixture/corpus dữ liệu và catalogs không phải source implementation; giữ ngoài hàm test, có schema/rights/checksum khi cần. File JSON không được dùng như nơi chứa chuỗi mã nguồn để tránh cap. Tài liệu không có cap LOC này; task packet vẫn đọc có giới hạn.
- Exception ghi exact path, loại metric, cap được duyệt, lý do kỹ thuật, reviewer, revision và mốc xem lại. Không dùng wildcard/null/unlimited hoặc exception chung cho một provider.
- Reviewer/coordinator duyệt exception trong task có scope gate; đây không phải yêu cầu xin phép người dùng cho refactor thường lệ. Feature agent không tự nâng limit hay sửa exception registry để pass.

## ps02 phải triển khai gate gì

Vị trí dự kiến dưới `tooling/native/quality/`: policy thresholds và exception manifest được review; checker nằm trong `tooling/native/`. Tên cuối được ps02 ghi trong `docs/development/agent-gates.md`. Chưa có các file/lệnh này ở foundation.

1. Report toàn bộ source tracked; hard-fail file mới/sửa vượt cap mà không có exception hợp lệ. File legacy unchanged chỉ báo; grandfathered file bị sửa phải tuân policy không tăng size và giới hạn exception.
2. Kiểm tra actual rename/path và loại file; `.inc`/header chứa implementation không được bỏ sót. Không quét build/cache/dependency ngoài manifest được nhận.
3. Xuất path, category, physical/nonblank lines, cap, exception ID, base/head và kết quả. Dùng formatter trước đếm; formatter không được minify source.
4. Negative fixtures: cap/cap+1, blank/comment/CRLF/Unicode, rename và implementation header, generated-marker giả, exception hết hạn, feature diff sửa threshold/oracle.
5. Checker/policy/exceptions là shared gate; feature tasks không có quyền sửa. CI hoặc integrator chạy trên integrated SHA. Không có checker thì ghi manual review, không ghi “enforced”.

Thành công của gate LOC chỉ chứng minh diff có phạm vi đọc được. Semantics, performance, fidelity, license và containment vẫn theo các gate riêng.
