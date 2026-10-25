#include "pikdraw/desktop/shell/native_window.hpp"
#include "../performance/canvas_trace.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_canvas_frame.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#include <commctrl.h>
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <optional>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace pikdraw::desktop {
namespace {

bool has_svg_extension(const std::string& path) {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
        return false;
    std::string extension = path.substr(dot);
    for (auto& character : extension)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return extension == ".svg";
}

} // namespace

void NativeWindow::set_active_workspace(int ws_index) noexcept {
    if (ws_index != 0)
        return;
    active_workspace_ = 0;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Không gian Vector 2D đang hoạt động"
        : "Vector 2D workspace is active";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_dock_tab(int tab_index) noexcept {
    active_dock_tab_ = tab_index;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::zoom_in() noexcept {
    auto& camera = root().renderer().camera();
    camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 118));
    camera.set_zoom(std::min(camera.zoom() * 1.25, 64.0));
    zoom_level_ = static_cast<int>(std::lround(camera.zoom() * 100.0));
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::zoom_out() noexcept {
    auto& camera = root().renderer().camera();
    camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 118));
    camera.set_zoom(std::max(camera.zoom() / 1.25, 0.01));
    zoom_level_ = static_cast<int>(std::lround(camera.zoom() * 100.0));
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Tỉ lệ hiển thị: " + std::to_string(zoom_level_) + "%" :
        "Zoom level: " + std::to_string(zoom_level_) + "%";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::fit_page_to_canvas() noexcept {
    if (artboard_overview_enabled()) {
        fit_artboards_to_canvas();
        return;
    }
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto& page = snapshot->page();
    const RECT area = canvas_rect(width_, height_);
    const int canvas_width = std::max(1, static_cast<int>(area.right - area.left));
    const int canvas_height = std::max(1, static_cast<int>(area.bottom - area.top));
    const double zoom = std::clamp(
        std::min(static_cast<double>(canvas_width) / page.width_mm,
                 static_cast<double>(canvas_height) / page.height_mm) * 0.9,
        0.01, 64.0);
    auto& camera = root().renderer().camera();
    camera.set_viewport_size(canvas_width, canvas_height);
    camera.set_center({page.width_mm * 0.5, page.height_mm * 0.5});
    camera.set_zoom(zoom);
    zoom_level_ = static_cast<int>(std::lround(zoom * 100.0));
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::artboard_overview_enabled() const noexcept {
    return !tabs_.empty() && active_tab().artboard_overview;
}

void NativeWindow::fit_artboards_to_canvas() noexcept {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot || snapshot->active_page().artboards.empty()) {
        fit_page_to_canvas();
        return;
    }
    std::optional<geometry::Rect2D> bounds;
    for (const auto& artboard : snapshot->active_page().artboards) {
        const geometry::Rect2D rect{artboard.x_mm, artboard.y_mm,
                                    artboard.width_mm, artboard.height_mm};
        bounds = bounds ? bounds->united(rect) : rect;
    }
    if (!bounds || bounds->is_empty()) {
        fit_page_to_canvas();
        return;
    }
    const RECT area = canvas_rect(width_, height_);
    const int canvas_width = std::max(1, static_cast<int>(area.right - area.left));
    const int canvas_height = std::max(1, static_cast<int>(area.bottom - area.top));
    const double zoom = std::clamp(
        std::min(static_cast<double>(canvas_width) / bounds->width,
                 static_cast<double>(canvas_height) / bounds->height) * 0.9,
        0.01, 64.0);
    auto& camera = root().renderer().camera();
    camera.set_viewport_size(canvas_width, canvas_height);
    camera.set_center(bounds->center());
    camera.set_zoom(zoom);
    zoom_level_ = static_cast<int>(std::lround(zoom * 100.0));
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::set_active_artboard(document::ArtboardId id) noexcept {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto& boards = snapshot->active_page().artboards;
    const auto found = std::find_if(boards.begin(), boards.end(),
        [id](const auto& artboard) { return artboard.id == id; });
    if (found == boards.end()) return false;
    active_tab().active_artboard_id = id;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

void NativeWindow::set_artboard_overview_enabled(bool enabled, bool save_default) noexcept {
    if (tabs_.empty()) return;
    active_tab().artboard_overview = enabled;
    const auto snapshot = root().document().current_snapshot();
    if (snapshot && !snapshot->active_page().artboards.empty()) {
        const auto& boards = snapshot->active_page().artboards;
        const bool active_exists = std::any_of(boards.begin(), boards.end(), [&](const auto& board) {
            return board.id == active_tab().active_artboard_id;
        });
        if (!active_exists) active_tab().active_artboard_id = boards.front().id;
    }
    if (save_default) {
        const auto path = tools::drawing_preferences_path();
        auto preferences = tools::load_drawing_preferences(path).value;
        preferences.artboard_overview = enabled;
        std::string diagnostic;
        if (!tools::save_drawing_preferences(path, preferences, &diagnostic))
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đổi chế độ xem nhưng không lưu được mặc định ứng dụng"
                : "View changed, but the app default could not be saved";
        else
            settings_artboard_overview_choice_ = enabled;
    }
    fit_page_to_canvas();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (enabled ? "Artboard · tổng quan vùng thiết kế · cùng một artwork" : "Trang · tập trung một trang")
        : (enabled ? "Artboard overview · shared scene artwork" : "Page focus · one page at a time");
}

bool NativeWindow::fit_document_content_to_canvas(bool* content_outside_page) noexcept {
    if (content_outside_page) *content_outside_page = false;
    try {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot || snapshot->objects().empty()) return false;

        std::optional<geometry::Rect2D> content_bounds;
        for (const auto& [id, object] : snapshot->objects().all()) {
            (void)object;
            const auto bounds = workflow().get_object_bounds(id);
            if (!bounds || !std::isfinite(bounds->x) || !std::isfinite(bounds->y) ||
                !std::isfinite(bounds->width) || !std::isfinite(bounds->height) ||
                bounds->width <= 0.0 || bounds->height <= 0.0)
                continue;
            content_bounds = content_bounds ? content_bounds->united(*bounds) : *bounds;
        }
        if (!content_bounds) return false;

        const auto& page = snapshot->page();
        constexpr double tolerance_mm = 1.0e-6;
        if (content_outside_page) {
            *content_outside_page = content_bounds->x < -tolerance_mm ||
                content_bounds->y < -tolerance_mm ||
                content_bounds->right() > page.width_mm + tolerance_mm ||
                content_bounds->bottom() > page.height_mm + tolerance_mm;
        }

        const int canvas_width = std::max(1, width_ - 352);
        const int canvas_height = std::max(1, height_ - 118);
        const double zoom = std::clamp(
            std::min(static_cast<double>(canvas_width) / content_bounds->width,
                     static_cast<double>(canvas_height) / content_bounds->height) * 0.9,
            0.01, 64.0);
        auto& camera = root().renderer().camera();
        camera.set_viewport_size(canvas_width, canvas_height);
        camera.set_center({content_bounds->x + content_bounds->width * 0.5,
                           content_bounds->y + content_bounds->height * 0.5});
        camera.set_zoom(zoom);
        zoom_level_ = static_cast<int>(std::lround(zoom * 100.0));
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return true;
    } catch (...) {
        if (content_outside_page) *content_outside_page = false;
        return false;
    }
}

void NativeWindow::open_menu(int menu_id) noexcept {
    if (menu_id < 0 || menu_id >= static_cast<int>(MenuId::Count)) return;
    if (is_inline_text_editing_ && menu_id != static_cast<int>(MenuId::Text)) commit_inline_text_editing();
    open_menu_id_ = menu_id;
    hovered_menu_bar_item_ = menu_id;
    hovered_menu_item_ = -1;
    open_submenu_action_ = -1;
    hovered_submenu_item_ = -1;
    show_file_menu_ = false;
    close_page_preset_dropdown();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::close_menu() noexcept {
    const bool was_open = open_menu_id_ >= 0;
    open_menu_id_ = -1;
    hovered_menu_bar_item_ = -1;
    hovered_menu_item_ = -1;
    open_submenu_action_ = -1;
    hovered_submenu_item_ = -1;
    if (was_open) {
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::toggle_menu(int menu_id) noexcept {
    if (open_menu_id_ == menu_id) close_menu();
    else open_menu(menu_id);
}

void NativeWindow::open_file_menu() noexcept {
    open_menu(static_cast<int>(MenuId::File));
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở menu Tệp" : "File menu opened";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_file_menu() noexcept {
    if (show_file_menu_ || open_menu_id_ >= 0) {
        show_file_menu_ = false;
        close_menu();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    }
}

void NativeWindow::toggle_file_menu() noexcept {
    toggle_menu(static_cast<int>(MenuId::File));
}

void NativeWindow::open_home_screen() noexcept {
    show_home_screen_ = true;
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã chuyển sang Trang chủ (PikDraw Home Screen & Templates)" : "Switched to Home Screen (PikDraw Welcome & Templates)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_home_screen() noexcept {
    show_home_screen_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã quay lại Không gian vẽ Vector (Canvas)" : "Returned to Vector Workspace Canvas";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_home_screen() noexcept {
    if (show_home_screen_) close_home_screen(); else open_home_screen();
}

void NativeWindow::set_active_home_tab(int tab) noexcept {
    if (tab < 0 || tab > 3) return;
    active_home_tab_ = tab;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::open_recent_document(int doc_index) noexcept {
    const char* vi_files[] = {
        "Bien_Hieu_An_Phat.pik (1850 × 420 mm - Biển alu đèn LED)",
        "Hop_Den_Hiflex_Techcom.pik (3000 × 1200 mm - Bạt xuyên sáng hiflex 3M)",
        "Chu_Noi_Inox_Vang_Guong.pik (800 × 350 mm - File cắt laser CNC kim loại)",
        "Standee_Cuon_Su_Kien_2026.pik (800 × 2000 mm - Bạt PP cán màng mờ)",
        "Tem_Nhan_Decal_Cuon.pik (500 × 500 mm - Decal bế demi chống nước)"
    };
    int idx = doc_index % 5;
    close_home_screen();
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đã nạp tệp gần đây: ") + vi_files[idx] :
        std::string("Loaded recent file: ") + vi_files[idx];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::create_from_template(int template_index) noexcept {
    const char* vi_tpls[] = {
        "Biển hiệu ngoài trời (6000 × 3000 mm)",
        "Tấm Alu chuẩn thi công (1220 × 2440 mm)",
        "Hộp đèn mica tròn hút nổi (600 × 600 mm)",
        "Standee cuốn sự kiện (800 × 2000 mm)",
        "Card visit chuẩn xưởng in (90 × 54 mm)",
        "Decal bế tem nhãn khổ cuộn (1000 × 1000 mm)"
    };
    int idx = template_index % 6;
    close_home_screen();
    close_file_menu();
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đã tạo bản vẽ từ mẫu chuẩn: ") + vi_tpls[idx] :
        std::string("Created new drawing from template: ") + vi_tpls[idx];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::trigger_export() noexcept {
#ifdef _WIN32
    if (export_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xuất SVG · nhấn Esc để hủy"
            : "SVG export is running · press Esc to cancel";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    constexpr int kSelectionScope = 4201;
    constexpr int kActivePageScope = 4202;
    constexpr int kAllPagesScope = 4203;
    constexpr int kCancelScope = 4204;
    constexpr int kActiveArtboardScope = 4205;
    const bool has_selection =
        !root().vector_controller().selection().selected_ids().empty();
    const std::wstring selection_label = language_ == UILanguage::Vietnamese
        ? L"Đối tượng đang chọn\nChỉ xuất các đối tượng đã chọn trên trang hiện tại"
        : L"Selected objects\nExport only the selected objects on the active page";
    const std::wstring page_label = language_ == UILanguage::Vietnamese
        ? L"Trang hiện tại\nXuất toàn bộ artwork hiển thị trên trang hiện tại"
        : L"Active page\nExport visible artwork from the current page";
    const std::wstring all_label = language_ == UILanguage::Vietnamese
        ? L"Tất cả trang · mỗi trang một SVG\nTạo một tệp riêng cho từng trang"
        : L"All pages · one SVG per page\nCreate a separate file for each page";
    const std::wstring artboard_label = language_ == UILanguage::Vietnamese
        ? L"Artboard đang chọn\nXuất artwork dùng chung theo khung Artboard hiện tại"
        : L"Active Artboard\nExport the shared artwork cropped to this Artboard";
    const std::wstring cancel_label = language_ == UILanguage::Vietnamese ? L"Hủy" : L"Cancel";
    std::vector<TASKDIALOG_BUTTON> buttons;
    if (has_selection) buttons.push_back({kSelectionScope, selection_label.c_str()});
    buttons.push_back({kActivePageScope, page_label.c_str()});
    if (artboard_overview_enabled() && active_tab().active_artboard_id != document::kInvalidArtboardId)
        buttons.push_back({kActiveArtboardScope, artboard_label.c_str()});
    buttons.push_back({kAllPagesScope, all_label.c_str()});
    buttons.push_back({kCancelScope, cancel_label.c_str()});
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = hwnd_;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    config.pszWindowTitle = language_ == UILanguage::Vietnamese
        ? L"Phạm vi xuất SVG" : L"SVG export scope";
    config.pszMainInstruction = language_ == UILanguage::Vietnamese
        ? L"Chọn nội dung cần xuất" : L"Choose what to export";
    config.pszContent = language_ == UILanguage::Vietnamese
        ? L"Các trang nhiều trang sẽ được lưu thành các tệp riêng."
        : L"Multi-page documents are written as separate files.";
    config.cButtons = static_cast<UINT>(buttons.size());
    config.pButtons = buttons.data();
    config.nDefaultButton = kActivePageScope;
    int choice = kActivePageScope;
    using TaskDialogFunction = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE common_controls = LoadLibraryW(L"comctl32.dll");
    const auto task_dialog = common_controls ? reinterpret_cast<TaskDialogFunction>(
        GetProcAddress(common_controls, "TaskDialogIndirect")) : nullptr;
    const HRESULT dialog_result = task_dialog
        ? task_dialog(&config, &choice, nullptr, nullptr) : E_NOTIMPL;
    if (common_controls) FreeLibrary(common_controls);
    if (FAILED(dialog_result)) {
        const auto fallback = MessageBoxW(hwnd_,
            language_ == UILanguage::Vietnamese
                ? L"Xuất trang hiện tại? Chọn Không để xuất các trang thành từng tệp riêng."
                : L"Export the active page? Choose No to export every page to its own file.",
            language_ == UILanguage::Vietnamese ? L"Phạm vi xuất SVG" : L"SVG export scope",
            MB_YESNOCANCEL | MB_ICONQUESTION);
        if (fallback == IDCANCEL) return;
        choice = fallback == IDYES ? kActivePageScope : kAllPagesScope;
    }
      if (choice == kCancelScope || choice == 0) return;
    const bool selected_objects = choice == kSelectionScope;
    const bool all_pages = choice == kAllPagesScope;
    const bool active_artboard = choice == kActiveArtboardScope;
    export_svg_dialog(selected_objects, all_pages, active_artboard);
#else
    status_msg_ = "SVG export requires the Windows desktop file dialog";
#endif
}

bool NativeWindow::export_svg_to(const std::string& file_path, bool selected_objects,
                                 bool allow_replace_existing, bool all_pages,
                                 bool active_artboard) {
    if (!has_svg_extension(file_path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "SVG cần phần mở rộng .svg; chưa ghi tệp"
            : "SVG export requires a .svg extension; no file was written";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
        return false;
    }
#ifdef _WIN32
    return start_svg_export(file_path, selected_objects, allow_replace_existing,
                            all_pages, active_artboard);
#else
    if (all_pages) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Xuất SVG nhiều trang cần Windows"
            : "Multi-page SVG export requires the Windows desktop";
        return false;
    }
    std::error_code existence_error;
    if (std::filesystem::exists(std::filesystem::u8path(file_path), existence_error) &&
        !allow_replace_existing) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tệp đã tồn tại · cần xác nhận ghi đè"
            : "The file already exists · overwrite confirmation is required";
        return false;
    }
    const bool saved = workflow().save_svg(file_path);
    status_msg_ = saved
        ? (language_ == UILanguage::Vietnamese ? "Đã xuất SVG ra tệp" : "SVG exported")
        : (language_ == UILanguage::Vietnamese ? "Không thể ghi tệp SVG" : "Could not write SVG file");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    return saved;
#endif
}

void NativeWindow::create_new_document() {
    open_new_doc_modal();
}

void NativeWindow::handle_double_click(int x, int y) {
    if (is_inline_text_editing_ && is_canvas_point(x, y) && handle_inline_text_mouse(x, y, 3)) return;
    if (active_dock_tab_ == 1 && x >= width_ - LayersDockMetrics::dock_width && x <= width_) {
        if (handle_layers_dock_click(x, y, true)) return;
    }
    if (!is_canvas_point(x, y))
        return;

    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        (void)finish_pen_session(false);
        return;
    }

    const auto pt_world = canvas_to_world(x, y);

    if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
        const double zoom = root().renderer().camera().zoom();
        const double tol_mm = (zoom > 1e-4) ? (8.0 / zoom) : 5.0;
        // Check if double clicked on an existing node -> delete it (CorelDRAW - Node)
        if (auto node_hit = node_tool_->tool().hit_test_node(pt_world, tol_mm)) {
            if (!node_tool_->is_node_selected(node_hit->first, node_hit->second)) {
                (void)node_tool_->pointer_down(pt_world, false, tol_mm);
            }
            if (node_tool_->delete_selected()) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã xóa node (Nhấp đúp chuột · CorelDRAW F10)"
                    : "Deleted node (Double-click · CorelDRAW F10)";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        // Check if double clicked on a segment -> add node at double click point (CorelDRAW + Node)
        double seg_t = 0.5;
        if (auto seg_hit = node_tool_->tool().hit_test_segment(pt_world, tol_mm, &seg_t)) {
            auto cmd = std::make_unique<document::SplitPathSegmentCommand>(
                node_tool_->target_object(), seg_hit->first, seg_hit->second, seg_t);
            if (root().document().execute_command(std::move(cmd)).success) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã thêm node mới trên đoạn (Nhấp đúp chuột · CorelDRAW F10)"
                    : "Added new node on segment (Double-click · CorelDRAW F10)";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        // Double-click on open canvas / object space in Node Tool selects all nodes
        node_tool_->tool().select_all();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã chọn toàn bộ " + std::to_string(node_tool_->selected_node_count()) + " node (Nhấp đúp chuột)"
            : "Selected all " + std::to_string(node_tool_->selected_node_count()) + " nodes (Double-click)";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const auto snapshot = root().document().current_snapshot();
    const double text_tol = 6.0 / std::max(0.01, root().renderer().camera().zoom());
    std::optional<document::ObjectId> hit_text_id;

    // Double-click on any TextObject activates inline text editing immediately
    // without requiring the user to switch to Text Tool first.
    const auto& selected = root().vector_controller().selection().selected_ids();
    for (const auto id : selected) {
        const auto obj = snapshot ? snapshot->objects().get(id) : std::nullopt;
        if (obj && std::holds_alternative<document::TextObject>(*obj)) {
            const auto* entry = snapshot->hierarchy().find(id);
            if (entry && snapshot->hierarchy().editable(id)) {
                if (auto inv = entry->world.inverted()) {
                    auto local = std::get<document::TextObject>(*obj);
                    local.transform = {};
                    auto b = local.bounds();
                    b.x -= text_tol; b.y -= text_tol; b.width += 2 * text_tol; b.height += 2 * text_tol;
                    if (b.contains(inv->map_point(pt_world))) {
                        hit_text_id = id;
                        break;
                    }
                }
            }
        }
    }
    if (!hit_text_id && snapshot) {
        for (auto it = snapshot->objects().all().rbegin(); it != snapshot->objects().all().rend(); ++it) {
            const auto& [id, obj] = *it;
            if (const auto* text_obj = std::get_if<document::TextObject>(&obj)) {
                const auto* entry = snapshot->hierarchy().find(id);
                if (!entry || !snapshot->hierarchy().editable(id)) continue;
                if (is_in_isolation_mode() && !is_object_in_isolated_group(id)) continue;
                if (auto inv = entry->world.inverted()) {
                    auto local = *text_obj;
                    local.transform = {};
                    auto b = local.bounds();
                    b.x -= text_tol; b.y -= text_tol; b.width += 2 * text_tol; b.height += 2 * text_tol;
                    if (b.contains(inv->map_point(pt_world))) {
                        hit_text_id = id;
                        break;
                    }
                }
            }
        }
    }
    if (hit_text_id) {
        start_inline_text_editing(pt_world, *hit_text_id);
        return;
    }
    auto hit_id = vector::SelectionModel::hit_test_in_scope(pt_world,*snapshot,current_isolated_group_id().value_or(0),2.0,isolation_frame_);

    if (active_tool_ == 0) {
        if(hit_id && workflow().is_group(*hit_id)) { enter_group_isolation(*hit_id); return; }
        if (hit_id) {
            auto obj_opt = snapshot->objects().get(*hit_id);
            if (obj_opt && std::holds_alternative<document::TextObject>(*obj_opt)) {
                start_inline_text_editing(pt_world, *hit_id);
                return;
            }
        }

        if (selection_handle_at(x, y) == 9) {
            custom_pivot_world_.reset();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "ÄÃ£ Ä‘áº·t láº¡i tÃ¢m xoay vá» chÃ­nh giá»¯a Ä‘á»‘i tÆ°á»£ng"
                : "Reset rotation pivot to object center";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto bounds = root().vector_controller().selection().bounds(*snapshot);
        const bool hit_selection = (hit_id && root().vector_controller().selection().is_selected(*hit_id)) ||
                                   (bounds && bounds->contains(pt_world));
        if (hit_selection) {
            const auto& sel_ids = root().vector_controller().selection().selected_ids();
            if (sel_ids.size() == 1 && workflow().is_clipping_container(sel_ids.front())) {
                enter_group_isolation(sel_ids.front());
                return;
            }
            toggle_rotate_mode();
            if (is_rotate_mode_) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Chế độ xoay: kéo 4 góc để xoay quanh tâm, kéo tâm để dời vị trí"
                    : "Rotate mode: drag 4 corners to rotate, drag pivot to reposition";
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Chế độ biến đổi: kéo tay nắm để đổi kích thước"
                    : "Transform mode: drag handles to scale";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (!hit_id) {
            if (is_rotate_mode_) {
                is_rotate_mode_ = false;
                custom_pivot_world_.reset();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            if (is_in_isolation_mode()) {
                exit_group_isolation();
            }
            return;
        }
    }

    if (is_in_isolation_mode()) {
        if (!hit_id) {
            // Double-click on blank canvas background exits isolation mode!
            exit_group_isolation();
            return;
        }

        // If double clicked on an outside object, also exit isolation mode
        if (!is_object_in_isolated_group(*hit_id)) {
            exit_group_isolation();
            return;
        }

        // If clicked on a subgroup within the current isolated group, isolate into it
        auto current_iso = *current_isolated_group_id();
        auto parent = workflow().find_parent_group(*hit_id);
        if (parent && *parent != current_iso) {
            enter_group_isolation(*parent);
            root().vector_controller().selection().select(*hit_id, vector::SelectionMode::Replace);
        }
    } else {
        if (hit_id) {
            auto parent = workflow().find_parent_group(*hit_id);
            auto target_group = workflow().is_group(*hit_id) ? *hit_id : (parent ? *parent : document::kInvalidObjectId);
            if (target_group != document::kInvalidObjectId) {
                enter_group_isolation(target_group);
                // Also select the specific clicked child so user can edit right away
                root().vector_controller().selection().select(*hit_id, vector::SelectionMode::Replace);
            }
        }
    }
}

#ifdef _WIN32

bool NativeWindow::initialize(const std::string& title) {
    // Opt in before creating any HWND on the native UI thread.
    using SetAwareness = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (const auto set_awareness = reinterpret_cast<SetAwareness>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
        set_awareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hinstance_ = GetModuleHandleW(NULL);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = NativeWindow::window_proc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"PikDrawShellWindowClass";

    RegisterClassExW(&wc);

    int w = width_;
    int h = height_;
    std::wstring wtitle = utf8_to_wide(title);

    hwnd_ = CreateWindowExW(
        0, wc.lpszClassName, wtitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        NULL, NULL, hinstance_, NULL
    );

    if (!hwnd_) return false;

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    using GetDpi = UINT(WINAPI*)(HWND);
    if (const auto get_dpi = reinterpret_cast<GetDpi>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")))
        dpi_scale_ = std::max(1u, get_dpi(hwnd_)) / 96.0f;
    RECT client_rc;
    if (GetClientRect(hwnd_, &client_rc)) {
        width_ = logical_pixel(client_rc.right - client_rc.left);
        height_ = logical_pixel(client_rc.bottom - client_rc.top);
        shell_.set_window_size(static_cast<double>(width_), static_cast<double>(height_));
        const auto area = canvas_rect(width_, height_);
        root().renderer().camera().set_viewport_size(
            area.right - area.left, area.bottom - area.top);
    }
    if (mcp_enabled_ && !start_mcp_gateway()) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    if (!SetTimer(hwnd_, kAutoSaveTimerId, 60000, nullptr))
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể bật bản phục hồi tự động" : "Could not start auto-recovery timer";
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    scan_recovery_checkpoints(true, false, true);
    record_performance_event(CanvasTraceKind::StartupReady);
    return true;
}

int NativeWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (canvas_trace_ && msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) {
            pending_trace_event_id_ = next_trace_event_id_++;
            record_performance_event(CanvasTraceKind::Input, pending_trace_event_id_);
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        pending_trace_event_id_ = 0;
    }
    return static_cast<int>(msg.wParam);
}
#else
bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
#endif

} // namespace pikdraw::desktop
