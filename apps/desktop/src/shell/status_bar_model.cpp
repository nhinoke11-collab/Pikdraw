#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include <sstream>
#include <iomanip>

namespace pikdraw::desktop {

void StatusBarModel::set_cursor_position(double x_mm, double y_mm) noexcept {
    cursor_x_ = x_mm;
    cursor_y_ = y_mm;
}

void StatusBarModel::set_zoom_ratio(double ratio) noexcept {
    if (ratio > 0.0) {
        zoom_ratio_ = ratio;
    }
}

void StatusBarModel::set_color_profile(std::string profile_name) {
    color_profile_ = std::move(profile_name);
}

void StatusBarModel::set_selection_count(size_t count) noexcept {
    selection_count_ = count;
}

void StatusBarModel::set_document_state(uint64_t revision, bool is_dirty) noexcept {
    revision_ = revision;
    is_dirty_ = is_dirty;
}

void StatusBarModel::set_preflight_message(std::string message) {
    preflight_message_ = std::move(message);
}

std::string StatusBarModel::format_coordinates_vi() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "X: " << cursor_x_ << " mm   Y: " << cursor_y_ << " mm";
    return ss.str();
}

std::string StatusBarModel::format_zoom_vi() const {
    std::ostringstream ss;
    ss << static_cast<int>(zoom_ratio_ * 100.0 + 0.5) << "%";
    return ss.str();
}

std::string StatusBarModel::format_selection_vi() const {
    if (selection_count_ == 0) {
        return "Không có đối tượng chọn";
    }
    if (selection_count_ == 1) {
        return "Đã chọn 1 đối tượng";
    }
    return "Đã chọn " + std::to_string(selection_count_) + " đối tượng";
}

std::string StatusBarModel::format_document_revision_vi() const {
    std::string text = "Rev " + std::to_string(revision_);
    if (is_dirty_) {
        text += " * (Chưa lưu)";
    } else {
        text += " (Đã lưu)";
    }
    return text;
}

} // namespace pikdraw::desktop
