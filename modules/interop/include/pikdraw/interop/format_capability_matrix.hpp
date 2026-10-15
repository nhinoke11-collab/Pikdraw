#pragma once

#include <string>
#include <vector>
#include <string_view>

namespace pikdraw::interop {

enum class FormatCapabilityStatus {
    SupportedPrototype,
    PlannedViaLibrary,
    DeferredResearch,
    UnsupportedReject
};

struct FormatCapabilityEntry {
    std::string format_name;
    std::string extension;
    std::string version_scope;
    FormatCapabilityStatus import_status;
    FormatCapabilityStatus export_status;
    std::string candidate_library;
    std::string candidate_version;
    std::string license;
    std::string containment_requirement;
    std::string notes;
};

struct ThirdPartyProvenance {
    std::string library_name;
    std::string pinned_version;
    std::string upstream_url;
    std::string license_spdx;
    std::string distribution_obligation;
    std::string worker_boundary_role;
    bool dynamic_linking_required;
};

class FormatCapabilityRegistry {
public:
    [[nodiscard]] static std::vector<FormatCapabilityEntry> GetMatrix();
    [[nodiscard]] static std::vector<ThirdPartyProvenance> GetCandidateLibraries();
    [[nodiscard]] static bool IsFormatSupported(std::string_view format_name) noexcept;
    [[nodiscard]] static std::string GetDiagnosticForFormat(std::string_view format_name);
};

} // namespace pikdraw::interop
