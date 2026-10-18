#include "pikdraw/interop/export/export_job.hpp"

#include <filesystem>
#include <unordered_set>

namespace pikdraw::interop {
namespace {

ExportJobBuildResult reject(ExportJobError error, const char* message) {
    return {nullptr, error, message};
}

std::vector<document::ObjectId> page_roots(const document::DocumentPage& page) {
    std::vector<document::ObjectId> roots;
    for (const auto& layer : page.layout.layers)
        roots.insert(roots.end(), layer.object_ids.begin(), layer.object_ids.end());
    return roots;
}

std::string normalized_output_path(std::string path) {
    std::u8string utf8_path;
    utf8_path.reserve(path.size());
    for (const unsigned char value : path)
        utf8_path.push_back(static_cast<char8_t>(value));
    const auto normalized_path = std::filesystem::path(std::move(utf8_path))
                                     .lexically_normal().generic_u8string();
    std::string normalized;
    normalized.reserve(normalized_path.size());
    for (const char8_t value : normalized_path) {
        char character = static_cast<char>(value);
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
        if (character == '\\') character = '/';
        normalized.push_back(character);
    }
    return normalized;
}

bool valid_scope(ExportScope scope) noexcept {
    switch (scope) {
        case ExportScope::Selection:
        case ExportScope::ActivePage:
        case ExportScope::PageRange:
        case ExportScope::AllPages:
        case ExportScope::AllArtwork:
            return true;
    }
    return false;
}

bool valid_bounds_policy(ExportBoundsPolicy policy) noexcept {
    switch (policy) {
        case ExportBoundsPolicy::Page:
        case ExportBoundsPolicy::VisibleArtwork:
        case ExportBoundsPolicy::Selection:
        case ExportBoundsPolicy::ExplicitRectangle:
            return true;
    }
    return false;
}

bool valid_target_mode(ExportTargetMode mode) noexcept {
    switch (mode) {
        case ExportTargetMode::SingleFile:
        case ExportTargetMode::PerPageBatch:
        case ExportTargetMode::PerAssetBatch:
            return true;
    }
    return false;
}

} // namespace

ExportJobBuildResult build_export_job(
    document::SnapshotPtr snapshot, ExportJobRequest request) {
    if (!snapshot)
        return reject(ExportJobError::MissingSnapshot, "An export job requires a document snapshot.");
    if (request.job_id.empty())
        return reject(ExportJobError::InvalidJobIdentity, "The export job ID cannot be empty.");
    if (request.adapter_id.empty() || request.output_version.empty() || request.profile_id.empty())
        return reject(ExportJobError::InvalidAdapterProfile,
                      "An adapter ID, output version and profile are required.");
    if (!request.options || request.options->schema_version() == 0 ||
        request.options->adapter_id() != request.adapter_id ||
        request.options->profile_id() != request.profile_id)
        return reject(ExportJobError::InvalidOptionsSchema,
                      "The request needs validated options for its adapter and output profile.");
    if (!valid_scope(request.scope) || !valid_bounds_policy(request.bounds_policy) ||
        !valid_target_mode(request.target_mode))
        return reject(ExportJobError::InvalidScope, "The export scope or policy is unknown.");
    if (request.bounds_policy == ExportBoundsPolicy::ExplicitRectangle) {
        if (!request.explicit_bounds || !request.explicit_bounds->is_finite() ||
            request.explicit_bounds->is_empty())
            return reject(ExportJobError::InvalidBounds,
                          "Explicit export bounds must be finite and have positive dimensions.");
    } else if (request.explicit_bounds) {
        return reject(ExportJobError::InvalidBounds,
                      "An explicit rectangle is valid only with the explicit-bounds policy.");
    }

    std::vector<const document::DocumentPage*> source_pages;
    switch (request.scope) {
        case ExportScope::Selection:
        case ExportScope::ActivePage:
            if (!request.page_ids.empty())
                return reject(ExportJobError::InvalidScope,
                              "Explicit page IDs are valid only for a page-range export.");
            source_pages.push_back(snapshot->find_page(snapshot->active_page_id()));
            break;
        case ExportScope::PageRange: {
            if (request.page_ids.empty())
                return reject(ExportJobError::InvalidScope,
                              "A page-range export needs at least one selected page.");
            std::unordered_set<document::PageId> seen;
            for (const auto page_id : request.page_ids) {
                const auto* page = snapshot->find_page(page_id);
                if (!page)
                    return reject(ExportJobError::InvalidPage,
                                  "A selected page is not present in the captured snapshot.");
                if (!seen.insert(page_id).second)
                    return reject(ExportJobError::InvalidPage,
                                  "A page cannot appear twice in an export range.");
                source_pages.push_back(page);
            }
            break;
        }
        case ExportScope::AllPages:
        case ExportScope::AllArtwork:
            if (!request.page_ids.empty())
                return reject(ExportJobError::InvalidScope,
                              "Explicit page IDs cannot be combined with all-pages scope.");
            for (const auto& page : snapshot->pages()) source_pages.push_back(&page);
            break;
    }
    if (source_pages.empty())
        return reject(ExportJobError::InvalidPage, "The export scope resolved to no pages.");

    std::vector<ExportJobPage> resolved_pages;
    resolved_pages.reserve(source_pages.size());
    size_t asset_count = 0;
    for (const auto* page : source_pages) {
        ExportJobPage resolved{page->id, {}};
        if (request.scope == ExportScope::Selection) {
            if (request.selected_object_ids.empty())
                return reject(ExportJobError::InvalidSelection,
                              "Selection export requires at least one selected object.");
            std::unordered_set<document::ObjectId> seen;
            for (const auto object_id : request.selected_object_ids) {
                if (object_id == document::kInvalidObjectId ||
                    !page->objects.contains(object_id) || !seen.insert(object_id).second)
                    return reject(ExportJobError::InvalidSelection,
                                  "Selection contains an unknown or repeated object ID.");
                resolved.object_ids.push_back(object_id);
            }
        } else {
            if (!request.selected_object_ids.empty())
                return reject(ExportJobError::InvalidSelection,
                              "Selected object IDs can be used only with selection scope.");
            resolved.object_ids = page_roots(*page);
        }
        asset_count += resolved.object_ids.size();
        resolved_pages.push_back(std::move(resolved));
    }
    if (request.bounds_policy == ExportBoundsPolicy::Selection &&
        request.scope != ExportScope::Selection)
        return reject(ExportJobError::InvalidBounds,
                      "Selection bounds require selection export scope.");

    if (request.output_paths.empty())
        return reject(ExportJobError::InvalidOutputTargets,
                      "At least one host-selected output path is required.");
    size_t expected_outputs = 1;
    switch (request.target_mode) {
        case ExportTargetMode::SingleFile:
            expected_outputs = 1;
            break;
        case ExportTargetMode::PerPageBatch:
            expected_outputs = resolved_pages.size();
            break;
        case ExportTargetMode::PerAssetBatch:
            expected_outputs = asset_count;
            break;
    }
    if (expected_outputs == 0 || request.output_paths.size() != expected_outputs)
        return reject(ExportJobError::InvalidOutputTargets,
                      "Output path count does not match the selected batch mode.");
    std::unordered_set<std::string> unique_paths;
    for (const auto& path : request.output_paths) {
        if (path.empty() || !unique_paths.insert(normalized_output_path(path)).second)
            return reject(ExportJobError::InvalidOutputTargets,
                          "Output paths must be nonempty and unique.");
    }

    std::vector<ExportOutputTarget> output_targets;
    output_targets.reserve(request.output_paths.size());
    size_t output_index = 0;
    if (request.target_mode == ExportTargetMode::SingleFile) {
        ExportOutputTarget target;
        target.path = request.output_paths.front();
        for (const auto& page : resolved_pages) {
            target.page_ids.push_back(page.page_id);
            target.object_ids.insert(target.object_ids.end(),
                                     page.object_ids.begin(), page.object_ids.end());
        }
        output_targets.push_back(std::move(target));
    } else if (request.target_mode == ExportTargetMode::PerPageBatch) {
        for (const auto& page : resolved_pages)
            output_targets.push_back({request.output_paths[output_index++],
                                      {page.page_id}, page.object_ids});
    } else {
        for (const auto& page : resolved_pages)
            for (const auto object_id : page.object_ids)
                output_targets.push_back({request.output_paths[output_index++],
                                          {page.page_id}, {object_id}});
    }

    return {std::shared_ptr<const ExportJob>(new ExportJob(
                std::move(snapshot), std::move(request), std::move(resolved_pages),
                std::move(output_targets))),
            ExportJobError::None, {}};
}

} // namespace pikdraw::interop
