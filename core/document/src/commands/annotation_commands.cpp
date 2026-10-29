#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document {

namespace {
void remove_created_annotation_layers(Page& page,
                                      std::optional<LayerId> child_id,
                                      std::optional<LayerId> group_id) {
    if (child_id) {
        const auto child_index = page.find_layer_by_id(*child_id);
        if (child_index && page.layers[*child_index].object_ids.empty() &&
            (page.layers[*child_index].role == LayerRole::AnnotationNotes ||
             page.layers[*child_index].role == LayerRole::AnnotationDetailView))
            page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(*child_index));
    }
    if (group_id) {
        const auto group_index = page.find_layer_by_id(*group_id);
        if (!group_index) return;
        const auto& group = page.layers[*group_index];
        const bool has_children = std::any_of(page.layers.begin(), page.layers.end(),
            [&](const Layer& layer) { return layer.group_id == *group_id; });
        if (group.role == LayerRole::AnnotationGroupHeader &&
            group.object_ids.empty() && !has_children)
            page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(*group_index));
    }
}

void remove_object_from_layers(Page& page, ObjectId id) {
    for (auto& layer : page.layers) {
        auto& ids = layer.object_ids;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    }
}

void restore_path_backed_annotation_fields(AnnotationObject& annotation,
                                          const PathObject& path,
                                          ObjectId id) {
    annotation.id = id;
    annotation.name = path.name;
    annotation.transform = path.transform;
    annotation.visible = path.visible;
    annotation.locked = path.locked;
    annotation.style.color = path.stroke;
}

void restore_path_backed_detail_view_fields(DetailViewObject& view,
                                            const PathObject& path,
                                            ObjectId id) {
    view.id = id;
    view.name = path.name;
    view.transform = path.transform;
    view.visible = path.visible;
    view.locked = path.locked;
    view.color = path.stroke;
}

bool check_annotation_layer_editable(const Page& page, ObjectId id,
                                     std::string& error) {
    for (size_t index = 0; index < page.layers.size(); ++index) {
        const auto& layer = page.layers[index];
        if (std::find(layer.object_ids.begin(), layer.object_ids.end(), id) ==
            layer.object_ids.end())
            continue;
        if (!page.effective_layer_visible(index)) {
            error = "Annotation is on a hidden layer or group";
            return false;
        }
        if (page.effective_layer_locked(index)) {
            error = "Annotation is on a locked layer or group";
            return false;
        }
        return true;
    }
    error = "Annotation is not assigned to a document layer";
    return false;
}
} // namespace

size_t ensure_annotation_layer(Page& page, LayerRole role) {
    if (role != LayerRole::AnnotationNotes && role != LayerRole::AnnotationDetailView)
        throw std::invalid_argument("Unsupported annotation child layer role");
    std::unordered_set<LayerId> seen_ids;
    seen_ids.reserve(page.layers.size());
    for (const auto& layer : page.layers) {
        if (layer.id != kInvalidLayerId && !seen_ids.insert(layer.id).second)
            throw std::invalid_argument("Duplicate layer identity on annotation page");
    }
    for (auto& layer : page.layers) {
        if (layer.id != kInvalidLayerId) continue;
        const LayerId next_id = page.generate_next_layer_id();
        if (next_id == kInvalidLayerId)
            throw std::overflow_error("Layer identity space is exhausted");
        layer.id = next_id;
        seen_ids.insert(next_id);
    }
    const auto generate_layer_id = [&page] {
        const LayerId next_id = page.generate_next_layer_id();
        if (next_id == kInvalidLayerId)
            throw std::overflow_error("Layer identity space is exhausted");
        return next_id;
    };
    LayerId group_header_id = kInvalidLayerId;

    // Check if group header layer "Chú thích" already exists
    const auto hdr_idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (hdr_idx.has_value()) {
        group_header_id = page.layers[*hdr_idx].id;
        page.layers[*hdr_idx].group_id = kInvalidLayerId;
    } else {
        Layer group_hdr;
        group_hdr.name = "Chú thích";
        group_hdr.visible = true;
        group_hdr.locked = false;
        group_hdr.printable = true;
        group_hdr.color_rgba = 0x8B5CF6FF; // Purple
        group_hdr.id = generate_layer_id();
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
    child_lyr.id = generate_layer_id();
    child_lyr.group_id = group_header_id;
    child_lyr.role = role;
    page.layers.push_back(std::move(child_lyr));
    return page.layers.size() - 1;
}

// --- AddAnnotationCommand ---

AddAnnotationCommand::AddAnnotationCommand(AnnotationObject annotation)
    : annotation_(std::move(annotation)) {}

CommandResult AddAnnotationCommand::execute(Document& doc) {
    created_layer_id_.reset();
    created_group_header_id_.reset();
    std::string validation_error;
    if (!validate_annotation_object(annotation_, &validation_error))
        return CommandResult::fail("Invalid annotation: " + validation_error);
    if ((annotation_.anchor.state == AssociationState::Valid ||
         annotation_.anchor.state == AssociationState::Partial) &&
        !doc.objects().contains(annotation_.anchor.source_object_id))
        return CommandResult::fail(
            "Associated annotation source is not present on the active page");

    Page page = doc.page();

    const bool had_group = page.find_layer_by_role(LayerRole::AnnotationGroupHeader).has_value();
    const bool had_child = page.find_layer_by_role(LayerRole::AnnotationNotes).has_value();

    const size_t layer_idx = ensure_annotation_layer(page, LayerRole::AnnotationNotes);
    const auto group_idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (!group_idx || page.layers[layer_idx].group_id != page.layers[*group_idx].id)
        return CommandResult::fail("Annotation child layer has an invalid group parent");
    if (!page.effective_layer_visible(layer_idx))
        return CommandResult::fail("Annotation group or child layer is hidden");
    if (page.effective_layer_locked(layer_idx))
        return CommandResult::fail("Annotation group or child layer is locked");

    if (!had_group) {
        created_group_header_id_ = page.layers[*page.find_layer_by_role(
            LayerRole::AnnotationGroupHeader)].id;
    }
    if (!had_child) {
        created_layer_id_ = page.layers[layer_idx].id;
    }

    if (annotation_.id == kInvalidObjectId) {
        annotation_.id = generate_object_id();
    }
    const ObjectId id = annotation_.id;
    if (id == kInvalidObjectId)
        return CommandResult::fail("Object ID allocator exhausted");

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Annotation ID already exists in document");
    }

    const auto layout = evaluate_annotation_layout(annotation_);
    const auto path_obj = annotation_to_path_object(annotation_, layout);

    PersistentObjectMap map = doc.objects().set(id, ObjectVariant{path_obj});
    page.layers[layer_idx].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);

    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddAnnotationCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddAnnotationCommand");
    }

    Page page = doc.page();
    const ObjectId id = annotation_.id;

    remove_object_from_layers(page, id);
    remove_created_annotation_layers(page, created_layer_id_, created_group_header_id_);

    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));

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
    std::string layer_error;
    if (current_path->locked || !check_annotation_layer_editable(doc.page(), id_, layer_error))
        return CommandResult::fail(current_path->locked
            ? "Annotation object is locked" : layer_error);

    old_path_object_ = *current_path;
    if (!get_annotation(doc, id_))
        return CommandResult::fail("Annotation specification is invalid");
    std::string validation_error;
    if (!validate_annotation_object(new_annotation_, &validation_error))
        return CommandResult::fail("Invalid annotation: " + validation_error);

    new_annotation_.id = id_;
    new_annotation_.transform = current_path->transform;
    new_annotation_.visible = current_path->visible;
    new_annotation_.locked = current_path->locked;
    new_annotation_.name = current_path->name;
    const auto layout = evaluate_annotation_layout(new_annotation_);
    auto new_path_obj = annotation_to_path_object(new_annotation_, layout);
    new_path_obj.appearance = current_path->appearance;
    new_path_obj.contour_paints = current_path->contour_paints;

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{new_path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateAnnotationCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted UpdateAnnotationCommand");
    }

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{old_path_object_});
    doc.apply_mutation(doc.page(), std::move(map));

    executed_ = false;
    return CommandResult::ok();
}

// --- AddDetailViewCommand ---

AddDetailViewCommand::AddDetailViewCommand(DetailViewObject detail_view)
    : detail_view_(std::move(detail_view)) {}

CommandResult AddDetailViewCommand::execute(Document& doc) {
    created_layer_id_.reset();
    created_group_header_id_.reset();
    std::string validation_error;
    if (!validate_detail_view_object(detail_view_, &validation_error))
        return CommandResult::fail("Invalid detail view: " + validation_error);
    if (detail_view_.source_page_id != doc.active_page_id())
        return CommandResult::fail("Detail View must reference the active source page");

    Page page = doc.page();

    const bool had_group = page.find_layer_by_role(LayerRole::AnnotationGroupHeader).has_value();
    const bool had_child = page.find_layer_by_role(LayerRole::AnnotationDetailView).has_value();

    const size_t layer_idx = ensure_annotation_layer(page, LayerRole::AnnotationDetailView);
    const auto group_idx = page.find_layer_by_role(LayerRole::AnnotationGroupHeader);
    if (!group_idx || page.layers[layer_idx].group_id != page.layers[*group_idx].id)
        return CommandResult::fail("Detail View child layer has an invalid group parent");
    if (!page.effective_layer_visible(layer_idx))
        return CommandResult::fail("Annotation group or child layer is hidden");
    if (page.effective_layer_locked(layer_idx))
        return CommandResult::fail("Annotation group or child layer is locked");

    if (!had_group) {
        created_group_header_id_ = page.layers[*page.find_layer_by_role(
            LayerRole::AnnotationGroupHeader)].id;
    }
    if (!had_child) {
        created_layer_id_ = page.layers[layer_idx].id;
    }

    if (detail_view_.id == kInvalidObjectId) {
        detail_view_.id = generate_object_id();
    }
    const ObjectId id = detail_view_.id;
    if (id == kInvalidObjectId)
        return CommandResult::fail("Object ID allocator exhausted");

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Detail view ID already exists in document");
    }

    const auto layout = evaluate_detail_view_layout(detail_view_);
    const auto path_obj = detail_view_to_path_object(detail_view_, layout);

    PersistentObjectMap map = doc.objects().set(id, ObjectVariant{path_obj});
    page.layers[layer_idx].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);

    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddDetailViewCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddDetailViewCommand");
    }

    Page page = doc.page();
    const ObjectId id = detail_view_.id;

    remove_object_from_layers(page, id);
    remove_created_annotation_layers(page, created_layer_id_, created_group_header_id_);

    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));

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
    std::string layer_error;
    if (current_path->locked || !check_annotation_layer_editable(doc.page(), id_, layer_error))
        return CommandResult::fail(current_path->locked
            ? "Detail View object is locked" : layer_error);

    old_path_object_ = *current_path;
    if (!get_detail_view(doc, id_))
        return CommandResult::fail("Detail View specification is invalid");
    std::string validation_error;
    if (!validate_detail_view_object(new_detail_view_, &validation_error))
        return CommandResult::fail("Invalid detail view: " + validation_error);
    if (new_detail_view_.source_page_id != doc.active_page_id())
        return CommandResult::fail("Detail View must reference the active source page");

    new_detail_view_.id = id_;
    new_detail_view_.transform = current_path->transform;
    new_detail_view_.visible = current_path->visible;
    new_detail_view_.locked = current_path->locked;
    new_detail_view_.name = current_path->name;
    const auto layout = evaluate_detail_view_layout(new_detail_view_);
    auto new_path_obj = detail_view_to_path_object(new_detail_view_, layout);
    new_path_obj.appearance = current_path->appearance;
    new_path_obj.contour_paints = current_path->contour_paints;

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{new_path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateDetailViewCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted UpdateDetailViewCommand");
    }

    PersistentObjectMap map = doc.objects().set(id_, ObjectVariant{old_path_object_});
    doc.apply_mutation(doc.page(), std::move(map));

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
    if (!obj.has_value()) return std::nullopt;
    const auto* path = std::get_if<PathObject>(&(*obj));
    if (!path) return std::nullopt;
    if (path->contour_spec.starts_with("annotation:")) {
        auto ann_deser = deserialize_annotation_spec(path->contour_spec);
        if (!ann_deser) return std::nullopt;
        restore_path_backed_annotation_fields(*ann_deser, *path, id);
        return ann_deser;
    }
    return std::nullopt;
}

std::optional<DetailViewObject> get_detail_view(const Document& doc, ObjectId id) {
    auto obj = doc.objects().get(id);
    if (!obj.has_value()) return std::nullopt;
    const auto* path = std::get_if<PathObject>(&(*obj));
    if (!path) return std::nullopt;
    if (path->contour_spec.starts_with("detail_view:")) {
        auto dv_deser = deserialize_detail_view_spec(path->contour_spec);
        if (!dv_deser) return std::nullopt;
        restore_path_backed_detail_view_fields(*dv_deser, *path, id);
        return dv_deser;
    }
    return std::nullopt;
}

} // namespace pikdraw::document
