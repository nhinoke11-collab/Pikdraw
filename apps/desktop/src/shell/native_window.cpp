#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_page_presets.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop {

std::wstring NativeWindow::utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return L"";
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wstr[0], size);
    return wstr;
#else
    return std::wstring(utf8.begin(), utf8.end());
#endif
}

std::string NativeWindow::wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return "";
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &str[0], size, NULL, NULL);
    return str;
#else
    return std::string(wide.begin(), wide.end());
#endif
}

void NativeWindow::initialize_default_tab() {
    DocumentTab tab;
    tab.id = "doc-1";
    tab.title = (language_ == UILanguage::Vietnamese) ? "Bản vẽ 1" : "Document 1";
    tab.page_width_mm = 297.0;
    tab.page_height_mm = 210.0;
    tab.color_mode = "CMYK";
    tab.dpi = 300;
    tab.unit = "mm";
    tab.root = std::make_unique<CompositionRoot>();
    tab.root->initialize(static_cast<double>(width_), static_cast<double>(height_), 1.0);
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(297.0, 210.0);
    tabs_.push_back(std::move(tab));
    active_tab_index_ = 0;
}

NativeWindow::DocumentTab& NativeWindow::active_tab() noexcept {
    if (tabs_.empty()) initialize_default_tab();
    return tabs_[active_tab_index_ < tabs_.size() ? active_tab_index_ : 0];
}

const NativeWindow::DocumentTab& NativeWindow::active_tab() const noexcept {
    return tabs_[active_tab_index_ < tabs_.size() ? active_tab_index_ : 0];
}

CompositionRoot& NativeWindow::root() noexcept {
    return *active_tab().root;
}

const CompositionRoot& NativeWindow::root() const noexcept {
    return *active_tab().root;
}

DocumentWorkflow& NativeWindow::workflow() noexcept {
    return *active_tab().workflow;
}

const DocumentWorkflow& NativeWindow::workflow() const noexcept {
    return *active_tab().workflow;
}

void NativeWindow::switch_to_tab(size_t index) noexcept {
    if (index < tabs_.size() && index != active_tab_index_) {
        active_tab_index_ = index;
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::close_tab(size_t index) noexcept {
    if (index >= tabs_.size()) return;
    if (tabs_.size() > 1) {
        if (tabs_[index].root) tabs_[index].root->shutdown();
        tabs_.erase(tabs_.begin() + index);
        if (active_tab_index_ >= tabs_.size()) {
            active_tab_index_ = tabs_.size() - 1;
        }
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    } else {
        workflow().new_document(297.0, 210.0);
        active_tab().title = (language_ == UILanguage::Vietnamese) ? "Bản vẽ 1" : "Document 1";
        active_tab().file_path = "";
        active_tab().page_width_mm = 297.0;
        active_tab().page_height_mm = 210.0;
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::create_new_tab(const std::string& title, double w_mm, double h_mm,
                                  const std::string& color_mode, int dpi) {
    DocumentTab tab;
    tab.id = "doc-" + std::to_string(next_doc_counter_++);
    tab.title = title.empty() ? ("Bản vẽ " + std::to_string(tabs_.size() + 1)) : title;
    tab.page_width_mm = w_mm;
    tab.page_height_mm = h_mm;
    tab.color_mode = color_mode;
    tab.dpi = dpi;
    tab.unit = "mm";
    tab.root = std::make_unique<CompositionRoot>();
    tab.root->initialize(static_cast<double>(width_), static_cast<double>(height_), 1.0);
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(w_mm, h_mm);
    tabs_.push_back(std::move(tab));
    active_tab_index_ = tabs_.size() - 1;
    fit_page_to_canvas();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_new_doc_modal() noexcept {
    show_new_doc_modal_ = true;
    new_doc_name_ = utf8_to_wide("Bản vẽ " + std::to_string(next_doc_counter_));
    new_doc_preset_ = 0;
    new_doc_w_ = 297.0;
    new_doc_h_ = 210.0;
    new_doc_landscape_ = true;
    new_doc_color_mode_ = 0;
    new_doc_dpi_ = 300;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::close_new_doc_modal() noexcept {
    show_new_doc_modal_ = false;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::confirm_new_document() noexcept {
    std::string title = wide_to_utf8(new_doc_name_);
    if (title.empty()) title = "Bản vẽ " + std::to_string(next_doc_counter_);
    std::string c_mode = (new_doc_color_mode_ == 0) ? "CMYK" : "RGB";
    new_doc_w_ = std::clamp(new_doc_w_, 10.0, geometry::kMaxObjectDimensionMm);
    new_doc_h_ = std::clamp(new_doc_h_, 10.0, geometry::kMaxObjectDimensionMm);
    create_new_tab(title, new_doc_w_, new_doc_h_, c_mode, new_doc_dpi_);
    show_new_doc_modal_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã tạo tài liệu mới: " + title : "Created new document: " + title;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::set_active_page_size(double w_mm, double h_mm) {
    if (w_mm <= 0.0 || h_mm <= 0.0) return;
    w_mm = std::clamp(w_mm, 10.0, geometry::kMaxObjectDimensionMm);
    h_mm = std::clamp(h_mm, 10.0, geometry::kMaxObjectDimensionMm);
    auto& tab = active_tab();
    tab.page_width_mm = w_mm;
    tab.page_height_mm = h_mm;
    workflow().set_page_size(w_mm, h_mm);
    fit_page_to_canvas();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_page_orientation() {
    auto& tab = active_tab();
    double temp = tab.page_width_mm;
    tab.page_width_mm = tab.page_height_mm;
    tab.page_height_mm = temp;
    workflow().set_page_size(tab.page_width_mm, tab.page_height_mm);
    fit_page_to_canvas();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_page_preset_dropdown() noexcept {
    show_page_preset_dropdown_ = !show_page_preset_dropdown_;
    if (show_page_preset_dropdown_) {
        show_file_menu_ = false;
        commit_numeric_field();
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::close_page_preset_dropdown() noexcept {
    if (show_page_preset_dropdown_) {
        show_page_preset_dropdown_ = false;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::select_page_preset(int preset_index) noexcept {
    if (preset_index < 0 || preset_index >= static_cast<int>(kPagePresetEntries.size())) return;
    const auto& entry = kPagePresetEntries[preset_index];
    const auto snapshot = root().document().current_snapshot();
    const double pw = snapshot->page().width_mm;
    const double ph = snapshot->page().height_mm;
    const bool is_land = (pw > ph);
    const double target_w = is_land ? std::max(entry.width_mm, entry.height_mm)
                                    : std::min(entry.width_mm, entry.height_mm);
    const double target_h = is_land ? std::min(entry.width_mm, entry.height_mm)
                                    : std::max(entry.width_mm, entry.height_mm);
    set_active_page_size(target_w, target_h);
    show_page_preset_dropdown_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? std::string("Đã chọn khổ giấy: ") + wide_to_utf8(entry.name) + " (" + wide_to_utf8(entry.dim_label) + ")"
        : std::string("Selected page preset: ") + wide_to_utf8(entry.name) + " (" + wide_to_utf8(entry.dim_label) + ")";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::resize_selected_object(double delta_w, double delta_h, bool change_w) {
    const auto ids = root().vector_controller().selection().selected_ids();
    if (ids.empty()) return;
    const auto id = ids.front();
    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(id);
    if (!obj) return;
    const auto bounds_opt = std::visit([](const auto& val) -> std::optional<geometry::Rect2D> {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, document::GroupObject>) return std::nullopt;
        else return val.bounds();
    }, *obj);
    if (!bounds_opt || bounds_opt->width <= 0.0 || bounds_opt->height <= 0.0) return;

    double cur_w = bounds_opt->width;
    double cur_h = bounds_opt->height;
    double new_w = std::clamp(cur_w + delta_w, 2.0, geometry::kMaxObjectDimensionMm);
    double new_h = std::clamp(cur_h + delta_h, 2.0, geometry::kMaxObjectDimensionMm);

    if (aspect_ratio_locked_) {
        const double ratio = cur_w / cur_h;
        if (change_w) {
            new_h = new_w / ratio;
            if (new_h > geometry::kMaxObjectDimensionMm) {
                new_h = geometry::kMaxObjectDimensionMm;
                new_w = new_h * ratio;
            }
        } else {
            new_w = new_h * ratio;
            if (new_w > geometry::kMaxObjectDimensionMm) {
                new_w = geometry::kMaxObjectDimensionMm;
                new_h = new_w / ratio;
            }
        }
    }
    if (new_w >= geometry::kMaxObjectDimensionMm || new_h >= geometry::kMaxObjectDimensionMm) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đạt giới hạn kích thước tối đa 40 mét (40,000 mm)"
            : "Reached maximum object size limit of 40 meters (40,000 mm)";
    }
    workflow().resize_object(id, new_w, new_h);
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::move_selected_object(double delta_x, double delta_y) {
    const auto ids = root().vector_controller().selection().selected_ids();
    if (ids.empty()) return;
    for (const auto id : ids) {
        workflow().transform_object(id, delta_x, delta_y);
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_fill_to_selection(const std::string& hex) {
    workflow().set_selected_fill(hex);
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_stroke_to_selection(const std::string& hex, double width_mm) {
    workflow().set_selected_stroke(hex, width_mm);
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::trigger_boolean_operation(geometry::BooleanOp op) {
    bool ok = workflow().boolean_operation_selected(op, keep_source_object_, keep_target_object_);
    if (ok) {
        const char* vi_msg = "Thực hiện tạo hình khối thành công";
        const char* en_msg = "Solid operation completed";
        switch (op) {
            case geometry::BooleanOp::Union:
                vi_msg = "Đã hàn các đối tượng (Weld)";
                en_msg = "Welded selected objects";
                break;
            case geometry::BooleanOp::Difference:
                vi_msg = "Đã xén đối tượng (Trim / Front Minus Back)";
                en_msg = "Trimmed selected object";
                break;
            case geometry::BooleanOp::Intersection:
                vi_msg = "Đã lấy phần giao (Intersect)";
                en_msg = "Intersected selected objects";
                break;
            case geometry::BooleanOp::Xor:
                vi_msg = "Đã loại trừ phần giao (Exclude)";
                en_msg = "Excluded overlapping areas";
                break;
            case geometry::BooleanOp::BackMinusFront:
                vi_msg = "Đã trừ đối tượng dưới (Back Minus Front)";
                en_msg = "Subtracted bottom object";
                break;
            case geometry::BooleanOp::Boundary:
                vi_msg = "Đã tạo đường viền bao (Boundary)";
                en_msg = "Created perimeter boundary";
                break;
            default: break;
        }
        status_msg_ = (language_ == UILanguage::Vietnamese) ? vi_msg : en_msg;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
    return ok;
}

bool NativeWindow::trigger_auto_weld() {
    bool ok = workflow().auto_weld_overlapping();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Tự động hàn sạch các nét chữ/hình giao nhau CNC thành công"
            : "Auto-welded intersecting shapes for CNC cutting";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
    return ok;
}

NativeWindow::NativeWindow(int width, int height)
    : width_(width),
      height_(height),
      shell_(static_cast<double>(width), static_cast<double>(height), 1.0) {
    initialize_default_tab();
    fit_page_to_canvas();
}

NativeWindow::~NativeWindow() {
#ifdef _WIN32
    cleanup_canvas_cache();
    cleanup_backbuffer();
    cleanup_ui_fonts();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
#endif
    for (auto& tab : tabs_) {
        if (tab.root) tab.root->shutdown();
    }
}

#ifdef _WIN32
void NativeWindow::cleanup_canvas_cache() noexcept {
    if (cached_canvas_dc_) {
        if (cached_canvas_old_bmp_) {
            SelectObject(cached_canvas_dc_, cached_canvas_old_bmp_);
            cached_canvas_old_bmp_ = nullptr;
        }
        DeleteDC(cached_canvas_dc_);
        cached_canvas_dc_ = nullptr;
    }
    if (cached_canvas_bmp_) {
        DeleteObject(cached_canvas_bmp_);
        cached_canvas_bmp_ = nullptr;
    }
    canvas_cache_valid_ = false;
}

void NativeWindow::cleanup_backbuffer() noexcept {
    if (backbuffer_dc_) {
        if (backbuffer_old_bmp_) {
            SelectObject(backbuffer_dc_, backbuffer_old_bmp_);
            backbuffer_old_bmp_ = nullptr;
        }
        DeleteDC(backbuffer_dc_);
        backbuffer_dc_ = nullptr;
    }
    if (backbuffer_bmp_) {
        DeleteObject(backbuffer_bmp_);
        backbuffer_bmp_ = nullptr;
    }
    backbuffer_w_ = 0;
    backbuffer_h_ = 0;
}

void NativeWindow::ensure_ui_fonts() noexcept {
    if (font_be_vietnam_) return;
    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, name);
    };
    font_be_vietnam_ = make_font(13, FW_NORMAL, L"Segoe UI");
    font_be_vietnam_bold_ = make_font(13, FW_BOLD, L"Segoe UI");
    font_secondary_ = make_font(12, FW_NORMAL, L"Segoe UI");
    font_mono_ = make_font(12, FW_NORMAL, L"JetBrains Mono");
    font_tab_ = make_font(12, FW_NORMAL, L"Segoe UI");
    font_tab_bold_ = make_font(12, FW_BOLD, L"Segoe UI");
    font_ruler_ = make_font(10, FW_NORMAL, L"Segoe UI");
}

void NativeWindow::cleanup_ui_fonts() noexcept {
    for (HFONT* f : {&font_be_vietnam_, &font_be_vietnam_bold_, &font_secondary_,
                     &font_mono_, &font_tab_, &font_tab_bold_, &font_ruler_}) {
        if (*f) {
            DeleteObject(*f);
            *f = nullptr;
        }
    }
}
#endif

void NativeWindow::set_language(UILanguage lang) noexcept {
    language_ = lang;
    status_msg_ = (lang == UILanguage::Vietnamese) ?
        "Đã chuyển giao diện sang Tiếng Việt (Be Vietnam Pro)" :
        "Switched UI language to English (Be Vietnam Pro)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_language() noexcept {
    set_language(language_ == UILanguage::Vietnamese ? UILanguage::English : UILanguage::Vietnamese);
}

void NativeWindow::set_theme(ThemeMode mode) noexcept {
    theme_ = mode;
    settings_theme_choice_ = (mode == ThemeMode::CyberDark) ? 0 : 1;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        (mode == ThemeMode::CyberDark ? "Đã chuyển sang Chế độ Tối (Dark Mode Obsidian)" : "Đã chuyển sang Chế độ Sáng (Light Mode Silver)") :
        (mode == ThemeMode::CyberDark ? "Switched to Dark Mode (Obsidian Slate)" : "Switched to Light Mode (Silver Tech)");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_theme() noexcept {
    set_theme(theme_ == ThemeMode::CyberDark ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::open_settings_modal() noexcept {
    show_settings_modal_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở Cài đặt hệ thống (PikDraw Preferences)" : "Opened System Preferences (PikDraw Settings)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_settings_modal() noexcept {
    show_settings_modal_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã lưu và đóng Cài đặt hệ thống" : "Saved and closed System Preferences";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_settings_modal() noexcept {
    if (show_settings_modal_) close_settings_modal(); else open_settings_modal();
}

void NativeWindow::set_settings_tab(int tab) noexcept {
    if (tab < 0 || tab > 5) return;
    settings_active_tab_ = tab;
    const char* vi_tabs[] = { "Chung & Ngôn ngữ", "Giao diện (Theme)", "Đơn vị & Bắt điểm", "Quản lý màu & ICC", "Thi công & CNC", "Hiệu năng & Hệ thống" };
    const char* en_tabs[] = { "General & Language", "Display & Theme", "Units & Snapping", "Color & ICC", "Production & CAM", "Hardware & System" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Tùy chọn: ") + vi_tabs[tab] : std::string("Options: ") + en_tabs[tab];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_settings_theme_choice(int theme) noexcept {
    settings_theme_choice_ = theme;
    set_theme(theme == 1 ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::set_settings_accent_choice(int accent) noexcept {
    settings_accent_choice_ = accent;
    const char* vi_accents[] = { "Cyber Mint (#00f2c3)", "Sky Blue (#0ea5e9)", "Purple Tech (#a855f7)", "Amber CAD (#f59e0b)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Màu điểm nhấn: ") + vi_accents[accent % 4] : std::string("Accent Color: ") + vi_accents[accent % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_settings_unit_choice(int unit) noexcept {
    settings_unit_choice_ = unit;
    const char* units[] = { "mm (Milimét)", "cm (Xentimét)", "inch (Inches)", "pt (Điểm in)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đơn vị mặc định: ") + units[unit % 4] : std::string("Default Units: ") + units[unit % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_tool(int tool_index) noexcept {
    if (tool_index < 0 || tool_index > 4)
        return;
    active_tool_ = tool_index;
    const auto mode = tool_index == 1 ? vector::ToolMode::Rectangle
                    : tool_index == 2 ? vector::ToolMode::Ellipse
                                      : vector::ToolMode::Select;
    root().vector_controller().set_mode(mode);
    const char* vi_tools[] = { "Chọn và di chuyển · V", "Vẽ hình chữ nhật · R",
                               "Vẽ hình elip · E", "Di chuyển khung nhìn · Space/chuột giữa",
                               "Văn bản tiếng Việt · A/T" };
    const char* en_tools[] = { "Select and move · V", "Draw rectangle · R",
                               "Draw ellipse · E", "Pan view · Space/middle mouse",
                               "Vietnamese text · A/T" };
    status_msg_ = language_ == UILanguage::Vietnamese ? vi_tools[tool_index] : en_tools[tool_index];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

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
    const auto& page = root().document().current_snapshot()->page();
    const int canvas_width = std::max(1, width_ - 352);
    const int canvas_height = std::max(1, height_ - 118);
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

void NativeWindow::open_file_menu() noexcept {
    show_file_menu_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở menu Tệp" : "File menu opened";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::close_file_menu() noexcept {
    if (show_file_menu_) {
        show_file_menu_ = false;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    }
}

void NativeWindow::toggle_file_menu() noexcept {
    if (show_file_menu_) close_file_menu(); else open_file_menu();
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
    export_svg_dialog();
#else
    status_msg_ = "SVG export requires the Windows desktop file dialog";
#endif
}

bool NativeWindow::export_svg_to(const std::string& file_path) {
    const bool saved = workflow().save_svg(file_path);
    status_msg_ = saved
        ? (language_ == UILanguage::Vietnamese ? "Đã xuất SVG ra tệp" : "SVG exported")
        : (language_ == UILanguage::Vietnamese ? "Không thể ghi tệp SVG" : "Could not write SVG file");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
    return saved;
}

void NativeWindow::create_new_document() {
    open_new_doc_modal();
}

void NativeWindow::enter_group_isolation(document::ObjectId group_id) {
    if (!workflow().is_group(group_id))
        return;

    isolation_stack_.push_back(group_id);
    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(group_id);
    if (obj) {
        if (const auto* grp = std::get_if<document::GroupObject>(&*obj)) {
            if (!grp->children_ids.empty()) {
                root().vector_controller().selection().select(grp->children_ids.front(), vector::SelectionMode::Replace);
            } else {
                root().vector_controller().selection().clear();
            }
        }
    }
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Chế độ cô lập nhóm: Đang chỉnh sửa bên trong Nhóm · Nhấn Esc để thoát"
        : "Group Isolation Mode: Editing inside Group · Press Esc or double-click to exit";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::exit_group_isolation() {
    if (isolation_stack_.empty())
        return;

    auto last_group = isolation_stack_.back();
    isolation_stack_.pop_back();

    if (isolation_stack_.empty()) {
        root().vector_controller().selection().select(last_group, vector::SelectionMode::Replace);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thoát chế độ cô lập · Quay lại tài liệu chính"
            : "Exited Group Isolation Mode · Returned to Main Document";
    } else {
        root().vector_controller().selection().select(last_group, vector::SelectionMode::Replace);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thoát ra cấp nhóm cha"
            : "Returned to parent group level";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::exit_all_isolation() {
    if (isolation_stack_.empty())
        return;

    auto root_group = isolation_stack_.front();
    isolation_stack_.clear();
    root().vector_controller().selection().select(root_group, vector::SelectionMode::Replace);
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã thoát toàn bộ chế độ cô lập · Tài liệu chính"
        : "Exited all isolation levels · Main Document";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::is_object_in_isolated_group(document::ObjectId id) const {
    if (!is_in_isolation_mode())
        return true;

    auto iso_id = *current_isolated_group_id();
    if (id == iso_id)
        return true;

    const auto snapshot = root().document().current_snapshot();
    std::vector<document::ObjectId> desc;
    std::function<void(document::ObjectId)> collect = [&](document::ObjectId gid) {
        auto obj = snapshot->objects().get(gid);
        if (!obj) return;
        if (const auto* g = std::get_if<document::GroupObject>(&*obj)) {
            for (auto cid : g->children_ids) {
                desc.push_back(cid);
                collect(cid);
            }
        }
    };
    collect(iso_id);
    return std::find(desc.begin(), desc.end(), id) != desc.end();
}

void NativeWindow::handle_double_click(int x, int y) {
    if (!is_canvas_point(x, y))
        return;

    const auto pt_world = canvas_to_world(x, y);
    const auto snapshot = root().document().current_snapshot();
    auto hit_id = vector::SelectionModel::hit_test(pt_world, *snapshot);

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
            auto obj_opt = snapshot->objects().get(*hit_id);
            if (obj_opt && std::holds_alternative<document::TextObject>(*obj_opt)) {
                start_inline_text_editing(pt_world, *hit_id);
                return;
            }
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
    hinstance_ = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
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
    RECT client_rc;
    if (GetClientRect(hwnd_, &client_rc)) {
        width_ = client_rc.right - client_rc.left;
        height_ = client_rc.bottom - client_rc.top;
        shell_.set_window_size(static_cast<double>(width_), static_cast<double>(height_));
        const auto area = canvas_rect(width_, height_);
        root().renderer().camera().set_viewport_size(
            area.right - area.left, area.bottom - area.top);
    }
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int NativeWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

namespace {
const char* kFontFamilies[] = {
    "Be Vietnam Pro",
    "Segoe UI",
    "Arial",
    "Times New Roman",
    "JetBrains Mono"
};
const int kNumFontFamilies = 5;
}

void NativeWindow::start_inline_text_editing(geometry::Point2D pos_mm, document::ObjectId existing_id) {
    is_inline_text_editing_ = true;
    inline_editing_text_id_ = existing_id;
    inline_text_pos_mm_ = pos_mm;

    if (existing_id != document::kInvalidObjectId) {
        auto snap = root().document().current_snapshot();
        auto opt = snap->objects().get(existing_id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            const auto& to = std::get<document::TextObject>(*opt);
            inline_text_buffer_ = to.text;
            inline_text_pos_mm_ = to.position;
            font_size_pt_ = to.font.size_pt;
            font_bold_ = (to.font.weight >= 700);
            font_italic_ = to.font.italic;
            font_underline_ = to.underline;
            font_alignment_ = to.alignment;
            for (int i = 0; i < kNumFontFamilies; ++i) {
                if (to.font.family_name == kFontFamilies[i]) {
                    font_family_index_ = i;
                    break;
                }
            }
        }
    } else {
        inline_text_buffer_.clear();
    }
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đang gõ văn bản trực tiếp trên trang giấy · Nhấn Enter để hoàn tất, Esc để hủy"
        : "Typing text directly on canvas · Press Enter to commit, Esc to cancel";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::commit_inline_text_editing() {
    if (!is_inline_text_editing_) return;

    if (!inline_text_buffer_.empty()) {
        const std::string font_family = kFontFamilies[font_family_index_ % kNumFontFamilies];
        if (inline_editing_text_id_ != document::kInvalidObjectId) {
            workflow().update_text_content(inline_editing_text_id_, inline_text_buffer_);
            workflow().update_text_style(
                inline_editing_text_id_, font_family, font_size_pt_,
                font_bold_, font_italic_, font_underline_, font_alignment_
            );
        } else {
            auto id = workflow().add_vietnamese_text(
                inline_text_pos_mm_.x, inline_text_pos_mm_.y,
                inline_text_buffer_, font_family, font_size_pt_, "#00f2c3"
            );
            if (id != document::kInvalidObjectId) {
                workflow().update_text_style(
                    id, font_family, font_size_pt_,
                    font_bold_, font_italic_, font_underline_, font_alignment_
                );
                if (is_in_isolation_mode()) {
                    auto iso_id = *current_isolated_group_id();
                    auto snap = root().document().current_snapshot();
                    auto obj = snap->objects().get(iso_id);
                    if (obj && std::holds_alternative<document::GroupObject>(*obj)) {
                        document::GroupObject updated_grp = std::get<document::GroupObject>(*obj);
                        updated_grp.children_ids.push_back(id);
                        auto map = snap->objects().set(iso_id, std::move(updated_grp));
                        root().document().apply_mutation(snap->page(), std::move(map));
                    }
                }
                root().vector_controller().selection().select(id);
            }
        }
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã cập nhật văn bản trên trang · Ctrl+Z để hoàn tác"
            : "Text committed on canvas · Ctrl+Z to undo";
    }

    is_inline_text_editing_ = false;
    inline_editing_text_id_ = document::kInvalidObjectId;
    inline_text_buffer_.clear();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cancel_inline_text_editing() {
    is_inline_text_editing_ = false;
    inline_editing_text_id_ = document::kInvalidObjectId;
    inline_text_buffer_.clear();
    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy soạn thảo văn bản" : "Canceled text editing";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cycle_font_family() {
    font_family_index_ = (font_family_index_ + 1) % kNumFontFamilies;
    const std::string fam = kFontFamilies[font_family_index_];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = std::string("Phông chữ / Font: ") + fam;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::adjust_font_size(double delta_pt) {
    font_size_pt_ = std::clamp(font_size_pt_ + delta_pt, 6.0, 144.0);
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = std::string("Cỡ chữ / Font Size: ") + std::to_string(static_cast<int>(font_size_pt_)) + " pt";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_bold() {
    font_bold_ = !font_bold_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_bold_ ? "In đậm: Bật (Bold: ON)" : "In đậm: Tắt (Bold: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_italic() {
    font_italic_ = !font_italic_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_italic_ ? "In nghiêng: Bật (Italic: ON)" : "In nghiêng: Tắt (Italic: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_underline() {
    font_underline_ = !font_underline_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_underline_ ? "Gạch chân: Bật (Underline: ON)" : "Gạch chân: Tắt (Underline: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::set_font_alignment(typography::TextAlignment align) {
    font_alignment_ = align;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = (align == typography::TextAlignment::Center) ? "Căn giữa (Center Align)"
                : (align == typography::TextAlignment::Right) ? "Căn phải (Right Align)"
                : "Căn trái (Left Align)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::convert_selected_text_to_curves() {
    const auto& sel = root().vector_controller().selection().selected_ids();
    if (sel.empty()) return;
    int converted = 0;
    for (auto id : sel) {
        if (workflow().convert_text_to_curves(id)) {
            converted++;
        }
    }
    if (converted > 0) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã chuyển văn bản thành đường cong vector (Convert to Curves · Ctrl+Q)"
            : "Converted text to vector curves (Ctrl+Q)";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::insert_table(int rows, int cols) {
    auto id = workflow().create_table(40.0, 40.0, 160.0, 75.0, rows, cols);
    if (id != document::kInvalidObjectId) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã tạo bảng biểu vector chuyên nghiệp (Corel Table " + std::to_string(rows) + "×" + std::to_string(cols) + ")"
            : "Created vector table (" + std::to_string(rows) + "x" + std::to_string(cols) + ")";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::is_text_object_selected() const {
    const auto& sel = root().vector_controller().selection().selected_ids();
    if (sel.empty()) return false;
    const auto snap = root().document().current_snapshot();
    for (auto id : sel) {
        auto opt = snap->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) return true;
    }
    return false;
}

void NativeWindow::align_selected_objects(geometry::AlignMode mode) {
    bool ok = workflow().align_selected(mode, align_reference_, key_object_id_);
    if (ok) {
        std::string mode_str = "";
        switch (mode) {
            case geometry::AlignMode::Left: mode_str = "Căn trái (Align Left · L)"; break;
            case geometry::AlignMode::CenterHorizontal: mode_str = "Căn giữa đứng (Align Centers Horizontally · C)"; break;
            case geometry::AlignMode::Right: mode_str = "Căn phải (Align Right · R)"; break;
            case geometry::AlignMode::Top: mode_str = "Căn trên (Align Top · T)"; break;
            case geometry::AlignMode::CenterVertical: mode_str = "Căn giữa ngang (Align Centers Vertically · E)"; break;
            case geometry::AlignMode::Bottom: mode_str = "Căn dưới (Align Bottom · B)"; break;
            case geometry::AlignMode::CenterPage: mode_str = "Căn giữa trang in (Center to Page · P)"; break;
            case geometry::AlignMode::CenterPageHorizontal: mode_str = "Căn giữa trang theo chiều ngang (Center Page X)"; break;
            case geometry::AlignMode::CenterPageVertical: mode_str = "Căn giữa trang theo chiều dọc (Center Page Y)"; break;
        }
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thực hiện: " + mode_str + " · Nhấn Ctrl+Z để hoàn tác"
            : "Executed: " + mode_str + " · Press Ctrl+Z to undo";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::distribute_selected_objects(geometry::DistributeMode mode) {
    bool ok = workflow().distribute_selected(mode, -1.0);
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã phân bổ đều các đối tượng (Distribute Objects) · Nhấn Ctrl+Z để hoàn tác"
            : "Distributed selected objects · Press Ctrl+Z to undo";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_specified_spacing(bool horizontal) {
    auto mode = horizontal ? geometry::DistributeMode::SpacingHorizontal : geometry::DistributeMode::SpacingVertical;
    bool ok = workflow().distribute_selected(mode, distribute_spacing_mm_);
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã phân bổ khoảng hở đều: " + std::to_string(static_cast<int>(distribute_spacing_mm_)) + " mm"
            : "Applied fixed spacing gap: " + std::to_string(static_cast<int>(distribute_spacing_mm_)) + " mm";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_inspector_properties_panel() noexcept {
    active_dock_tab_ = 0; // 0: Inspector & Properties
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã mở Bảng Thuộc tính & Căn chỉnh Tổng quát (Inspector & Align · Ctrl+Shift+A)"
        : "Opened Inspector & Align Properties Panel (Ctrl+Shift+A)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::update_hover_tooltip(int mouse_x, int mouse_y) {
    bool is_vi = (language_ == UILanguage::Vietnamese);
    int w = width_;
    int h = height_;
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, tool_w = 52, inspect_w = 280;
    int content_top = topbar_h + tabbar_h + ribbon_h;
    int insp_x = w - inspect_w;

    std::wstring tip_text;

    // 1. Top Bar Elements (y: 6 to 32)
    if (mouse_y >= 6 && mouse_y <= 32) {
        if (mouse_x >= 10 && mouse_x <= 36) {
            tip_text = is_vi ? L"PikDraw Pro · Phần mềm thiết kế đồ họa vector chuyên nghiệp"
                             : L"PikDraw Pro · Professional Vector & Manufacturing Editor";
        } else if (mouse_x >= 148 && mouse_x < 198) {
            tip_text = is_vi ? L"Tạo tài liệu mới (Ctrl+N)" : L"Create new vector document (Ctrl+N)";
        } else if (mouse_x >= 198 && mouse_x < 248) {
            tip_text = is_vi ? L"Mở tệp tài liệu vector (Ctrl+O)" : L"Open vector document (Ctrl+O)";
        } else if (mouse_x >= 248 && mouse_x < 298) {
            tip_text = is_vi ? L"Lưu tài liệu hiện tại (Ctrl+S)" : L"Save active document (Ctrl+S)";
        } else if (mouse_x >= 298 && mouse_x < 350) {
            tip_text = is_vi ? L"Hoàn tác thao tác vừa thực hiện (Ctrl+Z)" : L"Undo last action (Ctrl+Z)";
        } else if (mouse_x >= 350 && mouse_x < 402) {
            tip_text = is_vi ? L"Làm lại thao tác vừa hoàn tác (Ctrl+Y)" : L"Redo action (Ctrl+Y)";
        } else if (mouse_x >= 402 && mouse_x <= 456) {
            tip_text = is_vi ? L"Trình đơn Tệp (File Menu)" : L"File Menu (Open, Save, Export...)";
        } else if (mouse_x >= 520 && mouse_x <= 620) {
            tip_text = is_vi ? L"Không gian làm việc Vector 2D" : L"Vector 2D Workspace";
        } else if (mouse_x >= w - 215 && mouse_x <= w - 105) {
            tip_text = is_vi ? L"Cài đặt hệ thống & Phím tắt (Ctrl+J)" : L"Preferences & Shortcuts (Ctrl+J)";
        } else if (mouse_x >= w - 95 && mouse_x <= w - 12) {
            tip_text = is_vi ? L"Xuất file đồ họa vector SVG (Ctrl+E)" : L"Export SVG vector graphics (Ctrl+E)";
        }
    }

    // 2. Tab Bar (y: topbar_h to topbar_h + tabbar_h)
    else if (mouse_y >= topbar_h && mouse_y < topbar_h + tabbar_h) {
        int cur_x = 10;
        int tab_w = 210;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (mouse_x >= cur_x && mouse_x <= cur_x + tab_w) {
                if (mouse_x >= cur_x + tab_w - 24) {
                    tip_text = is_vi ? L"Đóng tab tài liệu này (Ctrl+W)" : L"Close document tab (Ctrl+W)";
                } else {
                    tip_text = (is_vi ? L"Tab tài liệu: " : L"Document Tab: ") + utf8_to_wide(tabs_[i].title);
                }
                break;
            }
            cur_x += tab_w + 4;
        }
        if (tip_text.empty() && mouse_x >= cur_x && mouse_x <= cur_x + 28) {
            tip_text = is_vi ? L"Tạo tài liệu mới dạng Tab (Ctrl+N)" : L"New Document Tab (Ctrl+N)";
        }
    }

    // 3. Ribbon (y: topbar_h + tabbar_h to content_top)
    else if (mouse_y >= topbar_h + tabbar_h && mouse_y < content_top) {
        const auto& selection = root().vector_controller().selection().selected_ids();
        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if (is_text_mode) {
            if (mouse_x >= 52 && mouse_x <= 192) {
                tip_text = is_vi ? L"Chọn kiểu phông chữ văn bản (Font Family)" : L"Select Font Family";
            } else if (mouse_x >= 220 && mouse_x <= 240) {
                tip_text = is_vi ? L"Giảm cỡ chữ (-2 pt)" : L"Decrease Font Size (-2 pt)";
            } else if (mouse_x >= 294 && mouse_x <= 314) {
                tip_text = is_vi ? L"Tăng cỡ chữ (+2 pt)" : L"Increase Font Size (+2 pt)";
            } else if (mouse_x >= 322 && mouse_x <= 350) {
                tip_text = is_vi ? L"In đậm chữ (Bold · Ctrl+B)" : L"Bold text (Ctrl+B)";
            } else if (mouse_x >= 354 && mouse_x <= 382) {
                tip_text = is_vi ? L"In nghiêng chữ (Italic · Ctrl+I)" : L"Italic text (Ctrl+I)";
            } else if (mouse_x >= 386 && mouse_x <= 414) {
                tip_text = is_vi ? L"Gạch chân chữ (Underline · Ctrl+U)" : L"Underline text (Ctrl+U)";
            } else if (mouse_x >= 422 && mouse_x <= 462) {
                tip_text = is_vi ? L"Căn lề trái dòng chữ (Align Left)" : L"Align text Left";
            } else if (mouse_x >= 466 && mouse_x <= 506) {
                tip_text = is_vi ? L"Căn giữa dòng chữ (Align Center)" : L"Align text Center";
            } else if (mouse_x >= 510 && mouse_x <= 550) {
                tip_text = is_vi ? L"Căn phải dòng chữ (Align Right)" : L"Align text Right";
            } else if (mouse_x >= 556 && mouse_x <= 616) {
                tip_text = is_vi ? L"Chuyển văn bản thành đường cong vector (Convert to Curves · Ctrl+Q)"
                                 : L"Convert Text to Curves / Vector Outlines (Ctrl+Q)";
            } else if (mouse_x >= 622 && mouse_x <= 692) {
                tip_text = is_vi ? L"Tạo bảng văn bản 3×3 (Table Tool - CorelDRAW Style)"
                                 : L"Insert 3×3 Text Table (CorelDRAW Style)";
            } else if (!selection.empty() && mouse_x >= 698 && mouse_x <= 748) {
                tip_text = is_vi ? L"Xóa văn bản đang chọn (Delete)" : L"Delete selected text (Delete)";
            } else {
                int color_start_x = (!selection.empty()) ? 754 : 698;
                if (mouse_x >= color_start_x && mouse_x <= color_start_x + 160) {
                    tip_text = is_vi ? L"Màu sắc tô chữ nhanh (Quick Fill Color Swatch)" : L"Quick Fill Color Swatch";
                }
            }
        } else if (selection.empty()) {
            if (mouse_x >= 52 && mouse_x <= 165) {
                tip_text = is_vi ? L"Danh sách khổ giấy chuẩn (A4, A3, A2, Pano, Standee, Card...)" : L"Standard Page Presets List (A4, A3, A2, Pano, Standee, Card...)";
            } else if (mouse_x >= 175 && mouse_x <= 275) {
                tip_text = is_vi ? L"Chiều rộng khổ giấy (W mm) · Nhập trực tiếp số từ bàn phím" : L"Page Width (W mm) · Type directly from keyboard";
            } else if (mouse_x >= 283 && mouse_x <= 383) {
                tip_text = is_vi ? L"Chiều cao khổ giấy (H mm) · Nhập trực tiếp số từ bàn phím" : L"Page Height (H mm) · Type directly from keyboard";
            } else if (mouse_x >= 391 && mouse_x <= 455) {
                tip_text = is_vi ? L"Chuyển trang in sang hướng Dọc (Portrait)" : L"Page Orientation: Portrait";
            } else if (mouse_x >= 461 && mouse_x <= 525) {
                tip_text = is_vi ? L"Chuyển trang in sang hướng Ngang (Landscape)" : L"Page Orientation: Landscape";
            } else if (mouse_x >= 533 && mouse_x <= 665) {
                tip_text = is_vi ? L"Hệ màu in ấn CMYK · Độ phân giải 300 DPI" : L"CMYK Print Color Profile · 300 DPI High-Res";
            }
        } else {
            // Object Selection Mode
            if (mouse_x >= 25 && mouse_x <= 43) {
                tip_text = is_vi ? L"Dịch chuyển đối tượng sang trái (-5 mm)" : L"Move object Left (-5 mm)";
            } else if (mouse_x >= 97 && mouse_x <= 115) {
                tip_text = is_vi ? L"Dịch chuyển đối tượng sang phải (+5 mm)" : L"Move object Right (+5 mm)";
            } else if (mouse_x >= 141 && mouse_x <= 159) {
                tip_text = is_vi ? L"Dịch chuyển đối tượng lên trên (-5 mm)" : L"Move object Up (-5 mm)";
            } else if (mouse_x >= 213 && mouse_x <= 231) {
                tip_text = is_vi ? L"Dịch chuyển đối tượng xuống dưới (+5 mm)" : L"Move object Down (+5 mm)";
            } else if (mouse_x >= 259 && mouse_x <= 277) {
                tip_text = is_vi ? L"Thu nhỏ chiều rộng (-10 mm)" : L"Decrease width (-10 mm)";
            } else if (mouse_x >= 337 && mouse_x <= 355) {
                tip_text = is_vi ? L"Mở rộng chiều rộng (+10 mm)" : L"Increase width (+10 mm)";
            } else if (mouse_x >= 362 && mouse_x <= 412) {
                tip_text = is_vi ? L"Khóa / Mở tỷ lệ kích thước W:H (Lock Proportions)" : L"Lock / Unlock Aspect Ratio";
            } else if (mouse_x >= 420 && mouse_x <= 438) {
                tip_text = is_vi ? L"Thu nhỏ chiều cao (-10 mm)" : L"Decrease height (-10 mm)";
            } else if (mouse_x >= 498 && mouse_x <= 516) {
                tip_text = is_vi ? L"Mở rộng chiều cao (+10 mm)" : L"Increase height (+10 mm)";
            } else if (mouse_x >= 522 && mouse_x <= 566) {
                tip_text = is_vi ? L"Xóa các đối tượng đang chọn (Delete)" : L"Delete selected object(s) (Delete)";
            } else if (mouse_x >= 570 && mouse_x <= 616) {
                tip_text = is_vi ? L"Nhân đôi đối tượng (Duplicate · Ctrl+D)" : L"Duplicate object(s) (Ctrl+D)";
            } else if (is_in_isolation_mode()) {
                if (mouse_x >= 622 && mouse_x <= 712) {
                    tip_text = is_vi ? L"Thoát chế độ cô lập nhóm (Phím Esc)" : L"Exit group isolation mode (Esc)";
                }
            } else {
                if (selection.size() >= 2) {
                    if (mouse_x >= 622 && mouse_x <= 662) {
                        tip_text = is_vi ? L"Căn lề trái đối tượng (Align Left · Phím L)" : L"Align Left edges (Shortcut: L)";
                    } else if (mouse_x >= 666 && mouse_x <= 706) {
                        tip_text = is_vi ? L"Căn giữa theo trục đứng (Align Centers Vertically · Phím C)" : L"Align Centers Vertically (Shortcut: C)";
                    } else if (mouse_x >= 710 && mouse_x <= 750) {
                        tip_text = is_vi ? L"Căn lề phải đối tượng (Align Right · Phím R)" : L"Align Right edges (Shortcut: R)";
                    } else if (mouse_x >= 754 && mouse_x <= 794) {
                        tip_text = is_vi ? L"Căn đỉnh trên đối tượng (Align Top · Phím T)" : L"Align Top edges (Shortcut: T)";
                    } else if (mouse_x >= 798 && mouse_x <= 838) {
                        tip_text = is_vi ? L"Căn giữa theo trục ngang (Align Centers Horizontally · Phím E)" : L"Align Centers Horizontally (Shortcut: E)";
                    } else if (mouse_x >= 842 && mouse_x <= 882) {
                        tip_text = is_vi ? L"Căn đáy dưới đối tượng (Align Bottom · Phím B)" : L"Align Bottom edges (Shortcut: B)";
                    } else if (mouse_x >= 886 && mouse_x <= 946) {
                        tip_text = is_vi ? L"Căn giữa toàn bộ vào trang in (Center to Page · Phím P)" : L"Center to Page bounds (Shortcut: P)";
                    } else if (mouse_x >= 950 && mouse_x <= 1010) {
                        tip_text = is_vi ? L"Nhóm các đối tượng lại (Group · Ctrl+G)" : L"Group selected objects (Ctrl+G)";
                    } else if (mouse_x >= 1014 && mouse_x <= 1068) {
                        tip_text = is_vi ? L"Hàn gộp các đối tượng thành 1 hình (Weld Shapes)" : L"Weld / Union selected shapes";
                    } else if (mouse_x >= 1072 && mouse_x <= 1126) {
                        tip_text = is_vi ? L"Cắt xén hình bằng đối tượng trên cùng (Trim Shapes)" : L"Trim underlying shape using top shape";
                    }
                } else if (selection.size() == 1) {
                    if (mouse_x >= 622 && mouse_x <= 682) {
                        tip_text = is_vi ? L"Căn giữa đối tượng vào trang in (Center Page · Phím P)" : L"Center to Page bounds (Shortcut: P)";
                    } else if (workflow().is_group(selection.front())) {
                        if (mouse_x >= 688 && mouse_x <= 758) {
                            tip_text = is_vi ? L"Rã nhóm đối tượng (Ungroup · Ctrl+Shift+G)" : L"Ungroup selected group (Ctrl+Shift+G)";
                        } else if (mouse_x >= 764 && mouse_x <= 834) {
                            tip_text = is_vi ? L"Chui vào trong nhóm để chỉnh sửa hình con (Enter hoặc Nhấp đúp)"
                                             : L"Enter group isolation mode to edit children (Enter or Double Click)";
                        }
                    }
                }
            }
        }
    }

    // 4. Left Tool Strip (x: 0 to tool_w, y: content_top to h - status_h)
    else if (mouse_x >= 0 && mouse_x <= tool_w && mouse_y >= content_top && mouse_y < h - status_h) {
        int tool_idx = (mouse_y - (content_top + 10)) / 42;
        if (tool_idx == 0) {
            tip_text = is_vi ? L"Công cụ chọn & biến đổi đối tượng (Pick Tool) · Phím V hoặc Space"
                             : L"Pick & Select / Transform Tool · Shortcut: V or Space";
        } else if (tool_idx == 1) {
            tip_text = is_vi ? L"Vẽ hình chữ nhật / hình vuông (Rectangle Tool) · Phím F6"
                             : L"Rectangle & Square Tool · Shortcut: F6";
        } else if (tool_idx == 2) {
            tip_text = is_vi ? L"Vẽ hình elip / hình tròn (Ellipse Tool) · Phím F7"
                             : L"Ellipse & Circle Tool · Shortcut: F7";
        } else if (tool_idx == 3) {
            tip_text = is_vi ? L"Kéo trượt vùng làm việc (Pan Hand Tool) · Phím H hoặc Giữ Chuột giữa"
                             : L"Pan Viewport Hand Tool · Shortcut: H or Middle Drag";
        } else if (tool_idx == 4) {
            tip_text = is_vi ? L"Soạn thảo văn bản & Bảng (Text Tool) · Phím F8"
                             : L"Text Typography & Table Tool · Shortcut: F8";
        } else if (tool_idx == 5) {
            tip_text = is_vi ? L"Dao cắt xén đối tượng vector (Knife Tool) · Phím K"
                             : L"Knife / Slice Tool · Shortcut: K";
        }
    }

    // 5. Right Inspector & Docker Panel (x: insp_x to w, y: content_top to h - status_h)
    else if (mouse_x >= insp_x && mouse_x <= w && mouse_y >= content_top && mouse_y < h - status_h) {
        int tab_w = inspect_w / 3;
        if (mouse_y >= content_top && mouse_y <= content_top + 28) {
            int tidx = (mouse_x - insp_x) / tab_w;
            if (tidx == 0) tip_text = is_vi ? L"Bảng thuộc tính đối tượng & Căn chỉnh (Ctrl+Shift+A)" : L"Object Inspector & Align Panel (Ctrl+Shift+A)";
            else if (tidx == 1) tip_text = is_vi ? L"Danh sách các lớp & Đối tượng (Layers)" : L"Layers & Objects Hierarchy";
            else if (tidx == 2) tip_text = is_vi ? L"Lịch sử các thao tác hoàn tác / làm lại (History)" : L"Command History & Undo/Redo Log";
        } else if (active_dock_tab_ == 0) {
            int content_y = content_top + 36;
            // Checkboxes: Keep source / Keep target
            if (mouse_y >= content_y + 222 && mouse_y <= content_y + 244) {
                if (mouse_x >= insp_x + 14 && mouse_x <= insp_x + 135) {
                    tip_text = is_vi ? L"Giữ lại đối tượng gốc làm khuôn mẫu sau khi cắt/hàn"
                                     : L"Keep source objects intact as template after boolean operation";
                } else if (mouse_x >= insp_x + 140 && mouse_x <= w - 14) {
                    tip_text = is_vi ? L"Giữ lại đối tượng đích sau khi thao tác tạo hình"
                                     : L"Keep target shapes intact after operation";
                }
            }
            int btn_w = (inspect_w - 38) / 3;
            int by1 = content_y + 248;
            int by2 = by1 + 30;
            if (mouse_y >= by1 && mouse_y <= by1 + 26) {
                if (mouse_x >= insp_x + 14 && mouse_x <= insp_x + 14 + btn_w) {
                    tip_text = is_vi ? L"Hợp nhất các đối tượng thành một hình kín (Weld / Union)"
                                     : L"Weld multiple shapes into a single boundary";
                } else if (mouse_x >= insp_x + 14 + btn_w + 5 && mouse_x <= insp_x + 14 + 2 * btn_w + 5) {
                    tip_text = is_vi ? L"Cắt xén đối tượng bên dưới bằng đối tượng bên trên (Trim)"
                                     : L"Trim underlying shape using top shape";
                } else if (mouse_x >= insp_x + 14 + (btn_w + 5) * 2 && mouse_x <= insp_x + 14 + 3 * btn_w + 10) {
                    tip_text = is_vi ? L"Lấy phần diện tích giao nhau giữa các hình (Intersect)"
                                     : L"Keep only overlapping intersection region";
                }
            } else if (mouse_y >= by2 && mouse_y <= by2 + 26) {
                if (mouse_x >= insp_x + 14 && mouse_x <= insp_x + 14 + btn_w) {
                    tip_text = is_vi ? L"Loại trừ phần giao nhau (XOR Exclude)" : L"Exclude overlapping areas (XOR)";
                } else if (mouse_x >= insp_x + 14 + btn_w + 5 && mouse_x <= insp_x + 14 + 2 * btn_w + 5) {
                    tip_text = is_vi ? L"Lấy hình đáy trừ đi hình đỉnh (Back Minus Front)" : L"Subtract front shapes from back shape";
                } else if (mouse_x >= insp_x + 14 + (btn_w + 5) * 2 && mouse_x <= insp_x + 14 + 3 * btn_w + 10) {
                    tip_text = is_vi ? L"Tạo đường bao viền ngoài toàn bộ cụm hình (Boundary)" : L"Create outer boundary contour";
                }
            }
            int by3 = by2 + 32;
            if (mouse_y >= by3 && mouse_y <= by3 + 28 && mouse_x >= insp_x + 14 && mouse_x <= w - 14) {
                tip_text = is_vi ? L"Tự động tìm kiếm và hàn phẳng mọi điểm giao cắt (Chuẩn bị file cắt CNC/Laser)"
                                 : L"Auto-detect and weld all overlapping vector paths (Ready for CNC/Laser)";
            }
            // Align & Distribute in Docker
            int by_align_hdr = by3 + 36;
            int by_a1 = by_align_hdr + 24;
            int by_a2 = by_a1 + 30;
            int by_a3 = by_a2 + 30;
            int by_a4 = by_a3 + 32;
            int by_a5 = by_a4 + 30;

            if (mouse_y >= by_a1 && mouse_y <= by_a1 + 26) {
                if (mouse_x >= insp_x + 14 && mouse_x <= insp_x + 14 + btn_w) {
                    tip_text = is_vi ? L"Căn lề trái các đối tượng đã chọn (Align Left · Phím L)"
                                     : L"Align selected objects to Left edges (Shortcut: L)";
                } else if (mouse_x >= insp_x + 14 + btn_w + 5 && mouse_x <= insp_x + 14 + 2 * btn_w + 5) {
                    tip_text = is_vi ? L"Căn giữa theo trục đứng (Align Centers Vertically · Phím C)"
                                     : L"Align Centers Vertically (Shortcut: C)";
                } else if (mouse_x >= insp_x + 14 + 2 * (btn_w + 5) && mouse_x <= insp_x + 14 + 3 * btn_w + 10) {
                    tip_text = is_vi ? L"Căn lề phải các đối tượng đã chọn (Align Right · Phím R)"
                                     : L"Align selected objects to Right edges (Shortcut: R)";
                }
            } else if (mouse_y >= by_a2 && mouse_y <= by_a2 + 26) {
                if (mouse_x >= insp_x + 14 && mouse_x <= insp_x + 14 + btn_w) {
                    tip_text = is_vi ? L"Căn đỉnh trên các đối tượng đã chọn (Align Top · Phím T)"
                                     : L"Align selected objects to Top edges (Shortcut: T)";
                } else if (mouse_x >= insp_x + 14 + btn_w + 5 && mouse_x <= insp_x + 14 + 2 * btn_w + 5) {
                    tip_text = is_vi ? L"Căn giữa theo trục ngang (Align Centers Horizontally · Phím E)"
                                     : L"Align Centers Horizontally (Shortcut: E)";
                } else if (mouse_x >= insp_x + 14 + 2 * (btn_w + 5) && mouse_x <= insp_x + 14 + 3 * btn_w + 10) {
                    tip_text = is_vi ? L"Căn đáy dưới các đối tượng đã chọn (Align Bottom · Phím B)"
                                     : L"Align selected objects to Bottom edges (Shortcut: B)";
                }
            } else if (mouse_y >= by_a3 && mouse_y <= by_a3 + 26 && mouse_x >= insp_x + 14 && mouse_x <= w - 14) {
                tip_text = is_vi ? L"Căn giữa tuyệt đối vào trang in (Center Page · Phím P) · Shift+P: Trục đứng · Alt+P: Trục ngang"
                                 : L"Center to Page bounds (Shortcut: P) · Shift+P: Horizontal · Alt+P: Vertical";
            } else if (mouse_y >= by_a4 - 2 && mouse_y <= by_a4 + 22) {
                int ref_w = (inspect_w - 85) / 3;
                if (mouse_x >= insp_x + 72 && mouse_x <= insp_x + 72 + ref_w) {
                    tip_text = is_vi ? L"Quy chiếu căn chỉnh: Theo khung bao tổng của vùng chọn (Selection Bounds)"
                                     : L"Align Reference: Selection Bounding Box";
                } else if (mouse_x >= insp_x + 72 + ref_w + 3 && mouse_x <= insp_x + 72 + 2 * ref_w + 3) {
                    tip_text = is_vi ? L"Quy chiếu căn chỉnh: Cố định theo Đối tượng chủ / Neo (Key Object)"
                                     : L"Align Reference: Key Object (Anchor)";
                } else if (mouse_x >= insp_x + 72 + 2 * (ref_w + 3) && mouse_x <= insp_x + 72 + 3 * ref_w + 6) {
                    tip_text = is_vi ? L"Quy chiếu căn chỉnh: Căn thẳng theo Trang in (Page Margins)"
                                     : L"Align Reference: Page Margins";
                }
            } else if (mouse_y >= by_a5 - 2 && mouse_y <= by_a5 + 24) {
                int gap_half = (inspect_w - 90) / 2;
                if (mouse_x >= insp_x + 72 && mouse_x <= insp_x + 72 + gap_half) {
                    tip_text = is_vi ? L"Phân bố khoảng cách ngang đều nhau 10mm giữa các đối tượng"
                                     : L"Distribute equal 10mm horizontal spacing between objects";
                } else if (mouse_x >= insp_x + 72 + gap_half + 4 && mouse_x <= insp_x + 72 + 2 * gap_half + 4) {
                    tip_text = is_vi ? L"Phân bố khoảng cách dọc đều nhau 10mm giữa các đối tượng"
                                     : L"Distribute equal 10mm vertical spacing between objects";
                }
            }
        }
    }

    // 6. Bottom Status Bar (y: h - status_h to h)
    else if (mouse_y >= h - status_h && mouse_y <= h) {
        if (mouse_x >= 16 && mouse_x <= 210) {
            tip_text = is_vi ? L"Tọa độ con trỏ chuột trên mặt vẽ (mm)" : L"Pointer coordinates on canvas in mm";
        } else if (mouse_x >= 215 && mouse_x <= 400) {
            tip_text = is_vi ? L"Chế độ giao diện (Dark / Light Theme)" : L"Current Theme Mode (Dark / Light)";
        } else if (mouse_x >= w - 210 && mouse_x <= w - 145) {
            tip_text = is_vi ? L"Thu phóng vừa vặn toàn bộ trang in trong khung nhìn (Fit Page)" : L"Zoom to fit entire page in view";
        } else if (mouse_x >= w - 120 && mouse_x <= w - 95) {
            tip_text = is_vi ? L"Thu nhỏ góc nhìn vùng vẽ (-)" : L"Zoom Out (-)";
        } else if (mouse_x >= w - 45 && mouse_x <= w - 20) {
            tip_text = is_vi ? L"Phóng to góc nhìn vùng vẽ (+)" : L"Zoom In (+)";
        }
    }

    // 7. Canvas Rulers & Origin
    const RECT c_area = canvas_rect(w, h);
    if (mouse_x >= 52 && mouse_x < c_area.left && mouse_y >= 102 && mouse_y < c_area.top) {
        tip_text = is_vi ? L"Gốc tọa độ thước (0,0) · Phím Ctrl+; để Ẩn/Hiện đường gióng"
                         : L"Ruler Origin (0,0) · Shortcut: Ctrl+; to toggle guidelines";
    } else if (mouse_x >= c_area.left && mouse_x <= c_area.right && mouse_y >= 102 && mouse_y < c_area.top) {
        tip_text = is_vi ? L"Thước đo ngang · Nhấp và kéo chuột xuống để tạo đường gióng ngang"
                         : L"Horizontal Ruler · Drag down to place horizontal guideline";
    } else if (mouse_x >= 52 && mouse_x < c_area.left && mouse_y >= c_area.top && mouse_y <= c_area.bottom) {
        tip_text = is_vi ? L"Thước đo dọc · Nhấp và kéo chuột sang phải để tạo đường gióng dọc"
                         : L"Vertical Ruler · Drag right to place vertical guideline";
    }

    // Update state
    std::wstring prev_text = tooltip_.text;

    if (!tip_text.empty()) {
        tooltip_.visible = true;
        tooltip_.x = mouse_x;
        tooltip_.y = mouse_y;
        tooltip_.text = tip_text;
    } else {
        tooltip_.visible = false;
        tooltip_.text.clear();
    }

    // Also reflect into status bar for immediate feedback
    if (tooltip_.visible && !tooltip_.text.empty() && prev_text != tooltip_.text) {
        status_msg_ = wide_to_utf8(tooltip_.text);
    }
}

void NativeWindow::add_guideline(GuidelineOrientation orient, double pos_mm) {
    guidelines_.push_back({orient, pos_mm, false});
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << pos_mm;
    if (orient == GuidelineOrientation::Horizontal) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thêm đường gióng ngang: Y = " + ss.str() + " mm"
            : "Added horizontal guideline: Y = " + ss.str() + " mm";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thêm đường gióng dọc: X = " + ss.str() + " mm"
            : "Added vertical guideline: X = " + ss.str() + " mm";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_guidelines() noexcept {
    show_guidelines_ = !show_guidelines_;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? (show_guidelines_ ? "Đã bật hiển thị đường gióng (Ctrl+;)" : "Đã ẩn đường gióng (Ctrl+;)")
        : (show_guidelines_ ? "Showing guidelines (Ctrl+;)" : "Guidelines hidden (Ctrl+;)");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::clear_guidelines() noexcept {
    size_t count = guidelines_.size();
    guidelines_.clear();
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã xóa toàn bộ " + std::to_string(count) + " đường gióng"
        : "Cleared all " + std::to_string(count) + " guidelines";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::start_numeric_input(NumericFieldId field) {
    if (active_num_field_ != NumericFieldId::None && active_num_field_ != field) {
        commit_numeric_field();
    }
    active_num_field_ = field;
    num_input_buffer_.clear();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (field == NumericFieldId::PageWidth) {
        ss << active_tab().page_width_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PageHeight) {
        ss << active_tab().page_height_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalWidth) {
        ss << new_doc_w_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalHeight) {
        ss << new_doc_h_;
        num_input_buffer_ = ss.str();
    } else {
        const auto ids = root().vector_controller().selection().selected_ids();
        if (!ids.empty()) {
            const auto snapshot = root().document().current_snapshot();
            const auto obj = snapshot->objects().get(ids.front());
            if (obj) {
                const auto bounds_opt = std::visit([](const auto& val) -> std::optional<geometry::Rect2D> {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, document::GroupObject>) return std::nullopt;
                    else return val.bounds();
                }, *obj);
                if (bounds_opt) {
                    if (field == NumericFieldId::ObjectX) ss << bounds_opt->x;
                    else if (field == NumericFieldId::ObjectY) ss << bounds_opt->y;
                    else if (field == NumericFieldId::ObjectWidth) ss << bounds_opt->width;
                    else if (field == NumericFieldId::ObjectHeight) ss << bounds_opt->height;
                    num_input_buffer_ = ss.str();
                }
            }
        }
    }

    if (num_input_buffer_.size() > 2 && num_input_buffer_.substr(num_input_buffer_.size() - 2) == ".0") {
        num_input_buffer_.erase(num_input_buffer_.size() - 2);
    }

    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đang nhập số: gõ phím số, Enter để áp dụng, Tab chuyển ô, Esc để hủy"
        : "Numeric input: type value, Enter to apply, Tab to next field, Esc to cancel";

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::commit_numeric_field() {
    if (active_num_field_ == NumericFieldId::None) return;

    if (!num_input_buffer_.empty()) {
        try {
            double val = std::stod(num_input_buffer_);
            const auto field = active_num_field_;

            if (field == NumericFieldId::PageWidth) {
                set_active_page_size(val, active_tab().page_height_mm);
            } else if (field == NumericFieldId::PageHeight) {
                set_active_page_size(active_tab().page_width_mm, val);
            } else if (field == NumericFieldId::ModalWidth) {
                new_doc_w_ = std::clamp(val, 10.0, geometry::kMaxObjectDimensionMm);
            } else if (field == NumericFieldId::ModalHeight) {
                new_doc_h_ = std::clamp(val, 10.0, geometry::kMaxObjectDimensionMm);
            } else {
                const auto ids = root().vector_controller().selection().selected_ids();
                if (!ids.empty()) {
                    const auto id = ids.front();
                    const auto snapshot = root().document().current_snapshot();
                    const auto obj = snapshot->objects().get(id);
                    if (obj) {
                        const auto bounds_opt = std::visit([](const auto& val) -> std::optional<geometry::Rect2D> {
                            using T = std::decay_t<decltype(val)>;
                            if constexpr (std::is_same_v<T, document::GroupObject>) return std::nullopt;
                            else return val.bounds();
                        }, *obj);
                        if (bounds_opt) {
                            if (field == NumericFieldId::ObjectX) {
                                double target_x = std::clamp(val, -geometry::kMaxCanvasDimensionMm * 0.5, geometry::kMaxCanvasDimensionMm * 0.5);
                                move_selected_object(target_x - bounds_opt->x, 0.0);
                            } else if (field == NumericFieldId::ObjectY) {
                                double target_y = std::clamp(val, -geometry::kMaxCanvasDimensionMm * 0.5, geometry::kMaxCanvasDimensionMm * 0.5);
                                move_selected_object(0.0, target_y - bounds_opt->y);
                            } else if (field == NumericFieldId::ObjectWidth) {
                                double new_w = std::clamp(val, 1.0, geometry::kMaxObjectDimensionMm);
                                double new_h = bounds_opt->height;
                                if (aspect_ratio_locked_ && bounds_opt->width > 0.001) {
                                    new_h = std::clamp(bounds_opt->height * (new_w / bounds_opt->width), 1.0, geometry::kMaxObjectDimensionMm);
                                }
                                workflow().resize_object(id, new_w, new_h);
                            } else if (field == NumericFieldId::ObjectHeight) {
                                double new_h = std::clamp(val, 1.0, geometry::kMaxObjectDimensionMm);
                                double new_w = bounds_opt->width;
                                if (aspect_ratio_locked_ && bounds_opt->height > 0.001) {
                                    new_w = std::clamp(bounds_opt->width * (new_h / bounds_opt->height), 1.0, geometry::kMaxObjectDimensionMm);
                                }
                                workflow().resize_object(id, new_w, new_h);
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore format errors
        }
    }

    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cancel_numeric_field() {
    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();
    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy nhập số" : "Cancelled numeric input";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::handle_numeric_char_input(wchar_t ch) {
    if (active_num_field_ == NumericFieldId::None) return;

    if (ch == 8) { // Backspace
        if (!num_input_buffer_.empty()) {
            num_input_buffer_.pop_back();
        }
    } else if (ch == 13) { // Enter
        commit_numeric_field();
        return;
    } else if (ch == 27) { // Escape
        cancel_numeric_field();
        return;
    } else if (ch == 9) { // Tab: cycle to next field
        NumericFieldId next = NumericFieldId::None;
        switch (active_num_field_) {
            case NumericFieldId::PageWidth: next = NumericFieldId::PageHeight; break;
            case NumericFieldId::PageHeight: next = NumericFieldId::PageWidth; break;
            case NumericFieldId::ModalWidth: next = NumericFieldId::ModalHeight; break;
            case NumericFieldId::ModalHeight: next = NumericFieldId::ModalWidth; break;
            case NumericFieldId::ObjectX: next = NumericFieldId::ObjectY; break;
            case NumericFieldId::ObjectY: next = NumericFieldId::ObjectWidth; break;
            case NumericFieldId::ObjectWidth: next = NumericFieldId::ObjectHeight; break;
            case NumericFieldId::ObjectHeight: next = NumericFieldId::ObjectX; break;
            default: break;
        }
        commit_numeric_field();
        if (next != NumericFieldId::None) {
            start_numeric_input(next);
        }
        return;
    } else if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-') {
        if (ch == L'.' && num_input_buffer_.find('.') != std::string::npos) {
            return;
        }
        if (ch == L'-' && !num_input_buffer_.empty()) {
            return;
        }
        if (num_input_buffer_.size() < 12) {
            num_input_buffer_.push_back(static_cast<char>(ch));
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = width_, h = height_;
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    paint(mem_dc, w, h);

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER), w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0 };
    int row_stride = ((w * 3 + 3) / 4) * 4;
    int data_size = row_stride * h;
    bi.biSizeImage = data_size;

    std::vector<uint8_t> pixel_data(data_size);
    GetDIBits(mem_dc, mem_bmp, 0, h, pixel_data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = { 0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };

    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        out.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
        out.write(reinterpret_cast<const char*>(pixel_data.data()), data_size);
    }

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

#else
bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
void NativeWindow::capture_screenshot_bmp(const std::string&) {}
#endif

} // namespace pikdraw::desktop
