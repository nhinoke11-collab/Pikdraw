#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"

namespace pikdraw::desktop {

DesktopWindow::DesktopWindow(double width, double height, double dpi_scale)
    : width_(width), height_(height), dpi_scale_(dpi_scale) {
    setup_default_actions();
}

void DesktopWindow::set_window_size(double width, double height) noexcept {
    width_ = width;
    height_ = height;
}

void DesktopWindow::set_dpi_scale(double scale) noexcept {
    dpi_scale_ = scale;
}

ShellGeometry::CanvasArea DesktopWindow::canvas_area() const noexcept {
    return ShellGeometry::compute_canvas_area(
        width_,
        height_,
        dpi_scale_,
        inspector_.is_visible(),
        inspector_.width(),
        ShellGeometry::kDefaultToolStripWidth
    );
}

void DesktopWindow::setup_default_actions() {
    using namespace actions;

    // File Actions (CorelDRAW Reference)
    dispatcher_.register_action({
        std::string(ActionId::kFileNew), "Tạo mới...", "New...", "Ctrl+N",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileNewFromTemplate), "Tạo từ mẫu...", "New from Template...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileOpen), "Mở tệp...", "Open...", "Ctrl+O",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileOpenCloud), "Mở từ đám mây...", "Open from Cloud...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileOpenRecent), "Tệp gần đây", "Open Recent", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileClose), "Đóng", "Close", "Ctrl+W",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileCloseAll), "Đóng tất cả", "Close All", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileSave), "Lưu", "Save", "Ctrl+S",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileSaveAs), "Lưu thành...", "Save As...", "Ctrl+Shift+S",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileSaveCloud), "Lưu lên đám mây...", "Save to Cloud...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileSaveTemplate), "Lưu dưới dạng mẫu...", "Save as Template...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileRevert), "Khôi phục gốc", "Revert", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileAcquireImage), "Thu nạp hình ảnh", "Acquire Image", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileImport), "Nhập tệp...", "Import...", "Ctrl+I",
        {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileExport), "Xuất tệp...", "Export...", "Ctrl+E",
        {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileExportFor), "Xuất cho", "Export For", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFilePublishPdf), "Xuất bản PDF...", "Publish to PDF...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFilePrint), "In ấn...", "Print...", "Ctrl+P",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFilePrintPreview), "Xem trước bản in...", "Print Preview...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileProperties), "Thuộc tính tài liệu...", "Document Properties...", "",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kFileExit), "Thoát", "Exit", "Alt+F4",
        []() {}, nullptr
    });

    // Home Action
    dispatcher_.register_action({
        std::string(ActionId::kViewHome), "Trang chủ", "Home", "Alt+H",
        []() {}, nullptr
    });

    // Edit Actions
    dispatcher_.register_action({
        std::string(ActionId::kEditUndo), "Hoàn tác", "Undo", "Ctrl+Z",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kEditRedo), "Làm lại", "Redo", "Ctrl+Y",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kEditDelete), "Xóa", "Delete", "Delete",
        []() {}, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kEditSelectAll), "Chọn tất cả", "Select All", "Ctrl+A",
        []() {}, nullptr
    });

    // View Actions
    dispatcher_.register_action({
        std::string(ActionId::kViewZoomIn), "Phóng to", "Zoom In", "Ctrl++",
        [this]() { status_bar_.set_zoom_ratio(status_bar_.zoom_ratio() * 1.25); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kViewZoomOut), "Thu nhỏ", "Zoom Out", "Ctrl+-",
        [this]() { status_bar_.set_zoom_ratio(status_bar_.zoom_ratio() * 0.8); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kViewZoom100), "Tỷ lệ 100%", "Zoom 100%", "Ctrl+0",
        [this]() { status_bar_.set_zoom_ratio(1.0); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kViewReloadUi), "Nạp lại giao diện", "Reload UI", "F5",
        [this]() { trigger_f5_reload(); }, nullptr
    });

    // Tool Actions
    dispatcher_.register_action({
        std::string(ActionId::kToolSelect), "Công cụ chọn", "Select Tool", "V",
        [this]() { tool_strip_.set_active_tool(ToolMode::Select); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kToolRectangle), "Hình chữ nhật", "Rectangle Tool", "R",
        [this]() { tool_strip_.set_active_tool(ToolMode::Rectangle); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kToolEllipse), "Hình elip", "Ellipse Tool", "O",
        [this]() { tool_strip_.set_active_tool(ToolMode::Ellipse); }, nullptr
    });
    dispatcher_.register_action({
        std::string(ActionId::kToolText), "Văn bản", "Text Tool", "T",
        [this]() { tool_strip_.set_active_tool(ToolMode::Text); }, nullptr
    });
}

ReloadReport DesktopWindow::trigger_f5_reload(
    std::string_view qss_override,
    std::string_view manifest_override
) {
    const std::string qss = qss_override.empty() ?
        theme_manager_.current_tokens().generate_qss() : std::string(qss_override);

    const std::string manifest = manifest_override.empty() ?
        "{\"panel\": \"topbar\", \"class\": \"TopbarWidget\"}" : std::string(manifest_override);

    return reload_controller_.reload(qss, manifest);
}

} // namespace pikdraw::desktop
