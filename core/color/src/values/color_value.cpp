#include "pikdraw/color/values/color_value.hpp"

namespace pikdraw::color {

bool ColorValue::is_valid() const noexcept {
    if (profile_resource_ &&
        profile_resource_->identity().color_space != color_space())
        return false;
    return std::visit([](const auto& c) noexcept -> bool {
        return c.is_valid();
    }, payload_);
}

} // namespace pikdraw::color
