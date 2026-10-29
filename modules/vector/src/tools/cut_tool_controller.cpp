#include "pikdraw/vector/tools/cut_tool_controller.hpp"

#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/document/model/object.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <optional>
#include <unordered_set>
#include <utility>

namespace pikdraw::vector::tools {
namespace {

using PathComponents = std::vector<geometry::Path2D>;
using CutOperation = std::function<geometry::PathCutResult(const geometry::Path2D&, const document::PathObject&)>;

size_t primary_component(const PathComponents& components) {
    size_t primary = 0;
    double largest_area = -1.0;
    for (size_t index = 0; index < components.size(); ++index) {
        const auto bounds = components[index].bounds();
        const double area = bounds.width * bounds.height;
        if (std::isfinite(area) && area > largest_area) {
            largest_area = area;
            primary = index;
        }
    }
    return primary;
}

std::optional<document::PathObject> make_piece(
    const document::PathObject& source,
    document::ObjectId id,
    const std::string& name,
    const geometry::Path2D& world_path,
    const document::SceneHierarchy::Entry& owner
) {
    const auto parent_inverse = owner.parent_world.inverted();
    if (!parent_inverse) return std::nullopt;

    auto piece = source;
    piece.id = id;
    piece.name = name;
    piece.path = world_path.transformed(*parent_inverse);
    piece.transform = geometry::Transform2D::identity();
    piece.contour_spec.clear();

    const auto authoring = document::editable_path_from_path2d(piece.path, nullptr);
    if (!authoring) return std::nullopt;
    try {
        piece.set_authoring_source(*authoring);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return piece;
}

struct BatchCommit {
    bool changed{false};
    std::vector<document::ObjectId> created_ids;
};

BatchCommit commit_cut_batch(
    document::Document& document,
    const std::shared_ptr<const document::DocumentSnapshot>& snapshot,
    const std::vector<document::ObjectId>& candidate_ids,
    std::string command_name,
    const CutOperation& operation
) {
    BatchCommit result;
    if (!snapshot || snapshot->is_content_locked() || !operation) return result;

    std::unordered_set<document::ObjectId> visited;
    std::vector<std::unique_ptr<document::DocumentCommand>> commands;
    for (const auto id : candidate_ids) {
        if (!id || !visited.insert(id).second) continue;
        const auto object = snapshot->objects().get(id);
        const auto* path_object = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        const auto* owner = snapshot->hierarchy().find(id);
        if (!path_object || !owner || owner->mask ||
            !snapshot->hierarchy().editable(id)) continue;

        const auto world_path = path_object->path.transformed(owner->world);
        auto cut = operation(world_path, *path_object);
        if (!cut.success || cut.status == geometry::PathCutStatus::NoCut ||
            cut.status == geometry::PathCutStatus::Unsupported ||
            cut.status == geometry::PathCutStatus::InvalidInput ||
            cut.status == geometry::PathCutStatus::WorkBudgetExceeded) continue;
        if (cut.status == geometry::PathCutStatus::Success && cut.pieces.empty()) continue;

        std::vector<document::ObjectVariant> pieces;
        pieces.reserve(cut.pieces.size());
        std::vector<document::ObjectId> target_created_ids;
        const size_t primary = cut.pieces.empty() ? 0 : primary_component(cut.pieces);
        bool valid = true;
        for (size_t index = 0; index < cut.pieces.size(); ++index) {
            const auto piece_id = index == primary ? id : document::generate_object_id();
            const auto suffix = cut.pieces.size() > 1
                ? " (Mảnh " + std::to_string(index + 1) + ")" : std::string{};
            auto piece = make_piece(*path_object, piece_id,
                                    path_object->name + suffix,
                                    cut.pieces[index], *owner);
            if (!piece) {
                valid = false;
                break;
            }
            target_created_ids.push_back(piece_id);
            pieces.emplace_back(std::move(*piece));
        }
        if (!valid || pieces.size() != cut.pieces.size()) {
            continue;
        }

        // EmptyResult is a deliberate full erase. It replaces the object with
        // zero pieces, while NoCut never reaches the document writer.
        if (cut.status == geometry::PathCutStatus::EmptyResult) pieces.clear();
        result.created_ids.insert(result.created_ids.end(), target_created_ids.begin(),
                                  target_created_ids.end());
        commands.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(pieces), command_name));
    }

    if (commands.empty()) {
        result.created_ids.clear();
        return result;
    }

    auto transaction = std::make_unique<document::CompositeCommand>(std::move(command_name));
    for (auto& command : commands) transaction->add_command(std::move(command));
    if (!document.execute_command(std::move(transaction)).success) {
        result.created_ids.clear();
        return result;
    }
    result.changed = true;
    return result;
}

bool is_editable_path(const document::DocumentSnapshot& snapshot,
                             document::ObjectId id,
                             const document::PathObject*& path_object,
                             const document::SceneHierarchy::Entry*& owner) {
    const auto* object = snapshot.objects().lookup_ref(id);
    path_object = object ? std::get_if<document::PathObject>(object) : nullptr;
    owner = snapshot.hierarchy().find(id);
    return path_object && owner && !owner->mask &&
           snapshot.hierarchy().editable(id);
}

} // namespace

void CutToolController::reset_gesture() noexcept {
    gesture_active_ = false;
    gesture_points_.clear();
}

void CutToolController::on_pointer_down(const geometry::Point2D& pt_world) {
    reset_gesture();
    if (!std::isfinite(pt_world.x) || !std::isfinite(pt_world.y)) return;
    gesture_active_ = true;
    gesture_points_.push_back(pt_world);
}

void CutToolController::on_pointer_move(const geometry::Point2D& pt_world) {
    if (!gesture_active_ || !std::isfinite(pt_world.x) || !std::isfinite(pt_world.y)) return;
    if (gesture_points_.empty()) {
        gesture_points_.push_back(pt_world);
        return;
    }
    const auto& last = gesture_points_.back();
    if (std::hypot(pt_world.x - last.x, pt_world.y - last.y) > 0.1) {
        if (mode_ == CutToolMode::Knife && knife_style_ == KnifeCutStyle::Straight) {
            if (gesture_points_.size() < 2) gesture_points_.push_back(pt_world);
            else gesture_points_.back() = pt_world;
        } else {
            gesture_points_.push_back(pt_world);
        }
    }
}

bool CutToolController::on_pointer_up(
    const geometry::Point2D& pt_world,
    const std::vector<document::ObjectId>& candidate_ids
) {
    if (!gesture_active_ || gesture_points_.empty() ||
        !std::isfinite(pt_world.x) || !std::isfinite(pt_world.y)) {
        reset_gesture();
        return false;
    }
    gesture_points_.push_back(pt_world);

    bool modified = false;
    if (mode_ == CutToolMode::Knife && gesture_points_.size() >= 2) {
        if (knife_style_ == KnifeCutStyle::Straight) {
            modified = execute_knife_cut(candidate_ids, gesture_points_.front(),
                                         gesture_points_.back(), knife_gap_mm_);
        } else {
            last_created_ids_.clear();
            const auto snapshot = document_.current_snapshot();
            auto result = commit_cut_batch(document_, snapshot, candidate_ids, "Knife Cut",
                [this](const geometry::Path2D& path, const document::PathObject&) {
                    return geometry::cut_path_by_polyline(path, gesture_points_, knife_gap_mm_);
                });
            modified = result.changed;
            last_created_ids_ = std::move(result.created_ids);
        }
    } else if (mode_ == CutToolMode::VectorEraser) {
        modified = execute_eraser_cut(candidate_ids, gesture_points_,
                                      eraser_diameter_mm_, eraser_round_nib_);
    }

    reset_gesture();
    return modified;
}

bool CutToolController::execute_knife_cut(
    const std::vector<document::ObjectId>& candidate_ids,
    const geometry::Point2D& p1,
    const geometry::Point2D& p2,
    double gap_mm
) {
    last_created_ids_.clear();
    if (!std::isfinite(p1.x) || !std::isfinite(p1.y) ||
        !std::isfinite(p2.x) || !std::isfinite(p2.y) ||
        !std::isfinite(gap_mm) || gap_mm < 0.0) return false;
    const auto snapshot = document_.current_snapshot();
    auto result = commit_cut_batch(document_, snapshot, candidate_ids, "Knife Cut",
        [p1, p2, gap_mm](const geometry::Path2D& path, const document::PathObject&) {
            return geometry::cut_path_by_line(path, p1, p2, gap_mm);
        });
    last_created_ids_ = std::move(result.created_ids);
    return result.changed;
}

bool CutToolController::execute_eraser_cut(
    const std::vector<document::ObjectId>& candidate_ids,
    const std::vector<geometry::Point2D>& stroke,
    double diameter_mm,
    bool round_nib
) {
    last_created_ids_.clear();
    if (stroke.empty() || !std::isfinite(diameter_mm) || diameter_mm <= 0.0 ||
        std::any_of(stroke.begin(), stroke.end(), [](const auto& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y);
        })) return false;
    const auto snapshot = document_.current_snapshot();
    auto result = commit_cut_batch(document_, snapshot, candidate_ids, "Vector Eraser",
        [&stroke, diameter_mm, round_nib](const geometry::Path2D& path, const document::PathObject& source) {
            if (!source.has_fill) {
                if (!source.has_stroke) return geometry::PathCutResult{};
                return geometry::erase_open_path_stroke(path, stroke, diameter_mm,
                    source.stroke_width_mm, round_nib);
            }
            return geometry::erase_path(path, stroke, diameter_mm, round_nib);
        });
    last_created_ids_ = std::move(result.created_ids);
    return result.changed;
}

bool CutToolController::execute_trim_objects(
    document::ObjectId target_id,
    const std::vector<document::ObjectId>& source_ids,
    bool keep_sources
) {
    last_created_ids_.clear();
    const auto snapshot = document_.current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || source_ids.empty()) return false;

    const document::PathObject* target = nullptr;
    const document::SceneHierarchy::Entry* target_owner = nullptr;
    if (!is_editable_path(*snapshot, target_id, target, target_owner)) return false;

    std::unordered_set<document::ObjectId> unique_sources;
    std::vector<geometry::Path2D> source_paths;
    source_paths.reserve(source_ids.size());
    for (const auto source_id : source_ids) {
        if (!source_id || source_id == target_id || !unique_sources.insert(source_id).second)
            return false;
        const document::PathObject* source = nullptr;
        const document::SceneHierarchy::Entry* source_owner = nullptr;
        if (!is_editable_path(*snapshot, source_id, source, source_owner)) return false;
        source_paths.push_back(source->path.transformed(source_owner->world));
    }

    const auto target_world = target->path.transformed(target_owner->world);
    auto trim = geometry::trim_target_with_sources(target_world, source_paths);
    if (!trim.success || trim.status != geometry::BooleanStatus::Success || trim.components.empty())
        return false; // No change or full removal is not silently committed as Delete.

    auto components = std::move(trim.components);
    const size_t primary = primary_component(components);
    std::vector<document::ObjectVariant> pieces;
    std::vector<document::ObjectId> trimmed_ids;
    pieces.reserve(components.size());
    trimmed_ids.reserve(components.size());
    for (size_t index = 0; index < components.size(); ++index) {
        const auto id = index == primary ? target_id : document::generate_object_id();
        const auto suffix = components.size() > 1
            ? " (Mảnh " + std::to_string(index + 1) + ")" : std::string{};
        auto piece = make_piece(*target, id, target->name + suffix,
                                components[index], *target_owner);
        if (!piece) return false;
        trimmed_ids.push_back(id);
        pieces.emplace_back(std::move(*piece));
    }

    auto transaction = std::make_unique<document::CompositeCommand>("Trim Objects");
    transaction->add_command(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
        target_id, std::move(pieces), "Trim Objects"));
    if (!keep_sources) {
        for (const auto source_id : source_ids)
            transaction->add_command(std::make_unique<document::RemoveObjectCommand>(source_id));
    }
    if (!document_.execute_command(std::move(transaction)).success) {
        return false;
    }
    last_created_ids_ = std::move(trimmed_ids);
    return true;
}

} // namespace pikdraw::vector::tools
