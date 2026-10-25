#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/document/commands/raster_edit_commands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace pikdraw::desktop {

document::RasterLightParams NativeWindow::selected_bitmap_light() const {
    document::RasterLightParams parameters{};
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return parameters;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return parameters;
    bool found_light = false;
    bool found_exposure = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (!found_light) {
            if (const auto* light = std::get_if<document::RasterLightParams>(
                    &node->parameters)) {
                const double exposure_before_light = parameters.exposure_ev;
                parameters = *light;
                if (found_exposure)
                    parameters.exposure_ev = exposure_before_light;
                found_light = true;
                found_exposure = true;
            }
        }
        if (const auto* exposure = std::get_if<document::RasterExposureParams>(
                &node->parameters); exposure && !found_exposure) {
            parameters.exposure_ev = exposure->exposure_ev;
            found_exposure = true;
        }
        if (found_light && found_exposure) break;
    }
    return parameters;
}

bool NativeWindow::selected_bitmap_has_light_node() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    return image && std::any_of(image->edit_graph.nodes.begin(),
        image->edit_graph.nodes.end(), [](const auto& node) {
            return std::holds_alternative<document::RasterLightParams>(
                node.parameters);
        });
}

bool NativeWindow::set_selected_bitmap_light_value(NumericFieldId field,
                                                    double value) {
    if (!std::isfinite(value) || std::abs(value) > 100.0 ||
        !can_edit_selected_bitmap(true))
        return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    auto parameters = selected_bitmap_light();
    switch (field) {
    case NumericFieldId::RasterLightContrast:
        parameters.contrast_percent = value;
        break;
    case NumericFieldId::RasterLightHighlights:
        parameters.highlights_percent = value;
        break;
    case NumericFieldId::RasterLightShadows:
        parameters.shadows_percent = value;
        break;
    case NumericFieldId::RasterLightWhites:
        parameters.whites_percent = value;
        break;
    case NumericFieldId::RasterLightBlacks:
        parameters.blacks_percent = value;
        break;
    default:
        return false;
    }

    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (std::holds_alternative<document::RasterLightParams>(node->parameters)) {
            updated = *node;
            updated.parameters = parameters;
            found = true;
            break;
        }
    }
    if (!found) {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{
            {maximum_id + 1}, true, parameters};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

document::RasterHslParams NativeWindow::selected_bitmap_hsl() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return {};
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return {};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* hsl = std::get_if<document::RasterHslParams>(
                &node->parameters))
            return *hsl;
    }
    return {};
}

document::RasterLevelsParams NativeWindow::selected_bitmap_levels() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return {};
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return {};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* levels = std::get_if<document::RasterLevelsParams>(
                &node->parameters))
            return *levels;
    }
    return {};
}

bool NativeWindow::set_selected_bitmap_levels_value(NumericFieldId field,
                                                      double value) {
    if (!std::isfinite(value) || !can_edit_selected_bitmap(true)) return false;
    auto parameters = selected_bitmap_levels();
    switch (field) {
    case NumericFieldId::RasterLevelsBlackIn:
        if (value < 0.0 || value > 100.0) return false;
        parameters.black_in = value / 100.0;
        break;
    case NumericFieldId::RasterLevelsWhiteIn:
        if (value < 0.0 || value > 100.0) return false;
        parameters.white_in = value / 100.0;
        break;
    case NumericFieldId::RasterLevelsGamma:
        if (value < 0.1 || value > 10.0) return false;
        parameters.gamma = value;
        break;
    case NumericFieldId::RasterLevelsBlackOut:
        if (value < 0.0 || value > 100.0) return false;
        parameters.black_out = value / 100.0;
        break;
    case NumericFieldId::RasterLevelsWhiteOut:
        if (value < 0.0 || value > 100.0) return false;
        parameters.white_out = value / 100.0;
        break;
    default:
        return false;
    }
    if (!(parameters.black_in < parameters.white_in) ||
        !(parameters.black_out < parameters.white_out))
        return false;

    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (!std::holds_alternative<document::RasterLevelsParams>(
                node->parameters))
            continue;
        updated = *node;
        updated.parameters = parameters;
        found = true;
        break;
    }
    if (!found) {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{
            {maximum_id + 1}, true, parameters};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

bool NativeWindow::set_selected_bitmap_levels_channel(
    document::RasterToneChannel channel) {
    if (static_cast<uint8_t>(channel) >
            static_cast<uint8_t>(document::RasterToneChannel::Blue) ||
        !can_edit_selected_bitmap(true))
        return false;
    auto parameters = selected_bitmap_levels();
    parameters.channel = channel;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (!std::holds_alternative<document::RasterLevelsParams>(
                node->parameters))
            continue;
        updated = *node;
        updated.parameters = parameters;
        found = true;
        break;
    }
    if (!found) {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{
            {maximum_id + 1}, true, parameters};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

document::RasterWhiteBalanceParams NativeWindow::selected_bitmap_white_balance() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return {};
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return {};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* white_balance =
                std::get_if<document::RasterWhiteBalanceParams>(
                    &node->parameters))
            return *white_balance;
    }
    return {};
}

bool NativeWindow::set_selected_bitmap_white_balance_value(
    NumericFieldId field, double value) {
    if (!std::isfinite(value) || value < -100.0 || value > 100.0 ||
        !can_edit_selected_bitmap(true))
        return false;
    auto parameters = selected_bitmap_white_balance();
    if (field == NumericFieldId::RasterWhiteBalanceTemperature)
        parameters.temperature_shift_percent = value;
    else if (field == NumericFieldId::RasterWhiteBalanceTint)
        parameters.tint_percent = value;
    else
        return false;

    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (!std::holds_alternative<document::RasterWhiteBalanceParams>(
                node->parameters))
            continue;
        updated = *node;
        updated.parameters = parameters;
        found = true;
        break;
    }
    if (!found) {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{
            {maximum_id + 1}, true, parameters};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

document::RasterCurvesParams NativeWindow::selected_bitmap_curves() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return {};
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return {};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* curves = std::get_if<document::RasterCurvesParams>(
                &node->parameters))
            return *curves;
    }
    return {};
}

bool NativeWindow::set_selected_bitmap_curves(
    document::RasterCurvesParams parameters) {
    if (!can_edit_selected_bitmap(true) ||
        static_cast<uint8_t>(parameters.channel) >
            static_cast<uint8_t>(document::RasterToneChannel::Blue) ||
        std::any_of(parameters.output_points.begin(),
            parameters.output_points.end(), [](double point) {
                return !std::isfinite(point) || point < 0.0 || point > 1.0;
            }))
        return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (!std::holds_alternative<document::RasterCurvesParams>(
                node->parameters))
            continue;
        updated = *node;
        updated.parameters = parameters;
        found = true;
        break;
    }
    if (!found) {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{
            {maximum_id + 1}, true, parameters};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

bool NativeWindow::set_selected_bitmap_curve_point(NumericFieldId field,
                                                    double value) {
    if (!std::isfinite(value) || value < 0.0 || value > 100.0) return false;
    auto parameters = selected_bitmap_curves();
    size_t point = 0;
    switch (field) {
    case NumericFieldId::RasterCurvePoint0: point = 0; break;
    case NumericFieldId::RasterCurvePoint1: point = 1; break;
    case NumericFieldId::RasterCurvePoint2: point = 2; break;
    case NumericFieldId::RasterCurvePoint3: point = 3; break;
    case NumericFieldId::RasterCurvePoint4: point = 4; break;
    default: return false;
    }
    parameters.output_points[point] = value / 100.0;
    return set_selected_bitmap_curves(parameters);
}

bool NativeWindow::set_selected_bitmap_curves_channel(
    document::RasterToneChannel channel) {
    auto parameters = selected_bitmap_curves();
    parameters.channel = channel;
    return set_selected_bitmap_curves(parameters);
}

double NativeWindow::selected_bitmap_vibrance_amount() const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return 0.0;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return 0.0;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* vibrance = std::get_if<document::RasterVibranceParams>(
                &node->parameters))
            return vibrance->amount_percent;
    }
    return 0.0;
}

bool NativeWindow::set_selected_bitmap_color_adjustment(NumericFieldId field,
                                                         double value) {
    if (!std::isfinite(value) || !can_edit_selected_bitmap(true)) return false;
    const bool hue = field == NumericFieldId::RasterHslHue;
    if ((hue && (value < -180.0 || value > 180.0)) ||
        (!hue && value < -100.0) || (!hue && value > 100.0))
        return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    document::RasterEditNode updated;
    bool found = false;
    if (field == NumericFieldId::RasterVibranceAmount) {
        document::RasterVibranceParams parameters{
            selected_bitmap_vibrance_amount()};
        parameters.amount_percent = value;
        for (auto node = image->edit_graph.nodes.rbegin();
             node != image->edit_graph.nodes.rend(); ++node) {
            if (!std::holds_alternative<document::RasterVibranceParams>(
                    node->parameters))
                continue;
            updated = *node;
            updated.parameters = parameters;
            found = true;
            break;
        }
        if (!found) {
            uint64_t maximum_id = 0;
            for (const auto& node : image->edit_graph.nodes)
                maximum_id = (std::max)(maximum_id, node.id.value);
            if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
            updated = document::RasterEditNode{
                {maximum_id + 1}, true, parameters};
        }
    } else {
        auto parameters = selected_bitmap_hsl();
        if (field == NumericFieldId::RasterHslHue)
            parameters.hue_degrees = value;
        else if (field == NumericFieldId::RasterHslSaturation)
            parameters.saturation_percent = value;
        else if (field == NumericFieldId::RasterHslLightness)
            parameters.lightness_percent = value;
        else
            return false;
        for (auto node = image->edit_graph.nodes.rbegin();
             node != image->edit_graph.nodes.rend(); ++node) {
            if (!std::holds_alternative<document::RasterHslParams>(
                    node->parameters))
                continue;
            updated = *node;
            updated.parameters = parameters;
            found = true;
            break;
        }
        if (!found) {
            uint64_t maximum_id = 0;
            for (const auto& node : image->edit_graph.nodes)
                maximum_id = (std::max)(maximum_id, node.id.value);
            if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
            updated = document::RasterEditNode{
                {maximum_id + 1}, true, parameters};
        }
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

} // namespace pikdraw::desktop
