#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include "../clipboard/native_clipboard.hpp"
#include "../utf8_path.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
using namespace detail;

namespace {

class SetObjectFillCommand final : public document::DocumentCommand {
public:
    SetObjectFillCommand(std::vector<document::ObjectId> targets, color::ColorValue fill)
        : targets_(std::move(targets)), fill_(std::move(fill)) {}

    std::string_view name() const noexcept override { return "Set Fill"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("Already executed");
        before_objects_ = doc.objects();
        auto objects = doc.objects();
        bool changed = false;
        for (const auto id : targets_) {
            const auto opt = objects.get(id);
            if (!opt) continue;
            auto updated = *opt;
            if (auto* path = std::get_if<document::PathObject>(&updated)) {
                path->fill = fill_;
                path->has_fill = true;
                path->fill_gradient.reset();
                objects = objects.set(id, std::move(updated));
                changed = true;
            } else if (auto* text = std::get_if<document::TextObject>(&updated)) {
                text->fill = fill_;
                objects = objects.set(id, std::move(updated));
                changed = true;
            }
        }
        if (!changed) return document::CommandResult::no_change();
        doc.apply_mutation(doc.page(), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_objects_) return document::CommandResult::fail("No state to undo");
        doc.apply_mutation(doc.page(), *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> targets_;
    color::ColorValue fill_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class SetObjectStrokeCommand final : public document::DocumentCommand {
public:
    SetObjectStrokeCommand(std::vector<document::ObjectId> targets, color::ColorValue stroke, double width_mm)
        : targets_(std::move(targets)), stroke_(std::move(stroke)), width_mm_(width_mm) {}

    std::string_view name() const noexcept override { return "Set Stroke"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("Already executed");
        before_objects_ = doc.objects();
        auto objects = doc.objects();
        bool changed = false;
        for (const auto id : targets_) {
            const auto opt = objects.get(id);
            if (!opt) continue;
            auto updated = *opt;
            if (auto* path = std::get_if<document::PathObject>(&updated)) {
                path->stroke = stroke_;
                path->stroke_width_mm = width_mm_;
                path->has_stroke = true;
                objects = objects.set(id, std::move(updated));
                changed = true;
            }
        }
        if (!changed) return document::CommandResult::no_change();
        doc.apply_mutation(doc.page(), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_objects_) return document::CommandResult::fail("No state to undo");
        doc.apply_mutation(doc.page(), *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> targets_;
    color::ColorValue stroke_;
    double width_mm_{0.0};
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

} // namespace

bool DocumentWorkflow::set_selected_fill(const std::string& hex_color) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return false;
    const auto& objects = snapshot->objects();

    std::vector<document::ObjectId> targets;
    for (const auto id : ids) {
        targets.push_back(id);
        collect_all_descendants(id, objects, targets);
    }

    auto cmd = std::make_unique<SetObjectFillCommand>(std::move(targets), col);
    auto res = doc.execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::set_selected_stroke(const std::string& hex_color, double stroke_width_mm) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return false;
    const auto& objects = snapshot->objects();

    std::vector<document::ObjectId> targets;
    for (const auto id : ids) {
        targets.push_back(id);
        collect_all_descendants(id, objects, targets);
    }

    auto cmd = std::make_unique<SetObjectStrokeCommand>(std::move(targets), col, stroke_width_mm);
    auto res = doc.execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::can_set_selected_object_opacity() const {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        if (std::visit([](const auto& value) { return value.locked; }, *object)) return false;
        if (!std::holds_alternative<document::PathObject>(*object) &&
            !std::holds_alternative<document::TextObject>(*object))
            return false;
    }
    return true;
}

bool DocumentWorkflow::set_selected_object_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 || normalized_opacity > 1.0 ||
        !can_set_selected_object_opacity())
        return false;

    const auto ids = root_.vector_controller().selection().selected_ids();
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::AppearanceReplacement> replacements;
    replacements.reserve(ids.size());
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        document::Appearance appearance;
        if (const auto* path = std::get_if<document::PathObject>(&*object))
            appearance = path->appearance;
        else if (const auto* text = std::get_if<document::TextObject>(&*object))
            appearance = text->appearance;
        else
            return false;
        appearance.object_opacity = normalized_opacity;
        replacements.push_back({id, std::move(appearance)});
    }

    const auto result = root_.document().execute_command(
        std::make_unique<document::SetAppearanceCommand>(std::move(replacements)));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::can_set_selected_fill_opacity() const {
    return can_edit_selected_appearance();
}

bool DocumentWorkflow::set_selected_fill_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_set_selected_fill_opacity())
        return false;
    return update_selected_appearance([normalized_opacity](document::Appearance &appearance) {
        appearance.fill_opacity = normalized_opacity;
        return true;
    });
}

bool DocumentWorkflow::can_set_selected_stroke_opacity() const {
    const auto &ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        const auto *path = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        if (!path || path->locked) return false;
    }
    return true;
}

bool DocumentWorkflow::set_selected_stroke_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_set_selected_stroke_opacity())
        return false;
    return update_selected_appearance([normalized_opacity](document::Appearance &appearance) {
        appearance.stroke_opacity = normalized_opacity;
        return true;
    });
}

bool DocumentWorkflow::can_edit_selected_appearance() const {
    const auto &ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object || std::visit([](const auto &value) { return value.locked; }, *object) ||
            (!std::holds_alternative<document::PathObject>(*object) &&
             !std::holds_alternative<document::TextObject>(*object)))
            return false;
    }
    return true;
}

bool DocumentWorkflow::can_add_selected_effect() const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || appearance->effects.size() >= document::kMaxEffectsPerObject ||
            std::any_of(appearance->effects.begin(), appearance->effects.end(),
                [](const document::EffectNode &node) {
                    const auto *shadow = std::get_if<document::DropShadowEffect>(
                        &node.parameters);
                    return node.enabled && shadow &&
                           shadow->blend_mode != document::BlendMode::Normal;
                }))
            return false;
    }
    return true;
}

bool DocumentWorkflow::can_add_selected_effect(
    const document::EffectParameters& parameters) const {
    if (!valid_effect_parameters(parameters) ||
        !root_.renderer().backend().supports_effect(parameters) ||
        !can_add_selected_effect()) return false;
    if (!std::holds_alternative<document::DropShadowEffect>(parameters)) return true;

    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || std::any_of(appearance->effects.begin(), appearance->effects.end(),
                [](const document::EffectNode& node) {
                    return std::holds_alternative<document::DropShadowEffect>(node.parameters);
                }))
            return false;
    }
    return true;
}

std::optional<size_t> DocumentWorkflow::selected_effect_index(
    const document::EffectParameters& parameters) const {
    if (!can_edit_selected_appearance()) return std::nullopt;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return std::nullopt;

    std::optional<size_t> shared_index;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance) return std::nullopt;

        const auto found = std::find_if(appearance->effects.begin(), appearance->effects.end(),
            [&parameters](const document::EffectNode& node) {
                return node.parameters.index() == parameters.index();
            });
        if (found == appearance->effects.end()) return std::nullopt;
        const size_t index = static_cast<size_t>(found - appearance->effects.begin());
        if (shared_index && *shared_index != index) return std::nullopt;
        shared_index = index;
    }
    if (!shared_index || !can_edit_selected_effect(*shared_index)) return std::nullopt;
    return shared_index;
}

bool DocumentWorkflow::can_activate_selected_effect(
    const document::EffectParameters& parameters) const {
    if (!valid_effect_parameters(parameters) || !can_edit_selected_appearance()) return false;
    if (const auto selected = selected_effect_index(parameters)) {
        const auto snapshot = root_.document().current_snapshot();
        if (!snapshot) return false;
        for (const auto id : root_.vector_controller().selection().selected_ids()) {
            const auto object = snapshot->objects().get(id);
            const auto* appearance = object ? std::visit(
                [](const auto& value) -> const document::Appearance* {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, document::PathObject> ||
                                  std::is_same_v<T, document::TextObject>)
                        return &value.appearance;
                    return nullptr;
                }, *object) : nullptr;
            if (!appearance || *selected >= appearance->effects.size() ||
                !root_.renderer().backend().supports_effect(
                    appearance->effects[*selected].parameters))
                return false;
        }
        return true;
    }

    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    bool matching_effect_found = false;
    std::optional<std::vector<std::tuple<uint32_t, uint32_t, size_t>>> reference_shape;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance) return false;

        matching_effect_found = matching_effect_found || std::any_of(
            appearance->effects.begin(), appearance->effects.end(),
            [&parameters](const document::EffectNode& node) {
                return node.parameters.index() == parameters.index();
            });
        std::vector<std::tuple<uint32_t, uint32_t, size_t>> shape;
        shape.reserve(appearance->effects.size());
        for (const auto& node : appearance->effects)
            shape.emplace_back(node.schema_version, node.schema_minor, node.parameters.index());
        if (!reference_shape) reference_shape = std::move(shape);
        else if (*reference_shape != shape) return false;
    }
    return !matching_effect_found && can_add_selected_effect(parameters);
}

DocumentWorkflow::SelectedEffectActivation DocumentWorkflow::activate_selected_effect(
    document::EffectParameters parameters, size_t& effect_index) {
    if (!can_activate_selected_effect(parameters))
        return SelectedEffectActivation::Unavailable;
    if (const auto existing = selected_effect_index(parameters)) {
        effect_index = *existing;
        return SelectedEffectActivation::Existing;
    }

    const auto snapshot = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (!snapshot || ids.empty()) return SelectedEffectActivation::Unavailable;
    const auto object = snapshot->objects().get(ids.front());
    const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject> ||
                      std::is_same_v<T, document::TextObject>)
            return &value.appearance;
        return nullptr;
    }, *object) : nullptr;
    if (!appearance) return SelectedEffectActivation::Unavailable;

    effect_index = appearance->effects.size();
    if (!add_selected_effect(std::move(parameters)))
        return SelectedEffectActivation::Unavailable;
    return SelectedEffectActivation::Added;
}



bool DocumentWorkflow::can_clear_selected_effects() const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (appearance && !appearance->effects.empty()) return true;
    }
    return false;
}

bool DocumentWorkflow::can_edit_selected_effect(size_t effect_index) const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    std::vector<std::tuple<uint32_t, uint32_t, size_t>> reference_shape;
    bool have_reference = false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size()) return false;
        std::vector<std::tuple<uint32_t, uint32_t, size_t>> current_shape;
        current_shape.reserve(appearance->effects.size());
        for (const auto &node : appearance->effects)
            current_shape.emplace_back(node.schema_version, node.schema_minor,
                                        node.parameters.index());
        if (!have_reference) {
            reference_shape = std::move(current_shape);
            have_reference = true;
        } else if (current_shape != reference_shape) {
            return false;
        }
    }
    return have_reference;
}

bool DocumentWorkflow::update_selected_appearance(
    const std::function<bool(document::Appearance &)> &update) {
    if (!update || !can_edit_selected_appearance()) return false;
    const auto ids = root_.vector_controller().selection().selected_ids();
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::AppearanceReplacement> replacements;
    replacements.reserve(ids.size());
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        document::Appearance appearance;
        if (const auto *path = std::get_if<document::PathObject>(&*object))
            appearance = path->appearance;
        else if (const auto *text = std::get_if<document::TextObject>(&*object))
            appearance = text->appearance;
        else
            return false;
        if (!update(appearance)) return false;
        replacements.push_back({id, std::move(appearance)});
    }
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetAppearanceCommand>(std::move(replacements)));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_selected_blend_mode(document::BlendMode mode) {
    switch (mode) {
    case document::BlendMode::Normal:
    case document::BlendMode::Multiply:
    case document::BlendMode::Screen:
    case document::BlendMode::Overlay:
    case document::BlendMode::Darken:
    case document::BlendMode::Lighten:
        break;
    default:
        return false;
    }
    return update_selected_appearance([mode](document::Appearance &appearance) {
        appearance.blend_mode = mode;
        return true;
    });
}

bool DocumentWorkflow::add_selected_effect(document::EffectParameters parameters) {
    if (!can_add_selected_effect(parameters)) return false;
    return update_selected_appearance([parameters = std::move(parameters)](
                                          document::Appearance &appearance) {
        document::EffectNode node;
        node.id = document::generate_effect_instance_id();
        node.parameters = parameters;
        node.schema_minor = document::effect_node_schema_minor(node.parameters);
        appearance.effects.push_back(std::move(node));
        return true;
    });
}

bool DocumentWorkflow::clear_selected_effects() {
    if (!can_clear_selected_effects()) return false;
    return update_selected_appearance([](document::Appearance &appearance) {
        if (appearance.effects.empty()) return true;
        appearance.effects.clear();
        return true;
    });
}

bool DocumentWorkflow::can_clear_all_effects() const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        if (!object) continue;
        if (const auto *path = std::get_if<document::PathObject>(&*object)) {
            if (!path->appearance.effects.empty() || path->contour_spec.starts_with("contour:"))
                return true;
        } else if (const auto *text = std::get_if<document::TextObject>(&*object)) {
            if (!text->appearance.effects.empty())
                return true;
        }
    }
    return false;
}

bool DocumentWorkflow::clear_all_effects() {
    if (!can_clear_all_effects()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    // 1. Remove contour on paths with contour
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        if (!object) continue;
        if (const auto *path = std::get_if<document::PathObject>(&*object)) {
            if (path->contour_spec.starts_with("contour:")) {
                cmds.push_back(std::make_unique<document::RemoveContourCommand>(id));
            }
        }
    }
    // 2. Clear appearance effects
    std::vector<document::AppearanceReplacement> replacements;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        if (!object) continue;
        document::Appearance appearance;
        if (const auto *path = std::get_if<document::PathObject>(&*object))
            appearance = path->appearance;
        else if (const auto *text = std::get_if<document::TextObject>(&*object))
            appearance = text->appearance;
        else
            continue;
        if (!appearance.effects.empty()) {
            appearance.effects.clear();
            replacements.push_back({id, std::move(appearance)});
        }
    }
    if (!replacements.empty()) {
        cmds.push_back(std::make_unique<document::SetAppearanceCommand>(std::move(replacements)));
    }
    if (cmds.empty()) return false;
    bool ok = false;
    if (cmds.size() == 1) {
        ok = root_.document().execute_command(std::move(cmds.front())).success;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Clear All Effects");
        for (auto &cmd : cmds) composite->add_command(std::move(cmd));
        ok = root_.document().execute_command(std::move(composite)).success;
    }
    sync_status_bar();
    return ok;
}

bool DocumentWorkflow::remove_selected_effect(size_t effect_index) {
    if (!can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index](document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        appearance.effects.erase(appearance.effects.begin() + static_cast<std::ptrdiff_t>(effect_index));
        return true;
    });
}

bool DocumentWorkflow::can_move_selected_effect(size_t effect_index,
                                                int direction) const {
    if ((direction != -1 && direction != 1) ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit(
            [](const auto &value) -> const document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size())
            return false;
        auto reordered = *appearance;
        const auto destination = static_cast<std::ptrdiff_t>(effect_index) + direction;
        if (destination < 0 ||
            destination >= static_cast<std::ptrdiff_t>(reordered.effects.size()))
            return false;
        std::swap(reordered.effects[effect_index],
                  reordered.effects[static_cast<size_t>(destination)]);
        try {
            document::validate_appearance(reordered);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool DocumentWorkflow::move_selected_effect(size_t effect_index, int direction) {
    if (!can_move_selected_effect(effect_index, direction)) return false;
    return update_selected_appearance([effect_index, direction](document::Appearance &appearance) {
        const auto destination = static_cast<std::ptrdiff_t>(effect_index) + direction;
        if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(appearance.effects.size()))
            return false;
        std::swap(appearance.effects[effect_index], appearance.effects[static_cast<size_t>(destination)]);
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_enabled(size_t effect_index, bool enabled) {
    if (!can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index, enabled](document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        appearance.effects[effect_index].enabled = enabled;
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_sigma(size_t effect_index, bool x_axis,
                                                  double sigma_mm) {
    if (!std::isfinite(sigma_mm) || sigma_mm < 0.0 ||
        sigma_mm > geometry::limits::kCanvasMaxMm ||
        !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, x_axis, sigma_mm](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &parameters = appearance.effects[effect_index].parameters;
        if (auto *blur = std::get_if<document::GaussianBlurEffect>(&parameters)) {
            (x_axis ? blur->sigma_x_mm : blur->sigma_y_mm) = sigma_mm;
            return true;
        }
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&parameters)) {
            (x_axis ? shadow->sigma_x_mm : shadow->sigma_y_mm) = sigma_mm;
            return true;
        }
        if (auto *feather = std::get_if<document::FeatherEffect>(&parameters)) {
            feather->radius_mm = sigma_mm;
            return true;
        }
        if (auto *outer_glow = std::get_if<document::OuterGlowEffect>(&parameters)) {
            outer_glow->radius_mm = sigma_mm;
            return true;
        }
        if (auto *inner_glow = std::get_if<document::InnerGlowEffect>(&parameters)) {
            inner_glow->radius_mm = sigma_mm;
            return true;
        }
        if (auto *inner_shadow = std::get_if<document::InnerShadowEffect>(&parameters)) {
            (x_axis ? inner_shadow->sigma_x_mm : inner_shadow->sigma_y_mm) = sigma_mm;
            return true;
        }
        return false;
    });
}

bool DocumentWorkflow::set_selected_effect_offset(size_t effect_index, bool x_axis,
                                                   double offset_mm) {
    if (!std::isfinite(offset_mm) || std::abs(offset_mm) > geometry::limits::kCanvasMaxMm ||
        !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, x_axis, offset_mm](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &parameters = appearance.effects[effect_index].parameters;
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&parameters)) {
            (x_axis ? shadow->dx_mm : shadow->dy_mm) = offset_mm;
            return true;
        }
        if (auto *inner_shadow = std::get_if<document::InnerShadowEffect>(&parameters)) {
            (x_axis ? inner_shadow->dx_mm : inner_shadow->dy_mm) = offset_mm;
            return true;
        }
        return false;
    });
}

bool DocumentWorkflow::set_selected_effect_parameters(
    size_t effect_index, document::EffectParameters parameters) {
    if (!valid_effect_parameters(parameters) ||
        !root_.renderer().backend().supports_effect(parameters) ||
        !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, parameters = std::move(parameters)](
                                          document::Appearance& appearance) {
        if (effect_index >= appearance.effects.size() ||
            appearance.effects[effect_index].parameters.index() != parameters.index())
            return false;
        if (parameters.index() >= 2) {
            appearance.effects[effect_index].schema_minor =
                document::kExtendedEffectNodeSchemaMinor;
        } else if (const auto *shadow = std::get_if<document::DropShadowEffect>(&parameters);
            shadow && shadow->blend_mode != document::BlendMode::Normal) {
            appearance.effects[effect_index].schema_minor =
                document::kDropShadowBlendSchemaMinor;
        }
        appearance.effects[effect_index].parameters = parameters;
        return true;
    });
}

bool DocumentWorkflow::can_set_selected_effect_blend_mode(
    size_t effect_index, document::BlendMode mode) const {
    if (static_cast<uint8_t>(mode) >
            static_cast<uint8_t>(document::BlendMode::Lighten) ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit(
            [](const auto &value) -> const document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size())
            return false;
        const auto &params = appearance->effects[effect_index].parameters;
        const bool has_blend = std::holds_alternative<document::DropShadowEffect>(params) ||
                               std::holds_alternative<document::InnerShadowEffect>(params) ||
                               std::holds_alternative<document::ColorOverlayEffect>(params);
        if (!has_blend)
            return false;
        auto candidate = params;
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&candidate))
            shadow->blend_mode = mode;
        else if (auto *shadow = std::get_if<document::InnerShadowEffect>(&candidate))
            shadow->blend_mode = mode;
        else if (auto *overlay = std::get_if<document::ColorOverlayEffect>(&candidate))
            overlay->blend_mode = mode;
        if (!root_.renderer().backend().supports_effect(candidate))
            return false;
        if (mode != document::BlendMode::Normal &&
            appearance->effects[effect_index].enabled &&
            std::holds_alternative<document::DropShadowEffect>(params)) {
            for (size_t later = effect_index + 1;
                 later < appearance->effects.size(); ++later) {
                if (appearance->effects[later].enabled) return false;
            }
        }
    }
    return true;
}

bool DocumentWorkflow::set_selected_effect_blend_mode(
    size_t effect_index, document::BlendMode mode) {
    if (!can_set_selected_effect_blend_mode(effect_index, mode)) return false;
    return update_selected_appearance([effect_index, mode](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &node = appearance.effects[effect_index];
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&node.parameters)) {
            shadow->blend_mode = mode;
            if (mode != document::BlendMode::Normal ||
                node.schema_minor == document::kDropShadowBlendSchemaMinor)
                node.schema_minor = document::kDropShadowBlendSchemaMinor;
            return true;
        }
        if (auto *inner_shadow = std::get_if<document::InnerShadowEffect>(&node.parameters)) {
            inner_shadow->blend_mode = mode;
            return true;
        }

        if (auto *overlay = std::get_if<document::ColorOverlayEffect>(&node.parameters)) {
            overlay->blend_mode = mode;
            return true;
        }
        return false;
    });
}

bool DocumentWorkflow::set_selected_effect_opacity(size_t effect_index,
                                                    double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, normalized_opacity](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &params = appearance.effects[effect_index].parameters;
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&params)) {
            shadow->opacity = normalized_opacity;
            return true;
        }
        if (auto *inner_shadow = std::get_if<document::InnerShadowEffect>(&params)) {
            inner_shadow->opacity = normalized_opacity;
            return true;
        }
        if (auto *outer_glow = std::get_if<document::OuterGlowEffect>(&params)) {
            outer_glow->opacity = normalized_opacity;
            return true;
        }
        if (auto *inner_glow = std::get_if<document::InnerGlowEffect>(&params)) {
            inner_glow->opacity = normalized_opacity;
            return true;
        }
        if (auto *overlay = std::get_if<document::ColorOverlayEffect>(&params)) {
            overlay->opacity = normalized_opacity;
            return true;
        }
        return false;
    });
}

bool DocumentWorkflow::set_selected_effect_color(size_t effect_index,
                                                 color::ColorValue color) {
    if (!color.is_valid() || !can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index, color = std::move(color)](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &params = appearance.effects[effect_index].parameters;
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&params)) {
            shadow->color = color;
            return true;
        }
        if (auto *inner_shadow = std::get_if<document::InnerShadowEffect>(&params)) {
            inner_shadow->color = color;
            return true;
        }
        if (auto *outer_glow = std::get_if<document::OuterGlowEffect>(&params)) {
            outer_glow->color = color;
            return true;
        }
        if (auto *inner_glow = std::get_if<document::InnerGlowEffect>(&params)) {
            inner_glow->color = color;
            return true;
        }
        if (auto *overlay = std::get_if<document::ColorOverlayEffect>(&params)) {
            overlay->color = color;
            return true;
        }
        return false;
    });
}


} // namespace pikdraw::desktop
