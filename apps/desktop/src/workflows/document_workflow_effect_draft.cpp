#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
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

bool DocumentWorkflow::has_effect_draft() const noexcept {
    return effect_draft_.has_value();
}

std::optional<DocumentWorkflow::EffectDraftView> DocumentWorkflow::effect_draft() const {
    if (!effect_draft_) return std::nullopt;
    return EffectDraftView{effect_draft_->parameters, effect_draft_->preview_enabled,
                           effect_draft_->edit_effect_index.has_value(),
                           effect_draft_->active_field, effect_draft_->numeric_buffer};
}

bool DocumentWorkflow::effect_draft_is_current() const {
    if (!effect_draft_) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->document_id() != effect_draft_->document_id ||
        snapshot->epoch() != effect_draft_->epoch ||
        snapshot->revision() != effect_draft_->revision ||
        root_.vector_controller().selection().selected_ids() != effect_draft_->target_ids)
        return false;
    if (!effect_draft_->edit_effect_index)
        return can_add_selected_effect(effect_draft_->parameters);
    if (!can_edit_selected_effect(*effect_draft_->edit_effect_index)) return false;
    for (const auto id : effect_draft_->target_ids) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || *effect_draft_->edit_effect_index >= appearance->effects.size() ||
            appearance->effects[*effect_draft_->edit_effect_index].parameters.index() !=
                effect_draft_->parameters.index())
            return false;
    }
    return true;
}

bool DocumentWorkflow::cancel_stale_effect_draft() {
    if (!effect_draft_ || effect_draft_is_current()) return false;
    effect_draft_.reset();
    return true;
}

bool DocumentWorkflow::begin_selected_effect_draft(document::EffectParameters parameters) {
    if (effect_draft_ || !valid_effect_parameters(parameters) ||
        !can_add_selected_effect(parameters))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    EffectDraftState draft;
    draft.parameters = std::move(parameters);
    draft.document_id = snapshot->document_id();
    draft.epoch = snapshot->epoch();
    draft.revision = snapshot->revision();
    draft.target_ids = root_.vector_controller().selection().selected_ids();
    if (draft.target_ids.empty()) return false;
    effect_draft_ = std::move(draft);
    return true;
}

bool DocumentWorkflow::begin_selected_effect_edit_draft(size_t effect_index) {
    if (effect_draft_ || root_.vector_controller().selection().selected_ids().size() != 1 ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (!snapshot || ids.empty()) return false;
    const auto object = snapshot->objects().get(ids.front());
    const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject> ||
                      std::is_same_v<T, document::TextObject>)
            return &value.appearance;
        return nullptr;
    }, *object) : nullptr;
    if (!appearance || effect_index >= appearance->effects.size()) return false;

    EffectDraftState draft;
    draft.parameters = appearance->effects[effect_index].parameters;
    if (!valid_effect_parameters(draft.parameters)) return false;
    draft.document_id = snapshot->document_id();
    draft.epoch = snapshot->epoch();
    draft.revision = snapshot->revision();
    draft.target_ids = ids;
    draft.edit_effect_index = effect_index;
    effect_draft_ = std::move(draft);
    return true;
}

bool DocumentWorkflow::rebuild_effect_draft_preview() {
    if (!effect_draft_ || !effect_draft_is_current() ||
        !valid_effect_parameters(effect_draft_->parameters))
        return false;
    const auto source = root_.document().current_snapshot();
    if (!source) return false;
    auto objects = source->objects();
    try {
        for (const auto id : effect_draft_->target_ids) {
            auto object = source->objects().get(id);
            if (!object) return false;
            auto *appearance = std::visit([](auto &value) -> document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object);
            if (!appearance) return false;
            if (effect_draft_->edit_effect_index) {
                const size_t index = *effect_draft_->edit_effect_index;
                if (index >= appearance->effects.size() ||
                    appearance->effects[index].parameters.index() != effect_draft_->parameters.index())
                    return false;
                appearance->effects[index].parameters = effect_draft_->parameters;
            } else {
                if (appearance->effects.size() >= document::kMaxEffectsPerObject) return false;
                document::EffectNode preview_node;
                preview_node.id = document::generate_effect_instance_id();
                preview_node.parameters = effect_draft_->parameters;
                appearance->effects.push_back(std::move(preview_node));
            }
            objects = objects.set(id, std::move(*object));
        }
        effect_draft_->preview_snapshot = std::make_shared<const document::DocumentSnapshot>(
            source->epoch(), source->revision(), source->page(), std::move(objects),
            source->document_id());
        return true;
    } catch (const std::exception &) {
        effect_draft_->preview_snapshot.reset();
        return false;
    }
}

bool DocumentWorkflow::set_effect_draft_parameters(document::EffectParameters parameters) {
    if (!effect_draft_ || !effect_draft_is_current() ||
        parameters.index() != effect_draft_->parameters.index() ||
        !valid_effect_parameters(parameters))
        return false;
    effect_draft_->parameters = std::move(parameters);
    effect_draft_->preview_snapshot.reset();
    return !effect_draft_->preview_enabled || rebuild_effect_draft_preview();
}

bool DocumentWorkflow::set_effect_draft_preview(bool enabled) {
    if (!effect_draft_ || !effect_draft_is_current()) return false;
    if (!enabled) {
        effect_draft_->preview_enabled = false;
        effect_draft_->preview_snapshot.reset();
        return true;
    }
    if (!rebuild_effect_draft_preview()) return false;
    effect_draft_->preview_enabled = true;
    return true;
}

bool DocumentWorkflow::apply_effect_draft() {
    if (!effect_draft_) return false;
    if (effect_draft_->active_field != EffectDraftField::None &&
        !commit_effect_draft_numeric_input())
        return false;
    if (!effect_draft_is_current()) {
        effect_draft_.reset();
        return false;
    }
    const auto parameters = effect_draft_->parameters;
    const bool applied = effect_draft_->edit_effect_index
        ? set_selected_effect_parameters(*effect_draft_->edit_effect_index, parameters)
        : add_selected_effect(parameters);
    if (!applied) return false;
    effect_draft_.reset();
    return true;
}

void DocumentWorkflow::cancel_effect_draft() noexcept {
    effect_draft_.reset();
}

std::vector<DocumentWorkflow::EffectDraftField>
DocumentWorkflow::effect_draft_numeric_fields() const {
    if (!effect_draft_) return {};
    if (std::holds_alternative<document::GaussianBlurEffect>(effect_draft_->parameters))
        return {EffectDraftField::SigmaX, EffectDraftField::SigmaY};
    return {EffectDraftField::OffsetX, EffectDraftField::OffsetY,
            EffectDraftField::SigmaX, EffectDraftField::SigmaY, EffectDraftField::Opacity};
}

bool DocumentWorkflow::start_effect_draft_numeric_input(EffectDraftField field) {
    if (!effect_draft_ || !effect_draft_is_current()) return false;
    const auto fields = effect_draft_numeric_fields();
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) return false;
    effect_draft_->active_field = field;
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::focus_next_effect_draft_numeric_field() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None) return false;
    const auto fields = effect_draft_numeric_fields();
    const auto found = std::find(fields.begin(), fields.end(), effect_draft_->active_field);
    if (found == fields.end() || fields.empty()) return false;
    const size_t current = static_cast<size_t>(found - fields.begin());
    effect_draft_->active_field = fields[(current + 1) % fields.size()];
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::effect_draft_numeric_input_active() const noexcept {
    return effect_draft_ && effect_draft_->active_field != EffectDraftField::None;
}

bool DocumentWorkflow::set_effect_draft_numeric_value(EffectDraftField field, double value) {
    if (!effect_draft_ || !effect_draft_is_current() || !std::isfinite(value)) return false;
    auto parameters = effect_draft_->parameters;
    if (auto *blur = std::get_if<document::GaussianBlurEffect>(&parameters)) {
        if (field == EffectDraftField::SigmaX) blur->sigma_x_mm = value;
        else if (field == EffectDraftField::SigmaY) blur->sigma_y_mm = value;
        else return false;
    } else if (auto *shadow = std::get_if<document::DropShadowEffect>(&parameters)) {
        if (field == EffectDraftField::OffsetX) shadow->dx_mm = value;
        else if (field == EffectDraftField::OffsetY) shadow->dy_mm = value;
        else if (field == EffectDraftField::SigmaX) shadow->sigma_x_mm = value;
        else if (field == EffectDraftField::SigmaY) shadow->sigma_y_mm = value;
        else if (field == EffectDraftField::Opacity) shadow->opacity = value / 100.0;
        else return false;
    }
    if (!valid_effect_parameters(parameters)) return false;
    effect_draft_->parameters = std::move(parameters);
    effect_draft_->preview_snapshot.reset();
    return !effect_draft_->preview_enabled || rebuild_effect_draft_preview();
}

bool DocumentWorkflow::input_effect_draft_character(char character) {
    if (!effect_draft_ || !effect_draft_is_current() ||
        effect_draft_->active_field == EffectDraftField::None ||
        effect_draft_->numeric_buffer.size() >= 32)
        return false;
    const bool offset = effect_draft_->active_field == EffectDraftField::OffsetX ||
                        effect_draft_->active_field == EffectDraftField::OffsetY;
    const bool sign = character == '-' && offset && effect_draft_->numeric_buffer.empty();
    if (!sign && character != '.' && character != ',' && (character < '0' || character > '9'))
        return false;
    effect_draft_->numeric_buffer.push_back(character);
    if (const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer))
        (void)set_effect_draft_numeric_value(effect_draft_->active_field, *value);
    return true;
}

bool DocumentWorkflow::backspace_effect_draft_numeric_input() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None ||
        effect_draft_->numeric_buffer.empty())
        return false;
    effect_draft_->numeric_buffer.pop_back();
    if (const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer))
        (void)set_effect_draft_numeric_value(effect_draft_->active_field, *value);
    return true;
}

bool DocumentWorkflow::commit_effect_draft_numeric_input() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None) return false;
    if (effect_draft_->numeric_buffer.empty()) {
        effect_draft_->active_field = EffectDraftField::None;
        return true;
    }
    const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer);
    if (!value || !set_effect_draft_numeric_value(effect_draft_->active_field, *value)) return false;
    effect_draft_->active_field = EffectDraftField::None;
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::handle_effect_draft_keydown(uint32_t key) {
    if (!effect_draft_) return false;
    if (key == 0x1b) {
        cancel_effect_draft();
        return true;
    }
    if (key == 0x08 && effect_draft_numeric_input_active())
        return backspace_effect_draft_numeric_input();
    if (key == 0x09 && effect_draft_numeric_input_active())
        return focus_next_effect_draft_numeric_field();
    if (key == 0x0d) {
        if (effect_draft_numeric_input_active())
            return commit_effect_draft_numeric_input();
        return apply_effect_draft();
    }
    return false;
}

} // namespace pikdraw::desktop
