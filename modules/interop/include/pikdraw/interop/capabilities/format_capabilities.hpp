#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::interop {

enum class FormatType {
    Svg,
    Pdf,
    Dxf,
    Png,
    Jpeg,
    Tiff,
    Cdr,
    Ai,
    Eps
};

enum class FidelitySeverity {
    Info,
    Warning,
    Unsupported
};

struct FidelityIssue {
    FidelitySeverity severity{FidelitySeverity::Warning};
    std::string element_name;
    std::string feature;
    std::string description;
};

/// Tracks fidelity issues and unsupported features during import and export
class FidelityLossReport {
public:
    FidelityLossReport() = default;

    void add_issue(FidelitySeverity severity, std::string element, std::string feature, std::string desc) {
        issues_.push_back(FidelityIssue{severity, std::move(element), std::move(feature), std::move(desc)});
    }

    [[nodiscard]] bool has_unsupported() const noexcept {
        for (const auto& issue : issues_) {
            if (issue.severity == FidelitySeverity::Unsupported) return true;
        }
        return false;
    }

    [[nodiscard]] size_t issue_count() const noexcept { return issues_.size(); }
    [[nodiscard]] const std::vector<FidelityIssue>& issues() const noexcept { return issues_; }

    [[nodiscard]] bool is_clean() const noexcept { return issues_.empty(); }

private:
    std::vector<FidelityIssue> issues_;
};

class FormatCapabilities {
public:
    [[nodiscard]] static bool is_import_supported(FormatType type) noexcept {
        switch (type) {
            case FormatType::Svg:
            case FormatType::Dxf:
            case FormatType::Png:
            case FormatType::Jpeg:
            case FormatType::Tiff:
            case FormatType::Pdf:
            case FormatType::Ai:
            case FormatType::Eps:
            case FormatType::Cdr:
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool is_worker_import_profile_supported(
        FormatType type, std::string_view profile) noexcept {
        switch (type) {
            case FormatType::Svg:
                return profile == "svg-hierarchy-v3";
            case FormatType::Dxf:
                return profile == "dxf-entities-v1";
            case FormatType::Pdf:
                return profile == "pdf-page-vector-v2" ||
                       profile == "pdf-page-set-v3";
            case FormatType::Ai:
                return profile == "ai-pdf-compatible-vector-v2" ||
                       profile == "ai-pdf-compatible-page-set-v3";
            case FormatType::Eps:
                return profile == "eps-path-v1";
            case FormatType::Cdr:
                return profile == "cdr-libcdr-svg-v2";
            case FormatType::Png:
            case FormatType::Jpeg:
            case FormatType::Tiff:
                return profile == "raster-rgba8-v1";
        }
        return false;
    }

    [[nodiscard]] static bool is_export_supported(FormatType type) noexcept {
        switch (type) {
            case FormatType::Svg:
                return true;
            case FormatType::Pdf:
            case FormatType::Dxf:
            case FormatType::Png:
            case FormatType::Jpeg:
            case FormatType::Tiff:
            case FormatType::Cdr:
            case FormatType::Ai:
            case FormatType::Eps:
                return false;
        }
    }
};

} // namespace pikdraw::interop
