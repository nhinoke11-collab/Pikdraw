#pragma once

#include <string>
#include <cstdint>

namespace pikdraw::desktop {

class StatusBarModel {
public:
    StatusBarModel() = default;

    void set_cursor_position(double x_mm, double y_mm) noexcept;
    void set_zoom_ratio(double ratio) noexcept;
    void set_color_profile(std::string profile_name);
    void set_selection_count(size_t count) noexcept;
    void set_document_state(uint64_t revision, bool is_dirty) noexcept;
    void set_preflight_message(std::string message);

    [[nodiscard]] double cursor_x() const noexcept { return cursor_x_; }
    [[nodiscard]] double cursor_y() const noexcept { return cursor_y_; }
    [[nodiscard]] double zoom_ratio() const noexcept { return zoom_ratio_; }
    [[nodiscard]] size_t selection_count() const noexcept { return selection_count_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }
    [[nodiscard]] const std::string& color_profile() const noexcept { return color_profile_; }
    [[nodiscard]] const std::string& preflight_message() const noexcept { return preflight_message_; }

    [[nodiscard]] std::string format_coordinates_vi() const;
    [[nodiscard]] std::string format_zoom_vi() const;
    [[nodiscard]] std::string format_selection_vi() const;
    [[nodiscard]] std::string format_document_revision_vi() const;

private:
    double cursor_x_{0.0};
    double cursor_y_{0.0};
    double zoom_ratio_{1.0};
    size_t selection_count_{0};
    uint64_t revision_{0};
    bool is_dirty_{false};
    std::string color_profile_{"sRGB IEC61966-2.1"};
    std::string preflight_message_{"Chuẩn bị"};
};

} // namespace pikdraw::desktop
