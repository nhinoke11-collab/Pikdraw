#include "pikdraw/interop/payload_validator.hpp"
#include <unordered_set>

namespace pikdraw::interop {

ValidationDiagnostic PayloadValidator::ValidateHeader(const IpcHeader& header) {
    if (header.magic != IPC_MAGIC) {
        return {ValidationResult::MalformedData, "Invalid IPC magic header"};
    }
    if (header.wire_version != IPC_WIRE_VERSION) {
        return {ValidationResult::VersionMismatch, "Wire version mismatch: expected 1"};
    }
    if (header.payload_bytes > IPC_MAX_PAYLOAD_BYTES) {
        return {ValidationResult::PayloadOversized, "Payload exceeds 16 MiB boundary limit"};
    }
    return {ValidationResult::Valid, "OK"};
}

ValidationDiagnostic PayloadValidator::ValidateJobEnvelope(
    const JobEnvelope& proposal,
    const JobEnvelope& host_current_state,
    bool is_job_cancelled)
{
    if (is_job_cancelled) {
        return {ValidationResult::CancelledJob, "Worker proposal rejected: task scope cancelled"};
    }
    if (proposal.document_id != host_current_state.document_id) {
        return {ValidationResult::StaleEpoch, "Worker proposal rejected: document ID mismatch"};
    }
    if (proposal.epoch != host_current_state.epoch) {
        return {ValidationResult::StaleEpoch, "Worker proposal rejected: document epoch changed (closed/reopened)"};
    }
    if (proposal.generation != host_current_state.generation) {
        return {ValidationResult::StaleGeneration, "Worker proposal rejected: job generation superseded"};
    }
    // Strict monotonic check: worker proposal base_revision must equal expected revision
    if (proposal.base_revision != host_current_state.base_revision) {
        return {ValidationResult::StaleRevision, "Worker proposal rejected: base revision is stale (undo/newer edit committed)"};
    }
    return {ValidationResult::Valid, "OK"};
}

ValidationDiagnostic PayloadValidator::ValidateGraphicPayload(
    const std::vector<VectorGraphicObject>& objects)
{
    if (objects.size() > IPC_MAX_OBJECTS) {
        return {ValidationResult::ObjectLimitExceeded, "Object count exceeds maximum limit (100,000)"};
    }

    std::unordered_set<std::string> ids;
    size_t total_segments = 0;
    for (const auto& obj : objects) {
        if (obj.depth > IPC_MAX_DEPTH) {
            return {ValidationResult::DepthLimitExceeded, "Tree nesting depth exceeds limit (64)"};
        }
        if (!obj.IsFinite()) {
            return {ValidationResult::NonFiniteCoordinate, "Payload contains non-finite scalar (NaN or Infinity)"};
        }
        if (obj.id.empty() || !ids.insert(obj.id).second || obj.stroke_width_mm < 0)
            return {ValidationResult::MalformedData, "Empty/duplicate ID or negative stroke"};
        if (obj.segments.size() > IPC_MAX_PAYLOAD_BYTES / 49 - total_segments)
            return {ValidationResult::PayloadOversized, "Segment memory budget exceeded"};
        total_segments += obj.segments.size();
        for (const auto& seg : obj.segments)
            if (static_cast<uint8_t>(seg.type) > static_cast<uint8_t>(PathSegment::Type::Close))
                return {ValidationResult::MalformedData, "Unknown segment type"};
    }
    return {ValidationResult::Valid, "OK"};
}

std::string_view PayloadValidator::ToString(ValidationResult result) {
    switch (result) {
        case ValidationResult::Valid: return "Valid";
        case ValidationResult::VersionMismatch: return "VersionMismatch";
        case ValidationResult::PayloadOversized: return "PayloadOversized";
        case ValidationResult::ObjectLimitExceeded: return "ObjectLimitExceeded";
        case ValidationResult::DepthLimitExceeded: return "DepthLimitExceeded";
        case ValidationResult::NonFiniteCoordinate: return "NonFiniteCoordinate";
        case ValidationResult::StaleEpoch: return "StaleEpoch";
        case ValidationResult::StaleRevision: return "StaleRevision";
        case ValidationResult::StaleGeneration: return "StaleGeneration";
        case ValidationResult::CancelledJob: return "CancelledJob";
        case ValidationResult::MalformedData: return "MalformedData";
        default: return "Unknown";
    }
}

} // namespace pikdraw::interop
