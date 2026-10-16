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
        query_context.active_layer_index = 0;
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
        result = path_.update_numeric_candidate(numeric_entry_);
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
        message_ = "First numeric point: #x;y mm";
        return true;
    }
    const auto result = path_.update_numeric_candidate(numeric_entry_);
    update_result_message(result);
    return true;
}

void DrawingToolController::erase_numeric_character() {
    if (!numeric_entry_.empty()) numeric_entry_.pop_back();
    if (numeric_entry_.empty()) {
        snap_kind_.reset();
        return;
    }
    const auto result = path_.update_numeric_candidate(numeric_entry_);
    update_result_message(result);
}

void DrawingToolController::clear_numeric_entry() noexcept {
    numeric_entry_.clear();
}

bool DrawingToolController::toggle_snap_master() {
    if (!snap_config_.toggle_master()) return false;
    preferences_.snap_master = snap_config_.snapshot().master_enabled;
    save_preferences();
    return true;
}

bool DrawingToolController::toggle_snap_option(SnapOption option) {
    const bool enabled = !snap_config_.snapshot().options[static_cast<size_t>(option)];
    if (!snap_config_.set_option_enabled(option, enabled)) return false;
    auto& value = preferences_;
    switch (option) {
    case SnapOption::Endpoint: value.endpoint = enabled; break;
    case SnapOption::SourceNode: value.source_node = enabled; break;
    case SnapOption::LineMidpoint: value.line_midpoint = enabled; break;
    case SnapOption::Grid: value.grid = enabled; break;
    default: return false;
    }
    save_preferences();
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
