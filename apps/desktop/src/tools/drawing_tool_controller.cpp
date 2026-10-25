#include "drawing_tool_controller.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace pikdraw::desktop::tools {
namespace {

using namespace vector::precision::snap;
using vector::tools::line::PathDrawingKind;
using vector::tools::line::PathDrawingResult;

double segment_angle(const std::vector<geometry::Point2D>& points) {
    if (points.size() < 2) return 0.0;
    const auto delta = points.back() - points[points.size() - 2];
    return std::atan2(delta.y, delta.x) * 180.0 / 3.14159265358979323846;
}

DirectionSnapResult hard_lock_result(const SnapQueryStamp& stamp,
                                     geometry::Point2D anchor,
                                     geometry::Point2D pointer,
                                     double step_degrees,
                                     DirectionMode mode) {
    DirectionSnapResult result;
    result.stamp = stamp;
    result.mode = mode;
    result.world_point = pointer;
    const double dx = pointer.x - anchor.x;
    const double dy = pointer.y - anchor.y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0 && dy == 0.0)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    const double radians = std::atan2(dy, dx);
    const double step = step_degrees * 3.14159265358979323846 / 180.0;
    result.world_angle_degrees = normalize_angle_degrees(
        std::round(radians / step) * step_degrees);
    const double projected = dx * std::cos(result.world_angle_degrees *
        3.14159265358979323846 / 180.0) + dy * std::sin(
        result.world_angle_degrees * 3.14159265358979323846 / 180.0);
    if (!std::isfinite(projected) || projected < 0.0) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    result.world_point = {anchor.x + projected * std::cos(
        result.world_angle_degrees * 3.14159265358979323846 / 180.0),
        anchor.y + projected * std::sin(
        result.world_angle_degrees * 3.14159265358979323846 / 180.0)};
    result.constrained = result.world_point.is_finite();
    return result;
}

} // namespace

DrawingToolController::DrawingToolController(document::Document& document,
                                             std::filesystem::path preference_path)
    : document_(document), preference_path_(std::move(preference_path)) {
    if (preference_path_.empty()) preference_path_ = drawing_preferences_path();
    const auto loaded = load_drawing_preferences(preference_path_);
    preferences_ = loaded.value;
    apply_configuration();
    if (!loaded.diagnostic.empty()) message_ = loaded.diagnostic;
}

vector::precision::numeric::ParseOptions DrawingToolController::numeric_parse_options() const noexcept {
    const auto snapshot = document_.current_snapshot();
    const auto unit = snapshot ? snapshot->page().display_length_unit
                               : preferences_.default_length_unit;
    return vector::precision::numeric::ParseOptions{'.', unit};
}

vector::tools::line::PathDrawingResult DrawingToolController::update_numeric_candidate() {
    return path_.update_numeric_candidate(numeric_entry_, numeric_parse_options());
}

bool DrawingToolController::activate(PathDrawingKind kind) {
    cancel();
    const auto snapshot = document_.current_snapshot();
    const auto result = path_.begin(kind, snapshot);
    update_result_message(result);
    if (result.code != vector::tools::line::PathDrawingResultCode::Applied)
        return false;
    requested_document_id_.clear();
    index_.reset();
    index_ready_ = false;
    index_failed_ = false;
    numeric_entry_.clear();
    snap_kind_.reset();
    snap_acquisition_.reset();
    direction_lock_.reset();
    return true;
}

void DrawingToolController::cancel() noexcept {
    path_.cancel_all();
    numeric_entry_.clear();
    snap_kind_.reset();
    snap_acquisition_.reset();
    direction_lock_.reset();
    index_.reset();
    index_ready_ = false;
    index_failed_ = false;
}

SnapCameraTransform DrawingToolController::camera_transform(
    const rendering::Camera2D& camera, double dpi_scale) {
    const auto center = camera.center();
    const std::array<uint64_t, 6> signature{
        std::bit_cast<uint64_t>(center.x), std::bit_cast<uint64_t>(center.y),
        std::bit_cast<uint64_t>(camera.zoom()), static_cast<uint64_t>(camera.width()),
        static_cast<uint64_t>(camera.height()), std::bit_cast<uint64_t>(dpi_scale)};
    if (!have_camera_signature_ || signature != camera_signature_) {
        if (have_camera_signature_) ++camera_generation_;
        camera_signature_ = signature;
        have_camera_signature_ = true;
    }
    const double zoom = camera.zoom();
    return {{zoom, 0.0, 0.0, -zoom,
             camera.width() * 0.5 - center.x * zoom,
             camera.height() * 0.5 + center.y * zoom},
            dpi_scale, dpi_scale, camera_generation_};
}

SnapQueryStamp DrawingToolController::make_stamp(
    const document::DocumentSnapshot& snapshot, const char* tool_context) {
    SnapQueryStamp stamp;
    stamp.document_id = snapshot.document_id();
    stamp.document_epoch = snapshot.epoch();
    stamp.document_revision = snapshot.revision();
    stamp.camera_generation = camera_generation_;
    stamp.config_generation = snap_config_.snapshot().generation;
    stamp.capability_generation = capabilities_.generation;
    stamp.tool_context = tool_context;
    stamp.query_generation = ++query_generation_;
    return stamp;
}

void DrawingToolController::pointer_move(geometry::Point2D point,
                                         const rendering::Camera2D& camera,
                                         double dpi_scale, bool hard_45_lock,
                                         bool hard_ortho_lock) {
    if (!path_.active() || !point.is_finite()) return;
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) return;
    const auto freshness = path_.observe_snapshot(snapshot);
    if (freshness.code == vector::tools::line::PathDrawingResultCode::Stale) {
        message_ = freshness.message;
        cancel();
        return;
    }
    if (snapshot && (requested_document_id_ != snapshot->document_id() ||
        requested_epoch_ != snapshot->epoch() || requested_revision_ != snapshot->revision())) {
        requested_document_id_ = snapshot->document_id();
        requested_epoch_ = snapshot->epoch();
        requested_revision_ = snapshot->revision();
        index_.reset();
        index_ready_ = false;
        index_failed_ = false;
        index_worker_.request(snapshot);
    }
    const auto built = index_worker_.result_for(*snapshot);
    if (built.index) {
        index_ = built.index;
        index_ready_ = true;
    } else if (!built.diagnostic.empty()) {
        message_ = built.diagnostic;
        index_failed_ = true;
    }

    const auto transform = camera_transform(camera, dpi_scale);
    const auto kind = path_.kind();
    const char* context = kind == PathDrawingKind::Line ? "line" : "polyline";
    const auto stamp = make_stamp(*snapshot, context);
    SnapQueryResult query;
    query.status = SnapQueryStatus::Success;
    query.stamp = stamp;
    const auto configuration = snap_config_.snapshot();
    if (index_) {
        SnapQueryContext query_context;
        query_context.active_layer_index = active_layer_index_;
        query_context.current_stamp = stamp;
        query_context.previous_acquisition = snap_acquisition_;
        query = index_->query(stamp, point, transform, configuration,
                              capabilities_, query_context);
    }
    snap_acquisition_ = query.next_acquisition;

    std::optional<DirectionSnapResult> direction;
    const auto& points = path_.points();
    if (!points.empty()) {
        const double reference = configuration.direction.reference == AngleReference::WorldX
            ? 0.0 : segment_angle(points);
        if (hard_45_lock) {
            const double angle_step = preferences_.profile == "Corel" ? 15.0
                : preferences_.profile == "Custom" ? preferences_.polar_step_degrees : 45.0;
            direction = hard_lock_result(stamp, points.back(), point, angle_step,
                                         DirectionMode::Polar);
        } else if (hard_ortho_lock) {
            auto hard_config = configuration;
            hard_config.direction.mode = DirectionMode::Ortho;
            direction = constrain_direction(stamp, points.back(), point, reference,
                                            transform, hard_config, direction_lock_);
        } else if (configuration.direction.mode != DirectionMode::Free) {
            direction = constrain_direction(stamp, points.back(), point, reference,
                                            transform, configuration, direction_lock_);
        }
    }
    direction_lock_ = direction ? direction->next_lock : std::nullopt;
    const bool hard_direction = hard_45_lock || hard_ortho_lock;
    const auto result = path_.update_from_snap_query(point, query, stamp, transform,
                                                      direction, hard_direction);
    update_result_message(result);
    snap_kind_.reset();
    if (query.chosen && path_.candidate() &&
        query.chosen->world_point == *path_.candidate())
        snap_kind_ = query.chosen->key.kind;
}

DrawingSnapQuery DrawingToolController::query_snap_point(
    geometry::Point2D point, const rendering::Camera2D& camera, double dpi_scale,
    const char* tool_context, std::optional<document::ObjectId> current_source,
    std::optional<geometry::Point2D> external_anchor) {
    using namespace vector::precision::snap;
    DrawingSnapQuery output;
    output.result.status = SnapQueryStatus::InvalidInput;
    if (!point.is_finite() || !std::isfinite(dpi_scale) || dpi_scale <= 0.0 ||
        !tool_context || !*tool_context) return output;
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) return output;
    if (requested_document_id_ != snapshot->document_id() ||
        requested_epoch_ != snapshot->epoch() || requested_revision_ != snapshot->revision()) {
        requested_document_id_ = snapshot->document_id();
        requested_epoch_ = snapshot->epoch();
        requested_revision_ = snapshot->revision();
        index_.reset();
        index_ready_ = false;
        index_failed_ = false;
        snap_acquisition_.reset();
        index_worker_.request(snapshot);
    }
    const auto built = index_worker_.result_for(*snapshot);
    if (built.index) {
        index_ = built.index;
        index_ready_ = true;
    } else if (!built.diagnostic.empty()) {
        message_ = built.diagnostic;
        index_failed_ = true;
    }

    output.camera = camera_transform(camera, dpi_scale);
    output.stamp = make_stamp(*snapshot, tool_context);
    output.result.status = SnapQueryStatus::Success;
    output.result.stamp = output.stamp;
    if (!index_) return output;
    SnapQueryContext context;
    context.active_layer_index = active_layer_index_;
    context.current_source_object_id = current_source;
    context.current_stamp = output.stamp;
    context.previous_acquisition = snap_acquisition_;
    context.external_anchor = external_anchor;
    output.result = index_->query(output.stamp, point, output.camera,
        snap_config_.snapshot(), capabilities_, context);
    snap_acquisition_ = output.result.next_acquisition;
    return output;
}

PathDrawingResult DrawingToolController::confirm_candidate() {
    const auto result = path_.confirm_candidate(document_);
    numeric_entry_.clear();
    snap_kind_.reset();
    update_result_message(result);
    if (result.committed()) {
        const auto snapshot = document_.current_snapshot();
        requested_revision_ = 0;
        if (snapshot) requested_document_id_.clear();
    }
    return result;
}

PathDrawingResult DrawingToolController::enter() {
    PathDrawingResult result;
    if (!numeric_entry_.empty()) {
        result = update_numeric_candidate();
        if (result.code == vector::tools::line::PathDrawingResultCode::PreviewUpdated)
            result = path_.confirm_candidate(document_);
        if (result.code == vector::tools::line::PathDrawingResultCode::Applied || result.committed())
            numeric_entry_.clear();
    } else {
        result = path_.enter(document_);
    }
    update_result_message(result);
    return result;
}

PathDrawingResult DrawingToolController::escape() {
    const auto result = path_.escape(document_);
    numeric_entry_.clear();
    snap_kind_.reset();
    update_result_message(result);
    return result;
}

PathDrawingResult DrawingToolController::backspace() {
    const auto result = path_.backspace();
    numeric_entry_.clear();
    snap_kind_.reset();
    update_result_message(result);
    return result;
}

bool DrawingToolController::append_numeric_character(char value) {
    const bool allowed = (value >= '0' && value <= '9') || value == '.' || value == ',' ||
        value == '<' ||
        value == ';' || value == '@' || value == '#' || value == '+' || value == '-' ||
        value == '*' || value == '/' || value == '(' || value == ')' ||
        (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    if (!path_.active() || !allowed || numeric_entry_.size() >=
        vector::precision::numeric::max_input_bytes)
        return false;
    numeric_entry_.push_back(value);
    if (path_.points().empty()) {
        const auto unit = numeric_parse_options().default_length_unit;
        message_ = "First numeric point: #x;y " +
            std::string(geometry::units::length_unit_suffix(unit));
        return true;
    }
    const auto result = update_numeric_candidate();
    update_result_message(result);
    return true;
}

void DrawingToolController::erase_numeric_character() {
    if (!numeric_entry_.empty()) numeric_entry_.pop_back();
    if (numeric_entry_.empty()) {
        snap_kind_.reset();
        return;
    }
    const auto result = update_numeric_candidate();
    update_result_message(result);
}

void DrawingToolController::clear_numeric_entry() noexcept {
    numeric_entry_.clear();
}

bool DrawingToolController::toggle_snap_master() {
    return set_snap_master_enabled(!snap_config_.master_enabled());
}

bool DrawingToolController::toggle_snap_objects() {
    return set_snap_group_enabled(SnapGroup::ObjectPoints,
        !snap_config_.group_enabled(SnapGroup::ObjectPoints));
}

bool DrawingToolController::toggle_snap_page() {
    using vector::precision::snap::SnapOption;
    const bool enabled = !snap_config_.option_enabled(SnapOption::PageEdge);
    if (!snap_config_.set_option_enabled(SnapOption::PageEdge, enabled) ||
        !snap_config_.set_option_enabled(SnapOption::PageCenter, enabled)) return false;
    preferences_.snap_page = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::toggle_snap_guides() {
    using vector::precision::snap::SnapOption;
    const bool enabled = !snap_config_.option_enabled(SnapOption::Guide);
    if (!snap_config_.set_option_enabled(SnapOption::Guide, enabled)) return false;
    preferences_.snap_guides = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::toggle_smart_guides() {
    return set_smart_guides_enabled(!snap_config_.snapshot().smart_guides_enabled);
}

bool DrawingToolController::toggle_smart_spacing() {
    return set_smart_spacing_enabled(!snap_config_.snapshot().smart_spacing_enabled);
}

bool DrawingToolController::toggle_snap_option(SnapOption option) {
    return set_snap_option_enabled(option,
        !snap_config_.snapshot().options[static_cast<size_t>(option)]);
}

bool DrawingToolController::set_snap_master_enabled(bool enabled) {
    if (!snap_config_.set_master_enabled(enabled)) return false;
    preferences_.snap_master = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::set_snap_group_enabled(SnapGroup group, bool enabled) {
    if (!snap_config_.set_group_enabled(group, enabled)) return false;
    const auto index = static_cast<size_t>(group);
    if (index >= preferences_.snap_groups.size()) return false;
    preferences_.snap_groups[index] = enabled;
    if (group == SnapGroup::ObjectPoints) preferences_.snap_objects = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::set_snap_option_enabled(SnapOption option, bool enabled) {
    if (static_cast<size_t>(option) >= vector::precision::snap::kSnapOptionCount)
        return false;
    using vector::precision::snap::SnapOption;
    if (option == SnapOption::PageEdge || option == SnapOption::PageCenter) {
        if (!snap_config_.set_option_enabled(SnapOption::PageEdge, enabled) ||
            !snap_config_.set_option_enabled(SnapOption::PageCenter, enabled)) return false;
        preferences_.snap_page = enabled;
        preferences_.snap_options[static_cast<size_t>(SnapOption::PageEdge)] = enabled;
        preferences_.snap_options[static_cast<size_t>(SnapOption::PageCenter)] = enabled;
    } else {
        if (!snap_config_.set_option_enabled(option, enabled)) return false;
        preferences_.snap_options[static_cast<size_t>(option)] = enabled;
        switch (option) {
        case SnapOption::Endpoint: preferences_.endpoint = enabled; break;
        case SnapOption::SourceNode: preferences_.source_node = enabled; break;
        case SnapOption::LineMidpoint: preferences_.line_midpoint = enabled; break;
        case SnapOption::Grid: preferences_.grid = enabled; break;
        case SnapOption::Guide: preferences_.snap_guides = enabled; break;
        default: break;
        }
    }
    save_preferences();
    return true;
}

bool DrawingToolController::set_smart_guides_enabled(bool enabled) {
    if (!snap_config_.set_smart_guides_enabled(enabled)) return false;
    preferences_.smart_guides = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::set_smart_spacing_enabled(bool enabled) {
    if (!snap_config_.set_smart_spacing_enabled(enabled)) return false;
    preferences_.smart_spacing = enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::set_snap_radii(double acquire_radius_dip,
                                            double release_radius_dip,
                                            std::string* diagnostic) {
    if (!snap_config_.set_hysteresis(acquire_radius_dip, release_radius_dip)) {
        if (diagnostic) *diagnostic = "Snap radii must be 1–64 DIP with release >= acquire";
        return false;
    }
    const auto old_acquire = preferences_.snap_acquire_radius_dip;
    const auto old_release = preferences_.snap_release_radius_dip;
    preferences_.snap_acquire_radius_dip = acquire_radius_dip;
    preferences_.snap_release_radius_dip = release_radius_dip;
    if (!save_drawing_preferences(preference_path_, preferences_, diagnostic)) {
        preferences_.snap_acquire_radius_dip = old_acquire;
        preferences_.snap_release_radius_dip = old_release;
        (void)snap_config_.set_hysteresis(old_acquire, old_release);
        return false;
    }
    return true;
}

bool DrawingToolController::toggle_direction(DirectionMode mode) {
    const bool changed = mode == DirectionMode::Free
        ? snap_config_.set_direction_mode(mode)
        : snap_config_.toggle_direction_mode(mode);
    if (!changed) return false;
    preferences_.direction = snap_config_.snapshot().direction.mode;
    save_preferences();
    return true;
}

bool DrawingToolController::cycle_polar_step() {
    const auto current = snap_config_.snapshot().direction.polar.step_degrees;
    const double next = current == 15.0 ? 30.0 : current == 30.0 ? 45.0
                       : current == 45.0 ? 90.0 : 15.0;
    if (!snap_config_.set_polar_preset(next)) return false;
    preferences_.polar_step_degrees = next;
    save_preferences();
    return true;
}

bool DrawingToolController::cycle_profile() {
    if (preferences_.profile == "PikDraw") preferences_.profile = "Illustrator";
    else if (preferences_.profile == "Illustrator") preferences_.profile = "Corel";
    else if (preferences_.profile == "Corel") preferences_.profile = "Custom";
    else preferences_.profile = "PikDraw";
    save_preferences();
    return true;
}

DrawingToolVisualState DrawingToolController::visual_state() const {
    DrawingToolVisualState state;
    state.active = path_.active();
    state.snap_index_ready = index_ready_;
    state.snap_index_failed = index_failed_;
    state.kind = path_.kind();
    state.points = path_.points();
    state.candidate = path_.candidate();
    state.snap_kind = snap_kind_;
    state.numeric_entry = numeric_entry_;
    state.message = message_;
    state.profile = preferences_.profile;
    state.snap_config = snap_config_.snapshot();
    return state;
}

DrawingToolGridSnapStatus DrawingToolController::grid_snap_status() const noexcept {
    using vector::precision::snap::SnapOption;
    using vector::precision::snap::group_for_option;
    const auto grid = snap_config_.grid_settings();
    return {snap_config_.master_enabled() &&
                snap_config_.group_enabled(group_for_option(SnapOption::Grid)) &&
                snap_config_.option_enabled(SnapOption::Grid),
            grid.spacing_mm};
}

bool DrawingToolController::set_grid_spacing_mm(double spacing_mm,
                                                std::string* diagnostic) {
    if (!is_valid_grid_spacing_mm(spacing_mm)) {
        if (diagnostic) *diagnostic = "Grid spacing is outside the supported range";
        return false;
    }
    const auto previous_grid = snap_config_.grid_settings();
    const double previous_preference = preferences_.grid_spacing_mm;
    auto grid = snap_config_.grid_settings();
    grid.spacing_mm = spacing_mm;
    if (!snap_config_.set_grid(grid)) {
        if (diagnostic) *diagnostic = "Grid configuration could not be updated";
        return false;
    }
    preferences_.grid_spacing_mm = spacing_mm;
    if (!save_drawing_preferences(preference_path_, preferences_, diagnostic)) {
        preferences_.grid_spacing_mm = previous_preference;
        (void)snap_config_.set_grid(previous_grid);
        return false;
    }
    return true;
}

void DrawingToolController::suspend_for_pan() noexcept {
    if (path_.active()) (void)path_.suspend_for_pan();
}

void DrawingToolController::resume_after_pan() noexcept {
    if (path_.active()) (void)path_.resume_after_pan();
}

void DrawingToolController::save_preferences() const {
    std::string ignored;
    (void)save_drawing_preferences(preference_path_, preferences_, &ignored);
}

void DrawingToolController::apply_configuration() {
    (void)apply_drawing_preferences(preferences_, snap_config_);
}

void DrawingToolController::update_result_message(const PathDrawingResult& result) {
    if (!result.message.empty()) message_ = result.message;
    else if (result.code == vector::tools::line::PathDrawingResultCode::Committed)
        message_ = "Path committed · Ctrl+Z to undo";
    else if (result.code == vector::tools::line::PathDrawingResultCode::InvalidInput)
        message_ = "Invalid point or path operation";
    else if (result.code == vector::tools::line::PathDrawingResultCode::Stale)
        message_ = "Document changed; stale drawing discarded";
    else if (result.code == vector::tools::line::PathDrawingResultCode::PreviewUpdated)
        message_ = index_ready_ ? "Snap ready" : "Building snap index · raw pointer remains available";
}

} // namespace pikdraw::desktop::tools
