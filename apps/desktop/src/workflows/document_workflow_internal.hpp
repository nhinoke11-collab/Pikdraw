#pragma once

#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/effects/effect_stack.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <variant>

namespace pikdraw::desktop::detail {

// Keep only a valid UTF-8 prefix; malformed/incomplete input ends the preview.
inline std::string utf8_truncate_codepoints(std::string_view text, std::size_t max_codepoints) {
    std::size_t end = 0;
    for (std::size_t count = 0; count < max_codepoints && end < text.size(); ++count) {
        const auto lead = static_cast<unsigned char>(text[end]);
        std::size_t length = 0;
        if (lead <= 0x7f) length = 1;
        else if (lead >= 0xc2 && lead <= 0xdf) length = 2;
        else if (lead >= 0xe0 && lead <= 0xef) length = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
        else break;
        if (length > text.size() - end) break;
        bool valid = true;
        for (std::size_t index = 1; index < length; ++index) {
            const auto byte = static_cast<unsigned char>(text[end + index]);
            if ((byte & 0xc0) != 0x80) valid = false;
        }
        if (length >= 3) {
            const auto second = static_cast<unsigned char>(text[end + 1]);
            // Exclude overlong sequences, surrogate codepoints and values > U+10FFFF.
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90))
                valid = false;
        }
        if (!valid) break;
        end += length;
    }
    return std::string(text.substr(0, end));
}

inline color::RGBColor parse_hex_color(const std::string& hex) {
    if (hex.length() >= 7 && hex[0] == '#') {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);
        return color::RGBColor{r / 255.0, g / 255.0, b / 255.0, 1.0};
    }
    return color::RGBColor{0.0, 0.949, 0.765, 1.0}; // Cyber Teal #00f2c3
}

inline void collect_all_descendants(document::ObjectId id,
                                    const document::PersistentObjectMap& map,
                                    std::vector<document::ObjectId>& out) {
    const auto obj = map.get(id);
    if (!obj) return;
    if (const auto* grp = std::get_if<document::GroupObject>(&*obj)) {
        for (const auto cid : grp->children_ids) {
            out.push_back(cid);
            collect_all_descendants(cid, map, out);
        }
    }
}

inline std::vector<document::ObjectId> filter_top_level_roots(
    const std::vector<document::ObjectId>& ids,
    const document::PersistentObjectMap& objects
) {
    std::unordered_set<document::ObjectId> all_children;
    for (const auto id : ids) {
        if (const auto obj = objects.get(id)) {
            if (std::holds_alternative<document::GroupObject>(*obj)) {
                std::vector<document::ObjectId> descendants;
                collect_all_descendants(id, objects, descendants);
                all_children.insert(descendants.begin(), descendants.end());
            }
        }
    }
    std::vector<document::ObjectId> roots;
    std::unordered_set<document::ObjectId> seen;
    for (const auto id : ids) {
        if (!all_children.contains(id) && seen.insert(id).second) {
            roots.push_back(id);
        }
    }
    return roots;
}

inline bool selection_has_editable_bounds(
    const std::vector<document::ObjectId>& selected,
    const document::DocumentSnapshot& snapshot,
    const DocumentWorkflow& workflow
) {
    if (selected.empty()) return false;
    for (const auto id : selected) {
        const auto object = snapshot.objects().get(id);
        if (!object || std::visit([](const auto& value) { return value.locked; }, *object)) return false;
        const auto bounds = workflow.get_object_bounds(id);
        if (!bounds || !bounds->is_finite()) return false;
    }
    return true;
}

inline bool is_supported_boolean_operation(geometry::BooleanOp op) noexcept {
    switch (op) {
    case geometry::BooleanOp::Union:
    case geometry::BooleanOp::Difference:
    case geometry::BooleanOp::Intersection:
    case geometry::BooleanOp::Xor:
    case geometry::BooleanOp::BackMinusFront:
        return true;
    case geometry::BooleanOp::Boundary:
    case geometry::BooleanOp::Simplify:
        return false;
    }
    return false;
}

inline bool valid_effect_parameters(const document::EffectParameters &parameters) {
    return std::visit([](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            return std::isfinite(effect.sigma_x_mm) && std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm;
        } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
            return std::isfinite(effect.dx_mm) && std::isfinite(effect.dy_mm) &&
                   std::abs(effect.dx_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::abs(effect.dy_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::isfinite(effect.sigma_x_mm) && std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0 &&
                   static_cast<uint8_t>(effect.blend_mode) <=
                       static_cast<uint8_t>(document::BlendMode::Lighten);
        } else if constexpr (std::is_same_v<T, document::FeatherEffect>) {
            return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                   effect.radius_mm <= geometry::limits::kCanvasMaxMm;
        } else if constexpr (std::is_same_v<T, document::OuterGlowEffect>) {
            return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                   effect.radius_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0;
        } else if constexpr (std::is_same_v<T, document::InnerGlowEffect>) {
            return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                   effect.radius_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0;
        } else if constexpr (std::is_same_v<T, document::InnerShadowEffect>) {
            return std::isfinite(effect.dx_mm) && std::isfinite(effect.dy_mm) &&
                   std::abs(effect.dx_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::abs(effect.dy_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::isfinite(effect.sigma_x_mm) && std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0 &&
                   static_cast<uint8_t>(effect.blend_mode) <=
                       static_cast<uint8_t>(document::BlendMode::Lighten);
        } else if constexpr (std::is_same_v<T, document::ColorOverlayEffect>) {
            return effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0 &&
                   static_cast<uint8_t>(effect.blend_mode) <=
                       static_cast<uint8_t>(document::BlendMode::Lighten);
        }
        return false;
    }, parameters);
}

inline std::optional<double> parse_effect_draft_number(const std::string &text) {
    if (text.empty()) return std::nullopt;
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', '.');
    try {
        size_t parsed = 0;
        const double value = std::stod(normalized, &parsed);
        if (parsed != normalized.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<geometry::BooleanResult> compute_selected_boolean(
    const document::DocumentSnapshot& snapshot,
    const std::vector<document::ObjectId>& selected,
    geometry::BooleanOp op
) {
    if (selected.size() != 2 || !is_supported_boolean_operation(op)) return std::nullopt;
    const auto* target_object = snapshot.objects().lookup_ref(selected[0]);
    const auto* source_object = snapshot.objects().lookup_ref(selected[1]);
    if (!target_object || !source_object) return std::nullopt;
    const auto* target = std::get_if<document::PathObject>(target_object);
    const auto* source = std::get_if<document::PathObject>(source_object);
    if (!target || !source || target->locked || source->locked) return std::nullopt;
    try {
        auto target_path = target->path.transformed(target->transform);
        auto source_path = source->path.transformed(source->transform);
        auto result = op == geometry::BooleanOp::BackMinusFront
            ? geometry::boolean_paths(source_path, target_path, geometry::BooleanOp::Difference)
            : geometry::boolean_paths(target_path, source_path, op);
        if (!result.success) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

class GroupObjectsCommand final : public document::DocumentCommand {
public:
    GroupObjectsCommand(document::GroupObject group, std::vector<document::ObjectId> children)
        : group_(std::move(group)), children_(std::move(children)) {}

    std::string_view name() const noexcept override { return "Group Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_)
            return document::CommandResult::fail("Group command was already executed");
        if (children_.size() < 2 || group_.id == document::kInvalidObjectId ||
            doc.objects().contains(group_.id))
            return document::CommandResult::fail("A group requires two existing objects and a new ID");

        auto page = doc.page();
        auto objects = doc.objects();
        const std::unordered_set<document::ObjectId> selected(children_.begin(), children_.end());
        if (selected.size() != children_.size())
            return document::CommandResult::fail("Group selection contains duplicate objects");

        const document::SceneHierarchy hierarchy(doc.page(),doc.objects());
        const auto* owner=hierarchy.find(children_.front());
        if(!owner)return document::CommandResult::fail("Missing group owner");
        for(auto id:children_) {
            const auto* entry=hierarchy.find(id);
            if(!entry||entry->parent!=owner->parent||entry->layer!=owner->layer||entry->mask||
                !hierarchy.editable(id))return document::CommandResult::fail("Group requires editable siblings");
        }
        auto roots=owner->parent?std::get<document::GroupObject>(*objects.get(owner->parent)).children_ids
            :page.layers[owner->layer].object_ids;
        std::vector<document::ObjectId> remaining,ordered;
        bool inserted=false;
        for(auto id:roots) {
            if(selected.contains(id)) {if(!inserted){remaining.push_back(group_.id);inserted=true;}ordered.push_back(id);}
            else remaining.push_back(id);
        }
        group_.children_ids=std::move(ordered);
        if(owner->parent) {
            auto parent=std::get<document::GroupObject>(*objects.get(owner->parent));parent.children_ids=remaining;
            objects=objects.set(parent.id,parent);
        } else page.layers[owner->layer].object_ids=remaining;

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        objects = objects.set(group_.id, group_);
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Group command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    document::GroupObject group_;
    std::vector<document::ObjectId> children_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

} // namespace pikdraw::desktop::detail
