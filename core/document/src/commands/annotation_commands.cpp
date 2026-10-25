#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/document/document.hpp"

namespace pikdraw::document {

size_t ensure_annotation_layer(Page& page, LayerRole role) {
    LayerId group_header_id = kInvalidLayerId;

    // Check if group header layer "Chú thích" already exists
    const auto hdr_idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (hdr_idx.has_value()) {
        group_header_id = page.layers[*hdr_idx].id;
    } else {
        Layer group_hdr;
        group_hdr.name = "Chú thích";
        group_hdr.visible = true;
        group_hdr.locked = false;
        group_hdr.printable = true;
        group_hdr.color_rgba = 0x8B5CF6FF; // Purple
        group_hdr.id = page.generate_next_layer_id();
        group_hdr.group_id = kInvalidLayerId;
        group_hdr.role = LayerRole::AnnotationGroupHeader;
        group_header_id = group_hdr.id;
        page.layers.push_back(std::move(group_hdr));
    }

    // Check if child layer for the requested role already exists
    const auto child_idx = page.find_layer_by_role(role);
    if (child_idx.has_value()) {
        return *child_idx;
    }

    Layer child_lyr;
    child_lyr.name = (role == LayerRole::AnnotationDetailView) ? "Ô chi tiết" : "Ghi chú";
    child_lyr.visible = true;
    child_lyr.locked = false;
    child_lyr.printable = true;
    child_lyr.color_rgba = (role == LayerRole::AnnotationDetailView) ? 0x00F2C3FF : 0xF59E0BFF;
    child_lyr.id = page.generate_next_layer_id();
    child_lyr.group_id = group_header_id;
    child_lyr.role = role;
    page.layers.push_back(std::move(child_lyr));
    return page.layers.size() - 1;
}

// --- AddAnnotationCommand ---

AddAnnotationCommand::AddAnnotationCommand(AnnotationObject annotation)
    : annotation_(std::move(annotation)) {}

CommandResult AddAnnotationCommand::execute(Document& doc) {
    Page page = doc.page();

    const bool had_group = page.find_layer_by_role(LayerRole::AnnotationGroupHeader).has_value();
    const bool had_child = page.find_layer_by_role(LayerRole::AnnotationNotes).has_value();

    const size_t layer_idx = ensure_annotation_layer(page, LayerRole::AnnotationNotes);

    if (!had_group) {
        created_group_header_index_ = *page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    }
    if (!had_child) {
        created_layer_index_ = layer_idx;
    }

    if (annotation_.id == kInvalidObjectId) {
        annotation_.id = generate_object_id();
    }
    const ObjectId id = annotation_.id;

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Annotation ID already exists in document");
    }

    const auto layout = evaluate_annotation_layout(annotation_);
    const auto path_obj = annotation_to_path_object(annotation_, layout);

    PersistentObjectMap map = doc.objects().set(id, ObjectVariant{path_obj});
    page.layers[layer_idx].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);

    AnnotationRegistry::instance().set(id, annotation_);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddAnnotationCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddAnnotationCommand");
    }

    Page page = doc.page();
    const ObjectId id = annotation_.id;

    for (auto& lyr : page.layers) {
        auto& ids = lyr.object_ids;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == id) {
                ids.erase(it);
                break;
            }
        }
    }

    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));

    AnnotationRegistry::instance().remove(id);
    executed_ = false;
    return CommandResult::ok();
}

// --- UpdateAnnotationCommand ---

UpdateAnnotationCommand::UpdateAnnotationCommand(ObjectId id, AnnotationObject updated_annotation)
    : id_(id), new_annotation_(std::move(updated_annotation)) {}

CommandResult UpdateAnnotationCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Annotation object not found");
    }

    const auto* current_path = std::get_if<PathObject>(&(*opt));
    if (!current_path) {
        return CommandResult::fail("Annotation target is not a PathObject");
    }

    old_path_object_ = *current_path;
    const auto reg_opt = AnnotationRegistry::instance().get(id_);
    if (reg_opt.has_value()) {
        old_annotation_ = *reg_opt;
    } else {
        auto parsed = deserialize_annotation_spec(current_path->contour_spec);
        if (parsed) old_annotation_ = *parsed;
    }

    new_annotation_.id = id_;
    const auto layout = evaluate_annotation_layout(new_annotation_);
    auto new_path_obj = annotation_to_path_object(new_annotation_, layout);

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{new_path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    AnnotationRegistry::instance().set(id_, new_annotation_);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateAnnotationCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted UpdateAnnotationCommand");
    }

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{old_path_object_});
    doc.apply_mutation(doc.page(), std::move(map));

    AnnotationRegistry::instance().set(id_, old_annotation_);
    executed_ = false;
    return CommandResult::ok();
}

// --- AddDetailViewCommand ---

AddDetailViewCommand::AddDetailViewCommand(DetailViewObject detail_view)
    : detail_view_(std::move(detail_view)) {}

CommandResult AddDetailViewCommand::execute(Document& doc) {
    Page page = doc.page();

    const bool had_group = page.find_layer_by_role(LayerRole::AnnotationGroupHeader).has_value();
    const bool had_child = page.find_layer_by_role(LayerRole::AnnotationDetailView).has_value();

    const size_t layer_idx = ensure_annotation_layer(page, LayerRole::AnnotationDetailView);

    if (!had_group) {
        created_group_header_index_ = *page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    }
    if (!had_child) {
        created_layer_index_ = layer_idx;
    }

    if (detail_view_.id == kInvalidObjectId) {
        detail_view_.id = generate_object_id();
    }
    const ObjectId id = detail_view_.id;

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Detail view ID already exists in document");
    }

    const auto layout = evaluate_detail_view_layout(detail_view_);
    const auto path_obj = detail_view_to_path_object(detail_view_, layout);

    PersistentObjectMap map = doc.objects().set(id, ObjectVariant{path_obj});
    page.layers[layer_idx].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);

    DetailViewRegistry::instance().set(id, detail_view_);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddDetailViewCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddDetailViewCommand");
    }

    Page page = doc.page();
    const ObjectId id = detail_view_.id;

    for (auto& lyr : page.layers) {
        auto& ids = lyr.object_ids;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == id) {
                ids.erase(it);
                break;
            }
        }
    }

    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));

    DetailViewRegistry::instance().remove(id);
    executed_ = false;
    return CommandResult::ok();
}

// --- UpdateDetailViewCommand ---

UpdateDetailViewCommand::UpdateDetailViewCommand(ObjectId id, DetailViewObject updated_detail_view)
    : id_(id), new_detail_view_(std::move(updated_detail_view)) {}

CommandResult UpdateDetailViewCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Detail view object not found");
    }

    const auto* current_path = std::get_if<PathObject>(&(*opt));
    if (!current_path) {
        return CommandResult::fail("Detail view target is not a PathObject");
    }

    old_path_object_ = *current_path;
    const auto reg_opt = DetailViewRegistry::instance().get(id_);
    if (reg_opt.has_value()) {
        old_detail_view_ = *reg_opt;
    } else {
        auto parsed = deserialize_detail_view_spec(current_path->contour_spec);
        if (parsed) old_detail_view_ = *parsed;
    }

    new_detail_view_.id = id_;
    const auto layout = evaluate_detail_view_layout(new_detail_view_);
    auto new_path_obj = detail_view_to_path_object(new_detail_view_, layout);

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{new_path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DetailViewRegistry::instance().set(id_, new_detail_view_);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateDetailViewCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted UpdateDetailViewCommand");
    }

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{old_path_object_});
    doc.apply_mutation(doc.page(), std::move(map));

    DetailViewRegistry::instance().set(id_, old_detail_view_);
    executed_ = false;
    return CommandResult::ok();
}

// --- ToggleAnnotationGroupCommand ---

ToggleAnnotationGroupCommand::ToggleAnnotationGroupCommand(ToggleKind kind)
    : kind_(kind) {}

CommandResult ToggleAnnotationGroupCommand::execute(Document& doc) {
    Page page = doc.page();
    const auto idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (!idx.has_value()) {
        return CommandResult::fail("Annotation group header layer not found");
    }

    auto& hdr = page.layers[*idx];
    switch (kind_) {
    case ToggleKind::Visible:
        old_val_ = hdr.visible;
        hdr.visible = !old_val_;
        break;
    case ToggleKind::Locked:
        old_val_ = hdr.locked;
        hdr.locked = !old_val_;
        break;
    case ToggleKind::Printable:
        old_val_ = hdr.printable;
        hdr.printable = !old_val_;
        break;
    }

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ToggleAnnotationGroupCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted ToggleAnnotationGroupCommand");
    }

    Page page = doc.page();
    const auto idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (!idx.has_value()) {
        return CommandResult::fail("Annotation group header layer not found");
    }

    auto& hdr = page.layers[*idx];
    switch (kind_) {
    case ToggleKind::Visible:
        hdr.visible = old_val_;
        break;
    case ToggleKind::Locked:
        hdr.locked = old_val_;
        break;
    case ToggleKind::Printable:
        hdr.printable = old_val_;
        break;
    }

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

std::optional<AnnotationObject> get_annotation(const Document& doc, ObjectId id) {
    auto obj = doc.objects().get(id);
    if (!obj.has_value()) {
        AnnotationRegistry::instance().remove(id);
        return std::nullopt;
    }
    const auto* path = std::get_if<PathObject>(&(*obj));
    if (!path) {
        AnnotationRegistry::instance().remove(id);
        return std::nullopt;
    }
    if (path->contour_spec.starts_with("annotation:")) {
        auto ann_deser = deserialize_annotation_spec(path->contour_spec);
        if (!ann_deser) {
            AnnotationRegistry::instance().remove(id);
            return std::nullopt;
        }
        ann_deser->id = id;
        AnnotationRegistry::instance().set(id, *ann_deser);
        return ann_deser;
    }
    return AnnotationRegistry::instance().get(id);
}

std::optional<DetailViewObject> get_detail_view(const Document& doc, ObjectId id) {
    auto obj = doc.objects().get(id);
    if (!obj.has_value()) {
        DetailViewRegistry::instance().remove(id);
        return std::nullopt;
    }
    const auto* path = std::get_if<PathObject>(&(*obj));
    if (!path) {
        DetailViewRegistry::instance().remove(id);
        return std::nullopt;
    }
    if (path->contour_spec.starts_with("detail_view:")) {
        auto dv_deser = deserialize_detail_view_spec(path->contour_spec);
        if (!dv_deser) {
            DetailViewRegistry::instance().remove(id);
            return std::nullopt;
        }
        dv_deser->id = id;
        DetailViewRegistry::instance().set(id, *dv_deser);
        return dv_deser;
    }
    return DetailViewRegistry::instance().get(id);
}

} // namespace pikdraw::document
