#pragma once

#include "path_command_support.hpp"

namespace pikdraw::document {

inline CommandResult PathTopologyCommand::execute(Document& doc) {
    auto object = doc.objects().get(object_id_);
    if (!object || !std::holds_alternative<PathObject>(*object))
        return CommandResult::fail("Editable path object was not found");
    if (mapping_result_) *mapping_result_ = {};
    auto current = std::get<PathObject>(*object);
    std::string error;
    if (!detail::path_is_editable(doc, current, error))
        return CommandResult::fail(std::move(error));

    if (!before_source_) {
        EditablePath candidate = *current.authoring_source;
        PathTopologyMapping candidate_mapping;
        if (!apply_edit(candidate, candidate_mapping, error))
            return CommandResult::fail(error.empty() ? "Path edit was rejected" : std::move(error));
        if (const auto invalid = candidate.validation_error())
            return CommandResult::fail(*invalid);
        before_source_ = current.authoring_source;
        before_adapter_ = current.path;
        after_adapter_ = editable_path_to_path2d(candidate);
        after_source_ = std::make_shared<const EditablePath>(std::move(candidate));
        mapping_ = std::move(candidate_mapping);
    }

    current.authoring_source = after_source_;
    current.path = after_adapter_;
    auto next_objects = doc.objects().set(object_id_, ObjectVariant{std::move(current)});
    doc.apply_mutation(doc.page(), std::move(next_objects));
    if (mapping_result_) *mapping_result_ = mapping_;
    return CommandResult::ok();
}

inline CommandResult PathTopologyCommand::undo(Document& doc) {
    if (!before_source_)
        return CommandResult::fail("Cannot undo a path edit that did not execute");
    auto object = doc.objects().get(object_id_);
    if (!object || !std::holds_alternative<PathObject>(*object))
        return CommandResult::fail("Editable path object was not found while undoing");
    auto restored = std::get<PathObject>(*object);
    restored.authoring_source = before_source_;
    restored.path = before_adapter_;
    auto next_objects = doc.objects().set(object_id_, ObjectVariant{std::move(restored)});
    doc.apply_mutation(doc.page(), std::move(next_objects));
    if (mapping_result_) *mapping_result_ = {};
    return CommandResult::ok();
}

} // namespace pikdraw::document
