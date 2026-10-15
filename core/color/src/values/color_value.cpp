#include "pikdraw/color/values/color_value.hpp"

namespace pikdraw::color {

bool ColorValue::is_valid() const noexcept {
    return std::visit([](const auto& c) noexcept -> bool {
        return c.is_valid();
    }, payload_);
}

} // namespace pikdraw::color
