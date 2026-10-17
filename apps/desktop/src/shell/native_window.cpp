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
        "Chỉnh sửa điểm Node", "Node Edit Tool", "", [this] { set_active_tool(11); }, nullptr});
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
#else
bool NativeWindow::initialize(const std::string&) { return true; }
int NativeWindow::run() { return 0; }
#endif

} // namespace pikdraw::desktop
