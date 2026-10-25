#include "pikdraw/desktop/presenters/object_style.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

namespace pikdraw::desktop {
namespace {
bool same_color(const color::ColorValue &a, const color::ColorValue &b) {
    if (a.color_space() != b.color_space() || !a.has_same_profile_binding(b) ||
        a.overprint().fill != b.overprint().fill ||
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

bool same_effect_type(const SelectionEffect &a, const document::EffectNode &b) {
    return a.schema_version == b.schema_version && a.schema_minor == b.schema_minor &&
           a.parameters.index() == b.parameters.index();
}

bool same_color_value(const color::ColorValue &a, const color::ColorValue &b) {
    return same_color(a, b);
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
                if constexpr (std::is_same_v<T, document::PathObject> || std::is_same_v<T, document::TextObject>) {
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
SelectionAppearance selection_appearance(const document::DocumentSnapshot &snapshot,
                                           const std::vector<document::ObjectId> &ids) {
    SelectionAppearance result;
    result.selected_count = ids.size();
    for (const auto id : ids) {
        const auto *object = snapshot.objects().lookup_ref(id);
        if (!object) {
            result.all_path_text = false;
            result.all_paths = false;
            result.effect_stack_compatible = false;
            continue;
        }
        if (!std::holds_alternative<document::PathObject>(*object))
            result.all_paths = false;
        result.contains_locked = result.contains_locked ||
            std::visit([](const auto &value) { return value.locked; }, *object);
        const auto *appearance = std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object);
        if (!appearance) {
            result.all_path_text = false;
            result.all_paths = false;
            result.effect_stack_compatible = false;
            continue;
        }
        if (result.supported_count == 0) {
            result.object_opacity = appearance->object_opacity;
            result.fill_opacity = appearance->fill_opacity;
            result.stroke_opacity = appearance->stroke_opacity;
            result.blend_mode = appearance->blend_mode;
            result.effects.reserve(appearance->effects.size());
            for (const auto &node : appearance->effects) {
                SelectionEffect summary;
                summary.parameters = node.parameters;
                summary.schema_version = node.schema_version;
                summary.schema_minor = node.schema_minor;
                summary.enabled = node.enabled;
                result.effects.push_back(std::move(summary));
            }
        } else {
            result.mixed_object_opacity = result.mixed_object_opacity ||
                std::abs(result.object_opacity - appearance->object_opacity) > 1e-9;
            result.mixed_fill_opacity = result.mixed_fill_opacity ||
                std::abs(result.fill_opacity - appearance->fill_opacity) > 1e-9;
            result.mixed_stroke_opacity = result.mixed_stroke_opacity ||
                std::abs(result.stroke_opacity - appearance->stroke_opacity) > 1e-9;
            result.mixed_blend_mode = result.mixed_blend_mode ||
                result.blend_mode != appearance->blend_mode;
            if (result.effect_stack_compatible &&
                result.effects.size() != appearance->effects.size()) {
                result.effect_stack_compatible = false;
            }
            if (result.effect_stack_compatible) {
                for (size_t index = 0; index < result.effects.size(); ++index) {
                    const auto &summary = result.effects[index];
                    const auto &first_parameters = summary.parameters;
                    const auto &node = appearance->effects[index];
                    // Instance IDs intentionally differ across objects; stack shape is
                    // compared by type/schema and position only.
                    if (!same_effect_type(summary, node)) {
                        result.effect_stack_compatible = false;
                        break;
                    }
                    auto &mutable_summary = result.effects[index];
                    mutable_summary.mixed_enabled = mutable_summary.mixed_enabled ||
                        mutable_summary.enabled != node.enabled;
                    std::visit([&](const auto &first) {
                        using T = std::decay_t<decltype(first)>;
                        const auto *current = std::get_if<T>(&node.parameters);
                        if (!current) {
                            result.effect_stack_compatible = false;
                            return;
                        }
                        if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
                            mutable_summary.mixed_sigma_x = mutable_summary.mixed_sigma_x ||
                                first.sigma_x_mm != current->sigma_x_mm;
                            mutable_summary.mixed_sigma_y = mutable_summary.mixed_sigma_y ||
                                first.sigma_y_mm != current->sigma_y_mm;
                        } else if constexpr (std::is_same_v<T, document::FeatherEffect>) {
                            mutable_summary.mixed_sigma_x = mutable_summary.mixed_sigma_x ||
                                first.radius_mm != current->radius_mm;
                        } else if constexpr (std::is_same_v<T, document::OuterGlowEffect> ||
                                            std::is_same_v<T, document::InnerGlowEffect>) {
                            mutable_summary.mixed_sigma_x = mutable_summary.mixed_sigma_x ||
                                first.radius_mm != current->radius_mm;
                            mutable_summary.mixed_opacity = mutable_summary.mixed_opacity ||
                                first.opacity != current->opacity;
                            mutable_summary.mixed_color = mutable_summary.mixed_color ||
                                !same_color_value(first.color, current->color);
                        } else if constexpr (std::is_same_v<T, document::ColorOverlayEffect>) {
                            mutable_summary.mixed_opacity = mutable_summary.mixed_opacity ||
                                first.opacity != current->opacity;
                            mutable_summary.mixed_color = mutable_summary.mixed_color ||
                                !same_color_value(first.color, current->color);
                            mutable_summary.mixed_shadow_blend_mode =
                                mutable_summary.mixed_shadow_blend_mode ||
                                first.blend_mode != current->blend_mode;
                        } else {
                            // DropShadowEffect & InnerShadowEffect
                            mutable_summary.mixed_offset_x = mutable_summary.mixed_offset_x ||
                                first.dx_mm != current->dx_mm;
                            mutable_summary.mixed_offset_y = mutable_summary.mixed_offset_y ||
                                first.dy_mm != current->dy_mm;
                            mutable_summary.mixed_sigma_x = mutable_summary.mixed_sigma_x ||
                                first.sigma_x_mm != current->sigma_x_mm;
                            mutable_summary.mixed_sigma_y = mutable_summary.mixed_sigma_y ||
                                first.sigma_y_mm != current->sigma_y_mm;
                            mutable_summary.mixed_opacity = mutable_summary.mixed_opacity ||
                                first.opacity != current->opacity;
                            mutable_summary.mixed_color = mutable_summary.mixed_color ||
                                !same_color_value(first.color, current->color);
                            mutable_summary.mixed_shadow_blend_mode =
                                mutable_summary.mixed_shadow_blend_mode ||
                                first.blend_mode != current->blend_mode;
                        }
                    }, first_parameters);
                }
            }
        }
        if (!result.all_path_text)
            result.effect_stack_compatible = false;
        ++result.supported_count;
    }
    if (result.supported_count == 0)
        result.effect_stack_compatible = false;
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
    if (const auto profile = value.profile_resource()) {
        text << " · ICC " << profile->identity().name;
    }
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
