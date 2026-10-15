#include "pikdraw/document/model/structure.hpp"
#include "pikdraw/document/model/validation.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"
#include <unordered_set>
#include <utility>

namespace pikdraw::document {

void validate_document_structure(const Page& page, const PersistentObjectMap& objects) {
    validate_page(page);
    std::unordered_set<ObjectId> reached;
    reached.reserve(objects.size());
    std::vector<std::pair<ObjectId, size_t>> pending;
    for (const auto& layer : page.layers)
        for (auto id : layer.object_ids) pending.emplace_back(id, 0);
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        if (depth > 256 || !reached.insert(id).second)
            throw std::invalid_argument("Object hierarchy is cyclic, multiply parented or too deep");
        const auto* object = objects.lookup_ref(id);
        if (!object) throw std::invalid_argument("Layer or group references a missing object");
        if (const auto* group = std::get_if<GroupObject>(object))
            for (auto child : group->children_ids) pending.emplace_back(child, depth + 1);
    }
    if (reached.size() != objects.size())
        throw std::invalid_argument("Object has no layer/group parent");
}

} // namespace pikdraw::document
