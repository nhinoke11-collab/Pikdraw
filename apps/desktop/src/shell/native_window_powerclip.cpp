#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include "native_window_menus.hpp"
#include "native_window_powerclip_ui.hpp"
#include "native_window_length_input.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include <cmath>
#include <algorithm>

namespace pikdraw::desktop {
bool NativeWindow::is_in_isolation_mode() const noexcept {
    try {
        if (isolation_stack_.empty()) return false;
        const auto snapshot = root().document().current_snapshot();
        if (snapshot->document_id() != isolation_document_id_ || snapshot->epoch() != isolation_epoch_ ||
            root().document().page_navigation_generation() != isolation_page_) return false;
        document::ObjectId previous = 0;
        for (auto id : isolation_stack_) {
            const auto* entry = snapshot->hierarchy().find(id);
            const auto* object = snapshot->objects().lookup_ref(id);
            if (!entry || entry->parent != previous || !snapshot->hierarchy().editable(id) ||
                !object || !std::holds_alternative<document::GroupObject>(*object)) return false;
            previous = id;
        }
        return true;
    } catch (...) { return false; }
}
void NativeWindow::synchronize_powerclip_view() {
    if (!isolation_stack_.empty() && !is_in_isolation_mode()) {
        if (drawing_tool_) drawing_tool_->cancel();
        if (pen_tool_) pen_tool_->cancel();
        isolation_stack_.clear(); isolation_restore_selection_.clear();
        ++isolation_generation_; canvas_cache_valid_ = false;
        root().vector_controller().set_selection_scope(0);
        root().vector_controller().selection().clear();
    }
    if (drawing_tool_) drawing_tool_->set_destination_owner(current_isolated_group_id().value_or(0));
    if (pen_tool_) pen_tool_->set_destination_owner(current_isolated_group_id().value_or(0));
    root().renderer().set_view_context({current_isolated_group_id().value_or(0), isolation_generation_,
                                       isolation_reveal_, isolation_frame_});
}
void NativeWindow::enter_group_isolation(document::ObjectId id) {
    synchronize_powerclip_view();
    const auto snapshot = root().document().current_snapshot();
    const auto* object = snapshot->objects().lookup_ref(id);
    if (!object || !std::holds_alternative<document::GroupObject>(*object) ||
        !snapshot->hierarchy().editable(id)) return;
    cancel_canvas_interaction(); cancel_numeric_field();
    if (drawing_tool_) drawing_tool_->cancel();
    if (pen_tool_) pen_tool_->cancel();
    set_active_tool(0);
    if (isolation_stack_.empty()) isolation_restore_selection_ = root().vector_controller().selection().selected_ids();
    isolation_stack_.clear();
    for (auto current = id; current;) {
        isolation_stack_.push_back(current);
        current = snapshot->hierarchy().find(current)->parent;
    }
    std::reverse(isolation_stack_.begin(), isolation_stack_.end());
    isolation_document_id_ = snapshot->document_id(); isolation_epoch_ = snapshot->epoch();
    // Owner IDs bind the active page; the navigation generation also rejects an away-and-back switch.
    isolation_page_ = root().document().page_navigation_generation(); isolation_reveal_ = true; isolation_frame_ = false;
    ++isolation_generation_; canvas_cache_valid_ = false;
    root().vector_controller().set_selection_scope(id);
    const auto contents = vector::SelectionModel::scope_roots(*snapshot, id);
    root().vector_controller().set_selection(contents.empty() ? std::vector<document::ObjectId>{}
        : std::vector<document::ObjectId>{contents.front()}, *snapshot);
    root().vector_controller().set_selection_scope(id);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Sửa nội dung PowerClip · Esc hủy thao tác, Esc lần nữa kết thúc"
        : "Editing PowerClip contents · Esc cancels a draft, then finishes editing";
    synchronize_powerclip_view();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
void NativeWindow::exit_group_isolation() {
    synchronize_powerclip_view();
    if (isolation_stack_.empty()) return;
    cancel_canvas_interaction(); cancel_numeric_field();
    if (drawing_tool_) drawing_tool_->cancel();
    if (pen_tool_) pen_tool_->cancel();
    set_active_tool(0);
    const auto last = isolation_stack_.back(); isolation_stack_.pop_back();
    ++isolation_generation_; canvas_cache_valid_ = false; isolation_frame_ = false; isolation_reveal_ = true;
    root().vector_controller().set_selection_scope(current_isolated_group_id().value_or(0));
    const auto snapshot = root().document().current_snapshot();
    root().vector_controller().set_selection(isolation_stack_.empty() && !isolation_restore_selection_.empty()
        ? isolation_restore_selection_ : std::vector<document::ObjectId>{last}, *snapshot);
    if (isolation_stack_.empty()) isolation_restore_selection_.clear();
    synchronize_powerclip_view();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
void NativeWindow::exit_all_isolation() {
    while (is_in_isolation_mode()) exit_group_isolation();
    synchronize_powerclip_view();
}
bool NativeWindow::is_object_in_isolated_group(document::ObjectId id) const {
    const auto owner = current_isolated_group_id();
    if (!owner) return true;
    const auto snapshot = root().document().current_snapshot();
    const auto* entry = snapshot->hierarchy().find(id);
    if (!entry || id == *owner || !snapshot->hierarchy().contains(*owner, id)) return false;
    return isolation_frame_ ? entry->mask && entry->parent == *owner : !entry->mask;
}
void NativeWindow::select_powerclip_contents(document::ObjectId id) {
    enter_group_isolation(id);
    if (current_isolated_group_id() != id) return;
    isolation_reveal_ = false; ++isolation_generation_;
    root().vector_controller().set_selection(vector::SelectionModel::scope_roots(
        *root().document().current_snapshot(), id), *root().document().current_snapshot());
    synchronize_powerclip_view();
}
void NativeWindow::edit_powerclip_frame(document::ObjectId id) {
    enter_group_isolation(id);
    if (current_isolated_group_id() != id) return;
    const auto snapshot = root().document().current_snapshot();
    const auto group = std::get<document::GroupObject>(*snapshot->objects().get(id));
    if (!group.clip) return;
    isolation_frame_ = true; isolation_reveal_ = false; ++isolation_generation_;
    root().vector_controller().set_selection_scope(id, true);
    root().vector_controller().set_selection({group.clip->mask_object_id}, *snapshot);
    synchronize_powerclip_view();
}
void NativeWindow::begin_powerclip_target_pick(bool copy) {
    const auto snapshot = root().document().current_snapshot();
    powerclip_pick_selection_ = root().vector_controller().selection().selected_ids();
    if(copy) { const auto target=powerclip_properties_target(); if(!target)return; powerclip_pick_selection_={*target}; }
    if (powerclip_pick_selection_.empty() || (copy && (powerclip_pick_selection_.size() != 1 ||
        !workflow().is_clipping_container(powerclip_pick_selection_.front())))) return;
    cancel_canvas_interaction();
    powerclip_target_pick_ = copy ? 2 : 1; powerclip_pick_stamp_ = vector::GestureStamp(*snapshot);
    status_msg_ = copy ? "Click the source PowerClip frame · Esc cancels"
                       : "Click the destination frame · Esc cancels";
}
bool NativeWindow::handle_powerclip_target_pick(geometry::Point2D point) {
    if (!powerclip_target_pick_) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!powerclip_pick_stamp_ || !powerclip_pick_stamp_->matches(*snapshot)) {
        powerclip_target_pick_ = 0; status_msg_ = "PowerClip target picking cancelled: document changed"; return true;
    }
    const auto hit=powerclip_ui::pick_frame(*snapshot,point,6.0/root().renderer().camera().zoom(),
        powerclip_pick_selection_,powerclip_target_pick_==2);
    if (!hit) return true;
    if (powerclip_target_pick_ == 2) {
        if (!workflow().copy_powerclip_contents(*hit, powerclip_pick_selection_.front())) return true;
    } else {
        auto command = std::make_unique<document::ClipCommand>(document::ClipOperation::Place, *hit, powerclip_pick_selection_);
        const auto result_id = workflow().is_clipping_container(*hit) ? *hit : command->result_id();
        if (!root().document().execute_command(std::move(command)).success) return true;
        root().vector_controller().set_selection({result_id}, *root().document().current_snapshot());
    }
    powerclip_target_pick_ = 0; powerclip_pick_stamp_.reset(); canvas_cache_valid_ = false;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}
std::optional<document::ObjectId> NativeWindow::powerclip_properties_target() const {
    if (auto id = current_isolated_group_id()) {
        const auto snapshot=root().document().current_snapshot();
        const auto* object=snapshot->objects().lookup_ref(*id);
        const auto* group=object?std::get_if<document::GroupObject>(object):nullptr;
        if(group&&group->clip)return id;
    }
    const auto& ids = root().vector_controller().selection().selected_ids();
    if (ids.size() != 1) return {};
    const auto snapshot = root().document().current_snapshot();
    for (auto id = ids.front(); id;) {
        const auto* object = snapshot->objects().lookup_ref(id);
        const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
        if (group && group->clip) return id;
        const auto* entry = snapshot->hierarchy().find(id);
        id = entry ? entry->parent : 0;
    }
    return {};
}
void NativeWindow::register_powerclip_actions(CompositionRoot& document_root) {
    auto& dispatcher = document_root.window().action_dispatcher();
    for (const auto& control : powerclip_ui::controls) {
        const auto id = std::string(control.action);
        dispatcher.register_action({id, std::string(control.vi), std::string(control.en), "",
            [this,id] {
                const auto target = powerclip_properties_target();
                if (id == "action.object.powerclip.finish") { exit_group_isolation(); return; }
                if (!target) return;
                if (id == "action.object.powerclip.edit") enter_group_isolation(*target);
                else if (id == "action.object.powerclip.edit_frame") edit_powerclip_frame(*target);
                else if (id == "action.object.powerclip.select_contents") select_powerclip_contents(*target);
                else if (id == "action.object.powerclip.properties") { open_inspector_properties_panel(); active_tool_=0; }
                else if (id == "action.object.powerclip.copy_from") begin_powerclip_target_pick(true);
                else {
                    const auto snapshot = root().document().current_snapshot();
                    const auto group = std::get<document::GroupObject>(*snapshot->objects().get(*target));
                    if (id == "action.object.powerclip.lock") workflow().set_powerclip_lock_contents(*target,!group.clip->lock_contents_to_frame);
                    else if (id == "action.object.powerclip.preserve_appearance") workflow().set_powerclip_preserve_appearance(*target,!group.clip->preserve_frame_appearance);
                    else if (id == "action.object.powerclip.extract") workflow().extract_powerclip_contents(*target);
                    else if (id == "action.object.powerclip.release") {
                        exit_all_isolation();
                        root().vector_controller().set_selection({*target},*root().document().current_snapshot());
                        workflow().release_clipping_mask_selected();
                    } else {
                        auto fit = document::ClipContentFitMode::CenterContents;
                        if (id == "action.object.powerclip.fit_prop") fit=document::ClipContentFitMode::FitProportionally;
                        if (id == "action.object.powerclip.fill_prop") fit=document::ClipContentFitMode::FillProportionally;
                        if (id == "action.object.powerclip.stretch") fit=document::ClipContentFitMode::StretchToFill;
                        workflow().fit_powerclip_contents(*target,fit);
                    }
                }
                synchronize_powerclip_view();
#ifdef _WIN32
                if (hwnd_) InvalidateRect(hwnd_,nullptr,FALSE);
#endif
            }, [this,id] {
                if (id == "action.object.powerclip.finish") return is_in_isolation_mode();
                const auto target = powerclip_properties_target();
                if (!target) return false;
                const auto snapshot=root().document().current_snapshot();
                if (!snapshot->hierarchy().editable(*target) || snapshot->is_content_locked()) return false;
                const auto group=std::get<document::GroupObject>(*snapshot->objects().get(*target));
                if (id.find("fit_")!=std::string::npos || id=="action.object.powerclip.stretch" || id=="action.object.powerclip.extract")
                    return group.children_ids.size()>1;
                return true;
            }});
    }
    dispatcher.register_action({"action.object.powerclip.make","Tạo PowerClip","Make PowerClip","",
        [this]{ workflow().make_clipping_mask_selected(); },[this]{return workflow().can_make_clipping_mask_selected();}});
    dispatcher.register_action({"action.object.powerclip.place_inside","Đặt vào khung...","Place Inside Frame...","",
        [this]{begin_powerclip_target_pick();},[this]{return !root().vector_controller().selection().empty();}});
    dispatcher.register_action({"action.object.powerclip.create_empty_frame","Tạo khung rỗng","Create Empty Frame","",
        [this]{ const auto ids=root().vector_controller().selection().selected_ids(); if(ids.size()==1) workflow().create_empty_powerclip(ids.front()); },
        [this]{ const auto ids=root().vector_controller().selection().selected_ids(); if(ids.size()!=1)return false;
            const auto snapshot=root().document().current_snapshot(); const auto* object=snapshot->objects().lookup_ref(ids.front());
            return object && snapshot->hierarchy().editable(ids.front()) && document::frame_geometry(*object).has_value(); }});
    dispatcher.register_action({"action.object.powerclip.remove_frame","Chuyển thành đối tượng thường","Convert to Regular Object","",
        [this]{if(auto id=powerclip_properties_target()){exit_all_isolation();root().vector_controller().set_selection({*id},*root().document().current_snapshot());workflow().release_clipping_mask_selected();}},
        [this]{auto id=powerclip_properties_target();if(!id)return false;const auto snapshot=root().document().current_snapshot();
            return std::get<document::GroupObject>(*snapshot->objects().get(*id)).children_ids.size()==1;}});
}
#ifdef _WIN32
bool NativeWindow::paint_powerclip_controls(HDC dc,int left,int top,int right,bool vertical) {
    const auto target=powerclip_properties_target();
    if (!target) return false;
    const auto snapshot=root().document().current_snapshot();
    const auto group=std::get<document::GroupObject>(*snapshot->objects().get(*target));
    const auto& dispatcher=root().window().action_dispatcher();
    const bool vi=language_==UILanguage::Vietnamese;
    if (vertical) {
        RECT summary{left,top,right,top+45};
        std::wstring label=L"PowerClip #"+std::to_wstring(*target)+L" · "+std::to_wstring(group.children_ids.size()-1)+
            (vi?L" nội dung":L" contents");
        DrawTextW(dc,label.c_str(),-1,&summary,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);top+=140;
    }
    for(const auto& box:powerclip_ui::layout(left,top,right,vertical,is_in_isolation_mode())) {
        const auto& control=powerclip_ui::controls[box.control];
        const bool enabled=dispatcher.is_action_enabled(control.action);
        RECT rect{box.left,box.top,box.right,box.bottom};
        HBRUSH brush=CreateSolidBrush(theme_==ThemeMode::CyberDark?RGB(25,40,57):RGB(235,243,246));
        FillRect(dc,&rect,brush);DeleteObject(brush);
        SetTextColor(dc,enabled?(theme_==ThemeMode::CyberDark?RGB(0,242,195):RGB(0,105,90)):RGB(128,128,128));
        std::string label(vi?control.vi:control.en);
        if(box.control==4)label=(group.clip->lock_contents_to_frame?"[x] ":"[ ] ")+label;
        if(box.control==12)label=(group.clip->preserve_frame_appearance?"[x] ":"[ ] ")+label;
        const auto wide=utf8_to_wide(label);
        DrawTextW(dc,wide.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    const auto& ids=root().vector_controller().selection().selected_ids();
    if(ids.size()==1) {
        const auto bounds=workflow().get_object_bounds(ids.front());
        const auto* entry=snapshot->hierarchy().find(ids.front());
        if(bounds&&entry) {
            const std::array<double,5> values{bounds->x,bounds->y,bounds->width,bounds->height,
                std::atan2(entry->world.b,entry->world.a)*180./3.14159265358979323846};
            const std::array<const wchar_t*,5> labels{L"X ",L"Y ",L"W ",L"H ",L"Angle "};
            const std::array<NumericFieldId,5> fields{NumericFieldId::ObjectX,NumericFieldId::ObjectY,
                NumericFieldId::ObjectWidth,NumericFieldId::ObjectHeight,NumericFieldId::ObjectRotation};
            int start=left;
            const auto boxes=powerclip_ui::layout(left,top,right,false,is_in_isolation_mode());
            if(!vertical&&!boxes.empty())start=boxes.back().right+5;
            for(size_t i=0;i<fields.size();++i) {
                const int x=vertical?left+int(i%2)*(right-left)/2:start+int(i)*88;
                const int y=vertical?top-90+int(i/2)*28:top;
                RECT rect{x,y,vertical?x+(right-left)/2-4:x+84,y+25};
                if(rect.right>right)continue;
                const auto value=fields[i]==active_num_field_?num_input_buffer_+"|":
                    i==4?std::to_string(values[i]):format_mm_display_utf8(values[i],display_length_unit_,4);
                auto label=std::wstring(labels[i])+utf8_to_wide(value);
                SetTextColor(dc,theme_==ThemeMode::CyberDark?RGB(235,235,235):RGB(30,35,40));
                DrawTextW(dc,label.c_str(),-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
    }
    return true;
}
bool NativeWindow::handle_powerclip_controls(int x,int y,int left,int top,int right,bool vertical) {
    if(!powerclip_properties_target())return false;
    if(vertical)top+=140;
    for(const auto& box:powerclip_ui::layout(left,top,right,vertical,is_in_isolation_mode()))
        if(x>=box.left&&x<box.right&&y>=box.top&&y<box.bottom) {
            const auto& control=powerclip_ui::controls[box.control];
            status_msg_=std::string(control.help);
            root().window().action_dispatcher().dispatch(control.action);return true;
        }
    if(root().vector_controller().selection().count()==1) {
        const std::array<NumericFieldId,5> fields{NumericFieldId::ObjectX,NumericFieldId::ObjectY,
            NumericFieldId::ObjectWidth,NumericFieldId::ObjectHeight,NumericFieldId::ObjectRotation};
        const auto boxes=powerclip_ui::layout(left,top,right,false,is_in_isolation_mode());
        const int start=boxes.empty()?left:boxes.back().right+5;
        for(size_t i=0;i<fields.size();++i) {
            const int fx=vertical?left+int(i%2)*(right-left)/2:start+int(i)*88;
            const int fy=vertical?top-90+int(i/2)*28:top;
            const int fr=vertical?fx+(right-left)/2-4:fx+84;
            if(fr<=right&&x>=fx&&x<fr&&y>=fy&&y<fy+25){start_numeric_input(fields[i]);return true;}
        }
    }
    return false;
}
#endif
} // namespace pikdraw::desktop
