#pragma once

#include "ipc_dto.hpp"
#include <string>
#include <string_view>

namespace pikdraw::interop {

enum class ValidationResult : uint8_t {
    Valid = 0,
    VersionMismatch,
    PayloadOversized,
    ObjectLimitExceeded,
    DepthLimitExceeded,
    NonFiniteCoordinate,
    StaleEpoch,
    StaleRevision,
    StaleGeneration,
    CancelledJob,
    MalformedData
};

struct ValidationDiagnostic {
    ValidationResult code{ValidationResult::Valid};
    std::string message;

    [[nodiscard]] bool IsOk() const noexcept {
        return code == ValidationResult::Valid;
    }
};

class PayloadValidator {
public:
    // Validates inbound wire header
    [[nodiscard]] static ValidationDiagnostic ValidateHeader(const IpcHeader& header);

    // Validates job envelope alignment against current host document state
    [[nodiscard]] static ValidationDiagnostic ValidateJobEnvelope(
        const JobEnvelope& proposal,
        const JobEnvelope& host_current_state,
        bool is_job_cancelled);

    // Validates payload objects (depth, count, finite values)
    [[nodiscard]] static ValidationDiagnostic ValidateGraphicPayload(
        const std::vector<VectorGraphicObject>& objects);

    // Converts ValidationResult to human-readable string
    [[nodiscard]] static std::string_view ToString(ValidationResult result);
};

} // namespace pikdraw::interop
