#include "pikdraw/document/commands/tac_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace pikdraw::document {
namespace {

const char* scope_name(TACScope scope) noexcept {
    switch (scope) {
    case TACScope::Document: return "document";
    case TACScope::ActivePage: return "active-page";
    case TACScope::Selection: return "selection";
    }
    return "invalid";
}

bool valid_scope(TACScope scope) noexcept {
    return scope == TACScope::Document || scope == TACScope::ActivePage ||
           scope == TACScope::Selection;
}

bool scan_color(const color::ColorValue& source, color::ColorValue* target,
                double limit, color::TACSummary& summary, std::string& error) {
    if (const auto* spot = std::get_if<color::SpotColor>(&source.payload())) {
        (void)spot;
        ++summary.spot_colors_not_assessed;
        return true;
    }
    const auto* cmyk = std::get_if<color::CMYKColor>(&source.payload());
    if (!cmyk) return true;
    if (!cmyk->is_valid()) {
        error = "TAC inspection found an invalid process CMYK color";
        return false;
    }

    ++summary.process_colors_examined;
    const double before = color::calculate_tac(*cmyk);
    summary.maximum_before_percent = std::max(summary.maximum_before_percent, before);
    if (!target) {
        summary.maximum_after_percent = std::max(summary.maximum_after_percent, before);
        if (before > limit) ++summary.process_colors_over_limit;
        return true;
    }
    if (before <= limit) {
        summary.maximum_after_percent = std::max(summary.maximum_after_percent, before);
        return true;
    }

    ++summary.process_colors_over_limit;

    const auto old_overprint = target->overprint();
    const auto clamped = color::clamp_tac(*cmyk, limit);
    const double after = color::calculate_tac(clamped);
    summary.maximum_after_percent = std::max(summary.maximum_after_percent, after);
    if (clamped.c == cmyk->c && clamped.m == cmyk->m &&
        clamped.y == cmyk->y && clamped.k == cmyk->k && clamped.a == cmyk->a)
        return true;

    *target = color::ColorValue{clamped};
    target->set_overprint(old_overprint);
    ++summary.process_colors_changed;
    return true;
}

bool scan_object(const ObjectVariant& source, ObjectVariant* target,
                 double limit, color::TACSummary& summary, std::string& error) {
    return std::visit([&](const auto& object) {
        using T = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<T, ImageObject>) {
            ++summary.raster_objects_not_assessed;
        } else if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
            auto* writable = target ? std::get_if<T>(target) : nullptr;
            if (!scan_color(object.fill, writable ? &writable->fill : nullptr,
                            limit, summary, error)) return false;

            if constexpr (std::is_same_v<T, PathObject>) {
                if (!scan_color(object.stroke, writable ? &writable->stroke : nullptr,
                                limit, summary, error)) return false;
                if (object.fill_gradient) {
                    auto gradient = *object.fill_gradient;
                    bool gradient_changed = false;
                    for (size_t index = 0; index < gradient.stops.size(); ++index) {
                        const auto prior_changes = summary.process_colors_changed;
                        if (!scan_color(object.fill_gradient->stops[index].color,
                                writable ? &gradient.stops[index].color : nullptr,
                                limit, summary, error)) return false;
                        gradient_changed |= summary.process_colors_changed != prior_changes;
                    }
                    if (gradient_changed && writable)
                        writable->fill_gradient = std::make_shared<const GradientPaint>(
                            std::move(gradient));
                }
            }

            for (size_t index = 0; index < object.appearance.effects.size(); ++index) {
                const auto& effect = object.appearance.effects[index];
                const auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters);
                if (!shadow) continue;
                auto* writable_shadow = writable && index < writable->appearance.effects.size()
                    ? std::get_if<DropShadowEffect>(
                        &writable->appearance.effects[index].parameters) : nullptr;
                if (!scan_color(shadow->color,
                        writable_shadow ? &writable_shadow->color : nullptr,
                        limit, summary, error)) return false;
            }
        }
        return true;
    }, source);
}

TACValidationResult scan_snapshot(const DocumentSnapshot& snapshot,
    const color::TACOutputCondition& output_condition, TACScope scope,
    const std::vector<ObjectId>& selection, std::vector<DocumentPage>* writable_pages) {
    TACValidationResult result;
    result.summary.output_condition_id = output_condition.identity;
    result.summary.scope = scope_name(scope);
    result.summary.maximum_tac_percent = output_condition.maximum_tac_percent;

    if (!output_condition.is_valid()) {
        result.error_message = "A valid output-condition ID and TAC limit from 100% to 400% are required";
        return result;
    }
    if (!valid_scope(scope)) {
        result.error_message = "Invalid TAC scope";
        return result;
    }
    if (scope != TACScope::Selection && !selection.empty()) {
        result.error_message = "Selection IDs are only valid with selection scope";
        return result;
    }

    std::unordered_set<ObjectId> selected;
    if (scope == TACScope::Selection) {
        selected.reserve(selection.size());
        for (const auto id : selection) {
            if (id == kInvalidObjectId || !selected.insert(id).second) {
                result.error_message = "Selection contains an invalid or duplicate object ID";
                return result;
            }
        }
        const auto& active = snapshot.active_page();
        for (const auto id : selected) {
            if (!active.objects.contains(id)) {
                result.error_message = "A selected object is not present on the active page";
                return result;
            }
        }
    }

    auto scan_page = [&](const DocumentPage& page, DocumentPage* writable_page) {
        if (scope == TACScope::Selection) {
            for (const auto id : selection) {
                const auto* object = page.objects.lookup_ref(id);
                if (!object) continue;
                auto writable_object = writable_page ? writable_page->objects.get(id) : std::nullopt;
                ObjectVariant* target = writable_object ? &*writable_object : nullptr;
                const auto prior_changes = result.summary.process_colors_changed;
                if (!scan_object(*object, target, output_condition.maximum_tac_percent,
                                 result.summary, result.error_message)) return false;
                if (writable_object && result.summary.process_colors_changed != prior_changes)
                    writable_page->objects = writable_page->objects.set(id, std::move(*writable_object));
            }
        } else {
            for (const auto& [id, object] : page.objects.all()) {
                auto writable_object = writable_page ? writable_page->objects.get(id) : std::nullopt;
                ObjectVariant* target = writable_object ? &*writable_object : nullptr;
                const auto prior_changes = result.summary.process_colors_changed;
                if (!scan_object(object, target, output_condition.maximum_tac_percent,
                                 result.summary, result.error_message)) return false;
                if (writable_object && result.summary.process_colors_changed != prior_changes)
                    writable_page->objects = writable_page->objects.set(id, std::move(*writable_object));
            }
        }
        return true;
    };

    for (size_t index = 0; index < snapshot.pages().size(); ++index) {
        const auto& page = snapshot.pages()[index];
        if (scope == TACScope::ActivePage || scope == TACScope::Selection) {
            if (page.id != snapshot.active_page_id()) continue;
        }
        DocumentPage* writable_page = writable_pages ? &(*writable_pages)[index] : nullptr;
        if (!scan_page(page, writable_page)) return result;
    }
    result.success = true;
    return result;
}

} // namespace

TACValidationResult validate_document_tac(const DocumentSnapshot& snapshot,
    const color::TACOutputCondition& output_condition, TACScope scope,
    const std::vector<ObjectId>& selection) {
    return scan_snapshot(snapshot, output_condition, scope, selection, nullptr);
}

FixDocumentTacCommand::FixDocumentTacCommand(color::TACOutputCondition output_condition,
    TACScope scope, std::vector<ObjectId> selection)
    : output_condition_(std::move(output_condition)), scope_(scope),
      selection_(std::move(selection)) {}

CommandResult FixDocumentTacCommand::execute(Document& doc) {
    const auto snapshot = doc.current_snapshot();
    auto pages = snapshot->pages();
    auto result = scan_snapshot(*snapshot, output_condition_, scope_, selection_, &pages);
    if (!result.success) return CommandResult::fail(std::move(result.error_message));
    if (result.summary.process_colors_changed == 0)
        return CommandResult::no_change(std::move(result.summary));

    result.summary.fix_method =
        "K-preserving proportional CMY reduction; appearance may change; no ICC colorimetric matching";
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), snapshot->active_page_id());
    return CommandResult::ok(std::nullopt, std::move(result.summary));
}

CommandResult FixDocumentTacCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No TAC fix to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

} // namespace pikdraw::document
