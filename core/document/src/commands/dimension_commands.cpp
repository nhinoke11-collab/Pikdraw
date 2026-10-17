#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace pikdraw::document {
namespace {

std::string serialize_dimension_spec(const DimensionObject& dim) {
    std::ostringstream ss;
    ss << "dimension:v1"
       << ";kind=" << static_cast<int>(dim.kind)
       << ";px=" << dim.placement_point.x << ";py=" << dim.placement_point.y
       << ";flip=" << (dim.flip_arrows ? 1 : 0)
       << ";lin_ori=" << static_cast<int>(dim.linear_orientation)
       << ";ord_ax=" << static_cast<int>(dim.ordinate_axis)
       << ";dat_x=" << dim.datum_origin.x << ";dat_y=" << dim.datum_origin.y
       << ";ref_cnt=" << dim.references.size();
    for (size_t i = 0; i < dim.references.size(); ++i) {
        const auto& r = dim.references[i];
        ss << ";r" << i << "_obj=" << r.source_object_id
           << ";r" << i << "_k=" << static_cast<int>(r.ref_kind)
           << ";r" << i << "_el=" << (r.element_id ? *r.element_id : 0)
           << ";r" << i << "_has_el=" << (r.element_id ? 1 : 0)
           << ";r" << i << "_x=" << r.explicit_point.x
           << ";r" << i << "_y=" << r.explicit_point.y;
    }
    const auto& s = dim.style;
    ss << ";style_id=" << s.style_id
       << ";l_w=" << s.line_width_mm
       << ";e_w=" << s.ext_width_mm
       << ";e_off=" << s.ext_offset_mm
       << ";e_ext=" << s.ext_overhang_mm
       << ";arr_t=" << static_cast<int>(s.arrow_type)
       << ";arr_sz=" << s.arrow_size_mm
       << ";flip1=" << (s.flip_first_arrow ? 1 : 0)
       << ";flip2=" << (s.flip_second_arrow ? 1 : 0)
       << ";t_sz=" << s.text_size_mm
       << ";t_pos=" << static_cast<int>(s.text_position)
       << ";t_aln=" << static_cast<int>(s.text_alignment)
       << ";t_gap=" << s.text_gap_mm
       << ";t_mask=" << (s.text_mask ? 1 : 0)
       << ";b_frame=" << (s.basic_frame ? 1 : 0)
       << ";fit_m=" << static_cast<int>(s.fit_mode)
       << ";bl_sp=" << s.baseline_spacing_mm
       << ";unit=" << static_cast<int>(s.units)
       << ";dec=" << s.decimals
       << ";rnd=" << s.round_increment_mm
       << ";dec_sep=" << static_cast<int>(s.decimal_separator)
       << ";show_u=" << (s.show_unit ? 1 : 0)
       << ";sup_l=" << (s.suppress_leading_zeros ? 1 : 0)
       << ";sup_t=" << (s.suppress_trailing_zeros ? 1 : 0)
       << ";ang_u=" << static_cast<int>(s.angular_units)
       << ";ang_dec=" << s.angular_decimals
       << ";dual_e=" << (s.dual_enabled ? 1 : 0)
       << ";dual_u=" << static_cast<int>(s.dual_units)
       << ";dual_dec=" << s.dual_decimals
       << ";tol_m=" << static_cast<int>(s.tolerance_mode)
       << ";tol_mag=" << s.tolerance_magnitude
       << ";tol_u=" << s.tolerance_upper
       << ";tol_l=" << s.tolerance_lower
       << ";tol_r=" << s.tolerance_text_ratio;
    return ss.str();
}

std::optional<DimensionObject> deserialize_dimension_spec(std::string_view spec) {
    if (spec.rfind("dimension:v1", 0) != 0) {
        return std::nullopt;
    }
    std::unordered_map<std::string, std::string> kv;
    size_t start = 0;
    while (start < spec.size()) {
        auto next = spec.find(';', start);
        if (next == std::string_view::npos) next = spec.size();
        auto token = spec.substr(start, next - start);
        auto eq = token.find('=');
        if (eq != std::string_view::npos) {
            kv.emplace(std::string(token.substr(0, eq)), std::string(token.substr(eq + 1)));
        }
        start = next + 1;
    }

    DimensionObject dim;
    auto get_int = [&](const std::string& k, int def) -> int {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return def;
    };
    auto get_double = [&](const std::string& k, double def) -> double {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stod(it->second); } catch (...) {}
        }
        return def;
    };
    auto get_u64 = [&](const std::string& k, uint64_t def) -> uint64_t {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stoull(it->second); } catch (...) {}
        }
        return def;
    };

    dim.kind = static_cast<DimensionKind>(get_int("kind", 0));
    dim.placement_point.x = get_double("px", 0.0);
    dim.placement_point.y = get_double("py", 0.0);
    dim.flip_arrows = (get_int("flip", 0) != 0);
    dim.linear_orientation = static_cast<geometry::LinearOrientation>(get_int("lin_ori", 0));
    dim.ordinate_axis = static_cast<geometry::OrdinateAxis>(get_int("ord_ax", 0));
    dim.datum_origin.x = get_double("dat_x", 0.0);
    dim.datum_origin.y = get_double("dat_y", 0.0);

    size_t ref_cnt = static_cast<size_t>(get_int("ref_cnt", 0));
    for (size_t i = 0; i < ref_cnt; ++i) {
        SourceReference r;
        r.source_object_id = get_u64("r" + std::to_string(i) + "_obj", kInvalidObjectId);
        r.ref_kind = static_cast<SourceRefKind>(get_int("r" + std::to_string(i) + "_k", 0));
        if (get_int("r" + std::to_string(i) + "_has_el", 0) != 0) {
            r.element_id = get_u64("r" + std::to_string(i) + "_el", 0);
        }
        r.explicit_point.x = get_double("r" + std::to_string(i) + "_x", 0.0);
        r.explicit_point.y = get_double("r" + std::to_string(i) + "_y", 0.0);
        dim.references.push_back(r);
    }

    dim.style.style_id = get_u64("style_id", 1);
    dim.style.line_width_mm = get_double("l_w", 0.25);
    dim.style.ext_width_mm = get_double("e_w", 0.25);
    dim.style.ext_offset_mm = get_double("e_off", 1.5);
    dim.style.ext_overhang_mm = get_double("e_ext", 2.0);
    dim.style.arrow_type = static_cast<geometry::ArrowType>(get_int("arr_t", 0));
    dim.style.arrow_size_mm = get_double("arr_sz", 2.5);
    dim.style.flip_first_arrow = (get_int("flip1", 0) != 0);
    dim.style.flip_second_arrow = (get_int("flip2", 0) != 0);
    dim.style.text_size_mm = get_double("t_sz", 3.0);
    dim.style.text_position = static_cast<DimensionTextPosition>(get_int("t_pos", 0));
    dim.style.text_alignment = static_cast<DimensionTextAlignment>(get_int("t_aln", 0));
    dim.style.text_gap_mm = get_double("t_gap", 0.75);
    dim.style.text_mask = (get_int("t_mask", 1) != 0);
    dim.style.basic_frame = (get_int("b_frame", 0) != 0);
    dim.style.fit_mode = static_cast<FitMode>(get_int("fit_m", 0));
    dim.style.baseline_spacing_mm = get_double("bl_sp", 8.0);
    dim.style.units = static_cast<UnitType>(get_int("unit", 0));
    dim.style.decimals = get_int("dec", 2);
    dim.style.round_increment_mm = get_double("rnd", 0.0);
    dim.style.decimal_separator = static_cast<DecimalSeparator>(get_int("dec_sep", 0));
    dim.style.show_unit = (get_int("show_u", 1) != 0);
    dim.style.suppress_leading_zeros = (get_int("sup_l", 0) != 0);
    dim.style.suppress_trailing_zeros = (get_int("sup_t", 1) != 0);
    dim.style.angular_units = static_cast<AngularUnit>(get_int("ang_u", 0));
    dim.style.angular_decimals = get_int("ang_dec", 2);
    dim.style.dual_enabled = (get_int("dual_e", 0) != 0);
    dim.style.dual_units = static_cast<UnitType>(get_int("dual_u", 3));
    dim.style.dual_decimals = get_int("dual_dec", 2);
    dim.style.tolerance_mode = static_cast<ToleranceMode>(get_int("tol_m", 0));
    dim.style.tolerance_magnitude = get_double("tol_mag", 0.05);
    dim.style.tolerance_upper = get_double("tol_u", 0.05);
    dim.style.tolerance_lower = get_double("tol_l", -0.05);
    dim.style.tolerance_text_ratio = get_double("tol_r", 0.7);

    return dim;
}

PathObject dimension_to_path_object(const DimensionObject& dim, const DimensionLayout& layout) {
    PathObject obj;
    obj.id = dim.id;
    obj.name = dim.name;
    obj.transform = dim.transform;
    obj.stroke = dim.style.line_color;
    obj.stroke_width_mm = dim.style.line_width_mm;
    obj.fill = dim.style.line_color;
    obj.has_stroke = true;
    obj.has_fill = (dim.style.arrow_type == geometry::ArrowType::ClosedFilled);
    obj.contour_spec = serialize_dimension_spec(dim);

    for (const auto& line : layout.lines) {
        for (const auto& el : line.elements()) {
            if (el.verb == geometry::PathVerb::MoveTo) {
                obj.path.move_to(el.pt);
            } else if (el.verb == geometry::PathVerb::LineTo) {
                obj.path.line_to(el.pt);
            } else if (el.verb == geometry::PathVerb::ArcTo) {
                obj.path.arc_to(el.rx, el.ry, el.rotation_deg, el.large_arc, el.sweep, el.pt.x, el.pt.y);
            } else if (el.verb == geometry::PathVerb::Close) {
                obj.path.close();
            }
        }
    }

    for (const auto& arr : layout.arrows) {
        for (const auto& el : arr.elements()) {
            if (el.verb == geometry::PathVerb::MoveTo) {
                obj.path.move_to(el.pt);
            } else if (el.verb == geometry::PathVerb::LineTo) {
                obj.path.line_to(el.pt);
            } else if (el.verb == geometry::PathVerb::Close) {
                obj.path.close();
            }
        }
    }

    return obj;
}

} // namespace

DimensionRegistry& DimensionRegistry::instance() {
    static DimensionRegistry s_instance;
    return s_instance;
}

void DimensionRegistry::set(ObjectId id, DimensionObject dim) {
    dimensions_[id] = std::move(dim);
}

void DimensionRegistry::remove(ObjectId id) {
    dimensions_.erase(id);
}

std::optional<DimensionObject> DimensionRegistry::get(ObjectId id) const {
    const auto it = dimensions_.find(id);
    if (it != dimensions_.end()) return it->second;
    return std::nullopt;
}

bool DimensionRegistry::contains(ObjectId id) const {
    return dimensions_.find(id) != dimensions_.end();
}

void DimensionRegistry::clear() {
    dimensions_.clear();
}

const std::unordered_map<ObjectId, DimensionObject>& DimensionRegistry::all() const {
    return dimensions_;
}

std::optional<DimensionObject> get_dimension(const Document& doc, ObjectId id) {
    auto obj = doc.objects().get(id);
    if (!obj.has_value()) return std::nullopt;
    const auto* path = std::get_if<PathObject>(&(*obj));
    if (!path) return std::nullopt;

    // Deserialization from path->contour_spec ensures perfect snapshot/undo/redo synchronization
    auto dim_deser = deserialize_dimension_spec(path->contour_spec);
    if (dim_deser) {
        dim_deser->id = id;
        dim_deser->name = path->name;
        dim_deser->transform = path->transform;
        dim_deser->style.line_width_mm = path->stroke_width_mm;
        dim_deser->style.line_color = path->stroke;
        DimensionRegistry::instance().set(id, *dim_deser);
        return dim_deser;
    }

    auto dim = DimensionRegistry::instance().get(id);
    if (!dim.has_value()) return std::nullopt;
    dim->style.line_width_mm = path->stroke_width_mm;
    dim->style.line_color = path->stroke;
    return dim;
}

// --- AddDimensionCommand ---

AddDimensionCommand::AddDimensionCommand(DimensionObject dimension, size_t layer_idx)
    : dimension_(std::move(dimension)), layer_idx_(layer_idx) {}

CommandResult AddDimensionCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for AddDimensionCommand");
    }
    if (page.layers[layer_idx_].locked) {
        return CommandResult::fail("Cannot add dimension to a locked layer");
    }

    if (dimension_.id == kInvalidObjectId) {
        dimension_.id = generate_object_id();
    }
    object_id_ = dimension_.id;

    if (doc.objects().contains(object_id_)) {
        return CommandResult::fail("Dimension ID already exists in document");
    }

    const auto layout = evaluate_dimension_layout(dimension_, doc);
    const auto path_obj = dimension_to_path_object(dimension_, layout);

    PersistentObjectMap map = doc.objects().set(object_id_, ObjectVariant{path_obj});
    page.layers[layer_idx_].object_ids.push_back(object_id_);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(object_id_);

    DimensionRegistry::instance().set(object_id_, dimension_);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddDimensionCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddDimensionCommand");
    }

    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo");
    }

    auto& ids = page.layers[layer_idx_].object_ids;
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (*it == object_id_) {
            ids.erase(it);
            break;
        }
    }

    PersistentObjectMap map = doc.objects().remove(object_id_);
    doc.apply_mutation(std::move(page), std::move(map));

    DimensionRegistry::instance().remove(object_id_);
    executed_ = false;
    return CommandResult::ok();
}

// --- RemoveDimensionCommand ---

RemoveDimensionCommand::RemoveDimensionCommand(ObjectId dimension_id)
    : dimension_id_(dimension_id) {}

CommandResult RemoveDimensionCommand::execute(Document& doc) {
    auto opt = doc.objects().get(dimension_id_);
    if (!opt) {
        return CommandResult::fail("Dimension object to remove not found");
    }

    saved_dimension_ = DimensionRegistry::instance().get(dimension_id_);

    Page page = doc.page();
    bool found = false;
    for (size_t i = 0; i < page.layers.size(); ++i) {
        auto& ids = page.layers[i].object_ids;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == dimension_id_) {
                saved_object_idx_ = static_cast<size_t>(it - ids.begin());
                ids.erase(it);
                saved_layer_idx_ = i;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    PersistentObjectMap map = doc.objects().remove(dimension_id_);
    doc.apply_mutation(std::move(page), std::move(map));
    DimensionRegistry::instance().remove(dimension_id_);

    executed_ = true;
    return CommandResult::ok();
}

CommandResult RemoveDimensionCommand::undo(Document& doc) {
    if (!executed_ || !saved_dimension_) {
        return CommandResult::fail("Cannot undo RemoveDimensionCommand");
    }

    Page page = doc.page();
    if (saved_layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo");
    }

    const auto layout = evaluate_dimension_layout(*saved_dimension_, doc);
    const auto path_obj = dimension_to_path_object(*saved_dimension_, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    auto& ids = page.layers[saved_layer_idx_].object_ids;
    if (saved_object_idx_ <= ids.size()) {
        ids.insert(ids.begin() + saved_object_idx_, dimension_id_);
    } else {
        ids.push_back(dimension_id_);
    }

    doc.apply_mutation(std::move(page), std::move(map));
    DimensionRegistry::instance().set(dimension_id_, *saved_dimension_);

    executed_ = false;
    return CommandResult::ok();
}

// --- UpdateDimensionStyleCommand ---

UpdateDimensionStyleCommand::UpdateDimensionStyleCommand(ObjectId dimension_id, DimensionStyle new_style)
    : dimension_id_(dimension_id), new_style_(std::move(new_style)) {}

CommandResult UpdateDimensionStyleCommand::execute(Document& doc) {
    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    old_style_ = dim_opt->style;
    dim_opt->style = new_style_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateDimensionStyleCommand::undo(Document& doc) {
    if (!executed_) return CommandResult::fail("Cannot undo UpdateDimensionStyleCommand");

    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    dim_opt->style = old_style_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = false;
    return CommandResult::ok();
}

// --- UpdateDimensionPlacementCommand ---

UpdateDimensionPlacementCommand::UpdateDimensionPlacementCommand(ObjectId dimension_id, geometry::Point2D new_placement)
    : dimension_id_(dimension_id), new_placement_(new_placement) {}

CommandResult UpdateDimensionPlacementCommand::execute(Document& doc) {
    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    old_placement_ = dim_opt->placement_point;
    dim_opt->placement_point = new_placement_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult UpdateDimensionPlacementCommand::undo(Document& doc) {
    if (!executed_) return CommandResult::fail("Cannot undo UpdateDimensionPlacementCommand");

    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    dim_opt->placement_point = old_placement_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = false;
    return CommandResult::ok();
}

// --- RelinkDimensionCommand ---

RelinkDimensionCommand::RelinkDimensionCommand(ObjectId dimension_id, std::vector<SourceReference> new_references)
    : dimension_id_(dimension_id), new_references_(std::move(new_references)) {}

CommandResult RelinkDimensionCommand::execute(Document& doc) {
    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    old_references_ = dim_opt->references;
    dim_opt->references = new_references_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult RelinkDimensionCommand::undo(Document& doc) {
    if (!executed_) return CommandResult::fail("Cannot undo RelinkDimensionCommand");

    auto dim_opt = DimensionRegistry::instance().get(dimension_id_);
    if (!dim_opt) return CommandResult::fail("Dimension not found in registry");

    dim_opt->references = old_references_;

    const auto layout = evaluate_dimension_layout(*dim_opt, doc);
    const auto path_obj = dimension_to_path_object(*dim_opt, layout);

    PersistentObjectMap map = doc.objects().set(dimension_id_, ObjectVariant{path_obj});
    doc.apply_mutation(doc.page(), std::move(map));

    DimensionRegistry::instance().set(dimension_id_, *dim_opt);
    executed_ = false;
    return CommandResult::ok();
}

// --- ReevaluateDimensionsCommand ---

ReevaluateDimensionsCommand::ReevaluateDimensionsCommand(ObjectId modified_source_id)
    : modified_source_id_(modified_source_id) {}

CommandResult ReevaluateDimensionsCommand::execute(Document& doc) {
    entries_.clear();
    PersistentObjectMap map = doc.objects();

    for (const auto& [id, dim] : DimensionRegistry::instance().all()) {
        bool refs_source = false;
        for (const auto& ref : dim.references) {
            if (ref.source_object_id == modified_source_id_) {
                refs_source = true;
                break;
            }
        }
        if (!refs_source) continue;

        auto current_obj = map.get(id);
        if (!current_obj) continue;
        const auto* cur_path = std::get_if<PathObject>(&(*current_obj));
        if (!cur_path) continue;

        const auto new_layout = evaluate_dimension_layout(dim, doc);
        const auto new_path = dimension_to_path_object(dim, new_layout);

        entries_.push_back({id, *cur_path, new_path});
        map = map.set(id, ObjectVariant{new_path});
    }

    if (!entries_.empty()) {
        doc.apply_mutation(doc.page(), std::move(map));
    }
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ReevaluateDimensionsCommand::undo(Document& doc) {
    if (!executed_) return CommandResult::fail("Cannot undo ReevaluateDimensionsCommand");

    PersistentObjectMap map = doc.objects();
    for (const auto& entry : entries_) {
        map = map.set(entry.id, ObjectVariant{entry.before_path});
    }
    doc.apply_mutation(doc.page(), std::move(map));

    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
