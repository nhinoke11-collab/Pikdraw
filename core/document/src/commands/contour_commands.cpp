#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/document/document.hpp"
#include <cstdio>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace pikdraw::document {

namespace {

std::mutex g_contour_mutex;
std::unordered_map<ObjectId, ContourFeature> g_attached_contours;

} // namespace

std::string serialize_contour_feature(const ContourFeature& feature) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "contour:v1;dir=%d;spacing=%.6f;steps=%d;join=%d;limit=%.6f;tol=%.6f",
                  static_cast<int>(feature.direction),
                  feature.spacing_mm,
                  feature.steps,
                  static_cast<int>(feature.join_type),
                  feature.miter_limit,
                  feature.arc_tolerance_mm);
    return std::string(buf);
}

std::optional<ContourFeature> deserialize_contour_feature(std::string_view spec) {
    if (spec.rfind("contour:v1;", 0) != 0) {
        return std::nullopt;
    }
    ContourFeature feature;
    int dir = 0;
    int join = 0;
    double spacing = 1.0;
    int steps = 1;
    double limit = 4.0;
    double tol = 0.005;

    const std::string s(spec);
    if (std::sscanf(s.c_str(), "contour:v1;dir=%d;spacing=%lf;steps=%d;join=%d;limit=%lf;tol=%lf",
                    &dir, &spacing, &steps, &join, &limit, &tol) >= 6) {
        feature.direction = static_cast<geometry::ContourDirection>(dir);
        feature.spacing_mm = spacing;
        feature.steps = steps;
        feature.join_type = static_cast<geometry::JoinType>(join);
        feature.miter_limit = limit;
        feature.arc_tolerance_mm = tol;
        return feature;
    }
    return std::nullopt;
}

std::optional<ContourFeature> get_object_contour(const PathObject& obj) {
    if (obj.contour_spec.empty()) {
        return std::nullopt;
    }
    auto feat = deserialize_contour_feature(obj.contour_spec);
    if (feat) {
        feat->target_id = obj.id;
        const auto res = evaluate_contour(obj, *feat);
        for (const auto& step : res.steps) {
            if (step.is_empty) continue;
            for (const auto& loop : step.loops) {
                feat->derived_loops.push_back(loop);
            }
        }
        return feat;
    }
    return std::nullopt;
}

std::optional<ContourFeature> get_object_contour(const Document& doc, ObjectId id) {
    const auto opt = doc.objects().get(id);
    if (!opt) return std::nullopt;
    const auto* path_obj = std::get_if<PathObject>(&(*opt));
    if (!path_obj) return std::nullopt;
    return get_object_contour(*path_obj);
}

std::optional<ContourFeature> get_attached_contour(ObjectId id) {
    std::lock_guard<std::mutex> lock(g_contour_mutex);
    const auto it = g_attached_contours.find(id);
    if (it != g_attached_contours.end()) {
        return it->second;
    }
    return std::nullopt;
}

void set_attached_contour(ObjectId id, ContourFeature feature) {
    std::lock_guard<std::mutex> lock(g_contour_mutex);
    feature.target_id = id;
    g_attached_contours[id] = std::move(feature);
}

void remove_attached_contour(ObjectId id) {
    std::lock_guard<std::mutex> lock(g_contour_mutex);
    g_attached_contours.erase(id);
}

void clear_all_attached_contours() {
    std::lock_guard<std::mutex> lock(g_contour_mutex);
    g_attached_contours.clear();
}

geometry::ContourResult evaluate_contour(const PathObject& object, const ContourFeature& feature) {
    geometry::ContourRequest req;
    req.path = object.path;
    req.direction = feature.direction;
    req.spacing_mm = feature.spacing_mm;
    req.steps = feature.steps;
    req.join_type = feature.join_type;
    req.miter_limit = feature.miter_limit;
    req.arc_tolerance_mm = feature.arc_tolerance_mm;

    return geometry::compute_contour(req);
}

// --- ApplyContourCommand ---

ApplyContourCommand::ApplyContourCommand(ObjectId target_id, ContourFeature feature) noexcept
    : target_id_(target_id), feature_(std::move(feature)) {}

CommandResult ApplyContourCommand::execute(Document& doc) {
    const auto opt = doc.objects().get(target_id_);
    if (!opt) {
        return CommandResult::fail("Target object not found for ApplyContourCommand");
    }
    const auto* path_obj = std::get_if<PathObject>(&(*opt));
    if (!path_obj) {
        return CommandResult::fail("Target object is not a PathObject for ApplyContourCommand");
    }

    const auto result = evaluate_contour(*path_obj, feature_);
    if (result.status != geometry::OffsetStatus::Success && result.status != geometry::OffsetStatus::Empty) {
        return CommandResult::fail(result.error_message ? result.error_message : "Contour evaluation failed");
    }

    feature_.derived_loops.clear();
    for (const auto& step : result.steps) {
        if (step.is_empty) continue;
        for (const auto& loop : step.loops) {
            feature_.derived_loops.push_back(loop);
        }
    }

    previous_feature_ = get_object_contour(*path_obj);

    PathObject updated = *path_obj;
    updated.contour_spec = serialize_contour_feature(feature_);

    set_attached_contour(target_id_, feature_);
    PersistentObjectMap map = doc.objects().set(target_id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ApplyContourCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted ApplyContourCommand");
    }

    const auto opt = doc.objects().get(target_id_);
    if (opt) {
        if (const auto* path_obj = std::get_if<PathObject>(&(*opt))) {
            PathObject updated = *path_obj;
            if (previous_feature_) {
                updated.contour_spec = serialize_contour_feature(*previous_feature_);
                set_attached_contour(target_id_, *previous_feature_);
            } else {
                updated.contour_spec.clear();
                remove_attached_contour(target_id_);
            }
            PersistentObjectMap map = doc.objects().set(target_id_, updated);
            doc.apply_mutation(doc.page(), std::move(map));
        }
    }

    executed_ = false;
    return CommandResult::ok();
}

// --- BreakContourApartCommand ---

BreakContourApartCommand::BreakContourApartCommand(ObjectId target_id) noexcept
    : target_id_(target_id) {}

CommandResult BreakContourApartCommand::execute(Document& doc) {
    const auto opt = doc.objects().get(target_id_);
    if (!opt) {
        return CommandResult::fail("Target object not found for BreakContourApartCommand");
    }
    const auto* path_obj = std::get_if<PathObject>(&(*opt));
    if (!path_obj) {
        return CommandResult::fail("Target object is not a PathObject for BreakContourApartCommand");
    }

    auto feature = get_object_contour(*path_obj);
    if (!feature || feature->derived_loops.empty()) {
        return CommandResult::fail("No contour to break apart on target object");
    }

    previous_feature_ = feature;

    Page page = doc.page();
    bool found_layer = false;
    for (size_t l = 0; l < page.layers.size(); ++l) {
        auto& ids = page.layers[l].object_ids;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == target_id_) {
                layer_idx_ = l;
                insert_idx_ = i + 1;
                found_layer = true;
                break;
            }
        }
        if (found_layer) break;
    }

    if (!found_layer) {
        return CommandResult::fail("Target object layer not found");
    }

    if (created_objects_.empty()) {
        created_objects_.reserve(feature->derived_loops.size());
        created_ids_.reserve(feature->derived_loops.size());

        for (const auto& loop : feature->derived_loops) {
            ObjectId new_id = generate_object_id();
            PathObject new_obj;
            new_obj.id = new_id;
            new_obj.name = path_obj->name.empty() ? "Contour" : (path_obj->name + " Contour");
            new_obj.path = loop;
            new_obj.transform = path_obj->transform;
            new_obj.fill = path_obj->fill;
            new_obj.stroke = path_obj->stroke;
            new_obj.stroke_width_mm = path_obj->stroke_width_mm;
            new_obj.has_fill = path_obj->has_fill;
            new_obj.has_stroke = path_obj->has_stroke;
            new_obj.appearance = path_obj->appearance;
            new_obj.visible = true;
            new_obj.locked = false;

            created_objects_.push_back(new_obj);
            created_ids_.push_back(new_id);
            reserve_object_id(new_id);
        }
    }

    // Clear contour_spec from source object
    PathObject updated_source = *path_obj;
    updated_source.contour_spec.clear();

    PersistentObjectMap map = doc.objects().set(target_id_, updated_source);
    for (const auto& obj : created_objects_) {
        map = map.set(obj.id, obj);
    }

    auto& layer_ids = page.layers[layer_idx_].object_ids;
    layer_ids.insert(layer_ids.begin() + static_cast<std::ptrdiff_t>(insert_idx_),
                     created_ids_.begin(), created_ids_.end());

    remove_attached_contour(target_id_);

    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult BreakContourApartCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted BreakContourApartCommand");
    }

    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo BreakContourApartCommand");
    }

    auto& layer_ids = page.layers[layer_idx_].object_ids;
    for (ObjectId id : created_ids_) {
        layer_ids.erase(std::remove(layer_ids.begin(), layer_ids.end(), id), layer_ids.end());
    }

    PersistentObjectMap map = doc.objects();
    for (ObjectId id : created_ids_) {
        map = map.remove(id);
    }

    const auto opt = map.get(target_id_);
    if (opt) {
        if (const auto* path_obj = std::get_if<PathObject>(&(*opt))) {
            PathObject updated = *path_obj;
            if (previous_feature_) {
                updated.contour_spec = serialize_contour_feature(*previous_feature_);
                set_attached_contour(target_id_, *previous_feature_);
            }
            map = map.set(target_id_, updated);
        }
    }

    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- RemoveContourCommand ---

RemoveContourCommand::RemoveContourCommand(ObjectId target_id) noexcept
    : target_id_(target_id) {}

CommandResult RemoveContourCommand::execute(Document& doc) {
    const auto opt = doc.objects().get(target_id_);
    if (!opt) return CommandResult::fail("Target object not found");
    const auto* path_obj = std::get_if<PathObject>(&(*opt));
    if (!path_obj) return CommandResult::fail("Target object is not PathObject");

    previous_feature_ = get_object_contour(*path_obj);
    if (!previous_feature_) {
        return CommandResult::fail("No contour to remove");
    }

    PathObject updated = *path_obj;
    updated.contour_spec.clear();
    remove_attached_contour(target_id_);

    PersistentObjectMap map = doc.objects().set(target_id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult RemoveContourCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted RemoveContourCommand");
    }
    const auto opt = doc.objects().get(target_id_);
    if (opt && previous_feature_) {
        if (const auto* path_obj = std::get_if<PathObject>(&(*opt))) {
            PathObject updated = *path_obj;
            updated.contour_spec = serialize_contour_feature(*previous_feature_);
            set_attached_contour(target_id_, *previous_feature_);
            PersistentObjectMap map = doc.objects().set(target_id_, updated);
            doc.apply_mutation(doc.page(), std::move(map));
        }
    }
    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
