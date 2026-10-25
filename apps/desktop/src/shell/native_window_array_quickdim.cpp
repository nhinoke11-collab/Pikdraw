#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_length_input.hpp"
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
#ifdef _WIN32
void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = dip(width_), h = dip(height_);
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);
    paint(mem_dc, width_, height_);
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
void NativeWindow::capture_screenshot_bmp(const std::string&) {}
#endif
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
    const auto parsed = parse_quick_dimension_input_mm(quick_dimension_buffer_, display_length_unit_);
    if (!parsed || (!parsed->width_mm && !parsed->height_mm)) {
        end_quick_dimension();
        return false;
    }
    if (parsed->width_mm) new_w = *parsed->width_mm;
    if (parsed->height_mm) new_h = *parsed->height_mm;
    else if (parsed->width_mm && (active_tool_ == 2 || active_tool_ == 9)) new_h = new_w;
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
        const std::string w_str = format_mm_display_utf8(new_w, display_length_unit_, 7);
        const std::string h_str = format_mm_display_utf8(new_h, display_length_unit_, 7);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã áp dụng kích thước: " + w_str + " × " + h_str
            : "Applied dimensions: " + w_str + " × " + h_str;
    }
    end_quick_dimension();
    return res.success;
}
} // namespace pikdraw::desktop
