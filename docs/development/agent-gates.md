# Cổng Kiểm Soát Kiến Trúc và Quy Chuẩn Cho Agent (Agent Gates)

Tài liệu này xác định các bất biến kiến trúc cốt lõi và tiêu chuẩn nghiệm thu mà mọi phiên làm việc của agent (Codex, Gemini, Claude, human engineer) đều phải tuân thủ tuyệt đối.

---

## 1. Các Bất Biến Kiến Trúc Bắt Buộc

### Bất biến 1: Cấm tuyệt đối Module phụ thuộc ngược vào UI (No Module-to-UI)
- Các module thuộc `core/` (`geometry`, `color`, `typography`, `document`, `rendering`), `modules/` (`vector`, `raster`, `production`, `scene3d`, `interop`) và `platform/` (`workers`) **không bao giờ được phép** include header từ `apps/desktop/`, `<pikdraw/desktop/...>`, hay các widget UI (`QtWidgets/QWidget`, `QApplication`, `QMainWindow`).
- Tầng UI (`apps/desktop`) đóng vai trò điều phối (orchestrator), gọi xuống các services của module. Các module hoàn toàn độc lập với công nghệ hiển thị UI cụ thể.
- Gate kiểm tra: `tests/architecture/test_architecture_gates.py` quét toàn bộ mã nguồn non-UI và fail ngay nếu phát hiện include vi phạm.

### Bất biến 2: Đồ thị Phụ thuộc là DAG (Directed Acyclic Graph)
- Mối quan hệ phụ thuộc giữa các module được định nghĩa tường minh tại `registry/modules.json`.
- Tuyệt đối không chấp nhận quan hệ phụ thuộc vòng (circular dependencies) trực tiếp hoặc bắc cầu.
- Gate kiểm tra: Thuật toán kiểm tra chu trình DFS trong suite kiến trúc sẽ chặn bất kỳ PR hoặc commit nào tạo thành chu trình.

### Bất biến 3: Tuân thủ Phạm vi Sở hữu (Owned Paths) và Khóa Tác vụ (Task Leases)
- Mỗi task chỉ được phép chỉnh sửa các tệp tin và thư mục nằm trong danh sách `owned_paths` đã được phân công và khóa bằng lease hợp lệ.
- Không được phép ghi đè hoặc chỉnh sửa các tệp của module khác mà không có task phối hợp được phê duyệt.
- Làm việc trên nhánh riêng biệt và worktree cách ly (`.worktrees/<provider>-<task-id>`).

### Bất biến 4: Bảo vệ Toàn vẹn Hợp đồng Chung, Fixtures và Oracles
- Các hợp đồng C1–C7 tại `contracts/`, bộ fixtures tại `docs/quality/reference-fixtures/` và oracles tại `docs/quality/oracles/` là căn cứ nghiệm thu khách quan.
- Nghiêm cấm việc sửa đổi expected results của oracle hoặc hạ thấp tiêu chuẩn kiểm thử chỉ để làm xanh test case của module đang triển khai.
- Sửa đổi contract dùng chung yêu cầu task scoped riêng và có sự đồng thuận từ các module tiêu thụ (consumers).

### Bất biến 5: Single Writer và Snapshot Bất Biến trong Document
- Mọi thao tác thay đổi trạng thái tài liệu phải đi qua Logical Writer duy nhất và hoàn tất bằng transaction nguyên tử (atomic transaction).
- Các thành phần đọc (render, export, preview, preflight) chỉ đọc trên Snapshot bất biến (COW Immutable Snapshot) có gắn số hiệu revision đơn điệu.
- Nghiêm cấm áp dụng proposal trả về trễ (stale proposal) sau khi revision hoặc epoch đã thay đổi.

### Bất biến 6: Cô lập Tiến trình (Process Isolation) đối với Parsers Bên thứ ba
- Bộ phân tích định dạng ngoài (PDF, AI, CDR, DXF) phải chạy trong tiến trình worker độc lập, được giới hạn nghiêm ngặt bởi AppContainer và Job Objects.
- Host chính tuyệt đối không transitive-link các thư viện parser bên thứ ba vào tiến trình shell.

---

## 2. Danh mục Kiểm tra Bắt buộc Trước khi Bàn giao (Handoff Checklist)

Trước khi thực hiện lệnh handoff một task, agent phải chạy và thu thập bằng chứng đầy đủ từ các công cụ:

1. **Kiểm tra Harness Baseline**:
   ```powershell
   .\tooling\pikdraw.ps1 doctor
   python -m unittest discover -s tests/harness -v
   ```
2. **Kiểm tra Cổng Kiến trúc và Hợp đồng**:
   ```powershell
   python -m unittest discover -s tests/architecture -v
   .\tooling\native\check.ps1 -Suite all
   ```
3. **Kiểm tra Build và Test Native C++ (nếu task có mã nguồn native)**:
   ```powershell
   cmake --preset win-dev
   cmake --build --preset win-dev
   ctest --preset win-dev --output-on-failure
   ```
4. **Kiểm tra Phạm vi Diff**:
   - `git status` và `git diff --stat` phải chứng minh 100% thay đổi nằm trong `owned_paths`.
5. **Ghi nhận Trung thực Bằng chứng Nghiệm thu**:
   - Báo cáo chính xác số đo hiệu năng, mã lỗi OS denial, số lượng test case vượt qua.
   - Không được ghi `pass` cho các điều kiện chưa được đo đạc (`not_measured`) hoặc thất bại (`fail`).
