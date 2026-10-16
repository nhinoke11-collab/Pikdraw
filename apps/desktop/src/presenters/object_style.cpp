#include "pikdraw/desktop/presenters/object_style.hpp"
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
namespace {
bool same_color(const color::ColorValue &a, const color::ColorValue &b) {
    if (a.color_space() != b.color_space() || a.overprint().fill != b.overprint().fill ||
        a.overprint().stroke != b.overprint().stroke)
        return false;
    return std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            const auto &other = std::get<T>(b.payload());
            if constexpr (std::is_same_v<T, color::SpotColor>)
                return value.spot_name == other.spot_name && value.tint == other.tint &&
                       value.overprint == other.overprint &&
                       value.alternate_rgb.approx_equal(other.alternate_rgb);
            else if constexpr (std::is_same_v<T, color::GrayscaleColor>)
                return value.gray == other.gray && value.a == other.a;
            else
                return value.approx_equal(other, 1e-8);
        },
        a.payload());
}
} // namespace
SelectionStyle selection_style(const document::DocumentSnapshot &snapshot,
                               const std::vector<document::ObjectId> &ids, const EditorStyle &defaults) {
    SelectionStyle result;
    result.values = defaults;
    for (const auto id : ids) {
        const auto *object = snapshot.objects().lookup_ref(id);
        if (!object)
            continue;
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (!std::is_same_v<T, document::GroupObject>) {
                    auto style = defaults;
                    style.fill = value.fill;
                    if constexpr (std::is_same_v<T, document::PathObject>) {
                        style.stroke = value.stroke;
                        style.width_mm = value.stroke_width_mm;
                        style.has_fill = value.has_fill;
                        style.has_stroke = value.has_stroke;
                    } else {
                        result.all_paths = false;
                        style.has_fill = true;
                        style.has_stroke = false;
                    }
                    if (result.count == 0)
                        result.values = style;
                    else {
                        result.mixed_fill |= !same_color(result.values.fill, style.fill);
                        result.mixed_stroke |= !same_color(result.values.stroke, style.stroke);
                        result.mixed_width |= result.values.width_mm != style.width_mm;
                        result.mixed_fill_enabled |= result.values.has_fill != style.has_fill;
                        result.mixed_stroke_enabled |= result.values.has_stroke != style.has_stroke;
                    }
                    ++result.count;
                } else {
                    result.all_paths = false;
                }
            },
            *object);
    }
    return result;
}
std::string color_description(const color::ColorValue &value) {
    std::ostringstream text;
    const auto number = [](double value) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        auto result = stream.str();
        while (result.back() == '0')
            result.pop_back();
        if (result.back() == '.')
            result.pop_back();
        return result;
    };
    std::visit(
        [&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, color::RGBColor>)
                text << "RGB " << number(c.r * 255) << "/" << number(c.g * 255) << "/" << number(c.b * 255);
            else if constexpr (std::is_same_v<T, color::CMYKColor>)
                text << "CMYK " << number(c.c * 100) << "/" << number(c.m * 100) << "/" << number(c.y * 100)
                     << "/" << number(c.k * 100);
            else if constexpr (std::is_same_v<T, color::SpotColor>)
                text << c.spot_name << " " << c.tint * 100 << "%";
            else
                text << "Gray " << c.gray * 100 << "%";
        },
        value.payload());
    return text.str();
}
const std::array<color::ColorValue, 18> &editor_palette(bool cmyk) {
    using color::ColorValue;
    static const std::array<color::ColorValue, 18> rgb = {
        ColorValue::make_rgb(0, 0, 0),       ColorValue::make_rgb(.3, .3, .3),
        ColorValue::make_rgb(.65, .65, .65), ColorValue::make_rgb(1, 1, 1),
        ColorValue::make_rgb(1, 0, 0),       ColorValue::make_rgb(1, .5, 0),
        ColorValue::make_rgb(1, 1, 0),       ColorValue::make_rgb(.5, 1, 0),
        ColorValue::make_rgb(0, .65, .2),    ColorValue::make_rgb(0, 1, 1),
        ColorValue::make_rgb(0, .65, .8),    ColorValue::make_rgb(0, .35, 1),
        ColorValue::make_rgb(0, 0, 1),       ColorValue::make_rgb(.5, 0, 1),
        ColorValue::make_rgb(1, 0, 1),       ColorValue::make_rgb(1, .3, .6),
        ColorValue::make_rgb(.5, .25, .1),   ColorValue::make_rgb(.9, .75, .5)};
    static const std::array<color::ColorValue, 18> print = {
        ColorValue::make_cmyk(0, 0, 0, 1),    ColorValue::make_cmyk(0, 0, 0, .7),
        ColorValue::make_cmyk(0, 0, 0, .3),   ColorValue::make_cmyk(0, 0, 0, 0),
        ColorValue::make_cmyk(0, 1, 1, 0),    ColorValue::make_cmyk(0, .5, 1, 0),
        ColorValue::make_cmyk(0, 0, 1, 0),    ColorValue::make_cmyk(.5, 0, 1, 0),
        ColorValue::make_cmyk(1, 0, 1, 0),    ColorValue::make_cmyk(1, 0, 0, 0),
        ColorValue::make_cmyk(1, 0, .3, 0),   ColorValue::make_cmyk(1, .5, 0, 0),
        ColorValue::make_cmyk(1, 1, 0, 0),    ColorValue::make_cmyk(.5, 1, 0, 0),
        ColorValue::make_cmyk(0, 1, 0, 0),    ColorValue::make_cmyk(0, .7, .3, 0),
        ColorValue::make_cmyk(.2, .6, 1, .3), ColorValue::make_cmyk(0, .2, .5, .1)};
    return cmyk ? print : rgb;
}
} // namespace pikdraw::desktop
