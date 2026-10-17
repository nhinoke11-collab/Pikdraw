#pragma once

#include "pikdraw/vector/tools/pen/pen_drawing_tool.hpp"

#include <bit>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::vector::tools::pen::detail {

static_assert(sizeof(double) == sizeof(uint64_t) &&
              std::numeric_limits<double>::is_iec559,
              "Pen recovery requires IEEE 754 binary64 coordinates");

struct PenRecoveryResumeTarget {
    document::ObjectId object_id{document::kInvalidObjectId};
    document::SubpathId subpath_id{};
    document::NodeId endpoint_id{};
};

struct PenRecoveryHistoryEntry {
    PenAnchorInput anchor;
    document::PathNode node;
    std::optional<document::EditableSegment> incoming_segment;
};

struct PenDraftRecoveryData {
    size_t target_layer{0};
    std::optional<PenRecoveryResumeTarget> resume_target;
    document::EditableSubpath subpath;
    std::vector<PenRecoveryHistoryEntry> undo;
    std::vector<PenRecoveryHistoryEntry> redo;
};

class PenRecoveryWriter {
public:
    explicit PenRecoveryWriter(size_t limit) : limit_(limit) {}

    void u8(uint8_t value) {
        reserve(1);
        bytes_.push_back(value);
    }

    void u32(uint32_t value) {
        reserve(4);
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes_.push_back(static_cast<uint8_t>(value >> shift));
    }

    void u64(uint64_t value) {
        reserve(8);
        for (unsigned shift = 0; shift < 64; shift += 8)
            bytes_.push_back(static_cast<uint8_t>(value >> shift));
    }

    void real(double value) { u64(std::bit_cast<uint64_t>(value)); }

    void point(geometry::Point2D value) {
        real(value.x);
        real(value.y);
    }

    void node(const document::PathNode& value) {
        u64(value.id.value());
        point(value.anchor);
        u8(static_cast<uint8_t>(value.mode));
        u8(static_cast<uint8_t>((value.has_incoming_handle ? 1u : 0u) |
                                (value.has_outgoing_handle ? 2u : 0u)));
        point(value.incoming_handle);
        point(value.outgoing_handle);
        real(value.auto_smooth_tension);
    }

    void segment(const document::EditableSegment& value) {
        u64(value.id.value());
        u8(static_cast<uint8_t>(value.kind));
        point(value.quadratic_control);
        point(value.arc.center);
        point(value.arc.axis_u);
        point(value.arc.axis_v);
        real(value.arc.start_angle_rad);
        real(value.arc.sweep_angle_rad);
    }

    template <class Edit>
    void history_entry(const Edit& value) {
        point(value.anchor.anchor);
        u8(static_cast<uint8_t>(value.anchor.mode));
        u8(static_cast<uint8_t>((value.anchor.incoming_handle ? 1u : 0u) |
                                (value.anchor.outgoing_handle ? 2u : 0u)));
        if (value.anchor.incoming_handle) point(*value.anchor.incoming_handle);
        if (value.anchor.outgoing_handle) point(*value.anchor.outgoing_handle);
        node(value.node);
        u8(value.incoming_segment ? 1u : 0u);
        if (value.incoming_segment) segment(*value.incoming_segment);
    }

    [[nodiscard]] std::vector<uint8_t> take() && { return std::move(bytes_); }

private:
    void reserve(size_t count) {
        if (bytes_.size() > limit_ || count > limit_ - bytes_.size())
            throw std::length_error("Pen recovery checkpoint exceeds its byte limit");
    }

    size_t limit_;
    std::vector<uint8_t> bytes_;
};

class PenRecoveryReader {
public:
    explicit PenRecoveryReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool u8(uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool u32(uint32_t& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<uint32_t>(bytes_[offset_++]) << shift;
        return true;
    }

    bool u64(uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<uint64_t>(bytes_[offset_++]) << shift;
        return true;
    }

    bool real(double& value) {
        uint64_t bits = 0;
        if (!u64(bits)) return false;
        value = std::bit_cast<double>(bits);
        return true;
    }

    bool point(geometry::Point2D& value) {
        return real(value.x) && real(value.y);
    }

    bool node(document::PathNode& value) {
        uint64_t id = 0;
        uint8_t mode = 0;
        uint8_t flags = 0;
        if (!u64(id) || !point(value.anchor) || !u8(mode) || !u8(flags) ||
            !point(value.incoming_handle) || !point(value.outgoing_handle) ||
            !real(value.auto_smooth_tension))
            return false;
        if (mode > static_cast<uint8_t>(document::PathNodeMode::AutoSmooth) ||
            (flags & ~uint8_t{3}) != 0)
            return false;
        value.id = document::NodeId{id};
        value.mode = static_cast<document::PathNodeMode>(mode);
        value.has_incoming_handle = (flags & 1u) != 0;
        value.has_outgoing_handle = (flags & 2u) != 0;
        return true;
    }

    bool segment(document::EditableSegment& value) {
        uint64_t id = 0;
        uint8_t kind = 0;
        if (!u64(id) || !u8(kind) || !point(value.quadratic_control) ||
            !point(value.arc.center) || !point(value.arc.axis_u) ||
            !point(value.arc.axis_v) || !real(value.arc.start_angle_rad) ||
            !real(value.arc.sweep_angle_rad) ||
            kind > static_cast<uint8_t>(document::PathSegmentKind::EllipticArc))
            return false;
        value.id = document::SegmentId{id};
        value.kind = static_cast<document::PathSegmentKind>(kind);
        return true;
    }

    bool history_entry(PenRecoveryHistoryEntry& value) {
        uint8_t mode = 0;
        uint8_t flags = 0;
        uint8_t has_segment = 0;
        if (!point(value.anchor.anchor) || !u8(mode) || !u8(flags) ||
            mode > static_cast<uint8_t>(document::PathNodeMode::AutoSmooth) ||
            (flags & ~uint8_t{3}) != 0)
            return false;
        value.anchor.mode = static_cast<document::PathNodeMode>(mode);
        if (flags & 1u) {
            geometry::Point2D point_value;
            if (!point(point_value)) return false;
            value.anchor.incoming_handle = point_value;
        }
        if (flags & 2u) {
            geometry::Point2D point_value;
            if (!point(point_value)) return false;
            value.anchor.outgoing_handle = point_value;
        }
        if (!node(value.node) || !u8(has_segment) || has_segment > 1) return false;
        if (has_segment) {
            value.incoming_segment.emplace();
            if (!segment(*value.incoming_segment)) return false;
        }
        return true;
    }

    [[nodiscard]] size_t remaining() const noexcept {
        return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
    }
    [[nodiscard]] bool finished() const noexcept { return offset_ == bytes_.size(); }

private:
    const std::vector<uint8_t>& bytes_;
    size_t offset_{0};
};

inline bool valid_recovery_node(const document::PathNode& node) {
    if (!node.anchor.is_finite() || !node.incoming_handle.is_finite() ||
        !node.outgoing_handle.is_finite() || !std::isfinite(node.auto_smooth_tension))
        return false;
    document::EditablePath one_node_path;
    document::EditableSubpath one_node;
    const auto subpath_id = node.id.value() == std::numeric_limits<uint64_t>::max()
        ? uint64_t{1} : std::numeric_limits<uint64_t>::max();
    one_node.id = document::SubpathId{subpath_id};
    one_node.nodes.push_back(node);
    one_node_path.subpaths.push_back(std::move(one_node));
    return !one_node_path.validation_error();
}

inline bool valid_recovery_segment(const document::EditableSegment& segment);

inline bool valid_recovery_history_entry(const PenRecoveryHistoryEntry& entry) {
    if (!entry.node.id.valid() || !valid_recovery_node(entry.node) ||
        !entry.anchor.anchor.is_finite() ||
        (entry.anchor.incoming_handle && !entry.anchor.incoming_handle->is_finite()) ||
        (entry.anchor.outgoing_handle && !entry.anchor.outgoing_handle->is_finite()))
        return false;
    const bool has_handles = entry.anchor.incoming_handle || entry.anchor.outgoing_handle;
    const auto expected_mode = entry.anchor.mode == document::PathNodeMode::Corner &&
            has_handles
        ? document::PathNodeMode::Cusp : entry.anchor.mode;
    if (entry.anchor.anchor != entry.node.anchor || expected_mode != entry.node.mode ||
        entry.anchor.incoming_handle.has_value() != entry.node.has_incoming_handle ||
        entry.anchor.outgoing_handle.has_value() != entry.node.has_outgoing_handle ||
        (entry.anchor.incoming_handle &&
         *entry.anchor.incoming_handle != entry.node.incoming_handle) ||
        (entry.anchor.outgoing_handle &&
         *entry.anchor.outgoing_handle != entry.node.outgoing_handle))
        return false;
    if (entry.anchor.mode == document::PathNodeMode::AutoSmooth && has_handles)
        return false;
    if (!entry.incoming_segment) return true;
    return valid_recovery_segment(*entry.incoming_segment);
}

inline bool valid_recovery_segment(const document::EditableSegment& segment) {
    if (!segment.id.valid() || !segment.quadratic_control.is_finite() ||
        !segment.arc.center.is_finite() || !segment.arc.axis_u.is_finite() ||
        !segment.arc.axis_v.is_finite() ||
        !std::isfinite(segment.arc.start_angle_rad) ||
        !std::isfinite(segment.arc.sweep_angle_rad))
        return false;
    switch (segment.kind) {
    case document::PathSegmentKind::Line:
    case document::PathSegmentKind::CubicBezier:
        return true;
    case document::PathSegmentKind::QuadraticBezier:
        return geometry::validate_point(segment.quadratic_control) ==
            geometry::GeometryStatus::Ok;
    case document::PathSegmentKind::EllipticArc:
        return segment.arc.is_finite() &&
            geometry::validate_rect(segment.arc.bounds()) == geometry::GeometryStatus::Ok &&
            geometry::encode_endpoint_arc(segment.arc).has_value();
    }
    return false;
}

template <class UndoHistory, class RedoHistory>
inline std::vector<uint8_t> encode_pen_draft_checkpoint(
    size_t target_layer,
    const std::optional<PenRecoveryResumeTarget>& resume_target,
    const document::EditableSubpath& subpath,
    const UndoHistory& undo, const RedoHistory& redo,
    size_t max_bytes) {
    if (subpath.nodes.size() > std::numeric_limits<uint32_t>::max() ||
        subpath.segments.size() > std::numeric_limits<uint32_t>::max() ||
        undo.size() > std::numeric_limits<uint32_t>::max() ||
        redo.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Pen recovery state exceeds the encoded count range");

    PenRecoveryWriter writer(max_bytes);
    writer.u8('P');
    writer.u8('E');
    writer.u8('N');
    writer.u8('R');
    writer.u32(1);
    writer.u64(static_cast<uint64_t>(target_layer));
    writer.u8(resume_target ? 1u : 0u);
    if (resume_target) {
        writer.u64(resume_target->object_id);
        writer.u64(resume_target->subpath_id.value());
        writer.u64(resume_target->endpoint_id.value());
    }
    writer.u64(subpath.id.value());
    writer.u8(subpath.closed ? 1u : 0u);
    writer.u32(static_cast<uint32_t>(subpath.nodes.size()));
    writer.u32(static_cast<uint32_t>(subpath.segments.size()));
    for (const auto& node : subpath.nodes) writer.node(node);
    for (const auto& segment : subpath.segments) writer.segment(segment);
    writer.u32(static_cast<uint32_t>(undo.size()));
    for (const auto& edit : undo) writer.history_entry(edit);
    writer.u32(static_cast<uint32_t>(redo.size()));
    for (const auto& edit : redo) writer.history_entry(edit);
    return std::move(writer).take();
}

inline std::optional<PenDraftRecoveryData> decode_pen_draft_checkpoint(
    const std::vector<uint8_t>& bytes, const interaction::ToolSessionLimits& limits,
    std::string* out_error = nullptr) {
    const auto fail = [&](const char* message)
        -> std::optional<PenDraftRecoveryData> {
        if (out_error) *out_error = message;
        return std::nullopt;
    };
    if (out_error) out_error->clear();
    if (bytes.size() < 4 + 4 + 8 + 1 + 8 + 1 + 4 + 4 + 4 + 4 ||
        bytes.size() > limits.max_checkpoint_bytes)
        return fail("Pen recovery payload has an invalid size");

    PenRecoveryReader reader(bytes);
    uint8_t magic[4]{};
    uint32_t version = 0;
    uint64_t target_layer = 0;
    uint8_t has_resume = 0;
    if (!reader.u8(magic[0]) || !reader.u8(magic[1]) || !reader.u8(magic[2]) ||
        !reader.u8(magic[3]) || magic[0] != 'P' || magic[1] != 'E' ||
        magic[2] != 'N' || magic[3] != 'R' || !reader.u32(version) ||
        version != 1 || !reader.u64(target_layer) || !reader.u8(has_resume) ||
        has_resume > 1 || target_layer > std::numeric_limits<size_t>::max())
        return fail("Pen recovery header is invalid or unsupported");

    PenDraftRecoveryData data;
    data.target_layer = static_cast<size_t>(target_layer);
    if (has_resume) {
        uint64_t object_id = 0;
        uint64_t subpath_id = 0;
        uint64_t endpoint_id = 0;
        if (!reader.u64(object_id) || !reader.u64(subpath_id) ||
            !reader.u64(endpoint_id) || object_id == document::kInvalidObjectId ||
            subpath_id == 0 || endpoint_id == 0)
            return fail("Pen recovery resume target is invalid");
        data.resume_target = PenRecoveryResumeTarget{
            object_id, document::SubpathId{subpath_id}, document::NodeId{endpoint_id}};
    }

    uint64_t subpath_id = 0;
    uint8_t closed = 0;
    uint32_t node_count = 0;
    uint32_t segment_count = 0;
    if (!reader.u64(subpath_id) || subpath_id == 0 || !reader.u8(closed) ||
        closed != 0 || !reader.u32(node_count) || !reader.u32(segment_count) ||
        node_count == 0 || node_count > limits.max_draft_payload_bytes /
                sizeof(document::PathNode) ||
        segment_count != node_count - 1 || segment_count >
                limits.max_draft_payload_bytes / sizeof(document::EditableSegment))
        return fail("Pen recovery path counts or topology are invalid");
    data.subpath.id = document::SubpathId{subpath_id};
    try {
        data.subpath.nodes.reserve(node_count);
        data.subpath.segments.reserve(segment_count);
        for (uint32_t index = 0; index < node_count; ++index) {
            document::PathNode node;
            if (!reader.node(node) || !node.id.valid() || !valid_recovery_node(node))
                return fail("Pen recovery contains an invalid path node");
            data.subpath.nodes.push_back(std::move(node));
        }
        for (uint32_t index = 0; index < segment_count; ++index) {
            document::EditableSegment segment;
            if (!reader.segment(segment) || !valid_recovery_segment(segment))
                return fail("Pen recovery contains an invalid path segment");
            data.subpath.segments.push_back(std::move(segment));
        }

        uint32_t undo_count = 0;
        uint32_t redo_count = 0;
        if (!reader.u32(undo_count) || undo_count > limits.max_history_entries ||
            undo_count > limits.max_history_payload_bytes /
                sizeof(PenRecoveryHistoryEntry))
            return fail("Pen recovery undo history exceeds its configured quota");
        data.undo.reserve(undo_count);
        for (uint32_t index = 0; index < undo_count; ++index) {
            PenRecoveryHistoryEntry entry;
            if (!reader.history_entry(entry) || !valid_recovery_history_entry(entry))
                return fail("Pen recovery contains an invalid undo entry");
            data.undo.push_back(std::move(entry));
        }
        if (!reader.u32(redo_count) || redo_count > limits.max_history_entries ||
            undo_count + static_cast<size_t>(redo_count) > limits.max_history_entries ||
            undo_count + static_cast<size_t>(redo_count) >
                limits.max_history_payload_bytes / sizeof(PenRecoveryHistoryEntry))
            return fail("Pen recovery redo history exceeds its configured quota");
        data.redo.reserve(redo_count);
        for (uint32_t index = 0; index < redo_count; ++index) {
            PenRecoveryHistoryEntry entry;
            if (!reader.history_entry(entry) || !valid_recovery_history_entry(entry))
                return fail("Pen recovery contains an invalid redo entry");
            data.redo.push_back(std::move(entry));
        }
    } catch (const std::exception&) {
        return fail("Pen recovery payload could not be allocated within its quota");
    }
    if (!reader.finished()) return fail("Pen recovery payload has trailing data");

    document::EditablePath path;
    path.subpaths.push_back(std::move(data.subpath));
    const auto path_error = path.validation_error();
    data.subpath = std::move(path.subpaths.front());
    if (path_error) return fail("Pen recovery path failed topology validation");
    if (data.subpath.nodes.size() != data.undo.size() +
            (data.resume_target ? 1u : 0u))
        return fail("Pen recovery history does not match the confirmed path");
    return data;
}

} // namespace pikdraw::vector::tools::pen::detail

namespace pikdraw::vector::tools::pen {

PenDrawingResult PenDrawingTool::write_recovery_checkpoint(
    const std::string& path, uint32_t base_document_crc32,
    const document::SaveOptions& options) {
    using interaction::ToolSessionState;
    if (!active() || !base_snapshot_ || subpath_.nodes.empty() || candidate_ ||
        numeric_constraints_ || numeric_conflicted_ ||
        (session_.state() != ToolSessionState::AwaitFirstPoint &&
         session_.state() != ToolSessionState::PreviewSegment))
        return {PenDrawingResultCode::InvalidInput,
                "Only a confirmed, idle Pen draft can be checkpointed", {}, {}};

    if (undo_.size() + redo_.size() > limits_.max_history_entries ||
        undo_.size() + redo_.size() >
            limits_.max_history_payload_bytes / sizeof(AnchorEdit))
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen checkpoint history exceeds its configured quota", {}, {}};
    const size_t node_bytes = subpath_.nodes.size() * sizeof(document::PathNode);
    const size_t segment_bytes = subpath_.segments.size() *
        sizeof(document::EditableSegment);
    if (node_bytes > limits_.max_draft_payload_bytes ||
        segment_bytes > limits_.max_draft_payload_bytes - node_bytes)
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen checkpoint draft exceeds its configured quota", {}, {}};
    const size_t history_bytes = (undo_.size() + redo_.size()) * sizeof(AnchorEdit);
    const size_t draft_bytes = node_bytes + segment_bytes;
    if (draft_bytes > limits_.max_session_payload_bytes ||
        history_bytes > limits_.max_session_payload_bytes - draft_bytes)
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen checkpoint exceeds the configured session quota", {}, {}};

    if (subpath_.closed)
        return {PenDrawingResultCode::InvalidInput,
                "Pen checkpoint path failed topology validation", {}, {}};
    document::EditablePath validation_path;
    try {
        validation_path.subpaths.reserve(1);
        validation_path.subpaths.push_back(std::move(subpath_));
        const auto path_error = validation_path.validation_error();
        subpath_ = std::move(validation_path.subpaths.front());
        if (path_error)
            return {PenDrawingResultCode::InvalidInput,
                    "Pen checkpoint path failed topology validation", {}, {}};
    } catch (const std::exception& error) {
        if (!validation_path.subpaths.empty())
            subpath_ = std::move(validation_path.subpaths.front());
        return {PenDrawingResultCode::BudgetExceeded, error.what(), {}, {}};
    }

    try {
        std::optional<detail::PenRecoveryResumeTarget> resume;
        if (resume_target_) {
            resume = detail::PenRecoveryResumeTarget{
                resume_target_->object_id, resume_target_->subpath_id,
                resume_target_->endpoint_id};
        }
        auto payload = detail::encode_pen_draft_checkpoint(
            target_layer_, resume, subpath_, undo_, redo_,
            limits_.max_checkpoint_bytes);
        document::RecoveryDraftRecord record{
            base_snapshot_->document_id(), base_snapshot_->epoch(),
            base_snapshot_->revision(), base_document_crc32,
            document::kRecoveryDraftSchemaVersion, std::move(payload)};
        const auto saved = document::SaveRecoveryService::atomic_write_recovery_draft(
            path, record, options);
        if (!saved.success)
            return {PenDrawingResultCode::CommandFailed,
                    saved.error_message, {}, {}};
    } catch (const std::length_error& error) {
        return {PenDrawingResultCode::BudgetExceeded, error.what(), {}, {}};
    } catch (const std::exception& error) {
        return {PenDrawingResultCode::CommandFailed, error.what(), {}, {}};
    }
    return {PenDrawingResultCode::Applied,
            "Pen recovery checkpoint was written with its base provenance", {}, {}};
}

PenDrawingResult PenDrawingTool::stage_recovery_checkpoint(
    const std::string& path, document::SnapshotPtr current,
    uint32_t expected_base_crc32, interaction::ToolSessionLimits limits) {
    if (active() || recovery_pending())
        return {PenDrawingResultCode::InvalidInput,
                "Resolve the current Pen session before staging recovery", {}, {}};
    if (!current || current->document_id().empty() || current->epoch() == 0 ||
        limits.max_draft_payload_bytes == 0 || limits.max_history_entries == 0 ||
        limits.max_session_payload_bytes == 0 ||
        limits.max_draft_payload_bytes > limits.max_session_payload_bytes ||
        limits.max_history_payload_bytes > limits.max_session_payload_bytes ||
        limits.max_checkpoint_bytes > document::kRecoveryDraftPayloadLimit ||
        limits.max_checkpoint_bytes < 17)
        return {PenDrawingResultCode::InvalidInput,
                "Pen recovery requires a valid document and bounded limits", {}, {}};

    std::string error;
    auto record = document::SaveRecoveryService::load_recovery_draft(path, &error);
    if (!record)
        return {PenDrawingResultCode::InvalidInput,
                error.empty() ? "Could not load the Pen recovery checkpoint" : error,
                {}, {}};
    if (record->document_epoch == 0 || record->document_id != current->document_id() ||
        record->base_revision != current->revision() ||
        record->base_document_crc32 != expected_base_crc32)
        return {PenDrawingResultCode::Stale,
                "Pen recovery checkpoint does not match this document revision or checksum",
                {}, {}};
    if (record->schema_version != document::kRecoveryDraftSchemaVersion ||
        record->payload.size() > limits.max_checkpoint_bytes)
        return {PenDrawingResultCode::InvalidInput,
                "Pen recovery schema or checkpoint quota is unsupported", {}, {}};

    auto decoded = detail::decode_pen_draft_checkpoint(record->payload, limits, &error);
    if (!decoded)
        return {PenDrawingResultCode::InvalidInput,
                error.empty() ? "Pen recovery payload is invalid" : error, {}, {}};
    const size_t history_count = decoded->undo.size() + decoded->redo.size();
    if (history_count > limits.max_history_entries ||
        history_count > limits.max_history_payload_bytes / sizeof(AnchorEdit))
        return {PenDrawingResultCode::BudgetExceeded,
                "Recovered Pen history exceeds its configured quota", {}, {}};
    const size_t node_bytes = decoded->subpath.nodes.size() *
        sizeof(document::PathNode);
    const size_t segment_bytes = decoded->subpath.segments.size() *
        sizeof(document::EditableSegment);
    if (node_bytes > limits.max_draft_payload_bytes ||
        segment_bytes > limits.max_draft_payload_bytes - node_bytes)
        return {PenDrawingResultCode::BudgetExceeded,
                "Recovered Pen draft exceeds its configured quota", {}, {}};
    const size_t draft_bytes = node_bytes + segment_bytes;
    const size_t history_bytes = history_count * sizeof(AnchorEdit);
    if (draft_bytes > limits.max_session_payload_bytes ||
        history_bytes > limits.max_session_payload_bytes - draft_bytes)
        return {PenDrawingResultCode::BudgetExceeded,
                "Recovered Pen draft exceeds its session quota", {}, {}};

    if (decoded->resume_target) {
        std::string resume_error;
        const auto seed = detail::resolve_pen_resume_seed(
            current, decoded->resume_target->object_id,
            decoded->resume_target->subpath_id,
            decoded->resume_target->endpoint_id, resume_error);
        if (!seed || seed->layer_index != decoded->target_layer ||
            decoded->subpath.nodes.front().id != decoded->resume_target->endpoint_id ||
            decoded->subpath.nodes.front().anchor != seed->endpoint.anchor)
            return {PenDrawingResultCode::Stale,
                    resume_error.empty()
                        ? "Pen recovery resume endpoint no longer matches its base path"
                        : std::move(resume_error), {}, {}};
    } else if (decoded->target_layer >= current->page().layers.size() ||
               current->page().layers[decoded->target_layer].locked) {
        return {PenDrawingResultCode::InvalidInput,
                "Recovered Pen target layer is missing or locked", {}, {}};
    }

    pending_recovery_ = PendingRecovery{
        current->document_id(), current->revision(), expected_base_crc32,
        std::move(record->payload), limits};
    return {PenDrawingResultCode::Applied,
            "Pen recovery is staged and awaits explicit acceptance", {}, {}};
}

PenDrawingResult PenDrawingTool::accept_recovery_checkpoint(
    document::SnapshotPtr current, uint32_t expected_base_crc32) {
    if (!pending_recovery_)
        return {PenDrawingResultCode::InvalidInput,
                "No Pen recovery checkpoint is awaiting acceptance", {}, {}};

    PendingRecovery pending = std::move(*pending_recovery_);
    pending_recovery_.reset();
    if (!current || current->document_id() != pending.document_id ||
        current->revision() != pending.base_revision ||
        expected_base_crc32 != pending.base_document_crc32) {
        return {PenDrawingResultCode::Stale,
                "The document changed after Pen recovery was staged", {}, {}};
    }

    std::string error;
    auto decoded = detail::decode_pen_draft_checkpoint(
        pending.payload, pending.limits, &error);
    if (!decoded) {
        pending_recovery_ = std::move(pending);
        return {PenDrawingResultCode::InvalidInput,
                error.empty() ? "Pen recovery payload is invalid" : std::move(error),
                {}, {}};
    }

    auto failed_restore = [&](PenDrawingResult result) {
        if (active()) session_.cancel();
        base_snapshot_.reset();
        clear_draft();
        pending_recovery_ = std::move(pending);
        return result;
    };

    PenDrawingResult started;
    if (decoded->resume_target) {
        started = begin_resume(current, decoded->resume_target->object_id,
                               decoded->resume_target->subpath_id,
                               decoded->resume_target->endpoint_id,
                               pending.limits);
    } else {
        started = begin(current, decoded->target_layer, pending.limits);
    }
    if (started.code != PenDrawingResultCode::Applied)
        return failed_restore(std::move(started));

    try {
        subpath_ = std::move(decoded->subpath);
        const auto restore_history = [](auto& source, auto& destination) {
            destination.reserve(source.size());
            for (auto& entry : source) {
                destination.push_back({std::move(entry.anchor),
                                       std::move(entry.incoming_segment),
                                       std::move(entry.node)});
            }
        };
        restore_history(decoded->undo, undo_);
        restore_history(decoded->redo, redo_);
    } catch (const std::exception& error) {
        return failed_restore({PenDrawingResultCode::BudgetExceeded,
                               error.what(), {}, {}});
    }

    document::reserve_path_element_id(subpath_.id);
    for (const auto& node : subpath_.nodes)
        document::reserve_path_element_id(node.id);
    for (const auto& segment : subpath_.segments)
        document::reserve_path_element_id(segment.id);
    const auto reserve_history = [](const auto& history) {
        for (const auto& edit : history) {
            document::reserve_path_element_id(edit.node.id);
            if (edit.incoming_segment)
                document::reserve_path_element_id(edit.incoming_segment->id);
        }
    };
    reserve_history(undo_);
    reserve_history(redo_);
    candidate_.reset();
    numeric_constraints_.reset();
    numeric_frame_.reset();
    numeric_pointer_world_.reset();
    numeric_conflicted_ = false;
    return {PenDrawingResultCode::Applied,
            "Pen recovery was accepted; the document remains unchanged", {}, {}};
}

void PenDrawingTool::discard_recovery_checkpoint() noexcept {
    pending_recovery_.reset();
}

} // namespace pikdraw::vector::tools::pen
