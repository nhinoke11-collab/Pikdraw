# Kết nối và công bố repository — 15/10/2026

Remote: https://github.com/nhinoke11-collab/Pikdraw (PUBLIC).

`origin` được gắn qua HTTPS; tài khoản GitHub đã xác minh là nhinoke11-collab. Git author dùng login và địa chỉ GitHub noreply trong cấu hình local của repository, không đổi cấu hình global.

Repo từ xa có initial commit b49dbcc và lịch sử local bắt đầu riêng. Hợp nhất hai lịch sử, giữ cả parent, không force push. Main nhận foundation/ADR/kế hoạch đã có trên local; README của PikDraw được giữ, .gitignore kết hợp với danh sách C++ artifacts của remote.

Theo lựa chọn trực tiếp của chủ repo, LICENSE trên main giữ nguyên Apache-2.0 của GitHub. Thông báo MIT của foundation trước đó được giữ ở LICENSES/MIT-foundation.txt.

Source ba spike và build bootstrap được lưu trên bốn nhánh agent hiện có; đây là các bản nháp chưa nghiệm thu. Không ghép runtime drafts vào main hoặc coi unit tests là performance/fidelity/containment gates đã pass. Các branch vẫn có baseline license/history trước khi đồng bộ main; lần tích hợp sau cần xử lý theo license/notices trên main.

Nhánh dự kiến công bố:

- main
- agent/codex/github-publication-20261015 (commit hợp nhất để truy vết)
- agent/codex/spike-leader-bootstrap-20261015
- agent/gemini/spike-a-viewport-20261015
- agent/gemini/spike-b-document-20261015
- agent/gemini/spike-c-interop-20261015

Chỉ source/tài liệu đã kiểm tra được stage/commit. Không publish .agent runtime/cache/toolchains, .worktrees directories, out/build, binaries, credentials hoặc provider response logs. Local uncommitted localization trong worktree cũ được giữ nguyên; không tự ghép vào publication.

Bằng chứng cuối cùng về commit/remote SHAs và trạng thái push được ghi trong task board/handoff cục bộ sau thao tác; không dự đoán push thành công chỉ từ tài liệu này.
