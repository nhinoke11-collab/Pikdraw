#include "pikdraw/document/commands/import_content_command.hpp"

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/structure.hpp"

#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pikdraw::document {

ImportContentCommand::ImportContentCommand(std::vector<Layer> layers,
                                           std::vector<ObjectVariant> objects)
    : imported_layers_(std::move(layers)), imported_objects_(std::move(objects)) {}

CommandResult ImportContentCommand::execute(Document& doc) {
    if (executed_ || imported_layers_.empty() || imported_objects_.empty())
        return CommandResult::fail("Import proposal is empty or already applied");

    Page next_page = doc.page();
    PersistentObjectMap next_objects = doc.objects();
    std::unordered_set<ObjectId> imported_ids;
    std::unordered_map<ObjectId, const ObjectVariant*> imported_by_id;
    imported_ids.reserve(imported_objects_.size());
    imported_by_id.reserve(imported_objects_.size());
    for (const auto& value : imported_objects_) {
        const ObjectId id = std::visit([](const auto& object) { return object.id; }, value);
        if (id == kInvalidObjectId || next_objects.contains(id) || !imported_ids.insert(id).second)
            return CommandResult::fail("Import proposal contains an invalid or duplicate object ID");
        imported_by_id.emplace(id, &value);
        next_objects = next_objects.set(id, value);
    }

    std::unordered_set<ObjectId> layer_roots;
    layer_roots.reserve(imported_ids.size());
    std::vector<std::pair<ObjectId, size_t>> pending;
    for (const auto& layer : imported_layers_) {
        if (layer.name.empty()) return CommandResult::fail("Import proposal contains an unnamed layer");
        for (const ObjectId id : layer.object_ids) {
            if (!imported_ids.contains(id) || !layer_roots.insert(id).second)
                return CommandResult::fail("Import layer references are incomplete or invalid");
            pending.emplace_back(id, 0);
        }
        next_page.layers.push_back(layer);
    }
    std::unordered_set<ObjectId> reached;
    reached.reserve(imported_ids.size());
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        const auto found = imported_by_id.find(id);
        if (found == imported_by_id.end() || depth > 256 || !reached.insert(id).second)
            return CommandResult::fail("Import proposal hierarchy is invalid, cyclic or multiply parented");
        if (const auto* group = std::get_if<GroupObject>(found->second))
            for (const ObjectId child : group->children_ids) pending.emplace_back(child, depth + 1);
    }
    if (reached.size() != imported_ids.size())
        return CommandResult::fail("Import proposal contains an object outside its layer/group hierarchy");

    try {
        validate_document_structure(next_page, next_objects);
    } catch (const std::exception& error) {
        return CommandResult::fail(std::string("Import proposal validation failed: ") + error.what());
    } catch (...) {
        return CommandResult::fail("Import proposal validation failed");
    }

    if (!has_before_) {
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        has_before_ = true;
    }
    doc.apply_mutation(std::move(next_page), std::move(next_objects));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ImportContentCommand::undo(Document& doc) {
    if (!executed_ || !has_before_)
        return CommandResult::fail("Import command has no applied state to undo");
    doc.apply_mutation(before_page_, before_objects_);
    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
