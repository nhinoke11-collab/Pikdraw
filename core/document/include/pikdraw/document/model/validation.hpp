#pragma once
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document {

inline void validate_page(const Page& page) {
    if (!std::isfinite(page.width_mm) || !std::isfinite(page.height_mm) ||
        page.width_mm <= 0 || page.height_mm <= 0 ||
        page.width_mm > geometry::limits::kCanvasMaxMm ||
        page.height_mm > geometry::limits::kCanvasMaxMm)
        throw std::invalid_argument("Page dimensions must be positive finite millimeters within canvas limits");
}

inline void validate_path_source(const PathObject& object) {
    if (!object.fill.is_valid() || !object.stroke.is_valid() ||
        !std::isfinite(object.stroke_width_mm) || object.stroke_width_mm < 0)
        throw std::invalid_argument("Invalid source color or stroke width");
    using geometry::GeometryStatus;
    for (const auto& element : object.path.elements()) {
        if (geometry::validate_point(element.pt) != GeometryStatus::Ok ||
            geometry::validate_point(element.ctrl1) != GeometryStatus::Ok ||
            geometry::validate_point(element.ctrl2) != GeometryStatus::Ok ||
            !std::isfinite(element.rx) || !std::isfinite(element.ry) ||
            !std::isfinite(element.rotation_deg) || element.rx < 0 || element.ry < 0)
            throw std::invalid_argument("Invalid or out-of-range source path coordinate");
    }
    if (object.authoring_source) {
        if (const auto error = object.authoring_source->validation_error())
            throw std::invalid_argument(*error);
        if (!editable_path_matches_adapter(*object.authoring_source, object.path))
            throw std::invalid_argument("Editable path and derived geometry adapter disagree");
        for (const auto& subpath : object.authoring_source->subpaths) {
            if (subpath.id.value() == object.id)
                throw std::invalid_argument("Subpath ID collides with its object ID");
            for (const auto& node : subpath.nodes)
                if (node.id.value() == object.id)
                    throw std::invalid_argument("Node ID collides with its object ID");
            for (const auto& segment : subpath.segments)
                if (segment.id.value() == object.id)
                    throw std::invalid_argument("Segment ID collides with its object ID");
        }
    }
}

inline void validate_text_source(const TextObject& object) {
    if (!object.fill.is_valid() || object.font.family_name.empty() ||
        !std::isfinite(object.font.size_pt) || object.font.size_pt <= 0 ||
        object.font.size_mm() > geometry::limits::kCanvasMaxMm ||
        object.font.weight == 0 || object.font.weight > 999 ||
        geometry::validate_point(object.position) != geometry::GeometryStatus::Ok)
        throw std::invalid_argument("Invalid text color, font properties or source position");
}

inline void validate_object_source(ObjectId key, const ObjectVariant& value) {
    std::visit([&](const auto& object) {
        if (key == kInvalidObjectId || object.id != key)
            throw std::invalid_argument("Object map key must match a nonzero stable ID");
        if (geometry::validate_transform(object.transform) != geometry::GeometryStatus::Ok)
            throw std::invalid_argument("Invalid or singular object transform");
        using T = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<T, PathObject>) validate_path_source(object);
        else if constexpr (std::is_same_v<T, TextObject>) validate_text_source(object);
        else {
            std::unordered_set<ObjectId> unique;
            for (auto child : object.children_ids)
                if (child == kInvalidObjectId || child == key || !unique.insert(child).second)
                    throw std::invalid_argument("Invalid or repeated group child ID");
        }
    }, value);
}

} // namespace pikdraw::document
