#pragma once

#include <string>
#include <functional>

namespace pikdraw::desktop {

struct RibbonGeometryState {
    double x_mm{0.0};
    double y_mm{0.0};
    double width_mm{0.0};
    double height_mm{0.0};
    bool ratio_locked{false};
};

struct RibbonStyleState {
    std::string fill_hex{"#00f2c3"};
    std::string stroke_hex{"#ffffff"};
    double stroke_width_mm{1.0};
};

struct RibbonTextState {
    std::string font_family{"Plus Jakarta Sans"};
    double font_size_pt{14.0};
};

class RibbonPresenter {
public:
    RibbonPresenter() = default;

    void update_selection(bool has_selection, std::string target_label) noexcept;
    void update_geometry(RibbonGeometryState geom) noexcept;
    void update_style(RibbonStyleState style) noexcept;
    void update_text(RibbonTextState text) noexcept;

    [[nodiscard]] bool has_selection() const noexcept { return has_selection_; }
    [[nodiscard]] const std::string& target_label() const noexcept { return target_label_; }
    [[nodiscard]] const RibbonGeometryState& geometry() const noexcept { return geom_; }
    [[nodiscard]] const RibbonStyleState& style() const noexcept { return style_; }
    [[nodiscard]] const RibbonTextState& text() const noexcept { return text_; }

    void set_on_geometry_changed(std::function<void(const RibbonGeometryState&)> cb) {
        on_geometry_changed_ = std::move(cb);
    }

private:
    bool has_selection_{false};
    std::string target_label_{"Khung vẽ"};
    RibbonGeometryState geom_;
    RibbonStyleState style_;
    RibbonTextState text_;
    std::function<void(const RibbonGeometryState&)> on_geometry_changed_;
};

} // namespace pikdraw::desktop
