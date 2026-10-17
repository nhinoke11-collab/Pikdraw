#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_canvas_frame.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    tab.root->renderer().backend().set_canvas_background(canvas_background_color());
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(297.0, 210.0);
    register_drawing_actions(*tab.root);
    tabs_.push_back(std::move(tab));
    active_tab_index_ = 0;
    bind_drawing_tool_to_active_document();
}

void NativeWindow::register_drawing_actions(CompositionRoot& document_root) {
    auto& dispatcher = document_root.window().action_dispatcher();
    dispatcher.register_action({std::string(actions::ActionId::kToolNodeEdit),
        "Chỉnh sửa điểm Node", "Node Edit Tool", "F10", [this] { set_active_tool(11); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolLine),
        "Vẽ đoạn thẳng", "Line Tool", "L", [this] { set_active_tool(6); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolPolyline),
        "Vẽ đường gấp khúc", "Polyline Tool", "Shift+L",
        [this] { set_active_tool(7); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolPen),
        "Công cụ Pen", "Pen Tool", "", [this] { set_active_tool(8); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolContour),
        "Tạo viền bao (Contour)", "Contour Tool", "", [this] { set_active_tool(12); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kEffectsContourFocus),
        "Hiệu ứng viền Contour", "Contour Effect", "", [this] { trigger_effects_contour(); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kObjectContourBreakApart),
        "Rã khối viền Contour", "Break Contour Apart", "Ctrl+K", [this] { trigger_break_contour_apart(); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolSnapPanel),
        "Bắt điểm và hướng", "Snap and Direction", "F3", [this] {
            if (drawing_tool_) drawing_tool_->toggle_snap_panel();
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        }, nullptr});
}

void NativeWindow::bind_drawing_tool_to_active_document() {
    if (drawing_tool_) {
        drawing_tool_->cancel();
        drawing_tool_.reset();
    }
    if (pen_tool_) {
        pen_tool_->cancel();
        pen_tool_.reset();
    }
    if (node_tool_) {
        node_tool_->deactivate();
        node_tool_.reset();
    }
    if (contour_tool_) {
        contour_tool_->deactivate();
        contour_tool_.reset();
    }
    if (tabs_.empty() || !active_tab().root)
        return;
    if (color_preview_)
        (void)root().renderer().backend().set_color_preview(color_preview_);
    const auto mode = active_tool_ == 1 ? vector::ToolMode::Rectangle
                    : active_tool_ == 2 ? vector::ToolMode::Ellipse
                    : active_tool_ == 4 ? vector::ToolMode::Text
                                        : vector::ToolMode::Select;
    root().vector_controller().set_mode(mode);
    drawing_tool_ = std::make_unique<tools::DrawingToolController>(root().document());
    pen_tool_ = std::make_unique<tools::PenToolController>(root().document());
    node_tool_ = std::make_unique<tools::NodeToolController>(root().document());
    contour_tool_ = std::make_unique<tools::ContourToolController>(root().document());
    if (active_tool_ == 6 || active_tool_ == 7) {
        const auto kind = active_tool_ == 6 ? vector::tools::line::PathDrawingKind::Line
                                            : vector::tools::line::PathDrawingKind::Polyline;
        if (!drawing_tool_->activate(kind)) {
            drawing_tool_.reset();
            active_tool_ = 0;
            root().vector_controller().set_mode(vector::ToolMode::Select);
        }
    }
    if (active_tool_ == 8) {
        // Reuse the existing D09 snap index/configuration as a preview-only adapter.
        if (drawing_tool_)
            (void)drawing_tool_->activate(vector::tools::line::PathDrawingKind::Line);
        const auto result = pen_tool_->activate();
        if (result.code != vector::tools::pen::PenDrawingResultCode::Applied) {
            pen_tool_.reset();
            active_tool_ = 0;
            root().vector_controller().set_mode(vector::ToolMode::Select);
        } else {
            auto preferences = tools::PenGesturePreferences{};
            preferences.profile = shortcut_profile_ == 0
                ? tools::PenShortcutProfile::Corel
                : shortcut_profile_ == 1 ? tools::PenShortcutProfile::Illustrator
                                         : tools::PenShortcutProfile::PikDraw;
            (void)pen_tool_->set_gesture_preferences(preferences);
        }
    }
    if (active_tool_ == 11) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto target = sel.empty() ? document::kInvalidObjectId : sel.front();
        (void)node_tool_->activate(target);
    }
    if (active_tool_ == 12) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto target = sel.empty() ? document::kInvalidObjectId : sel.front();
        (void)contour_tool_->activate(target);
    }
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

bool NativeWindow::drawing_snap_panel_open() const noexcept {
    return drawing_tool_ && drawing_tool_->snap_panel_open();
}

bool NativeWindow::drawing_snap_index_ready() const noexcept {
    return drawing_tool_ && drawing_tool_->snap_index_ready();
}

bool NativeWindow::drawing_path_active() const noexcept {
    return drawing_tool_ && drawing_tool_->active();
}

size_t NativeWindow::drawing_path_point_count() const noexcept {
    return drawing_tool_ ? drawing_tool_->path_point_count() : 0;
}

void NativeWindow::switch_to_tab(size_t index) noexcept {
    if (index < tabs_.size() && index != active_tab_index_) {
        if (drawing_tool_) drawing_tool_->cancel();
        active_tab_index_ = index;
        bind_drawing_tool_to_active_document();
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::close_tab(size_t index) noexcept {
    if (index >= tabs_.size()) return;
    if (tabs_.size() > 1) {
        const bool closing_active = index == active_tab_index_;
        if (closing_active && drawing_tool_) {
            drawing_tool_->cancel();
            drawing_tool_.reset();
        }
        if (tabs_[index].root) tabs_[index].root->shutdown();
        tabs_.erase(tabs_.begin() + index);
        if (index < active_tab_index_) {
            --active_tab_index_;
        } else if (active_tab_index_ >= tabs_.size()) {
            active_tab_index_ = tabs_.size() - 1;
        }
        if (closing_active) bind_drawing_tool_to_active_document();
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    } else {
        if (drawing_tool_) drawing_tool_->cancel();
        workflow().new_document(297.0, 210.0);
        bind_drawing_tool_to_active_document();
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
    tab.root->renderer().backend().set_canvas_background(canvas_background_color());
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(w_mm, h_mm);
    register_drawing_actions(*tab.root);
    if (drawing_tool_) drawing_tool_->cancel();
    tabs_.push_back(std::move(tab));
    active_tab_index_ = tabs_.size() - 1;
    bind_drawing_tool_to_active_document();
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
    if (!ok) {
        const bool boundary = op == geometry::BooleanOp::Boundary;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (boundary ? "Boundary chưa được hỗ trợ; chọn hai path chữ nhật hợp lệ để tạo hình."
                        : "Lệnh cần đúng hai path chữ nhật được hỗ trợ và kết quả không rỗng.")
            : (boundary ? "Boundary is unsupported; choose two eligible rectangle paths."
                        : "This command needs two supported rectangle paths and a non-empty result.");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return false;
    }
    if (ok) {
        const char* vi_msg = "Thực hiện tạo hình khối thành công";
        const char* en_msg = "Solid operation completed";
        switch (op) {
            case geometry::BooleanOp::Union:
                vi_msg = "Đã hàn các đối tượng (Weld)";
                en_msg = "Welded selected objects";
                break;
            case geometry::BooleanOp::Difference:
                vi_msg = "Đã trừ path chọn thứ hai khỏi path chọn thứ nhất";
                en_msg = "Subtracted the second selected path from the first";
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
                vi_msg = "Đã trừ path chọn thứ nhất khỏi path chọn thứ hai";
                en_msg = "Subtracted the first selected path from the second";
                break;
            case geometry::BooleanOp::Boundary:
                vi_msg = "Boundary chưa được hỗ trợ";
                en_msg = "Boundary is unsupported";
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
    mcp_runtime_.reset();
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
    cached_canvas_snapshot_.reset();
    cached_canvas_profile_.reset();
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
    canvas_cache_valid_ = false;
    const auto canvas_bg = canvas_background_color();
    for (auto& tab : tabs_) {
        if (tab.root) {
            tab.root->renderer().backend().set_canvas_background(canvas_bg);
        }
    }
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        (mode == ThemeMode::CyberDark ? "Đã chuyển sang Chế độ Tối (Dark Mode Obsidian)" : "Đã chuyển sang Chế độ Sáng (Light Mode Silver)") :
        (mode == ThemeMode::CyberDark ? "Switched to Dark Mode (Obsidian Slate)" : "Switched to Light Mode (Silver Tech)");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

color::DeviceRGBA NativeWindow::canvas_background_color() const noexcept {
    return (theme_ == ThemeMode::CyberDark)
        ? color::DeviceRGBA{68, 72, 80, 255}    // Neutral gray (màu ghi)
        : color::DeviceRGBA{255, 255, 255, 255}; // Pure white (màu trắng)
}

#ifdef _WIN32
COLORREF NativeWindow::canvas_background_colorref() const noexcept {
    return (theme_ == ThemeMode::CyberDark)
        ? RGB(68, 72, 80)
        : RGB(255, 255, 255);
}
#endif

void NativeWindow::toggle_theme() noexcept {
    set_theme(theme_ == ThemeMode::CyberDark ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::open_settings_modal() noexcept {
#ifdef _WIN32
    if (pointer_active_ || is_panning_)
        cancel_canvas_interaction();
#endif
    settings_mcp_enabled_choice_ = mcp_listener_active();
    settings_mcp_edit_choice_ = mcp_edit_access_active();
    settings_ai_focus_index_ = 0;
    show_settings_modal_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở Cài đặt hệ thống (PikDraw Preferences)" : "Opened System Preferences (PikDraw Settings)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::open_ai_connections_settings() noexcept {
    open_settings_modal();
    set_settings_tab(6);
}

void NativeWindow::close_settings_modal() noexcept {
    discard_ai_connections_choices();
    show_settings_modal_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã đóng Cài đặt hệ thống" : "Closed System Preferences";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_settings_modal() noexcept {
    if (show_settings_modal_) close_settings_modal(); else open_settings_modal();
}

void NativeWindow::set_settings_tab(int tab) noexcept {
    if (tab < 0 || tab > 6) return;
    settings_active_tab_ = tab;
    const char* vi_tabs[] = { "Chung & Ngôn ngữ", "Giao diện (Theme)", "Đơn vị & Bắt điểm", "Quản lý màu & ICC", "Phím tắt & Thao tác", "Thi công & CNC", "Kết nối AI" };
    const char* en_tabs[] = { "General & Language", "Display & Theme", "Units & Snapping", "Color & ICC", "Shortcuts & Hotkeys", "Production & CAM", "AI Connections" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Tùy chọn: ") + vi_tabs[tab] : std::string("Options: ") + en_tabs[tab];
    if (tab == 6) settings_ai_focus_index_ = 0;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::discard_ai_connections_choices() noexcept {
    settings_mcp_enabled_choice_ = mcp_listener_active();
    settings_mcp_edit_choice_ = mcp_edit_access_active();
}

void NativeWindow::toggle_ai_connections_read_choice() noexcept {
    settings_mcp_enabled_choice_ = !settings_mcp_enabled_choice_;
    if (!settings_mcp_enabled_choice_) settings_mcp_edit_choice_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Quyền MCP đang là bản nháp · chọn Áp dụng để xác nhận"
        : "MCP access is a draft · choose Apply to confirm";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_ai_connections_edit_choice() noexcept {
    if (!settings_mcp_enabled_choice_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bật Cho phép MCP trước khi cấp quyền sửa"
            : "Enable MCP access before granting edit permission";
    } else {
        settings_mcp_edit_choice_ = !settings_mcp_edit_choice_;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Quyền MCP đang là bản nháp · chọn Áp dụng để xác nhận"
            : "MCP access is a draft · choose Apply to confirm";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::reset_ai_connections_choices() noexcept {
    settings_mcp_enabled_choice_ = false;
    settings_mcp_edit_choice_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "MCP đang tắt trong bản nháp · chọn Áp dụng để thu hồi"
        : "MCP is off in this draft · choose Apply to revoke";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::apply_ai_connections_choices() noexcept {
    if (!set_mcp_access(settings_mcp_enabled_choice_, settings_mcp_edit_choice_)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string("Không thể khởi động MCP: ") + mcp_error_message()
            : std::string("Could not start MCP: ") + mcp_error_message();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return false;
    }
    discard_ai_connections_choices();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (mcp_listener_active() ? "Đã áp dụng quyền MCP" : "Đã thu hồi quyền MCP")
        : (mcp_listener_active() ? "MCP access applied" : "MCP access revoked");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

void NativeWindow::activate_ai_connections_focus() noexcept {
    const auto copy_value = [this](bool access_key) {
#ifdef _WIN32
        if (!mcp_listener_active() || !mcp_runtime_) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bật và áp dụng MCP trước khi chép thông tin kết nối"
                : "Enable and apply MCP before copying connection details";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto value = access_key ? mcp_runtime_->http.access_token()
                                      : mcp_runtime_->http.endpoint();
        const bool copied = !value.empty() && NativeClipboard::write(hwnd_, {}, {}, value);
        status_msg_ = copied
            ? (language_ == UILanguage::Vietnamese
                ? (access_key ? "Đã chép khóa MCP vào clipboard" : "Đã chép URL MCP vào clipboard")
                : (access_key ? "MCP access key copied to clipboard" : "MCP URL copied to clipboard"))
            : (language_ == UILanguage::Vietnamese
                ? "Không thể chép thông tin MCP vào clipboard"
                : "Could not copy MCP connection details to the clipboard");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#else
        (void)access_key;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "MCP clipboard chỉ dùng trên Windows" : "MCP clipboard is available on Windows";
#endif
    };

    switch (settings_ai_focus_index_) {
    case 0: toggle_ai_connections_read_choice(); return;
    case 1: toggle_ai_connections_edit_choice(); return;
    case 2: copy_value(false); return;
    case 3: copy_value(true); return;
    case 4: reset_ai_connections_choices(); return;
    case 5: close_settings_modal(); return;
    case 6: (void)apply_ai_connections_choices(); return;
    default: settings_ai_focus_index_ = 0; return;
    }
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
    if (tool_index < 0 || (tool_index > 9 && tool_index != 11 && tool_index != 12 && tool_index != 13))
        return;
    const bool path_tool = tool_index == 6 || tool_index == 7;
    const bool pen_tool = tool_index == 8;
    const bool node_tool = tool_index == 11;
    const bool contour_tool = tool_index == 12;
    const bool gradient_tool = tool_index == 13;
    if (tool_index == active_tool_ &&
        ((!path_tool || (drawing_tool_ && drawing_tool_->active())) &&
         (!pen_tool || (pen_tool_ && pen_tool_->active())) &&
         (!node_tool || (node_tool_ && node_tool_->active())) &&
         (!contour_tool || (contour_tool_ && contour_tool_->active())) &&
         (!gradient_tool || (gradient_tool_ && gradient_tool_->active()))))
        return;
    if (active_num_field_ != NumericFieldId::None &&
        active_num_field_ != NumericFieldId::PenExpression) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if (active_tool_ == 8 && tool_index != 8 && pen_tool_ && pen_tool_->active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Hãy nhấn Enter/Esc để kết thúc Pen hoặc Shift+Esc để hủy trước khi đổi công cụ"
            : "Press Enter/Esc to finish Pen or Shift+Esc to cancel before switching tools";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    if (drawing_tool_) drawing_tool_->cancel();
    if (pen_tool_) pen_tool_->cancel();
    if (node_tool_) node_tool_->deactivate();
    if (contour_tool_) contour_tool_->deactivate();
    if (gradient_tool_ && tool_index != 13) {
        gradient_tool_->cancel();
        gradient_tool_->deactivate();
    }
    pen_numeric_buffer_.clear();
    if (active_num_field_ == NumericFieldId::PenExpression) {
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
    }
#ifdef _WIN32
    cancel_canvas_interaction();
    if (hwnd_) KillTimer(hwnd_, 9);
#endif
    active_tool_ = tool_index;
    if (tool_index == 1 || tool_index == 2 || tool_index == 9)
        quick_shapes_last_tool_ = tool_index;
    if (tool_index == 6 || tool_index == 7 || tool_index == 8)
        path_drawing_last_tool_ = tool_index;
    toolbox_open_group_ = 0;
    const auto mode = tool_index == 1 ? vector::ToolMode::Rectangle
                    : tool_index == 2 ? vector::ToolMode::Ellipse
                    : tool_index == 4 ? vector::ToolMode::Text
                                      : vector::ToolMode::Select;
    root().vector_controller().set_mode(mode);
    if (path_tool) {
        const auto kind = tool_index == 6 ? vector::tools::line::PathDrawingKind::Line
                                          : vector::tools::line::PathDrawingKind::Polyline;
        if (!drawing_tool_ || !drawing_tool_->activate(kind)) {
            active_tool_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể bắt đầu công cụ vẽ đường" : "Could not start the path tool";
            return;
        }
#ifdef _WIN32
        if (hwnd_) SetTimer(hwnd_, 9, 33, nullptr);
#endif
    }
    if (pen_tool) {
        if (!pen_tool_) {
            try {
                pen_tool_ = std::make_unique<tools::PenToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Pen"
                    : "Not enough memory to start the Pen tool";
                return;
            }
        }
        if (drawing_tool_)
            (void)drawing_tool_->activate(vector::tools::line::PathDrawingKind::Line);
        const auto result = pen_tool_->activate();
        if (result.code != vector::tools::pen::PenDrawingResultCode::Applied) {
            active_tool_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể bắt đầu công cụ Pen" : "Could not start the Pen tool";
            return;
        }
        auto preferences = tools::PenGesturePreferences{};
        preferences.profile = shortcut_profile_ == 0
            ? tools::PenShortcutProfile::Corel
            : shortcut_profile_ == 1 ? tools::PenShortcutProfile::Illustrator
                                     : tools::PenShortcutProfile::PikDraw;
        (void)pen_tool_->set_gesture_preferences(preferences);
    }
    if (node_tool) {
        if (!node_tool_) {
            try {
                node_tool_ = std::make_unique<tools::NodeToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Node"
                    : "Not enough memory to start the Node tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto target = sel.empty() ? document::kInvalidObjectId : sel.front();
        (void)node_tool_->activate(target);
    }
    if (contour_tool) {
        if (!contour_tool_) {
            try {
                contour_tool_ = std::make_unique<tools::ContourToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Contour"
                    : "Not enough memory to start the Contour tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto target = sel.empty() ? document::kInvalidObjectId : sel.front();
        (void)contour_tool_->activate(target);
    }
    if (gradient_tool) {
        if (!gradient_tool_) {
            try {
                gradient_tool_ = std::make_unique<tools::GradientToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Gradient"
                    : "Not enough memory to start the Gradient tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        (void)gradient_tool_->activate(sel);
    }
    if (tool_index == 11) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chỉnh sửa điểm Node · F10 · Kéo chọn điểm · Chỉnh tiếp tuyến Bézier"
            : "Node Edit Tool · F10 · Marquee select nodes · Edit Bézier handles";
    } else if (tool_index == 12) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Contour / Offset · Tạo viền có tham số · Ctrl+K để rã khối viền"
            : "Contour / Offset · Parametric contour offset · Ctrl+K to break apart";
    } else if (tool_index == 13) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Công cụ Gradient · Kéo chuột trên canvas để tạo/chỉnh chuyển sắc · Phím Esc hủy"
            : "Gradient Tool · Drag on canvas to create or edit gradient · Esc to cancel";
    } else {
        const char* vi_tools[] = { "Chọn và di chuyển · V", "Kéo vẽ chữ nhật · Shift vuông · Ctrl từ tâm",
            "Kéo vẽ elip · Shift tròn · Ctrl từ tâm", "Di chuyển khung nhìn · Space/chuột giữa",
            "Văn bản tiếng Việt · A/T", "Dao cắt · K", "Vẽ đoạn thẳng · L",
            "Vẽ đường gấp khúc · Shift+L",
            "Pen: nhấp/ kéo · @L<góc hoặc #X;Y · Enter kết thúc · Shift+Esc hủy",
            "Đa giác đều · chỉnh số cạnh trong thanh thuộc tính · Shift khóa góc 15° · Ctrl từ tâm" };
        const char* en_tools[] = { "Select and move · V", "Drag rectangle · Shift square · Ctrl from center",
            "Drag ellipse · Shift circle · Ctrl from center", "Pan view · Space/middle mouse", "Vietnamese text · A/T",
            "Knife tool · K", "Draw Line · L", "Draw Polyline · Shift+L",
            "Pen: click/drag · @L<angle or #X;Y · Enter finishes · Shift+Esc cancels",
            "Regular polygon · set sides in the property bar · Shift snaps 15° · Ctrl from center" };
        status_msg_ = language_ == UILanguage::Vietnamese ? vi_tools[tool_index] : en_tools[tool_index];
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::trigger_effects_contour() {
    set_active_tool(12);
}

void NativeWindow::trigger_break_contour_apart() {
    if (!contour_tool_) {
        try {
            contour_tool_ = std::make_unique<tools::ContourToolController>(root().document());
        } catch (...) {
            return;
        }
    }
    if (!contour_tool_->active()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (!sel.empty()) {
            contour_tool_->activate(sel.front());
        }
    }
    if (contour_tool_->break_apart()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã rã khối viền Contour thành các đối tượng độc lập (Ctrl+K)"
            : "Broke contour apart into independent objects (Ctrl+K)";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
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

void NativeWindow::open_menu(int menu_id) noexcept {
    if (menu_id < 0 || menu_id >= static_cast<int>(MenuId::Count)) return;
    open_menu_id_ = menu_id;
    hovered_menu_bar_item_ = menu_id;
    hovered_menu_item_ = -1;
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

    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        (void)finish_pen_session(false);
        return;
    }

    const auto pt_world = canvas_to_world(x, y);
    const auto snapshot = root().document().current_snapshot();
    auto hit_id = vector::SelectionModel::hit_test(pt_world, *snapshot);

    if (active_tool_ == 0) {
        if (hit_id) {
            auto obj_opt = snapshot->objects().get(*hit_id);
            if (obj_opt && std::holds_alternative<document::TextObject>(*obj_opt)) {
                start_inline_text_editing(pt_world, *hit_id);
                return;
            }
        }

        const auto bounds = root().vector_controller().selection().bounds(*snapshot);
        const bool hit_selection = (hit_id && root().vector_controller().selection().is_selected(*hit_id)) ||
                                   (bounds && bounds->contains(pt_world));
        if (hit_selection) {
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
    RECT client_rc;
    if (GetClientRect(hwnd_, &client_rc)) {
        width_ = client_rc.right - client_rc.left;
        height_ = client_rc.bottom - client_rc.top;
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
            case geometry::AlignMode::Left: mode_str = "Căn trái (Align Left · Shift+L)"; break;
            case geometry::AlignMode::CenterHorizontal: mode_str = "Căn tâm theo phương ngang (Align Center Horizontally · C)"; break;
            case geometry::AlignMode::Right: mode_str = "Căn phải (Align Right · R)"; break;
            case geometry::AlignMode::Top: mode_str = "Căn trên (Align Top · T)"; break;
            case geometry::AlignMode::CenterVertical: mode_str = "Căn tâm theo phương dọc (Align Center Vertically · E)"; break;
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
    show_style_inspector_ = false;
    show_align_panel_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã mở bảng thuộc tính đối tượng"
        : "Opened object Properties";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_object_properties_panel() noexcept {
    active_dock_tab_ = 0;
    show_style_inspector_ = true;
    show_align_panel_ = false;
    style_inspector_scroll_ = 0;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Appearance · Opacity, Blend, Gaussian Blur, Drop Shadow · Enter áp dụng · Ctrl+Z hoàn tác"
        : "Appearance · Opacity, Blend, Gaussian Blur, Drop Shadow · Enter applies · Ctrl+Z undoes";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_align_distribute_panel() noexcept {
    active_dock_tab_ = 0;
    show_style_inspector_ = false;
    show_align_panel_ = true;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Bảng Căn chỉnh & Phân bổ · chọn mốc rồi chọn lệnh · Ctrl+Z để hoàn tác"
        : "Align & Distribute · choose a reference and operation · Ctrl+Z to undo";
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

    // 1. Main menu groups (y: 5 to 34)
    if (mouse_y >= 6 && mouse_y <= 32) {
        const int menu_index = get_menu_bar_item_at(mouse_x, mouse_y, 38);
        if (menu_index >= 0) {
            const auto& entry = kMenuBarEntries[static_cast<size_t>(menu_index)];
            tip_text = is_vi ? std::wstring(entry.label_vi) + L" · menu"
                             : std::wstring(entry.label_en) + L" menu";
        } else if (mouse_x >= 10 && mouse_x <= 36) {
            tip_text = is_vi ? L"PikDraw Pro · Phần mềm thiết kế đồ họa vector chuyên nghiệp"
                             : L"PikDraw Pro · Professional Vector & Manufacturing Editor";
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

        if (active_tool_ == 8) {
            if (mouse_x >= 52 && mouse_x <= 390)
                tip_text = is_vi
                    ? L"Nhập @L<góc, @ΔX;ΔY hoặc #X;Y; Enter đặt điểm, Tab cũng áp dụng"
                    : L"Enter @L<angle, @dX;dY or #X;Y; Enter places a point; Tab applies";
            else if (mouse_x >= 402 && mouse_x <= 480)
                tip_text = is_vi ? L"Hoàn tất đường đang vẽ · Enter" : L"Finish the current path · Enter";
            else if (mouse_x >= 490 && mouse_x <= 560)
                tip_text = is_vi ? L"Hủy toàn bộ bản nháp Pen · Shift+Esc" : L"Cancel the whole Pen draft · Shift+Esc";
        } else if (active_tool_ == 9) {
            if (mouse_x >= 58 && mouse_x <= 164)
                tip_text = is_vi
                    ? L"Số cạnh đa giác đều · nhập số nguyên 3–256 · Enter áp dụng"
                    : L"Regular polygon sides · enter a whole number from 3 to 256 · Enter applies";
            else if (mouse_x >= 174 && mouse_x <= 780)
                tip_text = is_vi
                    ? L"Shift khóa hướng theo bước 15° · Ctrl lấy điểm đầu làm tâm"
                    : L"Shift snaps direction to 15° · Ctrl uses the start point as center";
        } else if (is_text_mode) {
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
                int opacity_x = (!selection.empty()) ? 754 : 698;
                if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                    mouse_x >= opacity_x && mouse_x <= opacity_x + 124 && opacity_x + 124 <= w - inspect_w) {
                    tip_text = is_vi
                        ? L"Độ mờ toàn đối tượng 0–100% · nhập số · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                        : L"Object opacity 0–100% · type a value · Enter applies · Esc cancels · Ctrl+Z undoes";
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
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                mouse_x >= 720 && mouse_x <= 844 && 844 <= w - inspect_w) {
                tip_text = is_vi
                    ? L"Độ mờ toàn đối tượng 0–100% · nhập số · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                    : L"Object opacity 0–100% · type a value · Enter applies · Esc cancels · Ctrl+Z undoes";
            } else if (mouse_x >= 10 && mouse_x <= 95) {
                tip_text = is_vi ? L"Nhập vị trí X của đối tượng (mm)" : L"Enter the object's X position in millimeters";
            } else if (mouse_x >= 100 && mouse_x <= 185) {
                tip_text = is_vi ? L"Nhập vị trí Y của đối tượng (mm)" : L"Enter the object's Y position in millimeters";
            } else if (mouse_x >= 190 && mouse_x <= 285) {
                tip_text = is_vi ? L"Nhập chiều rộng của đối tượng (mm)" : L"Enter the object's width in millimeters";
            } else if (mouse_x >= 290 && mouse_x <= 360) {
                tip_text = is_vi ? L"Khóa hoặc mở tỷ lệ W:H" : L"Lock or unlock the width-to-height ratio";
            } else if (mouse_x >= 365 && mouse_x <= 460) {
                tip_text = is_vi ? L"Nhập chiều cao của đối tượng (mm)" : L"Enter the object's height in millimeters";
            } else if (is_in_isolation_mode()) {
                if (mouse_x >= 580 && mouse_x <= 670) {
                    tip_text = is_vi ? L"Thoát chế độ cô lập nhóm (Phím Esc)" : L"Exit group isolation mode (Esc)";
                }
            } else {
                if (selection.size() >= 2) {
                    const auto buttons = get_multi_object_context_buttons(workflow(), workflow().can_group_selected(), width_ - inspect_w);
                    for (const auto& button : buttons) {
                        if (mouse_x < button.left || mouse_x >= button.left + button.width) continue;
                        switch (button.action) {
                        case ObjectContextAction::AlignLeft:
                            tip_text = is_vi ? L"Căn cạnh trái của các đối tượng theo khung chọn" : L"Align selected objects by their left edges"; break;
                        case ObjectContextAction::AlignCenterHorizontal:
                            tip_text = is_vi ? L"Căn tâm theo phương ngang" : L"Align object centers horizontally"; break;
                        case ObjectContextAction::AlignRight:
                            tip_text = is_vi ? L"Căn cạnh phải của các đối tượng theo khung chọn" : L"Align selected objects by their right edges"; break;
                        case ObjectContextAction::AlignTop:
                            tip_text = is_vi ? L"Căn cạnh trên của các đối tượng theo khung chọn" : L"Align selected objects by their top edges"; break;
                        case ObjectContextAction::AlignCenterVertical:
                            tip_text = is_vi ? L"Căn tâm theo phương dọc" : L"Align object centers vertically"; break;
                        case ObjectContextAction::AlignBottom:
                            tip_text = is_vi ? L"Căn cạnh dưới của các đối tượng theo khung chọn" : L"Align selected objects by their bottom edges"; break;
                        case ObjectContextAction::CenterPage:
                            tip_text = is_vi ? L"Căn tâm vùng chọn vào tâm trang (P)" : L"Center the selection on the page (P)"; break;
                        case ObjectContextAction::DistributeHorizontal:
                            tip_text = is_vi ? L"Phân bổ khoảng hở ngang đều nhau · cần ít nhất 3 đối tượng" : L"Distribute equal horizontal gaps · requires at least 3 objects"; break;
                        case ObjectContextAction::DistributeVertical:
                            tip_text = is_vi ? L"Phân bổ khoảng hở dọc đều nhau · cần ít nhất 3 đối tượng" : L"Distribute equal vertical gaps · requires at least 3 objects"; break;
                        case ObjectContextAction::Group:
                            tip_text = is_vi ? L"Nhóm các đối tượng đã chọn (Ctrl+G)" : L"Group selected objects (Ctrl+G)"; break;
                        case ObjectContextAction::FlipHorizontal:
                            tip_text = is_vi ? L"Lật ngang đối tượng qua tâm (Ctrl+Shift+H)" : L"Flip selection horizontally (Ctrl+Shift+H)"; break;
                        case ObjectContextAction::FlipVertical:
                            tip_text = is_vi ? L"Lật dọc đối tượng qua tâm (Ctrl+Shift+V)" : L"Flip selection vertically (Ctrl+Shift+V)"; break;
                        case ObjectContextAction::Rotate90CW:
                            tip_text = is_vi ? L"Xoay 90° thuận chiều kim đồng hồ (Ctrl+])" : L"Rotate 90° clockwise (Ctrl+])"; break;
                        case ObjectContextAction::Rotate90CCW:
                            tip_text = is_vi ? L"Xoay 90° ngược chiều kim đồng hồ (Ctrl+[)" : L"Rotate 90° counter-clockwise (Ctrl+[)"; break;
                        case ObjectContextAction::BooleanUnion:
                            tip_text = is_vi ? L"Hàn hai path đã chọn thành một kết quả" : L"Weld the two selected paths into one result"; break;
                        case ObjectContextAction::BooleanDifference:
                            tip_text = is_vi ? L"Trừ path chọn thứ hai khỏi path chọn thứ nhất" : L"Subtract the second selected path from the first"; break;
                        case ObjectContextAction::BooleanIntersection:
                            tip_text = is_vi ? L"Chỉ giữ phần giao của hai path đã chọn" : L"Keep only the intersection of the selected paths"; break;
                        case ObjectContextAction::BooleanExclude:
                            tip_text = is_vi ? L"Loại bỏ phần giao; giữ phần không chồng nhau" : L"Exclude the overlap and keep non-overlapping areas"; break;
                        case ObjectContextAction::BooleanReverseDifference:
                            tip_text = is_vi ? L"Trừ path chọn thứ nhất khỏi path chọn thứ hai" : L"Subtract the first selected path from the second"; break;
                        }
                        break;
                    }
                } else if (selection.size() == 1) {
                    int rx = 580;
                    if (mouse_x >= rx && mouse_x < rx + 28) {
                        tip_text = is_vi ? L"Lật ngang đối tượng qua tâm (Ctrl+Shift+H)" : L"Flip selection horizontally (Ctrl+Shift+H)";
                    } else if (mouse_x >= rx + 32 && mouse_x < rx + 60) {
                        tip_text = is_vi ? L"Lật dọc đối tượng qua tâm (Ctrl+Shift+V)" : L"Flip selection vertically (Ctrl+Shift+V)";
                    } else if (mouse_x >= rx + 64 && mouse_x < rx + 92) {
                        tip_text = is_vi ? L"Xoay 90° thuận chiều kim đồng hồ (Ctrl+])" : L"Rotate 90° clockwise (Ctrl+])";
                    } else if (mouse_x >= rx + 96 && mouse_x < rx + 124) {
                        tip_text = is_vi ? L"Xoay 90° ngược chiều kim đồng hồ (Ctrl+[)" : L"Rotate 90° counter-clockwise (Ctrl+[)";
                    } else {
                        rx += 130;
                        if (workflow().is_group(selection.front())) {
                            if (mouse_x >= rx && mouse_x < rx + 66) {
                                tip_text = is_vi ? L"Rã nhóm đối tượng (Ungroup · Ctrl+Shift+G)" : L"Ungroup selected group (Ctrl+Shift+G)";
                            } else if (mouse_x >= rx + 72 && mouse_x < rx + 138) {
                                tip_text = is_vi ? L"Chui vào trong nhóm để chỉnh sửa hình con (Enter hoặc Nhấp đúp)"
                                                 : L"Enter group isolation mode to edit children (Enter or Double Click)";
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Tool groups and their flyouts.
    else if (toolbox_open_group_ > 0 && mouse_x >= 54 && mouse_x <= 242) {
        const int group_row = toolbox_open_group_ == 1 ? 1
                            : toolbox_open_group_ == 2 ? 2 : 6;
        const int flyout_top = content_top + 10 + group_row * 42;
        const int item = (mouse_y - flyout_top) / 30;
        if (mouse_y >= flyout_top && item >= 0 && item < 3) {
            if (toolbox_open_group_ == 1) {
                if (item == 0)
                    tip_text = is_vi ? L"Hình chữ nhật · Shift vuông · Ctrl từ tâm"
                                     : L"Rectangle · Shift for square · Ctrl from center";
                else if (item == 1)
                    tip_text = is_vi ? L"Hình elip · Shift hình tròn · Ctrl từ tâm"
                                     : L"Ellipse · Shift for circle · Ctrl from center";
                else
                    tip_text = is_vi ? L"Đa giác đều · đặt số cạnh trên thanh thuộc tính hoặc Properties"
                                     : L"Regular polygon · set sides in the property bar or Properties";
            } else if (toolbox_open_group_ == 3) {
                if (item == 0) {
                    tip_text = is_vi
                        ? L"Mở Properties > Style để chỉnh Blend, Opacity và stack hiệu ứng"
                        : L"Open Properties > Style for Blend, Opacity and the effect stack";
                } else if (workflow().can_add_selected_effect()) {
                    tip_text = item == 1
                        ? (is_vi ? L"Thêm Gaussian Blur cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                                 : L"Append Gaussian Blur · unlocked Path/Text only · Ctrl+Z undoes")
                        : (is_vi ? L"Thêm Drop Shadow cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                                 : L"Append Drop Shadow · unlocked Path/Text only · Ctrl+Z undoes");
                } else {
                    tip_text = is_vi
                        ? L"Đang tắt: chọn Path/Text không khóa và còn chỗ trong stack"
                        : L"Disabled: select unlocked Path/Text with available stack capacity";
                }
            } else if (item == 0) {
                tip_text = is_vi ? L"Đoạn thẳng · L" : L"Line · L";
            } else if (item == 1) {
                tip_text = is_vi ? L"Đường gấp khúc · Shift+L" : L"Polyline · Shift+L";
            } else {
                tip_text = is_vi ? L"Pen · nhấp/kéo điểm · Enter kết thúc · Esc kết thúc"
                                 : L"Pen · click/drag points · Enter finishes · Esc finishes";
            }
        }
    }
    else if (mouse_x >= 0 && mouse_x <= tool_w && mouse_y >= content_top && mouse_y < h - status_h) {
        const int tool_idx = (mouse_y - (content_top + 10)) / 42;
        if (tool_idx == 0) {
            tip_text = is_vi ? L"Công cụ chọn & biến đổi đối tượng (Pick Tool) · Phím V hoặc Space"
                             : L"Pick & Select / Transform Tool · Shortcut: V or Space";
        } else if (tool_idx == 1) {
            tip_text = is_vi ? L"Hình cơ bản · Hình chữ nhật, elip, đa giác · Alt+↓ mở nhóm"
                             : L"Quick Shapes · Rectangle, Ellipse, Polygon · Alt+Down opens group";
        } else if (tool_idx == 2) {
            tip_text = is_vi ? L"Đường vẽ · Line, Polyline, Pen · Alt+↓ mở nhóm"
                             : L"Path Drawing · Line, Polyline, Pen · Alt+Down opens group";
        } else if (tool_idx == 3) {
            tip_text = is_vi ? L"Kéo trượt vùng làm việc (Pan Hand Tool) · Phím H hoặc Giữ Chuột giữa"
                             : L"Pan Viewport Hand Tool · Shortcut: H or Middle Drag";
        } else if (tool_idx == 4) {
            tip_text = is_vi ? L"Soạn thảo văn bản & Bảng (Text Tool) · Phím F8"
                             : L"Text Typography & Table Tool · Shortcut: F8";
        } else if (tool_idx == 5) {
            tip_text = is_vi ? L"Dao cắt xén đối tượng vector (Knife Tool) · Phím K"
                             : L"Knife / Slice Tool · Shortcut: K";
        } else if (tool_idx == 6) {
            tip_text = mouse_x >= 37
                ? (is_vi ? L"Appearance · Alt+↓ mở nhóm · ↑/↓ chọn · Enter mở · Esc đóng"
                         : L"Appearance · Alt+Down opens group · Up/Down select · Enter opens · Esc closes")
                : (is_vi ? L"Thuộc tính diện mạo · mở Properties > Style để chỉnh Opacity, Blend và hiệu ứng"
                         : L"Appearance Properties · open Properties > Style for Opacity, Blend and effects");
        }
    }

    // 5. Right Inspector & Docker Panel (x: insp_x to w, y: content_top to h - status_h)
    else if (mouse_x >= insp_x && mouse_x <= w && mouse_y >= content_top && mouse_y < h - status_h) {
        int tab_w = inspect_w / 3;
        if (mouse_y >= content_top && mouse_y <= content_top + 28) {
            int tidx = (mouse_x - insp_x) / tab_w;
            if (tidx == 0) tip_text = is_vi ? L"Bảng thuộc tính đối tượng" : L"Object Properties";
            else if (tidx == 1) tip_text = is_vi ? L"Danh sách các lớp & Đối tượng (Layers)" : L"Layers & Objects Hierarchy";
            else if (tidx == 2) tip_text = is_vi ? L"Lịch sử các thao tác hoàn tác / làm lại (History)" : L"Command History & Undo/Redo Log";
        } else if (active_dock_tab_ == 0 &&
                   mouse_y >= AlignPanelMetrics::section_tabs_top &&
                   mouse_y < AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height) {
            const int panel_left = insp_x + AlignPanelMetrics::content_left;
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            const int index = (mouse_x - panel_left) / (button_width + 6);
            if (index == 0) tip_text = is_vi ? L"Xem thông tin đối tượng đang chọn" : L"Show selected object properties";
            else if (index == 1) tip_text = is_vi ? L"Mở thuộc tính kiểu dáng" : L"Open style properties";
            else if (index == 2) tip_text = is_vi ? L"Mở bảng Align & Distribute (Ctrl+Shift+A)" : L"Open Align & Distribute (Ctrl+Shift+A)";
        } else if (active_dock_tab_ == 0 && active_tool_ == 9) {
            if (mouse_y >= 172 && mouse_y <= 206)
                tip_text = is_vi ? L"Số cạnh của đa giác tạo tiếp theo · số nguyên 3–256"
                                 : L"Side count for the next polygon · whole number 3–256";
            else if (mouse_y >= 216 && mouse_y <= 250)
                tip_text = is_vi ? L"Góc của đỉnh đầu tiên · độ, −36000 đến 36000"
                                 : L"First vertex angle · degrees, −36000 to 36000";
        } else if (active_dock_tab_ == 0 && show_style_inspector_ &&
                   mouse_y >= 162 + 298 - style_inspector_scroll_ &&
                   mouse_y <= 162 + 328 - style_inspector_scroll_ &&
                   mouse_x >= insp_x + 12 && mouse_x <= insp_x + 268) {
            tip_text = is_vi
                ? L"Độ mờ đối tượng Path/Text · 0–100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                : L"Path/Text object opacity · 0–100% · Enter applies · Esc cancels · Ctrl+Z undoes";
        } else if (active_dock_tab_ == 0 && show_style_inspector_) {
            const int panel_x = mouse_x - insp_x;
            const int panel_y = mouse_y - 162 + style_inspector_scroll_;
            if (panel_y >= kAppearanceBlendTop + 26 &&
                panel_y < kAppearanceBlendTop + 80) {
                tip_text = is_vi
                    ? L"Chọn Blend mode của toàn object; đây là compositing, không phải công cụ Blend hình học"
                    : L"Choose whole-object compositing; this is not Illustrator's geometric Blend tool";
            } else if (panel_y >= kAppearanceEffectsTop + 50 &&
                       panel_y < kAppearanceEffectsTop + 78) {
                tip_text = is_vi
                    ? L"Thêm một node cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                    : L"Append an effect node to the stack · unlocked Path/Text only · Ctrl+Z undoes";
            } else if (panel_y >= kAppearanceEffectListTop &&
                       panel_x >= 12 && panel_x < 268) {
                const auto snapshot = root().document().current_snapshot();
                const auto summary = selection_appearance(
                    *snapshot, root().vector_controller().selection().selected_ids());
                if (summary.effect_stack_compatible) {
                    int node_top = kAppearanceEffectListTop;
                    for (size_t index = 0; index < summary.effects.size(); ++index) {
                        const auto &effect = summary.effects[index];
                        const bool shadow = std::holds_alternative<document::DropShadowEffect>(effect.parameters);
                        const int node_height = shadow ? 176 : 118;
                        if (panel_y >= node_top && panel_y < node_top + node_height) {
                            if (panel_y < node_top + 45) {
                                tip_text = panel_x < 96
                                    ? (is_vi ? L"Bật/tắt node; Mixed sẽ bật tất cả đối tượng đã chọn"
                                             : L"Enable or disable this node; Mixed enables every selected object")
                                    : panel_x < 150
                                        ? (is_vi ? L"Đưa node lên trước · đổi thứ tự làm đổi kết quả"
                                                 : L"Move node earlier · order changes the result")
                                        : panel_x < 210
                                            ? (is_vi ? L"Đưa node xuống sau · đổi thứ tự làm đổi kết quả"
                                                     : L"Move node later · order changes the result")
                                            : (is_vi ? L"Xóa node khỏi toàn bộ vùng chọn · Ctrl+Z khôi phục"
                                                     : L"Remove this node from the complete selection · Ctrl+Z restores it");
                            } else if (shadow && panel_y < node_top + 136 && panel_x >= 96) {
                                tip_text = is_vi
                                    ? L"Màu nguồn của bóng · đổi màu có thể chuyển hệ màu, thông số khác giữ nguyên"
                                    : L"Shadow source color · changing model may convert color; other parameters stay intact";
                            } else if (shadow && panel_y >= node_top + 140) {
                                tip_text = is_vi
                                    ? L"Độ mờ riêng của bóng · 0–100% · không thay Object Opacity"
                                    : L"Shadow opacity · 0–100% · separate from Object Opacity";
                            } else if (shadow && panel_y >= node_top + 78) {
                                tip_text = is_vi
                                    ? L"Sigma X/Y theo mm cục bộ · giá trị lớn có thể vượt ngân sách xem trước"
                                    : L"Local-mm Sigma X/Y · large values may exceed preview budgets";
                            } else if (shadow) {
                                tip_text = is_vi
                                    ? L"Độ lệch X/Y theo trục cục bộ mm; +Y hướng lên"
                                    : L"Local-mm X/Y offset; positive Y points up";
                            } else {
                                tip_text = is_vi
                                    ? L"Gaussian sigma X/Y theo mm cục bộ · không lưu pixel hoặc zoom"
                                    : L"Gaussian local-mm Sigma X/Y · pixel radius and zoom are not stored";
                            }
                            break;
                        }
                        node_top += node_height + 10;
                    }
                }
            }
        } else if (active_dock_tab_ == 0 && show_align_panel_) {
            if (mouse_y >= 164 && mouse_y < 185) {
                tip_text = is_vi
                    ? L"Căn theo vùng chọn, đối tượng chọn cuối hoặc trang; Giữa trang căn toàn bộ vùng chọn vào trang"
                    : L"Align to selection bounds, last selected object, or page; Center Page moves the selection as a group";
            } else if (mouse_y >= AlignPanelMetrics::reference_top &&
                       mouse_y < AlignPanelMetrics::reference_top + AlignPanelMetrics::reference_height) {
                tip_text = is_vi ? L"Chọn mốc cho sáu lệnh căn chỉnh" : L"Choose the reference for the six Align commands";
            } else if (mouse_y >= AlignPanelMetrics::align_top && mouse_y < AlignPanelMetrics::align_top + 58) {
                tip_text = is_vi ? L"Căn cạnh hoặc tâm đối tượng theo mốc đang chọn" : L"Align object edges or centers to the selected reference";
            } else if ((mouse_y >= AlignPanelMetrics::horizontal_top && mouse_y < AlignPanelMetrics::horizontal_top + 56) ||
                       (mouse_y >= AlignPanelMetrics::vertical_top && mouse_y < AlignPanelMetrics::vertical_top + 56)) {
                tip_text = is_vi
                    ? L"Phân bổ khoảng hở, tâm hoặc cạnh theo trục; khoảng hở tự động cần ít nhất 3 đối tượng"
                    : L"Distribute gaps, centers, or edges along an axis; automatic equal gaps need at least 3 objects";
            } else if (mouse_y >= AlignPanelMetrics::gap_field_top &&
                       mouse_y < AlignPanelMetrics::gap_field_top + AlignPanelMetrics::gap_field_height) {
                tip_text = is_vi ? L"Nhấp để nhập khoảng hở cố định (mm); Enter xác nhận, Esc hủy" : L"Enter a fixed gap in mm; Enter applies the value, Esc cancels";
            } else if (mouse_y >= AlignPanelMetrics::apply_top &&
                       mouse_y < AlignPanelMetrics::apply_top + AlignPanelMetrics::apply_height) {
                tip_text = is_vi ? L"Áp dụng khoảng hở nhập ở trên theo chiều ngang hoặc dọc" : L"Apply the entered gap horizontally or vertically";
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

int NativeWindow::guideline_at(int x, int y) const {
    if (!show_guidelines_ || guidelines_.empty()) return -1;
    const RECT area = canvas_rect(width_, height_);
    if (x < area.left || x > area.right || y < area.top || y > area.bottom)
        return -1;
    const auto& camera = root().renderer().camera();
    for (int i = static_cast<int>(guidelines_.size()) - 1; i >= 0; --i) {
        const auto& gl = guidelines_[i];
        if (gl.orientation == GuidelineOrientation::Horizontal) {
            const auto screen_pos = camera.world_to_screen({0.0, gl.position_mm});
            const int screen_y = area.top + static_cast<int>(std::round(screen_pos.y));
            if (std::abs(y - screen_y) <= 5) {
                return i;
            }
        } else {
            const auto screen_pos = camera.world_to_screen({gl.position_mm, 0.0});
            const int screen_x = area.left + static_cast<int>(std::round(screen_pos.x));
            if (std::abs(x - screen_x) <= 5) {
                return i;
            }
        }
    }
    return -1;
}

void NativeWindow::start_numeric_input(NumericFieldId field) {
    if (active_num_field_ == field)
        return;
    if (active_num_field_ != NumericFieldId::None && active_num_field_ != field) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if (field == NumericFieldId::ObjectOpacity && !workflow().can_set_selected_object_opacity()) {
        const auto snapshot = root().document().current_snapshot();
        const auto appearance = snapshot
            ? selection_appearance(*snapshot, root().vector_controller().selection().selected_ids())
            : SelectionAppearance{};
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (appearance.selected_count == 0
                ? "Chọn Path hoặc Text để sửa độ mờ"
                : appearance.contains_locked
                    ? "Không thể sửa Opacity: vùng chọn có đối tượng bị khóa"
                    : "Không thể sửa Opacity: chỉ hỗ trợ Path/Text trong toàn bộ vùng chọn")
            : (appearance.selected_count == 0
                ? "Select a Path or Text object to edit opacity"
                : appearance.contains_locked
                    ? "Opacity is unavailable because the selection contains a locked object"
                    : "Opacity requires the entire selection to contain editable Path/Text objects");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    if ((field == NumericFieldId::FillOpacity &&
         !workflow().can_set_selected_fill_opacity()) ||
        (field == NumericFieldId::StrokeOpacity &&
         !workflow().can_set_selected_stroke_opacity())) {
        const auto snapshot = root().document().current_snapshot();
        const auto summary = snapshot
            ? selection_appearance(*snapshot, root().vector_controller().selection().selected_ids())
            : SelectionAppearance{};
        const bool stroke = field == NumericFieldId::StrokeOpacity;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (summary.selected_count == 0
                ? "Chọn đối tượng để sửa độ mờ màu tô hoặc viền"
                : summary.contains_locked
                    ? "Vùng chọn có đối tượng khóa · không cập nhật một phần"
                    : stroke
                        ? "Độ mờ viền chỉ hỗ trợ Path không khóa; Text không có viền được render"
                        : "Độ mờ màu tô chỉ hỗ trợ Path/Text không khóa")
            : (summary.selected_count == 0
                ? "Select an object to edit fill or stroke opacity"
                : summary.contains_locked
                    ? "Selection contains a locked object · partial updates are disabled"
                    : stroke
                        ? "Stroke opacity supports unlocked Paths only; Text stroke is not rendered"
                        : "Fill opacity supports unlocked Path/Text objects only");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    const bool effect_numeric = field == NumericFieldId::EffectSigmaX ||
        field == NumericFieldId::EffectSigmaY || field == NumericFieldId::EffectOffsetX ||
        field == NumericFieldId::EffectOffsetY || field == NumericFieldId::EffectOpacity;
    if (effect_numeric) {
        const auto snapshot = root().document().current_snapshot();
        const auto selection = root().vector_controller().selection().selected_ids();
        const auto appearance = snapshot
            ? selection_appearance(*snapshot, selection) : SelectionAppearance{};
        const bool shadow_field = field == NumericFieldId::EffectOffsetX ||
            field == NumericFieldId::EffectOffsetY || field == NumericFieldId::EffectOpacity;
        const auto *shadow = appearance.can_edit_effect(property_effect_index_)
            ? std::get_if<document::DropShadowEffect>(
                &appearance.effects[property_effect_index_].parameters) : nullptr;
        if (!workflow().can_edit_selected_effect(property_effect_index_) ||
            (shadow_field && !shadow)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Hiệu ứng đã đổi hoặc vùng chọn không đồng nhất · chưa áp dụng"
                : "Effect stack changed or selection is mixed · no value was applied";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
    }
    active_num_field_ = field;
    num_input_buffer_.clear();
    numeric_object_opacity_targets_.clear();
    numeric_paint_opacity_targets_.clear();
    numeric_effect_targets_.clear();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (field == NumericFieldId::PageWidth) {
        ss << active_tab().page_width_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PageHeight) {
        ss << active_tab().page_height_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PolygonSides) {
        num_input_buffer_ = std::to_string(polygon_sides_);
    } else if (field == NumericFieldId::PolygonRotation) {
        ss << polygon_rotation_degrees_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalWidth) {
        ss << new_doc_w_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalHeight) {
        ss << new_doc_h_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::DistributeSpacing) {
        ss << distribute_spacing_mm_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PenExpression) {
        num_input_buffer_ = pen_numeric_buffer_;
    } else if (field == NumericFieldId::PenLength) {
        num_input_buffer_ = pen_numeric_length_;
    } else if (field == NumericFieldId::PenAngle) {
        num_input_buffer_ = pen_numeric_angle_;
    } else if (field == NumericFieldId::ObjectOpacity) {
        numeric_object_opacity_targets_ =
            root().vector_controller().selection().selected_ids();
    } else if (field == NumericFieldId::FillOpacity ||
               field == NumericFieldId::StrokeOpacity) {
        numeric_paint_opacity_targets_ =
            root().vector_controller().selection().selected_ids();
    } else if (effect_numeric) {
        numeric_effect_index_ = property_effect_index_;
        numeric_effect_targets_ = root().vector_controller().selection().selected_ids();
        const auto snapshot = root().document().current_snapshot();
        const auto summary = selection_appearance(
            *snapshot, root().vector_controller().selection().selected_ids());
        const auto &effect = summary.effects[numeric_effect_index_];
        const bool is_blur = std::holds_alternative<document::GaussianBlurEffect>(effect.parameters);
        bool mixed = false;
        double value = 0.0;
        if (field == NumericFieldId::EffectSigmaX) {
            mixed = effect.mixed_sigma_x;
            value = is_blur ? std::get<document::GaussianBlurEffect>(effect.parameters).sigma_x_mm
                            : std::get<document::DropShadowEffect>(effect.parameters).sigma_x_mm;
        } else if (field == NumericFieldId::EffectSigmaY) {
            mixed = effect.mixed_sigma_y;
            value = is_blur ? std::get<document::GaussianBlurEffect>(effect.parameters).sigma_y_mm
                            : std::get<document::DropShadowEffect>(effect.parameters).sigma_y_mm;
        } else if (field == NumericFieldId::EffectOffsetX) {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_offset_x;
            value = shadow.dx_mm;
        } else if (field == NumericFieldId::EffectOffsetY) {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_offset_y;
            value = shadow.dy_mm;
        } else {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_opacity;
            value = shadow.opacity * 100.0;
        }
        if (!mixed) {
            std::ostringstream effect_value;
            effect_value << std::setprecision(10) << value;
            num_input_buffer_ = effect_value.str();
        }
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

    if (field == NumericFieldId::ObjectOpacity) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập Opacity 0–100% (dấu phẩy hoặc chấm) · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter Opacity from 0–100% · comma or dot decimals · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::PenLength) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập chiều dài đoạn thẳng (mm) · Enter tạo đoạn · Tab nhập góc (°)"
            : "Enter line length (mm) · Enter creates line · Tab sets angle (°)";
    } else if (field == NumericFieldId::PenAngle) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập góc theo trục X (°) · Enter tạo đoạn · Tab nhập chiều dài"
            : "Enter angle from X axis (°) · Enter creates line · Tab sets length";
    } else if (field == NumericFieldId::FillOpacity ||
               field == NumericFieldId::StrokeOpacity) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Độ mờ 0–100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Opacity 0–100% · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (effect_numeric) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập mm (σ ≥ 0, lệch ±1.000.000) hoặc Opacity 0–100% · Enter áp dụng · Tab ô kế · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter mm values (σ ≥ 0, offset ±1,000,000) or Opacity 0–100% · Enter applies · Tab next field · Esc cancels · Ctrl+Z undoes";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang nhập số: gõ phím số, Enter để áp dụng, Tab chuyển ô, Esc để hủy"
            : "Numeric input: type value, Enter to apply, Tab to next field, Esc to cancel";
    }

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::start_effect_numeric_input(NumericFieldId field, size_t effect_index) {
    if (active_num_field_ == field && numeric_effect_index_ == effect_index)
        return;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    property_effect_index_ = effect_index;
    start_numeric_input(field);
}

void NativeWindow::commit_numeric_field() {
    if (active_num_field_ == NumericFieldId::None) return;

    if (active_num_field_ == NumericFieldId::PenLength || active_num_field_ == NumericFieldId::PenAngle) {
        if (active_num_field_ == NumericFieldId::PenLength) {
            pen_numeric_length_ = num_input_buffer_;
        } else {
            pen_numeric_angle_ = num_input_buffer_;
        }
        update_pen_dimension_candidate();

        if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
            const auto placed = pen_tool_->confirm_candidate();
            if (placed.code != vector::tools::pen::PenDrawingResultCode::Applied &&
                placed.code != vector::tools::pen::PenDrawingResultCode::PreviewUpdated) {
                status_msg_ = placed.message;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            active_num_field_ = NumericFieldId::None;
            pen_numeric_length_.clear();
            pen_numeric_angle_.clear();
            num_input_buffer_.clear();
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã tạo đoạn thẳng · Phím L nhập tiếp · Enter để kết thúc"
                : "Segment created · Press L for next · Enter to finish";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active()) {
            const auto result = drawing_tool_->confirm_candidate();
            if (result.committed() && result.object_id) {
                const auto snapshot = root().document().current_snapshot();
                if (snapshot)
                    root().vector_controller().set_selection({*result.object_id}, *snapshot);
            }
            active_num_field_ = NumericFieldId::None;
            pen_numeric_length_.clear();
            pen_numeric_angle_.clear();
            num_input_buffer_.clear();
            status_msg_ = drawing_tool_->visual_state().message;
            if (!drawing_tool_->active()) finish_drawing_tool(true);
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    if (active_num_field_ == NumericFieldId::PenExpression) {
        if (!pen_tool_ || !pen_tool_->active() || num_input_buffer_.empty()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập @L<góc, @ΔX;ΔY hoặc #X;Y trước khi đặt điểm"
                : "Enter @L<angle, @dX;dY or #X;Y before placing a point";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto preview = pen_tool_->update_numeric_candidate(num_input_buffer_);
        if (preview.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
            preview.code == vector::tools::pen::PenDrawingResultCode::Stale ||
            preview.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
            status_msg_ = preview.message;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto placed = pen_tool_->confirm_candidate();
        if (placed.code != vector::tools::pen::PenDrawingResultCode::Applied &&
            placed.code != vector::tools::pen::PenDrawingResultCode::PreviewUpdated) {
            status_msg_ = placed.message;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        pen_numeric_buffer_.clear();
        num_input_buffer_.clear();
        (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đặt điểm theo kích thước · Enter để kết thúc"
            : "Point placed from dimensions · Enter finishes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PolygonSides) {
        try {
            size_t parsed = 0;
            const int value = std::stoi(num_input_buffer_, &parsed);
            if (parsed != num_input_buffer_.size() ||
                value < vector::ShapeFactory::kMinimumPolygonSides ||
                value > vector::ShapeFactory::kMaximumPolygonSides) {
                throw std::out_of_range("polygon sides");
            }
            polygon_sides_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Số cạnh phải là số nguyên từ 3 đến 256"
                : "Sides must be a whole number from 3 to 256";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật số cạnh đa giác · chỉ áp dụng cho hình tạo tiếp theo"
            : "Polygon sides updated · applies to the next shape";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PolygonRotation) {
        try {
            size_t parsed = 0;
            const double value = std::stod(num_input_buffer_, &parsed);
            if (parsed != num_input_buffer_.size() || !std::isfinite(value) ||
                value < -36000.0 || value > 36000.0) {
                throw std::out_of_range("polygon rotation");
            }
            polygon_rotation_degrees_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Góc xoay phải nằm trong khoảng −36000° đến 36000°"
                : "Rotation must be between −36000° and 36000°";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật góc xoay đa giác · chỉ áp dụng cho hình tạo tiếp theo"
            : "Polygon rotation updated · applies to the next shape";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::DistributeSpacing) {
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            const double value = std::stod(normalized);
            if (!std::isfinite(value) || value < 0.0 || value > geometry::kMaxCanvasDimensionMm) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Khoảng hở phải từ 0 đến giới hạn kích thước bản vẽ (mm)"
                    : "Gap must be between 0 and the maximum canvas dimension (mm)";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            distribute_spacing_mm_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập khoảng hở hợp lệ theo mm"
                : "Enter a valid gap in mm";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật khoảng hở cố định (mm)"
            : "Updated fixed gap (mm)";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

    if (active_num_field_ == NumericFieldId::EffectSigmaX ||
        active_num_field_ == NumericFieldId::EffectSigmaY ||
        active_num_field_ == NumericFieldId::EffectOffsetX ||
        active_num_field_ == NumericFieldId::EffectOffsetY ||
        active_num_field_ == NumericFieldId::EffectOpacity) {
        const auto field = active_num_field_;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value))
                throw std::out_of_range("appearance parameter");

            const bool opacity = field == NumericFieldId::EffectOpacity;
            const bool offset = field == NumericFieldId::EffectOffsetX ||
                                field == NumericFieldId::EffectOffsetY;
            const bool in_range = opacity ? value >= 0.0 && value <= 100.0
                : offset ? std::abs(value) <= geometry::limits::kCanvasMaxMm
                         : value >= 0.0 && value <= geometry::limits::kCanvasMaxMm;
            if (!in_range)
                throw std::out_of_range("appearance parameter range");

            const auto &current_targets = root().vector_controller().selection().selected_ids();
            bool applied = current_targets == numeric_effect_targets_ &&
                workflow().can_edit_selected_effect(numeric_effect_index_);
            if (applied) {
                if (field == NumericFieldId::EffectSigmaX)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::EffectSigmaY)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, false, value);
                else if (field == NumericFieldId::EffectOffsetX)
                    applied = workflow().set_selected_effect_offset(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::EffectOffsetY)
                    applied = workflow().set_selected_effect_offset(numeric_effect_index_, false, value);
                else
                    applied = workflow().set_selected_effect_opacity(numeric_effect_index_, value / 100.0);
            }
            if (!applied) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_effect_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn hoặc thứ tự hiệu ứng đã đổi · thông số chưa áp dụng"
                    : "Selection or effect order changed · parameter was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giá trị không hợp lệ · Sigma: 0–1.000.000 mm; lệch: ±1.000.000 mm; Opacity: 0–100%"
                : "Invalid value · Sigma: 0–1,000,000 mm; offset: ±1,000,000 mm; opacity: 0–100%";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_effect_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật thông số Appearance · Ctrl+Z để hoàn tác"
            : "Updated Appearance parameter · Ctrl+Z to undo";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::ObjectOpacity) {
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double percent = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(percent) ||
                percent < 0.0 || percent > 100.0) {
                throw std::out_of_range("object opacity percent");
            }
            const auto& current_targets = root().vector_controller().selection().selected_ids();
            if (current_targets != numeric_object_opacity_targets_ ||
                !workflow().set_selected_object_opacity(percent / 100.0)) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_object_opacity_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn đã đổi hoặc không còn sửa được · Opacity chưa áp dụng"
                    : "Selection changed or is no longer editable · opacity was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Opacity phải là số hữu hạn từ 0 đến 100% · giá trị chưa được áp dụng"
                : "Opacity must be a finite number from 0 to 100% · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_object_opacity_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Opacity cho toàn bộ vùng chọn · Ctrl+Z để hoàn tác"
            : "Updated opacity for the entire selection · Ctrl+Z to undo";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::FillOpacity ||
        active_num_field_ == NumericFieldId::StrokeOpacity) {
        const auto field = active_num_field_;
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double percent = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(percent) ||
                percent < 0.0 || percent > 100.0)
                throw std::out_of_range("paint opacity percent");
            const auto &current_targets = root().vector_controller().selection().selected_ids();
            const bool applied = current_targets == numeric_paint_opacity_targets_ &&
                (field == NumericFieldId::FillOpacity
                    ? workflow().set_selected_fill_opacity(percent / 100.0)
                    : workflow().set_selected_stroke_opacity(percent / 100.0));
            if (!applied) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_paint_opacity_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn đã đổi hoặc không còn hỗ trợ · độ mờ chưa áp dụng"
                    : "Selection changed or no longer supports this paint · opacity was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Độ mờ phải là số hữu hạn từ 0 đến 100% · giá trị chưa được áp dụng"
                : "Opacity must be a finite number from 0 to 100% · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_paint_opacity_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (field == NumericFieldId::FillOpacity
                ? "Đã cập nhật độ mờ màu tô cho toàn bộ vùng chọn · Ctrl+Z hoàn tác"
                : "Đã cập nhật độ mờ viền cho toàn bộ vùng chọn · Ctrl+Z hoàn tác")
            : (field == NumericFieldId::FillOpacity
                ? "Updated fill opacity for the complete selection · Ctrl+Z undoes"
                : "Updated stroke opacity for the complete selection · Ctrl+Z undoes");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

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
    const bool cancel_pen_expression = active_num_field_ == NumericFieldId::PenExpression;
    const bool cancel_pen_dimension = (active_num_field_ == NumericFieldId::PenLength ||
                                       active_num_field_ == NumericFieldId::PenAngle);
    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();
    numeric_object_opacity_targets_.clear();
    numeric_paint_opacity_targets_.clear();
    numeric_effect_targets_.clear();
    if (cancel_pen_expression) {
        pen_numeric_buffer_.clear();
        if (pen_tool_ && pen_tool_->active())
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
    }
    if (cancel_pen_dimension) {
        pen_numeric_length_.clear();
        pen_numeric_angle_.clear();
        if (pen_tool_ && pen_tool_->active())
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())
            drawing_tool_->pointer_move(pen_last_pointer_world_, root().renderer().camera(),
                window_dpi_scale(), false, false);
    }
    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy nhập số" : "Cancelled numeric input";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::handle_numeric_char_input(wchar_t ch) {
    if (active_num_field_ == NumericFieldId::None) return;

    if (active_num_field_ == NumericFieldId::PenLength || active_num_field_ == NumericFieldId::PenAngle) {
        if (ch == 8) {
            if (!num_input_buffer_.empty()) num_input_buffer_.pop_back();
        } else if (ch == 13) {
            commit_numeric_field();
            return;
        } else if (ch == 27) {
            cancel_numeric_field();
            return;
        } else if (ch == 9) { // Tab: toggle between Length and Angle
            if (active_num_field_ == NumericFieldId::PenLength) {
                pen_numeric_length_ = num_input_buffer_;
                active_num_field_ = NumericFieldId::PenAngle;
                num_input_buffer_ = pen_numeric_angle_;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập góc theo trục X (°) · Enter tạo đoạn · Tab nhập chiều dài"
                    : "Enter angle from X axis (°) · Enter creates line · Tab sets length";
            } else {
                pen_numeric_angle_ = num_input_buffer_;
                active_num_field_ = NumericFieldId::PenLength;
                num_input_buffer_ = pen_numeric_length_;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập chiều dài đoạn thẳng (mm) · Enter tạo đoạn · Tab nhập góc (°)"
                    : "Enter line length (mm) · Enter creates line · Tab sets angle (°)";
            }
            update_pen_dimension_candidate();
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L',' ||
                   (active_num_field_ == NumericFieldId::PenAngle && ch == L'-')) {
            if ((ch == L'.' || ch == L',') &&
                (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
                return;
            }
            if (ch == L'-' && !num_input_buffer_.empty()) {
                return;
            }
            if (num_input_buffer_.size() < 12) {
                num_input_buffer_.push_back(static_cast<char>(ch));
            }
        }
        if (active_num_field_ == NumericFieldId::PenLength) {
            pen_numeric_length_ = num_input_buffer_;
        } else {
            pen_numeric_angle_ = num_input_buffer_;
        }
        update_pen_dimension_candidate();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PenExpression) {
        if (ch == 8) {
            if (!num_input_buffer_.empty()) num_input_buffer_.pop_back();
        } else if (ch == 13 || ch == 9) {
            commit_numeric_field();
            return;
        } else if (ch == 27) {
            cancel_numeric_field();
            return;
        } else if (ch >= 0x20 && ch <= 0x7e && num_input_buffer_.size() < 256) {
            num_input_buffer_.push_back(static_cast<char>(ch));
        }
        pen_numeric_buffer_ = num_input_buffer_;
        if (pen_tool_ && pen_tool_->active() && !pen_numeric_buffer_.empty()) {
            const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
            if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput)
                status_msg_ = result.message;
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Xem trước số · Enter đặt điểm · Esc khôi phục"
                    : "Numeric preview · Enter places point · Esc restores";
        }
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

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
            case NumericFieldId::PolygonSides: next = NumericFieldId::PolygonRotation; break;
            case NumericFieldId::PolygonRotation: next = NumericFieldId::PolygonSides; break;
            case NumericFieldId::ModalWidth: next = NumericFieldId::ModalHeight; break;
            case NumericFieldId::ModalHeight: next = NumericFieldId::ModalWidth; break;
            case NumericFieldId::ObjectX: next = NumericFieldId::ObjectY; break;
            case NumericFieldId::ObjectY: next = NumericFieldId::ObjectWidth; break;
            case NumericFieldId::ObjectWidth: next = NumericFieldId::ObjectHeight; break;
            case NumericFieldId::ObjectHeight: next = NumericFieldId::ObjectX; break;
            case NumericFieldId::EffectOffsetX: next = NumericFieldId::EffectOffsetY; break;
            case NumericFieldId::EffectOffsetY: next = NumericFieldId::EffectSigmaX; break;
            case NumericFieldId::EffectSigmaX: next = NumericFieldId::EffectSigmaY; break;
            case NumericFieldId::EffectSigmaY: {
                const auto snapshot = root().document().current_snapshot();
                const auto appearance = selection_appearance(
                    *snapshot, root().vector_controller().selection().selected_ids());
                const bool shadow = appearance.can_edit_effect(numeric_effect_index_) &&
                    std::holds_alternative<document::DropShadowEffect>(
                        appearance.effects[numeric_effect_index_].parameters);
                next = shadow ? NumericFieldId::EffectOpacity : NumericFieldId::EffectSigmaX;
                break;
            }
            case NumericFieldId::EffectOpacity: next = NumericFieldId::EffectOffsetX; break;
            default: break;
        }
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
        if (next != NumericFieldId::None) {
            start_numeric_input(next);
        }
        return;
    } else if ((ch >= L'0' && ch <= L'9') ||
               (active_num_field_ != NumericFieldId::PolygonSides && ch == L'.') ||
               (ch == L',' && active_num_field_ == NumericFieldId::DistributeSpacing) ||
               (ch == L',' && active_num_field_ == NumericFieldId::ObjectOpacity) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::FillOpacity ||
                               active_num_field_ == NumericFieldId::StrokeOpacity)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::EffectSigmaX ||
                               active_num_field_ == NumericFieldId::EffectSigmaY ||
                               active_num_field_ == NumericFieldId::EffectOffsetX ||
                               active_num_field_ == NumericFieldId::EffectOffsetY ||
                               active_num_field_ == NumericFieldId::EffectOpacity)) ||
               (active_num_field_ != NumericFieldId::PolygonSides && ch == L'-')) {
        if ((ch == L'.' || ch == L',') &&
            (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
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

void NativeWindow::handle_array_char(char c) {
    if (!workflow().is_array_session_active()) return;
    if (array_input_buffer_.size() < 16) {
        array_input_buffer_ += c;
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Mảng sao chép: " + array_input_buffer_ + " (Nhấn Enter áp dụng, Esc kết thúc)"
            : "Array: " + array_input_buffer_ + " (Press Enter to apply, Esc to finish)";
    }
}

void NativeWindow::handle_array_backspace() {
    if (!array_input_buffer_.empty()) {
        array_input_buffer_.pop_back();
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Mảng sao chép: " + array_input_buffer_ + " (Nhấn Enter áp dụng, Esc kết thúc)"
            : "Array: " + array_input_buffer_ + " (Press Enter to apply, Esc to finish)";
    }
}

void NativeWindow::commit_array_input() {
    if (!workflow().is_array_session_active() || array_input_buffer_.empty()) return;
    std::string buf = array_input_buffer_;
    array_input_buffer_.clear();

    bool is_div = false;
    int count = 0;
    if (buf[0] == '/') {
        is_div = true;
        try { count = std::stoi(buf.substr(1)); } catch (...) { return; }
    } else if (buf[0] == '*' || buf[0] == 'x' || buf[0] == 'X') {
        is_div = false;
        try { count = std::stoi(buf.substr(1)); } catch (...) { return; }
    } else {
        try { count = std::stoi(buf); } catch (...) { return; }
    }

    if (count <= 0) return;

    if (is_div) {
        if (workflow().apply_array_divider(count)) {
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đã chia đều " + std::to_string(count) + " khoảng (/" + std::to_string(count) + ") · Gõ tiếp để đổi hoặc Esc để chốt"
                : "Divided by /" + std::to_string(count) + " · Type to change or Esc to finish";
        }
    } else {
        if (workflow().apply_array_multiplier(count)) {
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đã nhân " + std::to_string(count) + " bản sao (*" + std::to_string(count) + ") · Gõ tiếp để đổi hoặc Esc để chốt"
                : "Multiplied by *" + std::to_string(count) + " · Type to change or Esc to finish";
        }
    }
}

void NativeWindow::end_quick_dimension() noexcept {
    quick_dimension_active_ = false;
    quick_dimension_object_id_ = document::kInvalidObjectId;
    quick_dimension_buffer_.clear();
}

bool NativeWindow::apply_quick_dimension() {
    if (!quick_dimension_active_ || quick_dimension_object_id_ == document::kInvalidObjectId) {
        return false;
    }
    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(quick_dimension_object_id_);
    if (!obj) {
        end_quick_dimension();
        return false;
    }
    const auto bounds_opt = workflow().get_object_bounds(quick_dimension_object_id_);
    if (!bounds_opt || bounds_opt->width <= 0.0 || bounds_opt->height <= 0.0) {
        end_quick_dimension();
        return false;
    }

    double new_w = bounds_opt->width;
    double new_h = bounds_opt->height;

    // Phân tích chuỗi quick_dimension_buffer_
    // Định dạng: "30,40" hoặc "30, 40" hoặc "30;40" hoặc "30"
    std::string s = quick_dimension_buffer_;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    if (s.empty()) {
        end_quick_dimension();
        return false;
    }

    size_t sep = s.find_first_of(",; ");
    if (sep != std::string::npos) {
        std::string part_w = s.substr(0, sep);
        std::string part_h = s.substr(sep + 1);
        part_w.erase(0, part_w.find_first_not_of(" \t\r\n"));
        part_w.erase(part_w.find_last_not_of(" \t\r\n") + 1);
        part_h.erase(0, part_h.find_first_not_of(" \t\r\n"));
        part_h.erase(part_h.find_last_not_of(" \t\r\n") + 1);
        try {
            if (!part_w.empty()) new_w = std::stod(part_w);
            if (!part_h.empty()) new_h = std::stod(part_h);
        } catch (...) {
            end_quick_dimension();
            return false;
        }
    } else {
        try {
            new_w = std::stod(s);
            if (active_tool_ == 2 || active_tool_ == 9) {
                new_h = new_w;
            }
        } catch (...) {
            end_quick_dimension();
            return false;
        }
    }

    if (new_w <= 0.0 || new_h <= 0.0) {
        end_quick_dimension();
        return false;
    }

    new_w = std::clamp(new_w, 0.1, geometry::kMaxObjectDimensionMm);
    new_h = std::clamp(new_h, 0.1, geometry::kMaxObjectDimensionMm);

    const double cur_w = bounds_opt->width;
    const double cur_h = bounds_opt->height;
    const double sx = new_w / cur_w;
    const double sy = new_h / cur_h;

    // Scale giữ cố định điểm gốc ban đầu quick_dimension_origin_
    const auto delta = geometry::Transform2D::translation(quick_dimension_origin_.x, quick_dimension_origin_.y) *
                       geometry::Transform2D::scale(sx, sy) *
                       geometry::Transform2D::translation(-quick_dimension_origin_.x, -quick_dimension_origin_.y);

    auto cmd = std::make_unique<document::TransformObjectCommand>(quick_dimension_object_id_, delta);
    const auto res = root().document().execute_command(std::move(cmd));
    if (res.success) {
        root().vector_controller().set_selection({quick_dimension_object_id_}, *root().document().current_snapshot());
        workflow().sync_status_bar();
        std::ostringstream ss_w, ss_h;
        ss_w << std::fixed << std::setprecision(1) << new_w;
        ss_h << std::fixed << std::setprecision(1) << new_h;
        std::string w_str = ss_w.str();
        std::string h_str = ss_h.str();
        if (w_str.size() > 2 && w_str.substr(w_str.size() - 2) == ".0") w_str.erase(w_str.size() - 2);
        if (h_str.size() > 2 && h_str.substr(h_str.size() - 2) == ".0") h_str.erase(h_str.size() - 2);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã áp dụng kích thước: " + w_str + " × " + h_str + " mm"
            : "Applied dimensions: " + w_str + " × " + h_str + " mm";
    }
    end_quick_dimension();
    return res.success;
}

#else
bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
void NativeWindow::capture_screenshot_bmp(const std::string&) {}
#endif

} // namespace pikdraw::desktop
