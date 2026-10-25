#pragma once
#include <array>
#include <vector>
#include "native_window_menus.hpp"
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include <functional>
#include <string_view>

namespace pikdraw::desktop::powerclip_ui {
inline std::string_view action_for(MenuActionId id) {
    switch(id) {
    case MenuActionId::ObjectPowerClipMake:return "action.object.powerclip.make";
    case MenuActionId::ObjectPowerClipPlaceInside:return "action.object.powerclip.place_inside";
    case MenuActionId::ObjectPowerClipRelease:return "action.object.powerclip.release";
    case MenuActionId::ObjectPowerClipFitCenter:return "action.object.powerclip.fit_center";
    case MenuActionId::ObjectPowerClipFitProp:return "action.object.powerclip.fit_prop";
    case MenuActionId::ObjectPowerClipFillProp:return "action.object.powerclip.fill_prop";
    case MenuActionId::ObjectPowerClipStretch:return "action.object.powerclip.stretch";
    case MenuActionId::ObjectPowerClipSelectContents:return "action.object.powerclip.select_contents";
    case MenuActionId::ObjectPowerClipToggleLock:return "action.object.powerclip.lock";
    case MenuActionId::ObjectPowerClipEditFrame:return "action.object.powerclip.edit_frame";
    case MenuActionId::ObjectPowerClipExtract:return "action.object.powerclip.extract";
    case MenuActionId::ObjectPowerClipCopyFrom:return "action.object.powerclip.copy_from";
    case MenuActionId::ObjectPowerClipEmpty:return "action.object.powerclip.create_empty_frame";
    case MenuActionId::ObjectPowerClipRemoveFrame:return "action.object.powerclip.remove_frame";
    case MenuActionId::ObjectPowerClipAppearance:return "action.object.powerclip.preserve_appearance";
    default:return {};
    }
}
inline std::optional<document::ObjectId> pick_frame(const document::DocumentSnapshot& snapshot,
    geometry::Point2D point,double tolerance,std::span<const document::ObjectId> excluded,bool clip_only) {
    std::function<std::optional<document::ObjectId>(std::vector<document::ObjectId>)> pick;
    pick=[&](std::vector<document::ObjectId> ids)->std::optional<document::ObjectId> {
        std::erase_if(ids,[&](auto id){for(auto source:excluded)if(snapshot.hierarchy().contains(source,id))return true;return false;});
        while(!ids.empty()) {
            const auto hit=vector::SelectionModel::hit_test_detail_candidates(point,snapshot,ids,tolerance);
            if(!hit)return {};
            const auto* object=snapshot.objects().lookup_ref(hit->id);
            const auto* entry=snapshot.hierarchy().find(hit->id);
            if(object&&entry&&!entry->mask) {
                if(const auto* group=std::get_if<document::GroupObject>(object)) {
                    if(group->clip)return hit->id;
                    if(auto child=pick(group->children_ids))return child;
                } else if(!clip_only&&document::frame_geometry(*object))return hit->id;
            }
            std::erase(ids,hit->id);
        }
        return {};
    };
    return pick(vector::SelectionModel::scope_roots(snapshot,0));
}
struct Control {
    std::string_view action, en, vi, help;
};
inline constexpr std::array controls{
    Control{"action.object.powerclip.edit", "Edit Contents", "Sửa nội dung", "Reveal the contents outside the frame"},
    Control{"action.object.powerclip.finish", "Finish Editing", "Kết thúc sửa", "Return to the parent scope"},
    Control{"action.object.powerclip.edit_frame", "Edit Frame", "Sửa khung", "Edit frame geometry without moving contents"},
    Control{"action.object.powerclip.select_contents", "Select Contents", "Chọn nội dung", "Select contents in the clipped view"},
    Control{"action.object.powerclip.lock", "Move with Frame", "Đi cùng khung", "Link content transforms to the frame; contents remain editable"},
    Control{"action.object.powerclip.fit_center", "Center", "Căn giữa", "Center without changing size"},
    Control{"action.object.powerclip.fit_prop", "Fit", "Vừa khung", "Scale proportionally to fit inside the frame"},
    Control{"action.object.powerclip.fill_prop", "Fill", "Phủ khung", "Scale proportionally to cover the frame"},
    Control{"action.object.powerclip.stretch", "Stretch", "Kéo giãn", "Scale width and height independently"},
    Control{"action.object.powerclip.extract", "Extract Contents", "Trích nội dung", "Keep an empty PowerClip frame"},
    Control{"action.object.powerclip.release", "Release Clip", "Bỏ clip", "Return frame and contents to their parent"},
    Control{"action.object.powerclip.copy_from", "Copy Contents From...", "Chép nội dung từ...", "Append copies from another frame"},
    Control{"action.object.powerclip.preserve_appearance", "Keep Frame Fill/Stroke", "Giữ màu/nét khung", "Toggle the frame appearance"},
    Control{"action.object.powerclip.properties", "Properties", "Thuộc tính", "Open PowerClip properties"}
};
struct Box { int left, top, right, bottom; size_t control; };
inline std::vector<Box> layout(int left, int top, int right, bool vertical, bool editing) {
    std::vector<Box> boxes;
    if (vertical) {
        for (size_t i = 0; i < controls.size(); ++i) {
            if ((editing && i == 0) || (!editing && i == 1) || i == controls.size()-1) continue;
            const int y = top + int(boxes.size()) * 29;
            boxes.push_back({left,y,right,y+25,i});
        }
    } else {
        const std::array<size_t,5> indices{editing ? 1u : 0u, 2, 4, 6, 13};
        const std::array<int,5> widths{118,80,120,55,90};
        for (size_t i = 0; i < indices.size(); ++i) {
            if (left + widths[i] > right) break;
            boxes.push_back({left,top,left+widths[i],top+27,indices[i]}); left+=widths[i]+5;
        }
    }
    return boxes;
}
} // namespace pikdraw::desktop::powerclip_ui
