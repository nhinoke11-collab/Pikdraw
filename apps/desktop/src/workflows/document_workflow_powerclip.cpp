#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include <algorithm>

namespace pikdraw::desktop {
bool DocumentWorkflow::can_make_clipping_mask_selected() const {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.size() < 2) return false;
    const auto snapshot = root_.document().current_snapshot();
    const auto* first = snapshot->hierarchy().find(ids.front());
    if (!first || snapshot->is_content_locked()) return false;
    for (auto id : ids) {
        const auto* entry = snapshot->hierarchy().find(id);
        if (!entry || entry->parent != first->parent || entry->layer != first->layer ||
            entry->mask || !snapshot->hierarchy().editable(id)) return false;
    }
    const auto siblings = first->parent
        ? std::get<document::GroupObject>(*snapshot->objects().get(first->parent)).children_ids
        : snapshot->page().layers[first->layer].object_ids;
    for (auto it = siblings.rbegin(); it != siblings.rend(); ++it)
        if (std::find(ids.begin(), ids.end(), *it) != ids.end())
            return document::frame_geometry(*snapshot->objects().get(*it)).has_value();
    return false;
}
bool DocumentWorkflow::is_clipping_container(document::ObjectId id) const {
    const auto snapshot = root_.document().current_snapshot();
    const auto* object = snapshot->objects().lookup_ref(id);
    const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
    return group && group->is_clipping_container();
}
document::ObjectId DocumentWorkflow::make_clipping_mask_selected(bool preserve) {
    if (!can_make_clipping_mask_selected()) return 0;
    auto& controller = root_.vector_controller();
    const auto ids = controller.selection().selected_ids();
    const auto snapshot = root_.document().current_snapshot();
    const auto* owner = snapshot->hierarchy().find(ids.front());
    const auto siblings = owner->parent
        ? std::get<document::GroupObject>(*snapshot->objects().get(owner->parent)).children_ids
        : snapshot->page().layers[owner->layer].object_ids;
    std::vector<document::ObjectId> contents;
    document::ObjectId frame = 0;
    for (auto id : siblings)
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) { contents.push_back(id); frame = id; }
    contents.pop_back();
    bool outside = true;
    const auto frame_bounds = get_object_bounds(frame);
    for (auto id : contents) {
        const auto bounds = get_object_bounds(id);
        if (bounds && frame_bounds && bounds->intersects(*frame_bounds)) outside = false;
    }
    auto command = std::make_unique<document::ClipCommand>(document::ClipOperation::Place, frame, contents,
        outside ? document::ClipContentFitMode::CenterContents : document::ClipContentFitMode::CurrentPosition, preserve);
    const auto id = is_clipping_container(frame) ? frame : command->result_id();
    if (!root_.document().execute_command(std::move(command)).success) return 0;
    controller.set_selection({id}, *root_.document().current_snapshot()); sync_status_bar(); return id;
}
document::ObjectId DocumentWorkflow::place_inside_powerclip(document::ObjectId content,
    document::ObjectId frame, document::ClipContentFitMode fit) {
    auto command = std::make_unique<document::ClipCommand>(document::ClipOperation::Place, frame,
        std::vector<document::ObjectId>{content}, fit);
    const auto id = is_clipping_container(frame) ? frame : command->result_id();
    if (!root_.document().execute_command(std::move(command)).success) return 0;
    root_.vector_controller().set_selection({id}, *root_.document().current_snapshot()); sync_status_bar(); return id;
}
bool DocumentWorkflow::release_clipping_mask_selected() {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.size() != 1 || !is_clipping_container(ids.front())) return false;
    const auto snapshot = root_.document().current_snapshot();
    const auto group = std::get<document::GroupObject>(*snapshot->objects().get(ids.front()));
    const auto result = root_.document().execute_command(std::make_unique<document::ClipCommand>(
        document::ClipOperation::Release, ids.front()));
    if (result.success) root_.vector_controller().set_selection(group.children_ids, *root_.document().current_snapshot());
    sync_status_bar(); return result.success;
}
bool DocumentWorkflow::extract_powerclip_contents(document::ObjectId frame) {
    const auto result = root_.document().execute_command(std::make_unique<document::ClipCommand>(document::ClipOperation::Extract, frame));
    if (result.success) root_.vector_controller().set_selection({frame}, *root_.document().current_snapshot());
    sync_status_bar(); return result.success;
}
document::ObjectId DocumentWorkflow::create_empty_powerclip(document::ObjectId frame) {
    auto command = std::make_unique<document::ClipCommand>(document::ClipOperation::Place, frame);
    const auto id = is_clipping_container(frame) ? frame : command->result_id();
    if (!root_.document().execute_command(std::move(command)).success) return 0;
    root_.vector_controller().set_selection({id}, *root_.document().current_snapshot()); sync_status_bar(); return id;
}
bool DocumentWorkflow::copy_powerclip_contents(document::ObjectId source, document::ObjectId target) {
    const auto snapshot = root_.document().current_snapshot();
    const auto* object = snapshot->objects().lookup_ref(source);
    const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
    if (!group || !group->clip || source == target) return false;
    auto ids = group->children_ids; std::erase(ids, group->clip->mask_object_id);
    if (ids.empty()) return false;
    return root_.document().execute_command(std::make_unique<document::ClipCommand>(document::ClipOperation::CopyContents, target, ids)).success;
}
bool DocumentWorkflow::fit_powerclip_contents(document::ObjectId frame, document::ClipContentFitMode fit) {
    const auto result = root_.document().execute_command(std::make_unique<document::ClipCommand>(document::ClipOperation::Fit, frame,
        std::vector<document::ObjectId>{}, fit));
    sync_status_bar(); return result.success;
}
bool DocumentWorkflow::set_powerclip_lock_contents(document::ObjectId frame, bool locked) {
    return root_.document().execute_command(std::make_unique<document::ClipCommand>(document::ClipOperation::SetLock, frame,
        std::vector<document::ObjectId>{}, document::ClipContentFitMode::CurrentPosition, locked)).success;
}
bool DocumentWorkflow::set_powerclip_preserve_appearance(document::ObjectId frame, bool preserve) {
    return root_.document().execute_command(std::make_unique<document::ClipCommand>(document::ClipOperation::SetAppearance, frame,
        std::vector<document::ObjectId>{}, document::ClipContentFitMode::CurrentPosition, preserve)).success;
}
} // namespace pikdraw::desktop
