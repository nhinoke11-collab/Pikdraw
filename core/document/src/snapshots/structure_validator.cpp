#include "pikdraw/document/model/structure.hpp"
#include "pikdraw/document/model/validation.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"
#include <cstdint>
#include <unordered_set>
#include <utility>

namespace pikdraw::document {

void validate_document_structure(const Page& page, const PersistentObjectMap& objects) {
    validate_page(page);
    std::unordered_set<uint64_t> element_ids;
    element_ids.reserve(objects.size());
    for (const auto& [id, object] : objects.all()) {
        validate_object_source(id, object);
        if (!element_ids.insert(id).second)
            throw std::invalid_argument("Duplicate stable document object ID");
        const auto* path = std::get_if<PathObject>(&object);
        if (!path || !path->authoring_source) continue;
        for (const auto& subpath : path->authoring_source->subpaths)
            if (!element_ids.insert(subpath.id.value()).second)
                throw std::invalid_argument("Authoring path ID collides in document");
        for (const auto& subpath : path->authoring_source->subpaths) {
            for (const auto& node : subpath.nodes)
                if (!element_ids.insert(node.id.value()).second)
                    throw std::invalid_argument("Authoring node ID collides in document");
            for (const auto& segment : subpath.segments)
                if (!element_ids.insert(segment.id.value()).second)
                    throw std::invalid_argument("Authoring segment ID collides in document");
        }
    }
    // Object IDs, editable path IDs, and effect instance IDs share one allocator.
    // Collect every source identity before checking effects so traversal order cannot
    // let an effect collide with a later object or path element unnoticed.
    size_t effect_count = 0;
    for (const auto& [id, object] : objects.all()) {
        (void)id;
        const Appearance* appearance = nullptr;
        if (const auto* path = std::get_if<PathObject>(&object)) {
            appearance = &path->appearance;
        } else if (const auto* text = std::get_if<TextObject>(&object)) {
            appearance = &text->appearance;
        }
        if (!appearance) continue;
        if (appearance->effects.size() > kMaxEffectsPerDocument - effect_count)
            throw std::invalid_argument("Document exceeds the maximum effect node count");
        effect_count += appearance->effects.size();
        for (const auto& effect : appearance->effects)
            if (effect.id == kInvalidEffectInstanceId || !element_ids.insert(effect.id).second)
                throw std::invalid_argument("Effect ID collides with another stable document ID");
    }
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
