#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pikdraw::document {

enum class ImportReportCoverage : uint8_t { Unknown, Partial, Complete };
enum class ImportReportAppearance : uint8_t { Unknown, Partial, Verified };
enum class ImportReportEditability : uint8_t { Unknown, Editable, ExplicitConversion };
enum class ImportReportProduction : uint8_t { Unknown, Blocked, Assessed };
enum class ImportReportIssueSeverity : uint8_t { Info = 1, Warning = 2, Unsupported = 3 };

struct ImportReportIssue {
    ImportReportIssueSeverity severity{ImportReportIssueSeverity::Warning};
    std::string source_locator;
    std::string feature_code;
    std::string diagnostic_code;
};

/// Host-reviewed source fidelity facts retained with the page that received an import.
/// It deliberately excludes local file paths and untrusted worker verification claims.
struct ImportFidelityReport {
    std::string format_name;
    std::string profile_id;
    uint64_t source_bytes{0};
    double source_page_width_mm{0.0};
    double source_page_height_mm{0.0};
    uint32_t source_page_index{0};
    ImportReportCoverage coverage{ImportReportCoverage::Unknown};
    ImportReportAppearance appearance{ImportReportAppearance::Unknown};
    ImportReportEditability editability{ImportReportEditability::Unknown};
    ImportReportProduction production{ImportReportProduction::Unknown};
    std::vector<ImportReportIssue> issues;

    [[nodiscard]] bool is_valid() const noexcept {
        if (format_name.empty() || format_name.size() > 64 || profile_id.size() > 128 ||
            !std::isfinite(source_page_width_mm) ||
            !std::isfinite(source_page_height_mm) ||
            source_page_width_mm < 0.0 || source_page_height_mm < 0.0 ||
            static_cast<uint8_t>(coverage) > static_cast<uint8_t>(ImportReportCoverage::Complete) ||
            static_cast<uint8_t>(appearance) > static_cast<uint8_t>(ImportReportAppearance::Verified) ||
            static_cast<uint8_t>(editability) > static_cast<uint8_t>(ImportReportEditability::ExplicitConversion) ||
            static_cast<uint8_t>(production) > static_cast<uint8_t>(ImportReportProduction::Assessed) ||
            issues.size() > 4096)
            return false;
        for (const auto& issue : issues) {
            if (static_cast<uint8_t>(issue.severity) < 1 ||
                static_cast<uint8_t>(issue.severity) > 3 ||
                issue.source_locator.size() > 128 || issue.feature_code.empty() ||
                issue.feature_code.size() > 64 || issue.diagnostic_code.size() > 128)
                return false;
        }
        return true;
    }
};

} // namespace pikdraw::document
