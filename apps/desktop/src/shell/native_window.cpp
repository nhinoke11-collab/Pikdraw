#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_canvas_frame.hpp"
#include "native_window_document_color.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
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
    tab.unit = "mm";
    tab.root = std::make_unique<CompositionRoot>();
    tab.root->initialize(static_cast<double>(width_), static_cast<double>(height_), 1.0);
    tab.root->renderer().backend().set_canvas_background(canvas_background_color());
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(297.0, 210.0, color::DocumentColorMode::Cmyk, 300);
    register_drawing_actions(*tab.root);
    tabs_.push_back(std::move(tab));
    active_tab_index_ = 0;
    document_path_ = active_tab().file_path;
    bind_drawing_tool_to_active_document();
#ifdef _WIN32
    load_color_preferences();
#endif
}

void NativeWindow::register_drawing_actions(CompositionRoot& document_root) {
    auto& dispatcher = document_root.window().action_dispatcher();
    document_color_ui::register_actions(dispatcher, [this](color::DocumentColorMode mode) {
#ifdef _WIN32
        const auto snapshot = root().document().current_snapshot();
        if (snapshot->page().color_mode_explicit && snapshot->color_mode() == mode) return;
        if (!document_color_ui::confirm_conversion(hwnd_, language_ == UILanguage::Vietnamese)) return;
        if (is_inline_text_editing_) commit_inline_text_editing();
        cancel_canvas_interaction();
#endif
        const bool success = workflow().convert_document_color_mode(mode, color_preview_);
        if (success) {
            default_style_.fill = color::convert_color_to_mode(default_style_.fill, mode, color_preview_.get());
            default_style_.stroke = color::convert_color_to_mode(default_style_.stroke, mode, color_preview_.get());
        }
        const std::string mode_name = mode == color::DocumentColorMode::Cmyk ? "CMYK" : "RGB";
        const bool icc_active = color_preview_ && color_preview_->has_cmyk_transform();
        const std::string conversion_method = icc_active ? "ICC"
            : (language_ == UILanguage::Vietnamese ? "gần đúng" : "approximate");
        status_msg_ = success ? (language_ == UILanguage::Vietnamese
            ? "Hệ màu tài liệu: " + mode_name + " · " + conversion_method + " · Ctrl+Z hoàn tác"
            : "Document color mode: " + mode_name + " · " + conversion_method + " · Ctrl+Z to undo")
            : (language_ == UILanguage::Vietnamese ? "Không thể chuyển hệ màu" : "Color conversion failed");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }, [this](int32_t dpi) {
#ifdef _WIN32
        if (is_inline_text_editing_) commit_inline_text_editing();
        cancel_canvas_interaction();
#endif
        const bool success = workflow().set_document_raster_dpi(dpi);
        status_msg_ = success ? (language_ == UILanguage::Vietnamese ? "DPI hiệu ứng raster: " : "Raster effects DPI: ")
            + std::to_string(dpi) + " · Ctrl+Z"
            : (language_ == UILanguage::Vietnamese ? "Không thể đổi DPI" : "DPI change failed");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });
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
        "Hiệu ứng viền Contour", "Contour Effect", "Ctrl+F9", [this] { trigger_effects_contour(); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kObjectContourBreakApart),
        "Rã khối viền Contour", "Break Contour Apart", "Ctrl+K", [this] { trigger_break_contour_apart(); }, nullptr});
    dispatcher.register_action({std::string(actions::ActionId::kToolSnapPanel),
        "Bắt điểm và hướng", "Snap and Direction", "F3", [this] {
            if (drawing_tool_) drawing_tool_->toggle_snap_panel();
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        }, nullptr});
    dispatcher.register_action({"action.toolbox.customize",
        "Tùy biến Hộp công cụ...", "Customize Toolbox...", "", [this] {
            toggle_toolbox_customize_flyout();
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
        if (is_inline_text_editing_) commit_inline_text_editing();
        if (drawing_tool_) drawing_tool_->cancel();
        active_tab_index_ = index;
        ++view_generation_;
        document_path_ = active_tab().file_path;
        bind_drawing_tool_to_active_document();
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

void NativeWindow::close_tab(size_t index) noexcept {
    if (index >= tabs_.size()) return;
    if (is_inline_text_editing_) commit_inline_text_editing();
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
        if (closing_active) ++view_generation_;
        document_path_ = active_tab().file_path;
        if (closing_active) bind_drawing_tool_to_active_document();
        fit_page_to_canvas();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    } else {
        if (drawing_tool_) drawing_tool_->cancel();
        workflow().new_document(297.0, 210.0, color::DocumentColorMode::Cmyk, 300);
        bind_drawing_tool_to_active_document();
        active_tab().title = (language_ == UILanguage::Vietnamese) ? "Bản vẽ 1" : "Document 1";
        active_tab().file_path = "";
        document_path_ = active_tab().file_path;
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
    if (is_inline_text_editing_) commit_inline_text_editing();
    DocumentTab tab;
    tab.id = "doc-" + std::to_string(next_doc_counter_++);
    tab.title = title.empty() ? ("Bản vẽ " + std::to_string(tabs_.size() + 1)) : title;
    tab.page_width_mm = w_mm;
    tab.page_height_mm = h_mm;
    tab.unit = "mm";
    tab.root = std::make_unique<CompositionRoot>();
    tab.root->initialize(static_cast<double>(width_), static_cast<double>(height_), 1.0);
    tab.root->renderer().backend().set_canvas_background(canvas_background_color());
    tab.workflow = std::make_unique<DocumentWorkflow>(*tab.root);
    tab.workflow->new_document(w_mm, h_mm, color_mode == "CMYK"
        ? color::DocumentColorMode::Cmyk : color::DocumentColorMode::Rgb, dpi);
    register_drawing_actions(*tab.root);
    if (drawing_tool_) drawing_tool_->cancel();
    tabs_.push_back(std::move(tab));
    active_tab_index_ = tabs_.size() - 1;
    ++view_generation_;
    document_path_ = active_tab().file_path;
    bind_drawing_tool_to_active_document();
    fit_page_to_canvas();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_new_doc_modal() noexcept {
    if (is_inline_text_editing_) commit_inline_text_editing();
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
#ifdef _WIN32
    cancel_export_job();
#endif
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

bool NativeWindow::is_selected_rectangle(document::ObjectId* out_id, vector::RectCornerSettings* out_settings) const {
    const auto& sel = root().vector_controller().selection().selected_ids();
    if (sel.size() != 1) return false;
    const auto id = sel.front();
    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(id);
    if (!obj || !std::holds_alternative<document::PathObject>(*obj)) return false;
    const auto& po = std::get<document::PathObject>(*obj);

    vector::RectCornerSettings s;
    s.type = rect_corner_type_;
    s.tl = rect_corner_tl_;
    s.tr = rect_corner_tr_;
    s.br = rect_corner_br_;
    s.bl = rect_corner_bl_;
    s.locked = rect_corners_locked_;
    s.scale_relative = rect_corners_scale_relative_;

    bool is_rect = false;
    if (po.contour_spec.starts_with("rect_corner:")) {
        auto deser = vector::deserialize_rect_corner_spec(po.contour_spec);
        if (deser) {
            s = *deser;
            is_rect = true;
        }
    } else if (po.name.starts_with("Rectangle") || po.name.starts_with("Square") ||
               po.name.find("Rect") != std::string::npos) {
        is_rect = true;
    } else if (po.authoring_source && po.authoring_source->subpaths.size() == 1 &&
               po.authoring_source->subpaths[0].closed) {
        const auto& sub = po.authoring_source->subpaths[0];
        const bool all_linear = std::all_of(sub.segments.begin(), sub.segments.end(),
            [](const auto& seg) { return seg.kind == document::PathSegmentKind::Line; });
        if (sub.nodes.size() == 4 && all_linear) {
            const auto& n0 = sub.nodes[0].anchor;
            const auto& n1 = sub.nodes[1].anchor;
            const auto& n2 = sub.nodes[2].anchor;
            const auto& n3 = sub.nodes[3].anchor;
            const double min_x = std::min({n0.x, n1.x, n2.x, n3.x});
            const double max_x = std::max({n0.x, n1.x, n2.x, n3.x});
            const double min_y = std::min({n0.y, n1.y, n2.y, n3.y});
            const double max_y = std::max({n0.y, n1.y, n2.y, n3.y});
            if (max_x > min_x && max_y > min_y) {
                is_rect = true;
            }
        }
    }

    if (is_rect) {
        if (out_id) *out_id = id;
        if (out_settings) *out_settings = s;
        return true;
    }
    return false;
}

void NativeWindow::sync_rect_corner_controls_from_selection() {
    vector::RectCornerSettings s;
    if (is_selected_rectangle(nullptr, &s)) {
        rect_corner_type_ = s.type;
        rect_corner_tl_ = s.tl;
        rect_corner_tr_ = s.tr;
        rect_corner_br_ = s.br;
        rect_corner_bl_ = s.bl;
        rect_corners_locked_ = s.locked;
        rect_corners_scale_relative_ = s.scale_relative;
    }
}

void NativeWindow::set_rect_corner_type(vector::RectCornerType type) noexcept {
    rect_corner_type_ = type;
    if (node_tool_) {
        node_tool_->set_corner_type(type);
    }
    vector::RectCornerSettings s;
    s.type = rect_corner_type_;
    s.tl = rect_corner_tl_;
    s.tr = rect_corner_tr_;
    s.br = rect_corner_br_;
    s.bl = rect_corner_bl_;
    s.locked = rect_corners_locked_;
    s.scale_relative = rect_corners_scale_relative_;
    apply_rect_corner_settings(s);
}

void NativeWindow::toggle_rect_corners_locked() noexcept {
    rect_corners_locked_ = !rect_corners_locked_;
    if (rect_corners_locked_) {
        rect_corner_tr_ = rect_corner_tl_;
        rect_corner_br_ = rect_corner_tl_;
        rect_corner_bl_ = rect_corner_tl_;
    }
    vector::RectCornerSettings s;
    s.type = rect_corner_type_;
    s.tl = rect_corner_tl_;
    s.tr = rect_corner_tr_;
    s.br = rect_corner_br_;
    s.bl = rect_corner_bl_;
    s.locked = rect_corners_locked_;
    s.scale_relative = rect_corners_scale_relative_;
    apply_rect_corner_settings(s);
}

void NativeWindow::toggle_rect_corners_scale_relative() noexcept {
    rect_corners_scale_relative_ = !rect_corners_scale_relative_;
    vector::RectCornerSettings s;
    s.type = rect_corner_type_;
    s.tl = rect_corner_tl_;
    s.tr = rect_corner_tr_;
    s.br = rect_corner_br_;
    s.bl = rect_corner_bl_;
    s.locked = rect_corners_locked_;
    s.scale_relative = rect_corners_scale_relative_;
    apply_rect_corner_settings(s);
}

void NativeWindow::set_creation_rect_corner_type(vector::RectCornerType type) noexcept {
    creation_rect_corner_type_ = type;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_creation_rect_corners_locked() noexcept {
    creation_rect_corners_locked_ = !creation_rect_corners_locked_;
    if (creation_rect_corners_locked_) {
        creation_rect_corner_tr_ = creation_rect_corner_tl_;
        creation_rect_corner_br_ = creation_rect_corner_tl_;
        creation_rect_corner_bl_ = creation_rect_corner_tl_;
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_creation_rect_corners_scale_relative() noexcept {
    creation_rect_corners_scale_relative_ = !creation_rect_corners_scale_relative_;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_rect_corner_settings(const vector::RectCornerSettings& settings) {
    rect_corner_type_ = settings.type;
    rect_corner_tl_ = settings.tl;
    rect_corner_tr_ = settings.tr;
    rect_corner_br_ = settings.br;
    rect_corner_bl_ = settings.bl;
    rect_corners_locked_ = settings.locked;
    rect_corners_scale_relative_ = settings.scale_relative;

    document::ObjectId id;
    if (!is_selected_rectangle(&id)) {
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(id);
    if (!obj || !std::holds_alternative<document::PathObject>(*obj)) return;
    const auto& po = std::get<document::PathObject>(*obj);

    const auto local_bounds = po.path.bounds();
    if (!local_bounds.is_finite() || local_bounds.width <= 0.0 || local_bounds.height <= 0.0) return;

    try {
        const auto T = po.transform;
        const double sx = std::hypot(T.a, T.b);
        const double sy = std::hypot(T.c, T.d);

        geometry::Rect2D target_bounds = local_bounds;
        std::optional<geometry::Transform2D> new_transform = std::nullopt;

        if (sx > 1e-6 && sy > 1e-6 && (std::abs(sx - 1.0) > 1e-4 || std::abs(sy - 1.0) > 1e-4)) {
            // Anisotropic scale detected: normalize scaling directly into path coordinates
            // so corner rounding in millimeters remains 100% circular and isotropic!
            target_bounds = geometry::Rect2D{
                local_bounds.x * sx,
                local_bounds.y * sy,
                local_bounds.width * sx,
                local_bounds.height * sy
            };
            new_transform = geometry::Transform2D{
                T.a / sx, T.b / sx,
                T.c / sy, T.d / sy,
                T.tx, T.ty
            };
        }

        const auto new_path = vector::ShapeFactory::generate_rectangle_path(target_bounds, settings);
        auto ep = document::editable_path_from_path2d(new_path, nullptr);
        if (!ep) return;
        std::string new_spec = vector::serialize_rect_corner_spec(settings);
        root().document().execute_command(
            std::make_unique<document::SetAuthoringSourceCommand>(
                id, std::move(*ep), std::move(new_spec), new_transform));

        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật bo góc hình chữ nhật · Ctrl+Z hoàn tác"
            : "Updated rectangle corners · Ctrl+Z undoes";
    } catch (...) {}

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}


} // namespace pikdraw::desktop
