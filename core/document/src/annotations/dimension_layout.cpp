#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <cmath>

namespace pikdraw::document {
namespace {

std::optional<geometry::Point2D> resolve_ref_point(
    const SourceReference& ref,
    const DocumentSnapshot& snapshot,
    AssociationState& state) {
    if (ref.ref_kind == SourceRefKind::ExplicitPoint || ref.source_object_id == kInvalidObjectId) {
        return ref.explicit_point;
    }

    const auto obj_opt = snapshot.get_object(ref.source_object_id);
    if (!obj_opt.has_value()) {
        state = AssociationState::Broken;
        return ref.explicit_point;
    }

    if (const auto* path_obj = std::get_if<PathObject>(&(*obj_opt))) {
        if (ref.ref_kind == SourceRefKind::Center) {
            const auto b = path_obj->bounds();
            return geometry::Point2D{b.x + b.width * 0.5, b.y + b.height * 0.5};
        }

        if (path_obj->authoring_source && ref.element_id.has_value()) {
            for (const auto& sp : path_obj->authoring_source->subpaths) {
                for (const auto& node : sp.nodes) {
                    if (node.id.value() == *ref.element_id) {
                        return path_obj->transform.map_point(node.anchor);
                    }
                }
            }
        }

        // If not in authoring source, check path elements
        if (ref.element_id.has_value()) {
            const auto& elems = path_obj->path.elements();
            const size_t idx = static_cast<size_t>(*ref.element_id);
            if (idx < elems.size()) {
                return path_obj->transform.map_point(elems[idx].pt);
            }
        }
    }

    state = AssociationState::Broken;
    return ref.explicit_point;
}

std::string apply_template(const std::string& measurement, const std::string& tpl) {
    if (tpl.empty()) return measurement;
    const auto pos = tpl.find("<>");
    if (pos == std::string::npos) return tpl;
    std::string res = tpl;
    res.replace(pos, 2, measurement);
    return res;
}

} // namespace

geometry::Rect2D DimensionObject::bounds() const {
    double min_x = placement_point.x;
    double max_x = placement_point.x;
    double min_y = placement_point.y;
    double max_y = placement_point.y;

    for (const auto& ref : references) {
        min_x = std::min(min_x, ref.explicit_point.x);
        max_x = std::max(max_x, ref.explicit_point.x);
        min_y = std::min(min_y, ref.explicit_point.y);
        max_y = std::max(max_y, ref.explicit_point.y);
    }

    constexpr double kMargin = 10.0;
    return geometry::Rect2D{min_x - kMargin, min_y - kMargin,
                            (max_x - min_x) + 2.0 * kMargin,
                            (max_y - min_y) + 2.0 * kMargin};
}

DimensionLayout evaluate_dimension_layout(const DimensionObject& dim, const DocumentSnapshot& snapshot) {
    DimensionLayout layout;
    layout.association_state = AssociationState::Valid;

    if (dim.kind == DimensionKind::Linear || dim.kind == DimensionKind::Aligned) {
        if (dim.references.size() < 2) {
            layout.valid = false;
            return layout;
        }

        const auto p1_opt = resolve_ref_point(dim.references[0], snapshot, layout.association_state);
        const auto p2_opt = resolve_ref_point(dim.references[1], snapshot, layout.association_state);

        const geometry::Point2D p1 = p1_opt.value_or(dim.references[0].explicit_point);
        const geometry::Point2D p2 = p2_opt.value_or(dim.references[1].explicit_point);

        const auto geom = geometry::compute_linear_dimension(
            p1, p2, dim.placement_point, dim.linear_orientation,
            dim.style.ext_offset_mm, dim.style.ext_overhang_mm, dim.style.arrow_size_mm);

        if (!geom.valid) {
            layout.valid = false;
            return layout;
        }

        layout.raw_value = geom.measured_value_mm;
        const std::string text = format_measurement_text(geom.measured_value_mm, dim.style, false);
        layout.formatted_text = apply_template(text, dim.note_template);
        layout.text_position = geom.text_position;
        layout.text_rotation_rad = geom.text_rotation_rad;

        const double char_w = dim.style.text_size_mm * 0.55;
        const double tw = std::max(6.0, layout.formatted_text.size() * char_w);
        const double th = dim.style.text_size_mm;
        layout.text_bounds = geometry::Rect2D{geom.text_position.x - tw * 0.5,
                                              geom.text_position.y - th * 0.5,
                                              tw, th};

        // Lines: dimension line + two extension lines
        // In AutoCAD/CorelDRAW, the dimension line is split to leave a gap for centered text
        const double half_gap = (tw * 0.5) + dim.style.text_gap_mm;
        const double line_len = std::hypot(geom.dim_line_end.x - geom.dim_line_start.x,
                                           geom.dim_line_end.y - geom.dim_line_start.y);
        geometry::Path2D dim_line;
        if (line_len > half_gap * 2.2) {
            const geometry::Point2D dir{(geom.dim_line_end.x - geom.dim_line_start.x) / line_len,
                                        (geom.dim_line_end.y - geom.dim_line_start.y) / line_len};
            dim_line.move_to(geom.dim_line_start);
            dim_line.line_to(geom.text_position - dir * half_gap);
            dim_line.move_to(geom.text_position + dir * half_gap);
            dim_line.line_to(geom.dim_line_end);
        } else {
            dim_line.move_to(geom.dim_line_start);
            dim_line.line_to(geom.dim_line_end);
        }
        layout.lines.push_back(dim_line);

        geometry::Path2D ext1;
        ext1.move_to(geom.ext1_start);
        ext1.line_to(geom.ext1_end);
        layout.lines.push_back(ext1);

        geometry::Path2D ext2;
        ext2.move_to(geom.ext2_start);
        ext2.line_to(geom.ext2_end);
        layout.lines.push_back(ext2);

        // Arrows
        geometry::DimensionArrow arr1 = geom.arrow1;
        geometry::DimensionArrow arr2 = geom.arrow2;
        if (dim.flip_arrows) {
            arr1.direction = geometry::Point2D{-arr1.direction.x, -arr1.direction.y};
            arr2.direction = geometry::Point2D{-arr2.direction.x, -arr2.direction.y};
        }
        layout.arrows.push_back(geometry::make_arrow_path(arr1, dim.style.arrow_type, dim.style.arrow_size_mm));
        layout.arrows.push_back(geometry::make_arrow_path(arr2, dim.style.arrow_type, dim.style.arrow_size_mm));

        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    } else if (dim.kind == DimensionKind::Angular) {
        if (dim.references.size() < 4) {
            layout.valid = false;
            return layout;
        }

        const auto r1_p1 = resolve_ref_point(dim.references[0], snapshot, layout.association_state).value_or(dim.references[0].explicit_point);
        const auto r1_p2 = resolve_ref_point(dim.references[1], snapshot, layout.association_state).value_or(dim.references[1].explicit_point);
        const auto r2_p1 = resolve_ref_point(dim.references[2], snapshot, layout.association_state).value_or(dim.references[2].explicit_point);
        const auto r2_p2 = resolve_ref_point(dim.references[3], snapshot, layout.association_state).value_or(dim.references[3].explicit_point);

        const auto geom = geometry::compute_angular_dimension(r1_p1, r1_p2, r2_p1, r2_p2, dim.placement_point, dim.style.arrow_size_mm);
        if (!geom.valid) {
            layout.valid = false;
            return layout;
        }

        layout.raw_value = geom.measured_value_rad;
        const std::string text = format_measurement_text(geom.measured_value_rad, dim.style, true);
        layout.formatted_text = apply_template(text, dim.note_template);
        layout.text_position = geom.text_position;

        geometry::Path2D arc_line;
        arc_line.move_to(geom.arrow1.tip);
        arc_line.arc_to(geom.radius, geom.radius, 0.0, geom.sweep_angle_rad > 3.14159, true, geom.arrow2.tip.x, geom.arrow2.tip.y);
        layout.lines.push_back(arc_line);

        layout.arrows.push_back(geometry::make_arrow_path(geom.arrow1, dim.style.arrow_type, dim.style.arrow_size_mm));
        layout.arrows.push_back(geometry::make_arrow_path(geom.arrow2, dim.style.arrow_type, dim.style.arrow_size_mm));

        layout.text_bounds = geometry::Rect2D{geom.text_position.x - 10.0, geom.text_position.y - 5.0, 20.0, 10.0};
        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    } else if (dim.kind == DimensionKind::Radius || dim.kind == DimensionKind::Diameter) {
        geometry::Point2D center = dim.datum_origin;
        double radius = 10.0;

        if (!dim.references.empty() && dim.references[0].source_object_id != kInvalidObjectId) {
            const auto obj = snapshot.get_object(dim.references[0].source_object_id);
            if (obj.has_value()) {
                if (const auto* path = std::get_if<PathObject>(&(*obj))) {
                    const auto b = path->bounds();
                    center = geometry::Point2D{b.x + b.width * 0.5, b.y + b.height * 0.5};
                    radius = b.width * 0.5;
                }
            } else {
                layout.association_state = AssociationState::Broken;
            }
        }

        const bool is_dia = (dim.kind == DimensionKind::Diameter);
        const auto geom = geometry::compute_radial_dimension(center, radius, is_dia, dim.placement_point, dim.style.arrow_size_mm);
        if (!geom.valid) {
            layout.valid = false;
            return layout;
        }

        layout.raw_value = geom.measured_value_mm;
        std::string prefix = is_dia ? "Ø " : "R ";
        const std::string text = prefix + format_measurement_text(geom.measured_value_mm, dim.style, false);
        layout.formatted_text = apply_template(text, dim.note_template);
        layout.text_position = geom.text_position;

        geometry::Path2D leader;
        leader.move_to(geom.surface_point);
        leader.line_to(geom.elbow_point);
        leader.line_to(geom.landing_end);
        layout.lines.push_back(leader);

        layout.arrows.push_back(geometry::make_arrow_path(geom.arrow, dim.style.arrow_type, dim.style.arrow_size_mm));
        layout.text_bounds = geometry::Rect2D{geom.text_position.x, geom.text_position.y - 5.0, 20.0, 10.0};
        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    } else if (dim.kind == DimensionKind::ArcLength) {
        geometry::Point2D center = dim.datum_origin;
        double radius = 20.0;
        constexpr double kPi = 3.14159265358979323846;
        double sweep = kPi * 0.5;

        const auto geom = geometry::compute_arc_length_dimension(center, radius, 0.0, sweep, dim.placement_point, dim.style.arrow_size_mm);
        if (!geom.valid) {
            layout.valid = false;
            return layout;
        }

        layout.raw_value = geom.measured_value_mm;
        const std::string text = "⌒ " + format_measurement_text(geom.measured_value_mm, dim.style, false);
        layout.formatted_text = apply_template(text, dim.note_template);
        layout.text_position = geom.text_position;

        geometry::Path2D arc_line;
        arc_line.move_to(geom.arrow1.tip);
        arc_line.arc_to(geom.radius, geom.radius, 0.0, false, true, geom.arrow2.tip.x, geom.arrow2.tip.y);
        layout.lines.push_back(arc_line);

        layout.arrows.push_back(geometry::make_arrow_path(geom.arrow1, dim.style.arrow_type, dim.style.arrow_size_mm));
        layout.arrows.push_back(geometry::make_arrow_path(geom.arrow2, dim.style.arrow_type, dim.style.arrow_size_mm));
        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    } else if (dim.kind == DimensionKind::Ordinate) {
        geometry::Point2D feat = dim.placement_point;
        if (!dim.references.empty()) {
            feat = resolve_ref_point(dim.references[0], snapshot, layout.association_state).value_or(dim.references[0].explicit_point);
        }

        const auto geom = geometry::compute_ordinate_dimension(dim.datum_origin, feat, dim.ordinate_axis, dim.placement_point);
        layout.raw_value = geom.measured_value_mm;
        const std::string text = format_measurement_text(geom.measured_value_mm, dim.style, false);
        layout.formatted_text = apply_template(text, dim.note_template);
        layout.text_position = geom.text_position;

        geometry::Path2D leader;
        leader.move_to(geom.feature_point);
        leader.line_to(geom.elbow_point);
        leader.line_to(geom.landing_end);
        layout.lines.push_back(leader);

        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    } else if (dim.kind == DimensionKind::MultiLeader) {
        std::vector<geometry::Point2D> heads;
        for (const auto& ref : dim.references) {
            heads.push_back(resolve_ref_point(ref, snapshot, layout.association_state).value_or(ref.explicit_point));
        }
        if (heads.empty()) {
            heads.push_back(dim.placement_point);
        }

        const auto geom = geometry::compute_multi_leader(heads, dim.placement_point, 8.0, dim.style.arrow_size_mm);
        layout.formatted_text = dim.note_template.empty() ? dim.name : dim.note_template;
        layout.text_position = geom.text_position;

        for (const auto& head : geom.arrow_heads) {
            geometry::Path2D branch;
            branch.move_to(head);
            branch.line_to(geom.elbow_point);
            layout.lines.push_back(branch);
        }
        geometry::Path2D landing;
        landing.move_to(geom.elbow_point);
        landing.line_to(geom.landing_end);
        layout.lines.push_back(landing);

        for (const auto& arr : geom.arrows) {
            layout.arrows.push_back(geometry::make_arrow_path(arr, dim.style.arrow_type, dim.style.arrow_size_mm));
        }

        layout.overall_bounds = dim.bounds();
        layout.valid = true;
    }

    return layout;
}

DimensionLayout evaluate_dimension_layout(const DimensionObject& dim, const Document& doc) {
    const auto snap = doc.current_snapshot();
    if (snap) {
        return evaluate_dimension_layout(dim, *snap);
    }
    DocumentSnapshot fallback(doc.epoch(), doc.revision(), doc.page(), doc.objects(), doc.id());
    return evaluate_dimension_layout(dim, fallback);
}

} // namespace pikdraw::document
