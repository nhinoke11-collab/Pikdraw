#pragma once
#include "pikdraw/desktop/shell/native_window_layers_dock.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <functional>
#include <cctype>
#include <algorithm>

namespace pikdraw::desktop {
struct LayerTreeRow { document::ObjectId id; size_t depth; bool mask; };
inline bool layer_tree_row_matches(const document::DocumentSnapshot& snapshot,
    const LayerTreeRow& row, const LayersDockState& state) {
    const auto* object = snapshot.objects().lookup_ref(row.id);
    if (!object) return false;
    const bool is_group = std::holds_alternative<document::GroupObject>(*object);
    const bool is_text = std::holds_alternative<document::TextObject>(*object);
    const bool is_image = std::holds_alternative<document::ImageObject>(*object);
    switch (state.filter) {
    case LayerFilterType::Path: if (is_group || is_text || is_image) return false; break;
    case LayerFilterType::Text: if (!is_text) return false; break;
    case LayerFilterType::Image: if (!is_image) return false; break;
    case LayerFilterType::Group: if (!is_group) return false; break;
    default: break;
    }
    if (state.search_query.empty()) return true;
    const auto name = std::visit([](const auto& value) { return value.name; }, *object);
    std::string id = std::to_string(row.id);
    const std::string hash_id = "#" + id;
    auto contains_ascii_ci = [](std::string haystack, std::string needle) {
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
        std::transform(needle.begin(), needle.end(), needle.begin(), lower);
        return haystack.find(needle) != std::string::npos;
    };
    return contains_ascii_ci(name, state.search_query) ||
           contains_ascii_ci(id, state.search_query) ||
           contains_ascii_ci(hash_id, state.search_query);
}
inline std::vector<LayerTreeRow> layer_tree_rows(const document::DocumentSnapshot& snapshot,
    const document::Layer& layer, const LayersDockState& state) {
    std::vector<LayerTreeRow> rows;
    std::function<void(document::ObjectId,size_t)> visit = [&](document::ObjectId id,size_t depth) {
        const auto* entry=snapshot.hierarchy().find(id);
        if (!entry) return;
        rows.push_back({id,depth,entry->mask});
        const auto* object=snapshot.objects().lookup_ref(id);
        const auto* group=object?std::get_if<document::GroupObject>(object):nullptr;
        if (group && !state.collapsed_groups.contains(id))
            for (auto it=group->children_ids.rbegin();it!=group->children_ids.rend();++it) visit(*it,depth+1);
    };
    for (auto it=layer.object_ids.rbegin();it!=layer.object_ids.rend();++it) visit(*it,0);
    return rows;
}
} // namespace pikdraw::desktop
