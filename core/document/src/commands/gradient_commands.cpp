#include "pikdraw/document/commands/gradient_commands.hpp"

#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <type_traits>
#include <unordered_set>

namespace pikdraw::document {
namespace {

bool same_color_payload(const color::ColorValue& lhs, const color::ColorValue& rhs) {
    if (lhs.payload().index() != rhs.payload().index()) {
        return false;
    }
    return std::visit([&](const auto& left) {
        using T = std::decay_t<decltype(left)>;
        const auto* right = std::get_if<T>(&rhs.payload());
        if (!right) return false;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            return left.r == right->r && left.g == right->g &&
                   left.b == right->b && left.a == right->a;
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            return left.c == right->c && left.m == right->m &&
                   left.y == right->y && left.k == right->k && left.a == right->a;
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            return left.spot_name == right->spot_name && left.tint == right->tint &&
                   left.alternate_rgb.r == right->alternate_rgb.r &&
                   left.alternate_rgb.g == right->alternate_rgb.g &&
                   left.alternate_rgb.b == right->alternate_rgb.b &&
                   left.alternate_rgb.a == right->alternate_rgb.a &&
                   left.overprint == right->overprint;
        } else {
            return left.gray == right->gray && left.a == right->a;
        }
    }, lhs.payload()) &&
    lhs.overprint().fill == rhs.overprint().fill &&
    lhs.overprint().stroke == rhs.overprint().stroke;
}

bool same_gradient(
    const std::shared_ptr<const GradientPaint>& lhs,
    const std::shared_ptr<const GradientPaint>& rhs) {
    if (!lhs && !rhs) return true;
    if (!lhs || !rhs) return false;
    return lhs->same_payload(*rhs);
}

} // namespace

SetFillPaintCommand::SetFillPaintCommand(std::vector<FillPaintReplacement> replacements)
    : replacements_(std::move(replacements)) {}

CommandResult SetFillPaintCommand::execute(Document& doc) {
    if (applied_) {
        return CommandResult::fail("SetFillPaintCommand is already applied");
    }
    if (replacements_.empty()) {
        return CommandResult::no_change();
    }

    std::unordered_set<ObjectId> targets;
    targets.reserve(replacements_.size());
    std::vector<ObjectVariant> updated;
    std::vector<PreviousFillPaint> current_values;
    updated.reserve(replacements_.size());
    current_values.reserve(replacements_.size());
    bool changed = false;

    for (const auto& replacement : replacements_) {
        if (replacement.object_id == kInvalidObjectId ||
            !targets.insert(replacement.object_id).second) {
            return CommandResult::fail("Fill paint targets must be unique nonzero IDs");
        }
        const auto current = doc.objects().get(replacement.object_id);
        if (!current) {
            return CommandResult::fail("Target object no longer exists: " + std::to_string(replacement.object_id));
        }
        const auto* path = std::get_if<PathObject>(&(*current));
        if (!path) {
            return CommandResult::fail("Fill paint is supported only on PathObject targets");
        }
        if (path->locked) {
            return CommandResult::fail("A locked object prevents atomic fill paint update: " + std::to_string(replacement.object_id));
        }
        if (!replacement.fill.is_valid()) {
            return CommandResult::fail("Invalid solid fill color");
        }
        if (replacement.fill_gradient) {
            if (const auto error = replacement.fill_gradient->validation_error()) {
                return CommandResult::fail("Invalid gradient fill paint: " + *error);
            }
        }

        current_values.push_back(PreviousFillPaint{
            path->has_fill,
            path->fill,
            path->fill_gradient
        });

        const bool this_changed = (path->has_fill != replacement.has_fill) ||
                                  !same_color_payload(path->fill, replacement.fill) ||
                                  !same_gradient(path->fill_gradient, replacement.fill_gradient);
        changed = changed || this_changed;

        auto next = *current;
        auto& next_path = std::get<PathObject>(next);
        next_path.has_fill = replacement.has_fill;
        next_path.fill = replacement.fill;
        next_path.fill_gradient = replacement.fill_gradient;
        updated.push_back(std::move(next));
    }

    if (!changed) {
        return CommandResult::no_change();
    }

    if (previous_.empty()) {
        previous_ = current_values;
    }

    auto objects = doc.objects();
    for (size_t i = 0; i < replacements_.size(); ++i) {
        objects = objects.set(replacements_[i].object_id, std::move(updated[i]));
    }
    doc.apply_mutation(doc.page(), std::move(objects));
    applied_ = true;
    return CommandResult::ok();
}

CommandResult SetFillPaintCommand::undo(Document& doc) {
    if (!applied_ || previous_.size() != replacements_.size()) {
        return CommandResult::fail("Cannot undo a SetFillPaintCommand that was not applied");
    }

    auto objects = doc.objects();
    std::vector<ObjectVariant> restored;
    restored.reserve(replacements_.size());

    for (size_t i = 0; i < replacements_.size(); ++i) {
        const auto current = objects.get(replacements_[i].object_id);
        if (!current || !std::holds_alternative<PathObject>(*current)) {
            return CommandResult::fail("Target path is unavailable while undoing fill paint");
        }
        auto next = *current;
        auto& next_path = std::get<PathObject>(next);
        next_path.has_fill = previous_[i].has_fill;
        next_path.fill = previous_[i].fill;
        next_path.fill_gradient = previous_[i].fill_gradient;
        restored.push_back(std::move(next));
    }

    for (size_t i = 0; i < replacements_.size(); ++i) {
        objects = objects.set(replacements_[i].object_id, std::move(restored[i]));
    }
    doc.apply_mutation(doc.page(), std::move(objects));
    applied_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
