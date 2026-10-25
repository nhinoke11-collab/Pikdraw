#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include "../clipboard/native_clipboard.hpp"
#include "../utf8_path.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
using namespace detail;

namespace {

class AddClonedHierarchyCommand final : public document::DocumentCommand {
public:
    AddClonedHierarchyCommand(
        std::vector<document::ObjectVariant> cloned_objects,
        std::vector<document::ObjectId> top_level_ids,
        size_t layer_idx = 0,
        std::optional<size_t> insert_position = std::nullopt,
        std::string command_name = "Add Cloned Hierarchy", document::ObjectId owner = 0
    )
        : cloned_objects_(std::move(cloned_objects)),
          top_level_ids_(std::move(top_level_ids)),
          layer_idx_(layer_idx),
          insert_position_(insert_position),
          name_(std::move(command_name)), owner_(owner) {}

    std::string_view name() const noexcept override { return name_; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("AddClonedHierarchyCommand already executed");
        auto page = doc.page();
        if (layer_idx_ >= page.layers.size()) return document::CommandResult::fail("Invalid layer index");
        if (page.layers[layer_idx_].locked) return document::CommandResult::fail("Layer is locked");

        auto objects = doc.objects();
        for (const auto& obj : cloned_objects_) {
            const auto id = std::visit([](const auto& o) { return o.id; }, obj);
            if (objects.contains(id)) return document::CommandResult::fail("Object ID collision");
            objects = objects.set(id, obj);
            document::reserve_object_id(id);
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->authoring_source) {
                    for (const auto& subpath : path->authoring_source->subpaths) {
                        document::reserve_path_element_id(subpath.id);
                        for (const auto& node : subpath.nodes) document::reserve_path_element_id(node.id);
                        for (const auto& segment : subpath.segments) document::reserve_path_element_id(segment.id);
                    }
                }
            }
        }

        if (owner_) {
            const auto snapshot=doc.current_snapshot();
            const auto* entry=snapshot->hierarchy().find(owner_);
            auto owner_object=objects.get(owner_);
            auto* group=owner_object?std::get_if<document::GroupObject>(&*owner_object):nullptr;
            if(!entry||!group||!snapshot->hierarchy().editable(owner_)) return document::CommandResult::fail("Invalid paste scope");
            const auto inverse=entry->world.inverted();if(!inverse)return document::CommandResult::fail("Singular paste scope");
            for(auto id:top_level_ids_) {
                auto object=*objects.get(id);
                std::visit([&](auto& value){value.transform=*inverse*value.transform;},object);
                objects=objects.set(id,object);
            }
            const auto at=std::min(insert_position_.value_or(group->children_ids.size()),group->children_ids.size());
            group->children_ids.insert(group->children_ids.begin()+at,top_level_ids_.begin(),top_level_ids_.end());
            if(group->clip)group->clip->is_empty_frame=false;
            objects=objects.set(owner_,*owner_object);
        } else {
            auto& roots=page.layers[layer_idx_].object_ids;
            const auto at=std::min(insert_position_.value_or(roots.size()),roots.size());
            roots.insert(roots.begin()+at,top_level_ids_.begin(),top_level_ids_.end());
        }

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_) {
            return document::CommandResult::fail("No state to undo");
        }
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectVariant> cloned_objects_;
    std::vector<document::ObjectId> top_level_ids_;
    size_t layer_idx_{0};
    std::optional<size_t> insert_position_;
    std::string name_;
    document::ObjectId owner_{0};
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class ReplaceClonedHierarchyCommand final : public document::DocumentCommand {
public:
    ReplaceClonedHierarchyCommand(
        std::vector<document::ObjectId> ids_to_remove,
        std::vector<document::ObjectVariant> new_objects,
        std::vector<document::ObjectId> new_top_level_ids,
        size_t layer_idx = 0,
        std::string command_name = "Array Instances"
    )
        : ids_to_remove_(std::move(ids_to_remove)),
          new_objects_(std::move(new_objects)),
          new_top_level_ids_(std::move(new_top_level_ids)),
          layer_idx_(layer_idx),
          name_(std::move(command_name)) {}

    std::string_view name() const noexcept override { return name_; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("ReplaceClonedHierarchyCommand already executed");
        auto page = doc.page();
        if (layer_idx_ >= page.layers.size()) return document::CommandResult::fail("Invalid layer index");
        if (page.layers[layer_idx_].locked) return document::CommandResult::fail("Layer is locked");

        auto objects = doc.objects();

        std::unordered_set<document::ObjectId> all_to_remove(ids_to_remove_.begin(), ids_to_remove_.end());
        for (const auto id : ids_to_remove_) {
            std::vector<document::ObjectId> desc;
            collect_all_descendants(id, objects, desc);
            all_to_remove.insert(desc.begin(), desc.end());
        }

        for (const auto id : all_to_remove) {
            objects = objects.remove(id);
        }

        auto& layer_roots = page.layers[layer_idx_].object_ids;
        std::vector<document::ObjectId> updated_roots;
        updated_roots.reserve(layer_roots.size());
        for (const auto id : layer_roots) {
            if (!all_to_remove.contains(id)) {
                updated_roots.push_back(id);
            }
        }
        layer_roots = std::move(updated_roots);

        for (const auto& obj : new_objects_) {
            const auto id = std::visit([](const auto& o) { return o.id; }, obj);
            objects = objects.set(id, obj);
            document::reserve_object_id(id);
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->authoring_source) {
                    for (const auto& subpath : path->authoring_source->subpaths) {
                        document::reserve_path_element_id(subpath.id);
                        for (const auto& node : subpath.nodes) document::reserve_path_element_id(node.id);
                        for (const auto& segment : subpath.segments) document::reserve_path_element_id(segment.id);
                    }
                }
            }
        }

        layer_roots.insert(layer_roots.end(), new_top_level_ids_.begin(), new_top_level_ids_.end());

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_) {
            return document::CommandResult::fail("No state to undo");
        }
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> ids_to_remove_;
    std::vector<document::ObjectVariant> new_objects_;
    std::vector<document::ObjectId> new_top_level_ids_;
    size_t layer_idx_{0};
    std::string name_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};


} // namespace

bool DocumentWorkflow::delete_selected() {
    auto& controller = root_.vector_controller();
    if (controller.selection().empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto to_delete = snapshot->hierarchy().roots(controller.selection().selected_ids());

    auto composite = std::make_unique<document::CompositeCommand>("Delete Selected");
    for (auto id : to_delete) {
        composite->add_command(std::make_unique<document::RemoveObjectCommand>(id));
    }
    controller.selection().clear();
    const auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::copy_selected(void* hwnd) {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto root_ids = filter_top_level_roots(selected, snapshot->objects());
    if (root_ids.empty())
        return false;

    document::Page clip_page = snapshot->page();
    clip_page.layers.clear();
    document::Layer clip_layer;
    clip_layer.name = "Clipboard";
    clip_layer.visible = true;
    clip_layer.locked = false;
    clip_layer.object_ids = root_ids;

    document::PersistentObjectMap clip_objects;
    std::string text_accumulator;

    for (const auto rid : root_ids) {
        if (auto obj = snapshot->objects().get(rid)) {
            auto root_object=*obj;
            std::visit([&](auto& value){value.transform=snapshot->hierarchy().find(rid)->world;},root_object);
            clip_objects = clip_objects.set(rid, root_object);
            if (const auto* txt = std::get_if<document::TextObject>(&*obj)) {
                if (!text_accumulator.empty()) text_accumulator += "\n";
                text_accumulator += txt->text;
            }
        }
        std::vector<document::ObjectId> descendants;
        collect_all_descendants(rid, snapshot->objects(), descendants);
        for (const auto cid : descendants) {
            if (auto cobj = snapshot->objects().get(cid)) {
                clip_objects = clip_objects.set(cid, *cobj);
                if (const auto* txt = std::get_if<document::TextObject>(&*cobj)) {
                    if (!text_accumulator.empty()) text_accumulator += "\n";
                    text_accumulator += txt->text;
                }
            }
        }
    }
    clip_page.layers.push_back(std::move(clip_layer));

    auto clip_snapshot = std::make_shared<const document::DocumentSnapshot>(
        snapshot->epoch(),
        snapshot->revision(),
        std::move(clip_page),
        std::move(clip_objects),
        snapshot->document_id().empty() ? std::string("clipboard-document") : snapshot->document_id()
    );

    const auto binary_bytes = document::NativeFormat::serialize(*clip_snapshot);
    const auto svg_str = svg_exporter_.export_to_string(*clip_snapshot);

    return NativeClipboard::write(
        static_cast<HWND>(hwnd),
        std::string_view(reinterpret_cast<const char*>(binary_bytes.data()), binary_bytes.size()),
        svg_str,
        text_accumulator
    );
}

bool DocumentWorkflow::cut_selected(void* hwnd) {
    if (!copy_selected(hwnd))
        return false;
    return delete_selected();
}

bool DocumentWorkflow::paste(void* hwnd, PastePlacement placement, double offset_x, double offset_y) {
    (void)hwnd;
    // 1. Try native snapshot
    if (NativeClipboard::has_native_snapshot()) {
        auto raw_bytes = NativeClipboard::read_native_snapshot();
        if (raw_bytes && !raw_bytes->empty()) {
            std::string err;
            auto clip_snap = document::NativeFormat::deserialize(
                reinterpret_cast<const uint8_t*>(raw_bytes->data()),
                raw_bytes->size(),
                &err
            );
            if (clip_snap && !clip_snap->page().layers.empty()) {
                const auto& clip_root_ids = clip_snap->page().layers[0].object_ids;
                if (!clip_root_ids.empty()) {
                    geometry::Transform2D shift;
                    if (placement == PastePlacement::InPlace || placement == PastePlacement::InFront || placement == PastePlacement::InBack) {
                        shift = geometry::Transform2D::identity();
                    } else if (placement == PastePlacement::Offset) {
                        shift = geometry::Transform2D::translation(offset_x, offset_y);
                    } else {
                        // CenterOfViewport / default paste: offset by (+5, +5)
                        shift = geometry::Transform2D::translation(offset_x, offset_y);
                    }

                    auto cloned = document::clone_hierarchy(clip_root_ids, *clip_snap, shift);
                    if (!cloned.top_level_cloned_ids.empty()) {
                        std::optional<size_t> insert_pos = std::nullopt;
                        const auto current_snap = root_.document().current_snapshot();
                        if (!current_snap->page().layers.empty()) {
                            const auto& layer_roots = current_snap->page().layers[0].object_ids;
                            if (placement == PastePlacement::InFront) {
                                size_t max_idx = 0;
                                bool found = false;
                                for (size_t i = 0; i < layer_roots.size(); ++i) {
                                    if (std::find(clip_root_ids.begin(), clip_root_ids.end(), layer_roots[i]) != clip_root_ids.end()) {
                                        max_idx = (std::max)(max_idx, i);
                                        found = true;
                                    }
                                }
                                if (found) insert_pos = max_idx + 1;
                            } else if (placement == PastePlacement::InBack) {
                                size_t min_idx = layer_roots.size();
                                bool found = false;
                                for (size_t i = 0; i < layer_roots.size(); ++i) {
                                    if (std::find(clip_root_ids.begin(), clip_root_ids.end(), layer_roots[i]) != clip_root_ids.end()) {
                                        min_idx = (std::min)(min_idx, i);
                                        found = true;
                                    }
                                }
                                if (found) insert_pos = min_idx;
                            }
                        }

                        auto cmd = std::make_unique<AddClonedHierarchyCommand>(
                            std::move(cloned.cloned_objects),
                            cloned.top_level_cloned_ids,
                            active_layer_index(),
                            insert_pos,
                            "Paste", root_.vector_controller().selection_scope()
                        );
                        auto& controller = root_.vector_controller();
                        const auto res = root_.document().execute_command(std::move(cmd));
                        if (res.success) {
                            controller.selection().clear();
                            for (auto nid : cloned.top_level_cloned_ids) {
                                controller.selection().select(nid, vector::SelectionMode::Add);
                            }
                            sync_status_bar();
                            return true;
                        }
                    }
                }
            }
        }
    }

    // 2. Try SVG vector data
    if (NativeClipboard::has_svg()) {
        auto svg_str = NativeClipboard::read_svg();
        if (svg_str && !svg_str->empty()) {
            auto res = interop::SvgImporter::import_from_string(*svg_str);
            if (res.success && !res.objects.empty()) {
                document::PersistentObjectMap source_objects;
                for(const auto& object:res.objects)source_objects=source_objects.set(std::visit([](const auto& value){return value.id;},object),object);
                document::DocumentSnapshot source{1,0,res.page,source_objects};
                std::vector<document::ObjectId> roots;
                for(const auto& layer:res.page.layers)roots.insert(roots.end(),layer.object_ids.begin(),layer.object_ids.end());
                const auto shift=geometry::Transform2D::translation(offset_x,offset_y);
                auto cloned=document::clone_hierarchy(roots,source,shift);
                const auto new_ids=cloned.top_level_cloned_ids;
                auto composite=std::make_unique<AddClonedHierarchyCommand>(std::move(cloned.cloned_objects),new_ids,active_layer_index(),
                    std::nullopt,"Paste SVG",root_.vector_controller().selection_scope());
                const auto cmd_res = root_.document().execute_command(std::move(composite));
                if (cmd_res.success) {
                    auto& controller = root_.vector_controller();
                    controller.selection().clear();
                    for (auto nid : new_ids) {
                        controller.selection().select(nid, vector::SelectionMode::Add);
                    }
                    sync_status_bar();
                    return true;
                }
            }
        }
    }

    // 3. Try plain text
    if (NativeClipboard::has_text()) {
        auto text = NativeClipboard::read_text();
        if (text && !text->empty()) {
            const auto snap = root_.document().current_snapshot();
            const double px = snap->page().width_mm * 0.5 - 20.0;
            const double py = snap->page().height_mm * 0.5;
            auto tid = add_vietnamese_text(px, py, *text);
            return tid != document::kInvalidObjectId;
        }
    }

    return false;
}

bool DocumentWorkflow::paste_in_place(void* hwnd) {
    return paste(hwnd, PastePlacement::InPlace, 0.0, 0.0);
}

bool DocumentWorkflow::paste_in_front(void* hwnd) {
    return paste(hwnd, PastePlacement::InFront, 0.0, 0.0);
}

bool DocumentWorkflow::paste_in_back(void* hwnd) {
    return paste(hwnd, PastePlacement::InBack, 0.0, 0.0);
}

bool DocumentWorkflow::duplicate_selected(double offset_x, double offset_y) {
    auto& controller = root_.vector_controller();
    const auto ids = controller.selection().selected_ids();
    if (ids.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto root_ids = filter_top_level_roots(ids, snapshot->objects());
    if (root_ids.empty())
        return false;

    const auto shift = geometry::Transform2D::translation(offset_x, offset_y);
    auto cloned = document::clone_hierarchy(root_ids, *snapshot);
    for(auto& object:cloned.cloned_objects) {
        const auto id=std::visit([](const auto& value){return value.id;},object);
        for(auto source_id:root_ids) if(cloned.id_map.at(source_id)==id)
            std::visit([&](auto& value){value.transform=shift*snapshot->hierarchy().find(source_id)->parent_world*value.transform;},object);
    }
    if (cloned.top_level_cloned_ids.empty())
        return false;

    const auto* source_entry = snapshot->hierarchy().find(root_ids.front());
    const size_t duplicate_layer = source_entry ? source_entry->layer : active_layer_index();

    auto cmd = std::make_unique<AddClonedHierarchyCommand>(
        std::move(cloned.cloned_objects),
        cloned.top_level_cloned_ids,
        duplicate_layer,
        std::nullopt,
        "Duplicate Selected", controller.selection_scope()
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        controller.selection().clear();
        for (const auto nid : cloned.top_level_cloned_ids) {
            controller.selection().select(nid, vector::SelectionMode::Add);
        }

        // Start dynamic array session (SketchUp *N / /N)
        if (!controller.selection_scope() && (std::abs(offset_x) > 1e-6 || std::abs(offset_y) > 1e-6)) {
            start_array_session(
                root_ids,
                geometry::Point2D{offset_x, offset_y},
                cloned.top_level_cloned_ids
            );
        } else {
            end_array_session();
        }

        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::duplicate_selected(double offset_mm) {
    return duplicate_selected(offset_mm, offset_mm);
}

bool DocumentWorkflow::duplicate_at_origin() {
    return duplicate_selected(0.0, 0.0);
}

bool DocumentWorkflow::is_array_session_active() const noexcept {
    return array_session_.active;
}

const DocumentWorkflow::ArraySession& DocumentWorkflow::array_session() const noexcept {
    return array_session_;
}

void DocumentWorkflow::start_array_session(
    std::vector<document::ObjectId> source_ids,
    const geometry::Point2D& base_delta,
    std::vector<document::ObjectId> first_copy_ids
) {
    array_session_.active = true;
    array_session_.source_ids = std::move(source_ids);
    array_session_.base_delta = base_delta;
    array_session_.current_array_ids = std::move(first_copy_ids);
    array_session_.is_multiplier = true;
    array_session_.current_count = 1;
}

bool DocumentWorkflow::apply_array_multiplier(int count) {
    if (!array_session_.active || array_session_.source_ids.empty())
        return false;
    if (count < 1 || count > 1000)
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto* source_entry = snapshot->hierarchy().find(array_session_.source_ids.front());
    const size_t source_layer = source_entry ? source_entry->layer : active_layer_index();

    std::vector<document::ObjectVariant> all_new_objects;
    std::vector<document::ObjectId> all_new_top_level_ids;

    for (int k = 1; k <= count; ++k) {
        const double factor = static_cast<double>(k);
        const auto shift = geometry::Transform2D::translation(
            array_session_.base_delta.x * factor,
            array_session_.base_delta.y * factor
        );
        auto cloned = document::clone_hierarchy(array_session_.source_ids, *snapshot, shift);
        for (auto& obj : cloned.cloned_objects) {
            all_new_objects.push_back(std::move(obj));
        }
        for (auto tid : cloned.top_level_cloned_ids) {
            all_new_top_level_ids.push_back(tid);
        }
    }

    auto cmd = std::make_unique<ReplaceClonedHierarchyCommand>(
        array_session_.current_array_ids,
        std::move(all_new_objects),
        all_new_top_level_ids,
        source_layer,
        "Array Multiplier (*" + std::to_string(count) + ")"
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        array_session_.current_array_ids = all_new_top_level_ids;
        array_session_.is_multiplier = true;
        array_session_.current_count = count;

        root_.vector_controller().selection().clear();
        for (auto nid : all_new_top_level_ids) {
            root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
        }
        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::apply_array_divider(int divisions) {
    if (!array_session_.active || array_session_.source_ids.empty())
        return false;
    if (divisions < 1 || divisions > 1000)
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto* source_entry = snapshot->hierarchy().find(array_session_.source_ids.front());
    const size_t source_layer = source_entry ? source_entry->layer : active_layer_index();

    std::vector<document::ObjectVariant> all_new_objects;
    std::vector<document::ObjectId> all_new_top_level_ids;

    for (int k = 1; k <= divisions; ++k) {
        const double factor = static_cast<double>(k) / static_cast<double>(divisions);
        const auto shift = geometry::Transform2D::translation(
            array_session_.base_delta.x * factor,
            array_session_.base_delta.y * factor
        );
        auto cloned = document::clone_hierarchy(array_session_.source_ids, *snapshot, shift);
        for (auto& obj : cloned.cloned_objects) {
            all_new_objects.push_back(std::move(obj));
        }
        for (auto tid : cloned.top_level_cloned_ids) {
            all_new_top_level_ids.push_back(tid);
        }
    }

    auto cmd = std::make_unique<ReplaceClonedHierarchyCommand>(
        array_session_.current_array_ids,
        std::move(all_new_objects),
        all_new_top_level_ids,
        source_layer,
        "Array Divider (/" + std::to_string(divisions) + ")"
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        array_session_.current_array_ids = all_new_top_level_ids;
        array_session_.is_multiplier = false;
        array_session_.current_count = divisions;

        root_.vector_controller().selection().clear();
        for (auto nid : all_new_top_level_ids) {
            root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
        }
        sync_status_bar();
        return true;
    }
    return false;
}

void DocumentWorkflow::end_array_session() noexcept {
    array_session_.active = false;
    array_session_.source_ids.clear();
    array_session_.current_array_ids.clear();
    array_session_.base_delta = {0.0, 0.0};
    array_session_.current_count = 1;
}


} // namespace pikdraw::desktop
