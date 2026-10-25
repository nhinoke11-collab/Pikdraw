#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/geometry/operations/path_cut.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace pikdraw::desktop {
namespace {

void collect_topmost_paths(const document::DocumentSnapshot& snapshot,
                           document::ObjectId id,
                           const geometry::Rect2D& gesture_bounds,
                           std::vector<document::ObjectId>& out) {
    const auto* entry = snapshot.hierarchy().find(id);
    if (!entry || entry->mask || !snapshot.hierarchy().editable(id)) return;
    const auto object = snapshot.objects().get(id);
    if (!object) return;
    if (const auto* group = std::get_if<document::GroupObject>(&*object)) {
        // Editing clipped contents requires entering the PowerClip scope first.
        if (group->clip) return;
        for (auto child = group->children_ids.rbegin(); child != group->children_ids.rend(); ++child)
            collect_topmost_paths(snapshot, *child, gesture_bounds, out);
        return;
    }
    if (!entry->bounds || !entry->bounds->intersects(gesture_bounds)) return;
    out.push_back(id);
}

std::vector<document::ObjectId> topmost_path_candidates(
    const document::DocumentSnapshot& snapshot,
    const geometry::Rect2D& gesture_bounds) {
    std::vector<document::ObjectId> result;
    const auto& layers = snapshot.page().layers;
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        if (!layer->visible || layer->locked) continue;
        for (auto id = layer->object_ids.rbegin(); id != layer->object_ids.rend(); ++id)
            collect_topmost_paths(snapshot, *id, gesture_bounds, result);
    }
    return result;
}

document::PathObject make_piece(const document::PathObject& source,
                                document::ObjectId id,
                                const std::string& name,
                                const geometry::Path2D& world_path,
                                const document::SceneHierarchy::Entry& owner,
                                const geometry::Transform2D& world_offset =
                                    geometry::Transform2D::identity()) {
    auto piece = source;
    piece.id = id;
    piece.name = name;
    const auto parent_inverse = owner.parent_world.inverted();
    if (!parent_inverse) {
        piece.path.clear();
        return piece;
    }
    piece.path = world_path.transformed(world_offset).transformed(*parent_inverse);
    piece.transform = geometry::Transform2D::identity();
    // The Boolean kernel returns lines at its declared error bound. Rebuild an
    // editable authoring graph from that committed geometry, and drop stale
    // parametric-rectangle metadata from the source object.
    piece.contour_spec.clear();
    const auto editable = document::editable_path_from_path2d(piece.path, nullptr);
    if (!editable) {
        piece.path.clear();
        return piece;
    }
    piece.set_authoring_source(*editable);
    return piece;
}

std::vector<geometry::Path2D> output_components(geometry::BooleanResult&& result) {
    if (!result.components.empty()) return std::move(result.components);
    if (!result.path.empty()) return {std::move(result.path)};
    return {};
}

size_t largest_component_index(const std::vector<geometry::Path2D>& paths) {
    size_t selected = 0;
    double largest = -1.0;
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto bounds = paths[index].bounds();
        const double area = bounds.width * bounds.height;
        if (std::isfinite(area) && area > largest) {
            largest = area;
            selected = index;
        }
    }
    return selected;
}

} // namespace

bool DocumentWorkflow::cut_objects_by_line(
    const geometry::Point2D& p1,
    const geometry::Point2D& p2,
    double separate_offset_mm,
    double gap_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked()) return false;
    if (!std::isfinite(p1.x) || !std::isfinite(p1.y) ||
        !std::isfinite(p2.x) || !std::isfinite(p2.y) ||
        !std::isfinite(separate_offset_mm) || separate_offset_mm < 0.0 ||
        !std::isfinite(gap_mm) || gap_mm < 0.0) return false;

    const auto& sel = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        candidate_ids = sel;
    } else {
        // Resolve in reverse paint order. The first path that the actual cut
        // can divide is the only implicit target.
        geometry::Rect2D cut_bounds;
        cut_bounds.x = std::min(p1.x, p2.x);
        cut_bounds.y = std::min(p1.y, p2.y);
        cut_bounds.width = std::max(0.01, std::abs(p2.x - p1.x));
        cut_bounds.height = std::max(0.01, std::abs(p2.y - p1.y));

        candidate_ids = topmost_path_candidates(*snapshot, cut_bounds);
    }

    if (candidate_ids.empty()) return false;

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> all_new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        const auto* po = std::get_if<document::PathObject>(&*obj_var);
        const auto* owner = snapshot->hierarchy().find(id);
        if (!po || !po->has_fill || !owner || owner->mask ||
            !snapshot->hierarchy().editable(id)) {
            if (sel.empty()) return false;
            continue;
        }

        const auto world_path = po->path.transformed(owner->world);
        auto cut_res = geometry::cut_path_by_line(world_path, p1, p2, gap_mm);
        if (!cut_res.success || cut_res.pieces.size() < 2) continue;

        std::vector<document::ObjectVariant> pieces;
        std::vector<document::ObjectId> piece_ids;
        pieces.reserve(cut_res.pieces.size());
        piece_ids.reserve(cut_res.pieces.size());
        const size_t primary = largest_component_index(cut_res.pieces);
        for (size_t index = 0; index < cut_res.pieces.size(); ++index) {
            const double offset_x = (separate_offset_mm > 0.0 && cut_res.pieces.size() == 2)
                ? (index == primary ? -1.0 : 1.0) * separate_offset_mm * 0.5 : 0.0;
            auto piece = make_piece(*po, index == primary ? id : document::generate_object_id(),
                po->name + " (Mảnh " + std::to_string(index + 1) + ")",
                cut_res.pieces[index], *owner,
                geometry::Transform2D::translation(offset_x, 0.0));
            if (piece.path.empty()) {
                pieces.clear();
                break;
            }
            piece_ids.push_back(piece.id);
            pieces.push_back(std::move(piece));
        }
        if (pieces.size() != cut_res.pieces.size()) continue;
        all_new_ids.insert(all_new_ids.end(), piece_ids.begin(), piece_ids.end());

        cmds.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(pieces), "Knife Cut"));
        if (sel.empty()) break;
    }

    if (cmds.empty()) return false;

    if (cmds.size() == 1) {
        if (!root_.document().execute_command(std::move(cmds.front())).success) return false;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Knife Cut Multiple");
        for (auto& cmd : cmds) {
            composite->add_command(std::move(cmd));
        }
        if (!root_.document().execute_command(std::move(composite)).success) return false;
    }

    root_.vector_controller().selection().clear();
    for (const auto nid : all_new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::erase_objects_by_stroke(
    const std::vector<geometry::Point2D>& stroke,
    double diameter_mm,
    bool round_nib
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || stroke.empty() ||
        !std::isfinite(diameter_mm) || diameter_mm <= 0.0) return false;
    double min_x = stroke.front().x, max_x = stroke.front().x;
    double min_y = stroke.front().y, max_y = stroke.front().y;
    for (const auto& point : stroke) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }

    const auto& sel = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        candidate_ids = sel;
    } else {
        const double radius = diameter_mm * 0.5;
        const geometry::Rect2D bounds{min_x - radius, min_y - radius,
                                      max_x - min_x + 2.0 * radius,
                                      max_y - min_y + 2.0 * radius};
        candidate_ids = topmost_path_candidates(*snapshot, bounds);
    }

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> all_new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        const auto* po = std::get_if<document::PathObject>(&*obj_var);
        const auto* owner = snapshot->hierarchy().find(id);
        if (!po || !po->has_fill || !owner || owner->mask ||
            !snapshot->hierarchy().editable(id)) {
            if (sel.empty()) return false;
            continue;
        }

        const auto world_path = po->path.transformed(owner->world);
        auto erase_res = geometry::erase_path(world_path, stroke, diameter_mm, round_nib);
        if (!erase_res.success) continue;
        if (erase_res.status == geometry::PathCutStatus::Success && erase_res.pieces.empty()) continue;

        std::vector<document::ObjectVariant> pieces;
        if (erase_res.status == geometry::PathCutStatus::Success) {
            const size_t primary = largest_component_index(erase_res.pieces);
            bool pieces_valid = true;
            std::vector<document::ObjectId> piece_ids;
            piece_ids.reserve(erase_res.pieces.size());
            for (size_t index = 0; index < erase_res.pieces.size(); ++index) {
                auto piece = make_piece(*po,
                    index == primary ? id : document::generate_object_id(),
                    erase_res.pieces.size() > 1
                        ? po->name + " (Mảnh " + std::to_string(index + 1) + ")" : po->name,
                    erase_res.pieces[index], *owner);
                if (piece.path.empty()) {
                    pieces_valid = false;
                    break;
                }
                piece_ids.push_back(piece.id);
                pieces.push_back(std::move(piece));
            }
            if (!pieces_valid || pieces.size() != erase_res.pieces.size()) continue;
            all_new_ids.insert(all_new_ids.end(), piece_ids.begin(), piece_ids.end());
        }

        cmds.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(pieces), "Vector Eraser"));
        if (sel.empty()) break;
    }

    if (cmds.empty()) return false;

    if (cmds.size() == 1) {
        if (!root_.document().execute_command(std::move(cmds.front())).success) return false;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Vector Eraser Multiple");
        for (auto& cmd : cmds) {
            composite->add_command(std::move(cmd));
        }
        if (!root_.document().execute_command(std::move(composite)).success) return false;
    }

    root_.vector_controller().selection().clear();
    for (const auto nid : all_new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::trim_objects_source_target(
    document::ObjectId target_id,
    const std::vector<document::ObjectId>& source_ids,
    bool keep_sources
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || source_ids.empty()) return false;

    const auto target_var = snapshot->objects().get(target_id);
    if (!target_var) return false;
    const auto* target = std::get_if<document::PathObject>(&*target_var);
    const auto* target_owner = snapshot->hierarchy().find(target_id);
    if (!target || !target->has_fill || !target_owner || target_owner->mask ||
        !snapshot->hierarchy().editable(target_id)) return false;

    std::vector<geometry::Path2D> source_paths;
    std::unordered_set<document::ObjectId> unique_sources;
    for (const auto sid : source_ids) {
        if (sid == target_id || !unique_sources.insert(sid).second) return false;
        const auto src_var = snapshot->objects().get(sid);
        if (!src_var) return false;
        const auto* src_po = std::get_if<document::PathObject>(&*src_var);
        const auto* source_owner = snapshot->hierarchy().find(sid);
        if (!src_po || !src_po->has_fill || !source_owner || source_owner->mask ||
            !snapshot->hierarchy().editable(sid)) return false;
        source_paths.push_back(src_po->path.transformed(source_owner->world));
    }
    if (source_paths.empty()) return false;

    const auto target_world_path = target->path.transformed(target_owner->world);
    auto trim_res = geometry::trim_target_with_sources(target_world_path, source_paths);
    if (!trim_res.success) return false;

    std::vector<document::ObjectVariant> pieces;
    auto trimmed_components = output_components(std::move(trim_res));
    // Full removal requires a separate preview/confirmation surface; do not
    // turn a trim into an unreviewed delete.
    if (trimmed_components.empty()) return false;
    const size_t primary = largest_component_index(trimmed_components);
    for (size_t index = 0; index < trimmed_components.size(); ++index) {
        auto piece = make_piece(*target,
            index == primary ? target_id : document::generate_object_id(),
            trimmed_components.size() > 1
                ? target->name + " (Mảnh " + std::to_string(index + 1) + ")" : target->name,
            trimmed_components[index], *target_owner);
        if (piece.path.empty()) return false;
        pieces.push_back(std::move(piece));
    }

    std::vector<document::ObjectId> result_ids;
    result_ids.reserve(pieces.size());
    for (const auto& piece : pieces)
        result_ids.push_back(std::visit([](const auto& object) { return object.id; }, piece));

    auto replace_cmd = std::make_unique<document::ReplaceObjectWithPiecesCommand>(
        target_id, std::move(pieces), "Trim Objects");

    bool ok = false;
    if (keep_sources) {
        ok = root_.document().execute_command(std::move(replace_cmd)).success;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Trim Objects");
        composite->add_command(std::move(replace_cmd));
        for (const auto sid : source_ids) {
            composite->add_command(std::make_unique<document::RemoveObjectCommand>(sid));
        }
        ok = root_.document().execute_command(std::move(composite)).success;
    }

    if (ok) {
        root_.vector_controller().selection().clear();
        for (const auto id : result_ids)
            root_.vector_controller().selection().select(id, vector::SelectionMode::Add);
        sync_status_bar();
    }
    return ok;
}

} // namespace pikdraw::desktop
