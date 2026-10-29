#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_length_input.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {
void draw_annotation_button(HDC dc, RECT rect, const wchar_t* text,
                            COLORREF fill, COLORREF border, COLORREF foreground,
                            HFONT font, bool selected = false) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 5, 5);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetTextColor(dc, foreground);
    SelectObject(dc, font);
    DrawTextW(dc, text, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}
} // namespace

bool NativeWindow::has_annotation_properties_target() const {
    if (active_tool_ == 17 && annotation_tool_ && annotation_tool_->active()) return true;
    const auto& ids = root().vector_controller().selection().selected_ids();
    if (ids.size() != 1) return false;
    const auto& doc = root().document();
    return document::get_detail_view(doc, ids.front()).has_value() ||
        document::get_annotation(doc, ids.front()).has_value();
}

void NativeWindow::paint_annotation_property_bar(
    HDC dc, int, int content_top, int topbar_h, int tabbar_h) {
    const bool draft = active_tool_ == 17 && annotation_tool_ && annotation_tool_->active();
    const auto& selected_ids = root().vector_controller().selection().selected_ids();
    const auto selected_detail = !draft && selected_ids.size() == 1
        ? document::get_detail_view(root().document(), selected_ids.front()) : std::nullopt;
    const auto selected_note = !draft && selected_ids.size() == 1
        ? document::get_annotation(root().document(), selected_ids.front()) : std::nullopt;
    if (!draft && !selected_detail && !selected_note) return;
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF fill = dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    const COLORREF border = dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF teal = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const RECT row{0, topbar_h + tabbar_h + 4, 620, content_top - 4};
    SetTextColor(dc, teal);
    SelectObject(dc, font_be_vietnam_bold_);
    RECT label{10, row.top, 68, row.bottom};
    const bool detail_mode = selected_detail.has_value() ||
        (draft && annotation_tool_->mode() == tools::AnnotationToolMode::DetailView);
    const bool leader_mode = selected_note
        ? selected_note->kind == document::AnnotationKind::LeaderNote
        : draft && annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote;
    const wchar_t* title = detail_mode
        ? (language_ == UILanguage::Vietnamese ? L"Chi tiết:" : L"Detail:")
        : leader_mode
            ? (language_ == UILanguage::Vietnamese ? L"Leader:" : L"Leader:")
            : (language_ == UILanguage::Vietnamese ? L"Ghi chú:" : L"Note:");
    DrawTextW(dc, title, -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (detail_mode) {
        const auto shape = selected_detail ? selected_detail->shape : annotation_tool_->detail_shape();
        const double scale_ratio = selected_detail ? selected_detail->scale_ratio : annotation_tool_->scale_ratio();
        const bool connector = selected_detail ? selected_detail->show_connector : annotation_tool_->show_connector();
        const auto& detail_label = selected_detail ? selected_detail->label : annotation_tool_->detail_label();
        const bool circle = shape == document::DetailViewShape::Circle;
        const wchar_t* shape_label = circle ? L"◯ Circle" : L"▭ Rectangle";
        draw_annotation_button(dc, {70, row.top, 158, row.bottom}, shape_label, fill, border, text,
                               font_be_vietnam_, false);
        const std::wstring scale = (language_ == UILanguage::Vietnamese ? L"Tỉ lệ " : L"Scale ") +
            utf8_to_wide(document::format_detail_scale_ratio(scale_ratio));
        const bool focused = active_num_field_ == NumericFieldId::AnnotationScale;
        draw_annotation_button(dc, {164, row.top, 278, row.bottom},
            focused ? (utf8_to_wide(num_input_buffer_) + L"|").c_str() : scale.c_str(),
            focused ? (dark ? RGB(22, 37, 54) : RGB(236, 242, 248)) : fill,
            focused ? teal : border, focused ? teal : text, font_mono_, focused);
        draw_annotation_button(dc, {284, row.top, 388, row.bottom},
            connector ? L"↗ Connector: On" : L"↗ Connector: Off",
            fill, border, connector ? teal : muted,
            font_be_vietnam_, connector);
        const auto label_text = utf8_to_wide(detail_label);
        draw_annotation_button(dc, {394, row.top, 500, row.bottom}, label_text.c_str(),
                               fill, border, text, font_be_vietnam_, false);
        draw_annotation_button(dc, {506, row.top, 610, row.bottom},
            language_ == UILanguage::Vietnamese ? L"⚙ Thuộc tính" : L"⚙ Properties",
            fill, teal, teal, font_be_vietnam_bold_, false);
    } else {
        const double text_size = selected_note ? selected_note->style.text_size_mm : annotation_tool_->note_text_size_mm();
        if (leader_mode) {
            const bool elbow = selected_note
                ? selected_note->leader_points.size() >= 2
                : annotation_tool_->leader_line_mode() == tools::LeaderLineMode::Elbow;
            draw_annotation_button(dc, {70, row.top, 184, row.bottom},
                elbow ? L"Elbow" : L"Straight", fill, border, text, font_be_vietnam_, false);
        }
        const int text_size_left = leader_mode ? 190 : 70;
        const bool focused = active_num_field_ == NumericFieldId::AnnotationTextSize;
        std::ostringstream size_text;
        size_text << std::setprecision(6) << text_size << " mm";
        const auto value = utf8_to_wide(size_text.str());
        draw_annotation_button(dc, {text_size_left, row.top, text_size_left + 126, row.bottom},
            focused ? (utf8_to_wide(num_input_buffer_) + L"|").c_str() : value.c_str(),
            focused ? (dark ? RGB(22, 37, 54) : RGB(236, 242, 248)) : fill,
            focused ? teal : border, focused ? teal : text, font_mono_, focused);
        const int property_left = text_size_left + 136;
        draw_annotation_button(dc, {property_left, row.top, property_left + 114, row.bottom},
            language_ == UILanguage::Vietnamese ? L"⚙ Thuộc tính" : L"⚙ Properties",
            fill, teal, teal, font_be_vietnam_bold_, false);
    }
}

bool NativeWindow::handle_annotation_property_bar_click(int x, int y) {
    if (y < 66 || y > 102) return false;
    const bool draft = active_tool_ == 17 && annotation_tool_ && annotation_tool_->active();
    const auto& ids = root().vector_controller().selection().selected_ids();
    auto detail = !draft && ids.size() == 1
        ? document::get_detail_view(root().document(), ids.front()) : std::nullopt;
    auto note = !draft && ids.size() == 1
        ? document::get_annotation(root().document(), ids.front()) : std::nullopt;
    if (!draft && !detail && !note) return false;
    const bool detail_mode = detail.has_value() ||
        (draft && annotation_tool_->mode() == tools::AnnotationToolMode::DetailView);
    const bool leader_mode = note ? note->kind == document::AnnotationKind::LeaderNote
        : draft && annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote;
    if (detail_mode) {
        if (x >= 70 && x < 158) {
            const auto shape = detail ? detail->shape : annotation_tool_->detail_shape();
            const auto next_shape = shape == document::DetailViewShape::Circle
                ? document::DetailViewShape::Rectangle : document::DetailViewShape::Circle;
            if (draft) annotation_tool_->set_detail_shape(next_shape);
            else {
                detail->shape = next_shape;
                if (next_shape == document::DetailViewShape::Circle) {
                    const auto center = detail->source_region.center();
                    const double side = (std::min)(detail->source_region.width, detail->source_region.height);
                    detail->source_region = {center.x - side * 0.5, center.y - side * 0.5, side, side};
                }
                const double top = detail->frame_rect.y + detail->frame_rect.height;
                const double fw = detail->source_region.width * detail->scale_ratio;
                const double fh = detail->source_region.height * detail->scale_ratio;
                detail->frame_rect = {detail->frame_rect.x, top - fh, fw, fh};
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            }
        } else if (x >= 164 && x < 278) {
            start_numeric_input(NumericFieldId::AnnotationScale);
        } else if (x >= 284 && x < 388) {
            if (draft) annotation_tool_->set_show_connector(!annotation_tool_->show_connector());
            else {
                detail->show_connector = !detail->show_connector;
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            }
        } else if (x >= 394 && x < 500) {
            std::string label = detail ? detail->label : annotation_tool_->detail_label();
            if (label == "DETAIL A") label = "DETAIL B";
            else if (label == "DETAIL B") label = "DETAIL C";
            else label = "DETAIL A";
            if (draft) annotation_tool_->set_detail_label(label);
            else {
                detail->label = label;
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            }
        } else if (x >= 506 && x < 610) {
            active_dock_tab_ = 0;
            properties_dock_scroll_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Thuộc tính ô chi tiết · chỉnh sửa bản nháp trước khi đặt"
                : "Detail View Properties · edit the draft before placement";
        } else return false;
    } else {
        const int text_size_left = leader_mode ? 190 : 70;
        if (leader_mode && x >= 70 && x < 184) {
            if (draft) {
                annotation_tool_->set_leader_line_mode(
                    annotation_tool_->leader_line_mode() == tools::LeaderLineMode::Elbow
                        ? tools::LeaderLineMode::Straight : tools::LeaderLineMode::Elbow);
            } else if (note) {
                if (note->leader_points.size() >= 2) {
                    const auto endpoint = note->leader_points.back();
                    note->leader_points.assign(1, endpoint);
                } else if (!note->leader_points.empty()) {
                    const auto endpoint = note->leader_points.back();
                    geometry::Point2D elbow{endpoint.x, note->anchor.anchor_pt.y};
                    if (std::hypot(elbow.x - note->anchor.anchor_pt.x,
                                   elbow.y - note->anchor.anchor_pt.y) < 1e-9)
                        elbow = {note->anchor.anchor_pt.x, endpoint.y};
                    note->leader_points = {elbow, endpoint};
                }
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateAnnotationCommand>(ids.front(), *note));
            }
        } else if (x >= text_size_left && x < text_size_left + 126) {
            start_numeric_input(NumericFieldId::AnnotationTextSize);
        } else if (x >= text_size_left + 136 && x < text_size_left + 250) {
            active_dock_tab_ = 0;
            properties_dock_scroll_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Thuộc tính ghi chú · chỉnh sửa bản nháp trước khi đặt"
                : "Note Properties · edit the draft before placement";
        } else return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::paint_annotation_properties_panel(HDC dc, int w, int inspector_x, int content_y) {
    const bool draft = active_tool_ == 17 && annotation_tool_ && annotation_tool_->active();
    const auto& ids = root().vector_controller().selection().selected_ids();
    const auto selected_detail = !draft && ids.size() == 1
        ? document::get_detail_view(root().document(), ids.front()) : std::nullopt;
    const auto selected_note = !draft && ids.size() == 1
        ? document::get_annotation(root().document(), ids.front()) : std::nullopt;
    if (!draft && !selected_detail && !selected_note) return;
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF teal = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const bool detail = draft && annotation_tool_->mode() == tools::AnnotationToolMode::DetailView;
    if (!draft) {
        SetTextColor(dc, teal);
        SelectObject(dc, font_be_vietnam_bold_);
        RECT selected_title{inspector_x + 14, content_y, w - 14, content_y + 26};
        DrawTextW(dc, selected_detail
            ? (language_ == UILanguage::Vietnamese ? L"THUỘC TÍNH · Ô CHI TIẾT" : L"PROPERTIES · DETAIL VIEW")
            : (language_ == UILanguage::Vietnamese ? L"THUỘC TÍNH · GHI CHÚ" : L"PROPERTIES · NOTE"),
            -1, &selected_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const int left = inspector_x + 14;
        if (selected_detail) {
            const auto& dv = *selected_detail;
            draw_annotation_button(dc, {left, content_y + 52, w - 14, content_y + 82},
                dv.shape == document::DetailViewShape::Circle ? L"◯ Circle" : L"▭ Rectangle",
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_be_vietnam_, false);
            const auto ratio_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Tỉ lệ · " : "Scale · ") + document::format_detail_scale_ratio(dv.scale_ratio));
            draw_annotation_button(dc, {left, content_y + 88, w - 14, content_y + 118}, ratio_text.c_str(),
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_, false);
            draw_annotation_button(dc, {left, content_y + 124, w - 14, content_y + 154},
                dv.show_connector ? L"Connector · On" : L"Connector · Off",
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249), teal, text, font_be_vietnam_, dv.show_connector);
            SetTextColor(dc, muted);
            RECT label{left, content_y + 160, w - 14, content_y + 184};
            const auto label_text = (language_ == UILanguage::Vietnamese ? std::wstring(L"Nhãn · ") : std::wstring(L"Label · ")) + utf8_to_wide(dv.label);
            DrawTextW(dc, label_text.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT source{left, content_y + 188, w - 14, content_y + 212};
            std::ostringstream region; region << std::setprecision(5) << dv.source_region.width << " × " << dv.source_region.height << " mm";
            const auto source_text = (language_ == UILanguage::Vietnamese ? std::wstring(L"Vùng nguồn · ") : std::wstring(L"Source region · ")) + utf8_to_wide(region.str());
            DrawTextW(dc, source_text.c_str(), -1, &source, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const auto source_x = dv.shape == document::DetailViewShape::Circle ? dv.source_region.center().x : dv.source_region.x;
            const auto source_y = dv.shape == document::DetailViewShape::Circle ? dv.source_region.center().y : dv.source_region.y;
            const auto source_size = dv.shape == document::DetailViewShape::Circle ? dv.source_region.width * 0.5 : dv.source_region.width;
            const int mid = (left + w - 14) / 2;
            const auto field_button = [&](int y, int x0, int x1, const wchar_t* name, double number) {
                const auto value = std::wstring(name ? name : L"") +
                    utf8_to_wide(format_mm_display_utf8(number, display_length_unit_, 5));
                draw_annotation_button(dc, {x0, y, x1, y + 26}, value.c_str(),
                    dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                    dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_, false);
            };
            field_button(content_y + 216, left, mid - 3, dv.shape == document::DetailViewShape::Circle ? L"Cx" : L"X", source_x);
            field_button(content_y + 216, mid + 3, w - 14, dv.shape == document::DetailViewShape::Circle ? L"Cy" : L"Y", source_y);
            field_button(content_y + 248, left, mid - 3, dv.shape == document::DetailViewShape::Circle ? L"R" : L"W", source_size);
            if (dv.shape == document::DetailViewShape::Rectangle)
                field_button(content_y + 248, mid + 3, w - 14, L"H", dv.source_region.height);
            field_button(content_y + 280, left, mid - 3, L"Frame X", dv.frame_rect.x);
            field_button(content_y + 280, mid + 3, w - 14, L"Frame Y", dv.frame_rect.y + dv.frame_rect.height);
        } else {
            const auto& ann = *selected_note;
            const bool leader = ann.kind == document::AnnotationKind::LeaderNote;
            if (leader) {
                draw_annotation_button(dc, {left, content_y + 52, w - 14, content_y + 82},
                    ann.leader_points.size() >= 2 ? L"Elbow" : L"Straight",
                    dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                    dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_be_vietnam_, false);
            }
            const int size_y = content_y + (leader ? 88 : 52);
            RECT size{left, size_y, w - 14, size_y + 30};
            std::ostringstream value; value << std::setprecision(6) << ann.style.text_size_mm << " mm";
            const auto size_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Cỡ chữ · " : "Text height · ") + value.str());
            draw_annotation_button(dc, size, size_text.c_str(),
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_, false);
            SetTextColor(dc, muted);
            const int font_y = leader ? 124 : 88;
            RECT font{left, content_y + font_y, w - 14, content_y + font_y + 24};
            const auto font_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Phông · " : "Font · ") + ann.style.font_family);
            DrawTextW(dc, font_text.c_str(), -1, &font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            const int content_top = leader ? 154 : 118;
            RECT content{left, content_y + content_top, w - 14, content_y + content_top + 80};
            HBRUSH brush = CreateSolidBrush(dark ? RGB(18, 28, 42) : RGB(241, 245, 249));
            FillRect(dc, &content, brush); DeleteObject(brush);
            SetTextColor(dc, text); SelectObject(dc, font_secondary_);
            const auto note_text = utf8_to_wide(ann.text);
            DrawTextW(dc, note_text.c_str(), -1, &content, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        return;
    }
    SetTextColor(dc, teal);
    SelectObject(dc, font_be_vietnam_bold_);
    RECT title{inspector_x + 14, content_y, w - 14, content_y + 26};
    const wchar_t* title_text = detail
        ? (language_ == UILanguage::Vietnamese ? L"THUỘC TÍNH · Ô CHI TIẾT" : L"PROPERTIES · DETAIL VIEW")
        : (language_ == UILanguage::Vietnamese ? L"THUỘC TÍNH · GHI CHÚ" : L"PROPERTIES · ANNOTATION NOTE");
    DrawTextW(dc, title_text, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, muted);
    SelectObject(dc, font_secondary_);
    RECT stage{inspector_x + 14, content_y + 26, w - 14, content_y + 48};
    const bool preview = annotation_tool_->has_preview();
    const std::wstring stage_text = preview
        ? (language_ == UILanguage::Vietnamese ? L"Bản nháp đang hoạt động" : L"Draft is active")
        : (language_ == UILanguage::Vietnamese ? L"Tạo mới · chưa ghi vào tài liệu" : L"Create · not yet in document");
    DrawTextW(dc, stage_text.c_str(), -1, &stage, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const int left = inspector_x + 14;
    if (detail) {
        RECT shape_row{left, content_y + 52, w - 14, content_y + 82};
        const bool circle = annotation_tool_->detail_shape() == document::DetailViewShape::Circle;
        draw_annotation_button(dc, shape_row, circle ? L"◯ Circle" : L"▭ Rectangle",
            dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
            dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_be_vietnam_, false);
        RECT ratio_row{left, content_y + 88, w - 14, content_y + 118};
        const auto ratio_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Tỉ lệ · " : "Scale · ") + document::format_detail_scale_ratio(annotation_tool_->scale_ratio()));
        draw_annotation_button(dc, ratio_row, ratio_text.c_str(),
            dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
            dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_,
            active_num_field_ == NumericFieldId::AnnotationScale);
        RECT connector_row{left, content_y + 124, w - 14, content_y + 154};
        draw_annotation_button(dc, connector_row,
            annotation_tool_->show_connector() ? L"Connector · On" : L"Connector · Off",
            dark ? RGB(18, 28, 42) : RGB(241, 245, 249), teal, text,
            font_be_vietnam_, annotation_tool_->show_connector());
        SetTextColor(dc, muted);
        RECT label{left, content_y + 160, w - 14, content_y + 184};
        const auto label_text = (language_ == UILanguage::Vietnamese ? std::wstring(L"Nhãn · ") : std::wstring(L"Label · ")) +
            utf8_to_wide(annotation_tool_->detail_label());
        DrawTextW(dc, label_text.c_str(), -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT source{left, content_y + 188, w - 14, content_y + 212};
        const auto& region = annotation_tool_->draft_detail_view().source_region;
        std::ostringstream region_text;
        region_text << std::setprecision(5) << region.width << " × " << region.height << " mm";
        const auto source_text = (language_ == UILanguage::Vietnamese ? std::wstring(L"Vùng nguồn · ") : std::wstring(L"Source region · ")) +
            utf8_to_wide(region_text.str());
        DrawTextW(dc, source_text.c_str(), -1, &source,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const auto detail_shape = annotation_tool_->detail_shape();
        const auto source_x = detail_shape == document::DetailViewShape::Circle ? region.center().x : region.x;
        const auto source_y = detail_shape == document::DetailViewShape::Circle ? region.center().y : region.y;
        const auto source_size = detail_shape == document::DetailViewShape::Circle ? region.width * 0.5 : region.width;
        const int mid = (left + w - 14) / 2;
        const auto field_button = [&](int y, int x0, int x1, const std::wstring& name, double number) {
            const auto value = name + utf8_to_wide(format_mm_display_utf8(number, display_length_unit_, 5));
            draw_annotation_button(dc, {x0, y, x1, y + 26}, value.c_str(),
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_, false);
        };
        if (annotation_tool_->has_preview()) {
            field_button(content_y + 216, left, mid - 3,
                detail_shape == document::DetailViewShape::Circle ? L"Cx · " : L"X · ", source_x);
            field_button(content_y + 216, mid + 3, w - 14,
                detail_shape == document::DetailViewShape::Circle ? L"Cy · " : L"Y · ", source_y);
            field_button(content_y + 248, left, mid - 3,
                detail_shape == document::DetailViewShape::Circle ? L"R · " : L"W · ", source_size);
            if (detail_shape == document::DetailViewShape::Rectangle)
                field_button(content_y + 248, mid + 3, w - 14, L"H · ", region.height);
            const auto& frame = annotation_tool_->draft_detail_view().frame_rect;
            field_button(content_y + 280, left, mid - 3, L"Frame X · ", frame.x);
            field_button(content_y + 280, mid + 3, w - 14, L"Frame Y · ", frame.y + frame.height);
        }
    } else {
        int y = content_y + 52;
        if (annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote) {
            RECT mode_row{left, y, w - 14, y + 30};
            const bool elbow = annotation_tool_->leader_line_mode() == tools::LeaderLineMode::Elbow;
            draw_annotation_button(dc, mode_row, elbow ? L"Elbow" : L"Straight",
                dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
                dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_be_vietnam_, false);
            y += 36;
        }
        RECT size_row{left, y, w - 14, y + 30};
        std::ostringstream size;
        size << std::setprecision(6) << annotation_tool_->note_text_size_mm() << " mm";
        const auto size_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Cỡ chữ · " : "Text height · ") + size.str());
        draw_annotation_button(dc, size_row, size_text.c_str(),
            dark ? RGB(18, 28, 42) : RGB(241, 245, 249),
            dark ? RGB(29, 38, 54) : RGB(203, 213, 225), text, font_mono_,
            active_num_field_ == NumericFieldId::AnnotationTextSize);
        y += 36;
        RECT font_row{left, y, w - 14, y + 24};
        const auto font_text = utf8_to_wide((language_ == UILanguage::Vietnamese ? "Phông · " : "Font · ") +
            annotation_tool_->note_font_family());
        SetTextColor(dc, text);
        DrawTextW(dc, font_text.c_str(), -1, &font_row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT content{left, y + 26, w - 14, y + 96};
        HBRUSH brush = CreateSolidBrush(dark ? RGB(18, 28, 42) : RGB(241, 245, 249));
        FillRect(dc, &content, brush);
        DeleteObject(brush);
        SetTextColor(dc, text);
        SelectObject(dc, font_secondary_);
        const auto value = utf8_to_wide(annotation_tool_->note_text().empty()
            ? (language_ == UILanguage::Vietnamese ? "Nhấp vào canvas để nhập nội dung" : "Click the canvas to enter note text")
            : annotation_tool_->note_text());
        DrawTextW(dc, value.c_str(), -1, &content, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

bool NativeWindow::handle_annotation_properties_panel_click(int x, int y) {
    if (active_dock_tab_ != 0 || x < width_ - 280 || y < 138) return false;
    const int content_y = y + properties_dock_scroll_;
    const int local = content_y - 138;
    const bool draft = active_tool_ == 17 && annotation_tool_ && annotation_tool_->active();
    if (!draft) {
        const auto& ids = root().vector_controller().selection().selected_ids();
        if (ids.size() != 1) return false;
        if (auto detail = document::get_detail_view(root().document(), ids.front())) {
            if (local >= 52 && local < 82) {
                detail->shape = detail->shape == document::DetailViewShape::Circle
                    ? document::DetailViewShape::Rectangle : document::DetailViewShape::Circle;
                if (detail->shape == document::DetailViewShape::Circle) {
                    const auto center = detail->source_region.center();
                    const double side = (std::min)(detail->source_region.width, detail->source_region.height);
                    detail->source_region = {center.x - side * 0.5, center.y - side * 0.5, side, side};
                }
                const double top = detail->frame_rect.y + detail->frame_rect.height;
                const double fw = detail->source_region.width * detail->scale_ratio;
                const double fh = detail->source_region.height * detail->scale_ratio;
                detail->frame_rect = {detail->frame_rect.x, top - fh, fw, fh};
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            } else if (local >= 88 && local < 118) {
                start_numeric_input(NumericFieldId::AnnotationScale);
            } else if (local >= 124 && local < 154) {
                detail->show_connector = !detail->show_connector;
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            } else if (local >= 160 && local < 184) {
                if (detail->label == "DETAIL A") detail->label = "DETAIL B";
                else if (detail->label == "DETAIL B") detail->label = "DETAIL C";
                else detail->label = "DETAIL A";
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateDetailViewCommand>(ids.front(), *detail));
            } else if (local >= 216 && local < 242) {
                const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
                const bool right = x >= mid + 3;
                if (detail->shape == document::DetailViewShape::Circle)
                    start_numeric_input(right ? NumericFieldId::AnnotationSourceY : NumericFieldId::AnnotationSourceX);
                else
                    start_numeric_input(right ? NumericFieldId::AnnotationSourceY : NumericFieldId::AnnotationSourceX);
            } else if (local >= 248 && local < 274) {
                const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
                if (detail->shape == document::DetailViewShape::Circle || x < mid + 3)
                    start_numeric_input(NumericFieldId::AnnotationSourceWidth);
                else start_numeric_input(NumericFieldId::AnnotationSourceHeight);
            } else if (local >= 280 && local < 306) {
                const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
                start_numeric_input(x < mid + 3 ? NumericFieldId::AnnotationFrameX : NumericFieldId::AnnotationFrameY);
            } else return false;
        } else if (auto annotation = document::get_annotation(root().document(), ids.front())) {
            const bool leader = annotation->kind == document::AnnotationKind::LeaderNote;
            if (leader && local >= 52 && local < 82) {
                if (annotation->leader_points.size() >= 2) {
                    const auto endpoint = annotation->leader_points.back();
                    annotation->leader_points.assign(1, endpoint);
                } else if (!annotation->leader_points.empty()) {
                    const auto endpoint = annotation->leader_points.back();
                    geometry::Point2D elbow{endpoint.x, annotation->anchor.anchor_pt.y};
                    if (std::hypot(elbow.x - annotation->anchor.anchor_pt.x,
                                   elbow.y - annotation->anchor.anchor_pt.y) < 1e-9)
                        elbow = {annotation->anchor.anchor_pt.x, endpoint.y};
                    annotation->leader_points = {elbow, endpoint};
                }
                (void)root().document().execute_command(
                    std::make_unique<document::UpdateAnnotationCommand>(ids.front(), *annotation));
            } else if ((!leader && local >= 52 && local < 82) || (leader && local >= 88 && local < 118)) {
                start_numeric_input(NumericFieldId::AnnotationTextSize);
            } else if ((!leader && local >= 118 && local < 198) || (leader && local >= 154 && local < 234)) {
                const auto mode = annotation->kind == document::AnnotationKind::PlainNote
                    ? tools::AnnotationToolMode::PlainNote : tools::AnnotationToolMode::LeaderNote;
                activate_annotation_tool(mode);
                annotation_tool_->begin_edit_annotation(*annotation);
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Sửa nội dung ghi chú · Enter xuống dòng · Ctrl+Enter nhận · Esc hủy"
                    : "Edit note text · Enter new line · Ctrl+Enter applies · Esc cancels";
            } else return false;
        } else return false;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (annotation_tool_->mode() == tools::AnnotationToolMode::DetailView) {
        if (local >= 52 && local < 82) {
            annotation_tool_->set_detail_shape(
                annotation_tool_->detail_shape() == document::DetailViewShape::Circle
                    ? document::DetailViewShape::Rectangle : document::DetailViewShape::Circle);
        } else if (local >= 88 && local < 118) {
            start_numeric_input(NumericFieldId::AnnotationScale);
        } else if (local >= 124 && local < 154) {
            annotation_tool_->set_show_connector(!annotation_tool_->show_connector());
        } else if (annotation_tool_->has_preview() && local >= 216 && local < 242) {
            const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
            const bool right = x >= mid + 3;
            start_numeric_input(right ? NumericFieldId::AnnotationSourceY : NumericFieldId::AnnotationSourceX);
        } else if (annotation_tool_->has_preview() && local >= 248 && local < 274) {
            const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
            if (annotation_tool_->detail_shape() == document::DetailViewShape::Circle || x < mid + 3)
                start_numeric_input(NumericFieldId::AnnotationSourceWidth);
            else start_numeric_input(NumericFieldId::AnnotationSourceHeight);
        } else if (annotation_tool_->has_preview() && local >= 280 && local < 306) {
            const int mid = (width_ - 280 + 14 + width_ - 14) / 2;
            start_numeric_input(x < mid + 3 ? NumericFieldId::AnnotationFrameX : NumericFieldId::AnnotationFrameY);
        } else return false;
    } else {
        int y0 = 52;
        if (annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote) {
            if (local >= y0 && local < y0 + 30) {
                annotation_tool_->set_leader_line_mode(
                    annotation_tool_->leader_line_mode() == tools::LeaderLineMode::Elbow
                        ? tools::LeaderLineMode::Straight : tools::LeaderLineMode::Elbow);
            } else y0 += 36;
        }
        if (local >= y0 && local < y0 + 30) start_numeric_input(NumericFieldId::AnnotationTextSize);
        else return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
#endif

} // namespace pikdraw::desktop
