#include "pikdraw/vector/gestures/text_operations.hpp"
#include <sstream>
#include <algorithm>

namespace pikdraw::vector {

// --- LinkTextFramesCommand ---

LinkTextFramesCommand::LinkTextFramesCommand(document::ObjectId source_id, document::ObjectId target_id)
    : source_id_(source_id), target_id_(target_id) {}

document::CommandResult LinkTextFramesCommand::execute(document::Document& doc) {
    if (source_id_ == target_id_ || source_id_ == document::kInvalidObjectId || target_id_ == document::kInvalidObjectId) {
        return document::CommandResult::fail("Cannot link a frame to itself or invalid ID");
    }

    auto snap = doc.current_snapshot();
    auto src_opt = snap->objects().get(source_id_);
    auto tgt_opt = snap->objects().get(target_id_);
    if (!src_opt || !tgt_opt) {
        return document::CommandResult::fail("Target or source object not found");
    }

    if (!std::holds_alternative<document::TextObject>(*src_opt) ||
        !std::holds_alternative<document::TextObject>(*tgt_opt)) {
        return document::CommandResult::fail("Both objects must be text objects to link");
    }

    auto src_obj = std::get<document::TextObject>(*src_opt);
    auto tgt_obj = std::get<document::TextObject>(*tgt_opt);

    before_source_ = src_obj;
    before_target_ = tgt_obj;

    src_obj.next_frame_id = target_id_;
    tgt_obj.prev_frame_id = source_id_;

    // Handle overflow flow if source has fixed height
    if (src_obj.frame_height_mm > 0.0 && !src_obj.text.empty()) {
        typography::LayoutOptions options;
        options.alignment = src_obj.alignment;
        options.max_width_mm = src_obj.frame_width_mm;
        options.line_spacing_ratio = src_obj.line_spacing_ratio;
        options.letter_spacing_mm = src_obj.letter_spacing_mm;

        auto split = typography::split_text_flow(src_obj.text, src_obj.font, options, src_obj.frame_height_mm);
        if (split.has_overflow) {
            src_obj.text = split.fitted_text;
            src_obj.has_overflow = true;
            tgt_obj.text = split.overflow_text;
        }
    }

    auto map = snap->objects();
    map = map.set(source_id_, document::ObjectVariant{std::move(src_obj)});
    map = map.set(target_id_, document::ObjectVariant{std::move(tgt_obj)});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult LinkTextFramesCommand::undo(document::Document& doc) {
    if (!before_source_ || !before_target_) {
        return document::CommandResult::fail("No state to undo LinkTextFramesCommand");
    }

    auto snap = doc.current_snapshot();
    auto map = snap->objects();
    map = map.set(source_id_, document::ObjectVariant{*before_source_});
    map = map.set(target_id_, document::ObjectVariant{*before_target_});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

// --- UnlinkTextFramesCommand ---

UnlinkTextFramesCommand::UnlinkTextFramesCommand(document::ObjectId frame_id)
    : frame_id_(frame_id) {}

document::CommandResult UnlinkTextFramesCommand::execute(document::Document& doc) {
    auto snap = doc.current_snapshot();
    auto obj_opt = snap->objects().get(frame_id_);
    if (!obj_opt || !std::holds_alternative<document::TextObject>(*obj_opt)) {
        return document::CommandResult::fail("Frame not found or not text");
    }

    auto frame = std::get<document::TextObject>(*obj_opt);
    if (!frame.is_linked()) {
        return document::CommandResult::no_change();
    }

    before_chain_.clear();
    before_chain_.push_back({frame_id_, frame});

    auto map = snap->objects();

    if (frame.prev_frame_id != document::kInvalidObjectId) {
        if (auto prev_opt = snap->objects().get(frame.prev_frame_id)) {
            if (std::holds_alternative<document::TextObject>(*prev_opt)) {
                auto prev_obj = std::get<document::TextObject>(*prev_opt);
                before_chain_.push_back({frame.prev_frame_id, prev_obj});
                prev_obj.next_frame_id = document::kInvalidObjectId;
                map = map.set(frame.prev_frame_id, document::ObjectVariant{std::move(prev_obj)});
            }
        }
    }

    if (frame.next_frame_id != document::kInvalidObjectId) {
        if (auto next_opt = snap->objects().get(frame.next_frame_id)) {
            if (std::holds_alternative<document::TextObject>(*next_opt)) {
                auto next_obj = std::get<document::TextObject>(*next_opt);
                before_chain_.push_back({frame.next_frame_id, next_obj});
                next_obj.prev_frame_id = document::kInvalidObjectId;
                map = map.set(frame.next_frame_id, document::ObjectVariant{std::move(next_obj)});
            }
        }
    }

    frame.prev_frame_id = document::kInvalidObjectId;
    frame.next_frame_id = document::kInvalidObjectId;
    frame.has_overflow = false;
    map = map.set(frame_id_, document::ObjectVariant{std::move(frame)});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult UnlinkTextFramesCommand::undo(document::Document& doc) {
    if (before_chain_.empty()) {
        return document::CommandResult::fail("No state to undo UnlinkTextFramesCommand");
    }

    auto snap = doc.current_snapshot();
    auto map = snap->objects();
    for (const auto& [id, obj] : before_chain_) {
        map = map.set(id, document::ObjectVariant{obj});
    }

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

// --- CombineTextObjectsCommand ---

CombineTextObjectsCommand::CombineTextObjectsCommand(std::vector<document::ObjectId> text_ids, size_t target_layer_idx, document::ObjectId* out_created_id)
    : source_ids_(std::move(text_ids)), target_layer_idx_(target_layer_idx), out_created_id_(out_created_id) {}

document::CommandResult CombineTextObjectsCommand::execute(document::Document& doc) {
    if (source_ids_.size() < 2) {
        return document::CommandResult::fail("Combining requires at least two text objects");
    }

    auto snap = doc.current_snapshot();
    removed_objects_.clear();
    std::vector<document::TextObject> texts;

    for (auto id : source_ids_) {
        auto opt = snap->objects().get(id);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
            return document::CommandResult::fail("One or more objects to combine are not valid text objects");
        }
        auto t = std::get<document::TextObject>(*opt);
        removed_objects_.push_back({id, t});
        texts.push_back(t);
    }

    // Sort texts vertically (top to bottom) so combination follows reading order
    std::sort(texts.begin(), texts.end(), [](const document::TextObject& a, const document::TextObject& b) {
        if (std::abs(a.position.y - b.position.y) > 2.0) {
            return a.position.y < b.position.y;
        }
        return a.position.x < b.position.x;
    });

    std::string combined_str;
    for (size_t i = 0; i < texts.size(); ++i) {
        if (i > 0) combined_str += "\n";
        combined_str += texts[i].text;
    }

    document::TextObject combined = texts.front();
    if (created_id_ == document::kInvalidObjectId) {
        created_id_ = document::generate_object_id();
    }
    combined.id = created_id_;
    combined.text = combined_str;
    combined.next_frame_id = document::kInvalidObjectId;
    combined.prev_frame_id = document::kInvalidObjectId;
    combined.has_overflow = false;

    auto page = snap->page();
    auto map = snap->objects();

    // Remove source objects
    for (auto id : source_ids_) {
        map = map.remove(id);
        for (auto& layer : page.layers) {
            auto it = std::find(layer.object_ids.begin(), layer.object_ids.end(), id);
            if (it != layer.object_ids.end()) {
                layer.object_ids.erase(it);
                break;
            }
        }
    }

    // Add combined object
    map = map.set(created_id_, document::ObjectVariant{std::move(combined)});
    if (target_layer_idx_ >= page.layers.size()) target_layer_idx_ = page.layers.size() - 1;
    page.layers[target_layer_idx_].object_ids.push_back(created_id_); if (out_created_id_) *out_created_id_ = created_id_;

    doc.apply_mutation(std::move(page), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult CombineTextObjectsCommand::undo(document::Document& doc) {
    if (removed_objects_.empty() || created_id_ == document::kInvalidObjectId) {
        return document::CommandResult::fail("No state to undo CombineTextObjectsCommand");
    }

    auto snap = doc.current_snapshot();
    auto page = snap->page();
    auto map = snap->objects();

    // Remove created object
    map = map.remove(created_id_);
    for (auto& layer : page.layers) {
        auto it = std::find(layer.object_ids.begin(), layer.object_ids.end(), created_id_);
        if (it != layer.object_ids.end()) {
            layer.object_ids.erase(it);
            break;
        }
    }

    // Restore removed objects
    for (const auto& [id, obj] : removed_objects_) {
        map = map.set(id, document::ObjectVariant{obj});
    }
    if (target_layer_idx_ >= page.layers.size()) target_layer_idx_ = 0;
    for (const auto& [id, _] : removed_objects_) {
        page.layers[target_layer_idx_].object_ids.push_back(id);
    }

    doc.apply_mutation(std::move(page), std::move(map));
    return document::CommandResult::ok();
}

// --- SplitTextLinesCommand ---

SplitTextLinesCommand::SplitTextLinesCommand(document::ObjectId text_id, size_t target_layer_idx, std::vector<document::ObjectId>* out_created_ids)
    : source_id_(text_id), target_layer_idx_(target_layer_idx), out_created_ids_(out_created_ids) {}

document::CommandResult SplitTextLinesCommand::execute(document::Document& doc) {
    auto snap = doc.current_snapshot();
    auto opt = snap->objects().get(source_id_);
    if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
        return document::CommandResult::fail("Source object not found or not text");
    }

    auto src = std::get<document::TextObject>(*opt);
    removed_source_ = src;

    // Split text by newlines
    std::vector<std::string> lines;
    std::stringstream ss(src.text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    if (lines.size() <= 1) {
        return document::CommandResult::no_change();
    }

    auto page = snap->page();
    auto map = snap->objects();

    // Remove source object
    map = map.remove(source_id_);
    for (auto& layer : page.layers) {
        auto it = std::find(layer.object_ids.begin(), layer.object_ids.end(), source_id_);
        if (it != layer.object_ids.end()) {
            layer.object_ids.erase(it);
            break;
        }
    }

    bool ids_preallocated = (created_ids_.size() == lines.size());
    if (!ids_preallocated) created_ids_.clear();

    const double line_spacing = src.font.size_mm() * src.line_spacing_ratio;
    for (size_t i = 0; i < lines.size(); ++i) {
        document::TextObject line_obj = src;
        document::ObjectId lid = ids_preallocated ? created_ids_[i] : document::generate_object_id();
        if (!ids_preallocated) created_ids_.push_back(lid);

        line_obj.id = lid;
        line_obj.text = lines[i];
        line_obj.frame_width_mm = 0.0;
        line_obj.frame_height_mm = 0.0;
        line_obj.next_frame_id = document::kInvalidObjectId;
        line_obj.prev_frame_id = document::kInvalidObjectId;
        line_obj.has_overflow = false;
        line_obj.position.y += i * line_spacing;

        map = map.set(lid, document::ObjectVariant{std::move(line_obj)});
        if (target_layer_idx_ >= page.layers.size()) target_layer_idx_ = page.layers.size() - 1;
        page.layers[target_layer_idx_].object_ids.push_back(lid); if (out_created_ids_) *out_created_ids_ = created_ids_;
    }

    doc.apply_mutation(std::move(page), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult SplitTextLinesCommand::undo(document::Document& doc) {
    if (!removed_source_ || created_ids_.empty()) {
        return document::CommandResult::fail("No state to undo SplitTextLinesCommand");
    }

    auto snap = doc.current_snapshot();
    auto page = snap->page();
    auto map = snap->objects();

    // Remove created line objects
    for (auto cid : created_ids_) {
        map = map.remove(cid);
        for (auto& layer : page.layers) {
            auto it = std::find(layer.object_ids.begin(), layer.object_ids.end(), cid);
            if (it != layer.object_ids.end()) {
                layer.object_ids.erase(it);
                break;
            }
        }
    }

    // Restore source object
    map = map.set(source_id_, document::ObjectVariant{*removed_source_});
    if (target_layer_idx_ >= page.layers.size()) target_layer_idx_ = 0;
    page.layers[target_layer_idx_].object_ids.push_back(source_id_);

    doc.apply_mutation(std::move(page), std::move(map));
    return document::CommandResult::ok();
}

// --- AttachTextToPathCommand ---

AttachTextToPathCommand::AttachTextToPathCommand(document::ObjectId text_id, document::ObjectId path_id)
    : text_id_(text_id), path_id_(path_id) {}

document::CommandResult AttachTextToPathCommand::execute(document::Document& doc) {
    auto snap = doc.current_snapshot();
    auto txt_opt = snap->objects().get(text_id_);
    auto pth_opt = snap->objects().get(path_id_);
    if (!txt_opt || !pth_opt) {
        return document::CommandResult::fail("Text or Path object not found");
    }

    if (!std::holds_alternative<document::TextObject>(*txt_opt)) {
        return document::CommandResult::fail("First object must be a text object");
    }
    if (!std::holds_alternative<document::PathObject>(*pth_opt)) {
        return document::CommandResult::fail("Second object must be a vector path object");
    }

    auto txt = std::get<document::TextObject>(*txt_opt);
    before_text_ = txt;

    txt.path_binding_id = path_id_;
    auto map = snap->objects();
    map = map.set(text_id_, document::ObjectVariant{std::move(txt)});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult AttachTextToPathCommand::undo(document::Document& doc) {
    if (!before_text_) {
        return document::CommandResult::fail("No state to undo AttachTextToPathCommand");
    }

    auto snap = doc.current_snapshot();
    auto map = snap->objects();
    map = map.set(text_id_, document::ObjectVariant{*before_text_});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

// --- DetachTextFromPathCommand ---

DetachTextFromPathCommand::DetachTextFromPathCommand(document::ObjectId text_id)
    : text_id_(text_id) {}

document::CommandResult DetachTextFromPathCommand::execute(document::Document& doc) {
    auto snap = doc.current_snapshot();
    auto txt_opt = snap->objects().get(text_id_);
    if (!txt_opt || !std::holds_alternative<document::TextObject>(*txt_opt)) {
        return document::CommandResult::fail("Text object not found");
    }

    auto txt = std::get<document::TextObject>(*txt_opt);
    if (!txt.is_text_on_path()) {
        return document::CommandResult::no_change();
    }

    before_text_ = txt;
    txt.path_binding_id = document::kInvalidObjectId;
    txt.path_offset_mm = 0.0;
    txt.path_reversed = false;
    txt.path_baseline_offset_mm = 0.0;

    auto map = snap->objects();
    map = map.set(text_id_, document::ObjectVariant{std::move(txt)});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

document::CommandResult DetachTextFromPathCommand::undo(document::Document& doc) {
    if (!before_text_) {
        return document::CommandResult::fail("No state to undo DetachTextFromPathCommand");
    }

    auto snap = doc.current_snapshot();
    auto map = snap->objects();
    map = map.set(text_id_, document::ObjectVariant{*before_text_});

    doc.apply_mutation(snap->page(), std::move(map));
    return document::CommandResult::ok();
}

} // namespace pikdraw::vector
