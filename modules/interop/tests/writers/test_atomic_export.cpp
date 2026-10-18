#include "pikdraw/interop/export/atomic_export.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::interop;

namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) result.push_back(static_cast<char>(character));
    return result;
}

class TempDirectory final {
public:
    TempDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("pikdraw-atomic-export-test-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

SnapshotPtr make_snapshot(size_t page_count = 1) {
    std::vector<DocumentPage> pages;
    pages.reserve(page_count);
    for (size_t index = 0; index < page_count; ++index)
        pages.push_back(make_document_page("Page " + std::to_string(index + 1),
                                           Page::make_default()));
    const auto active_page = pages.front().id;
    return std::make_shared<const DocumentSnapshot>(1, 23, std::move(pages), active_page,
                                                    "atomic-export-test-document");
}

std::shared_ptr<const ExportJob> make_job(const std::vector<std::filesystem::path>& paths,
                                          ExportScope scope = ExportScope::ActivePage,
                                          ExportTargetMode target_mode = ExportTargetMode::SingleFile) {
    ExportJobRequest request;
    request.job_id = "atomic-export-test-job";
    request.adapter_id = "test-vector-writer";
    request.output_version = "test-v1";
    request.profile_id = "test-profile";
    request.scope = scope;
    request.target_mode = target_mode;
    for (const auto& path : paths) request.output_paths.push_back(path_to_utf8(path));
    ExportOptionsSchema schema;
    schema.adapter_id = request.adapter_id;
    schema.profile_id = request.profile_id;
    const auto options = build_export_options(schema, {});
    assert(options);
    request.options = options.snapshot;
    const auto built = build_export_job(make_snapshot(scope == ExportScope::AllPages ?
                                                       paths.size() : 1), std::move(request));
    assert(built);
    return built.job;
}

std::string read_stream(std::istream& stream);

AtomicExportOptions options_with_text_writer(std::string output = "validated PikDraw output") {
    AtomicExportOptions options;
    options.writer_build = "test-writer/1";
    options.max_output_bytes_per_file = 1024 * 1024;
    options.allowed_output_extensions = {".svg"};
    const auto expected_output = output;
    options.serialize = [output = std::move(output)](
        const ExportOutputTarget&, std::ostream& stream, std::string&) {
        stream.write(output.data(), static_cast<std::streamsize>(output.size()));
        return stream.good();
    };
    options.validate = [expected_output](const ExportOutputTarget&, std::istream& staged,
                                         uint64_t, std::string& diagnostic) {
        const auto contents = read_stream(staged);
        if (staged.bad()) {
            diagnostic = "The test validator could not read the staged file.";
            return false;
        }
        if (contents != expected_output) {
            diagnostic = "The staged bytes do not match the expected test output.";
            return false;
        }
        return true;
    };
    options.publish_if_current = [](uint64_t, const std::function<bool()>& publish) {
        return publish();
    };
    return options;
}

bool has_staging_file(const std::filesystem::path& directory) {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().wstring().starts_with(L".pikdraw-export-")) return true;
    }
    return false;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string read_stream(std::istream& stream) {
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void test_success_commits_validated_bytes_and_receipt() {
    TempDirectory directory;
    const auto target = directory.path() / "single.SVG";
    auto options = options_with_text_writer();
    options.validate = [](const ExportOutputTarget&, std::istream& staged, uint64_t,
                          std::string& diagnostic) {
        const auto contents = read_stream(staged);
        if (contents != "validated PikDraw output") {
            diagnostic = "Unexpected test payload (" + std::to_string(contents.size()) +
                         " bytes): " + contents;
            return false;
        }
        return true;
    };
    const auto job = make_job({target});

    const auto result = execute_atomic_export(*job, options);
    if (result.report.status != ExportJobStatus::Complete) {
        std::cerr << "Atomic export status: " << static_cast<int>(result.report.status) << '\n';
        std::cerr << "Target: " << result.report.files.front().path << '\n';
        for (const auto& diagnostic : result.report.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Complete);
    assert(result.report.files.size() == 1);
    assert(result.report.files.front().status == ExportFileStatus::Committed);
    assert(result.report.files.front().byte_size == std::string("validated PikDraw output").size());
    assert(result.report.files.front().sha256_hex ==
           "fecf96e284c9e5e9f18c6c533cf615aa64f3b4725876165dcf15c5107ea3907a");
    assert(result.report.files.front().snapshot_revision == job->snapshot_revision());
    assert(result.report.files.front().page_ids == job->output_targets().front().page_ids);
    assert(read_text(target) == "validated PikDraw output");
    assert(!has_staging_file(directory.path()));
}

void test_failed_writer_cleans_partial_staging_file() {
    TempDirectory directory;
    const auto target = directory.path() / "failed.svg";
    auto options = options_with_text_writer();
    options.serialize = [](const ExportOutputTarget&, std::ostream& stream, std::string&) {
        stream << "partial content";
        return false;
    };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::WriteFailed);
    assert(result.report.files.front().status == ExportFileStatus::Failed);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_failed_validator_never_publishes_final_name() {
    TempDirectory directory;
    const auto target = directory.path() / "invalid.svg";
    auto options = options_with_text_writer();
    options.validate = [](const ExportOutputTarget&, std::istream&, uint64_t,
                          std::string& diagnostic) {
        diagnostic = "The output signature is invalid.";
        return false;
    };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::WriteFailed);
    assert(result.report.files.front().status == ExportFileStatus::Failed);
    assert(result.report.files.front().sha256_hex.empty());
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_overwrite_requires_confirmation_and_protects_source() {
    TempDirectory directory;
    const auto target = directory.path() / "existing.svg";
    {
        std::ofstream file(target, std::ios::binary);
        file << "original";
    }
    const auto job = make_job({target});
    auto options = options_with_text_writer();

    auto result = execute_atomic_export(*job, options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Rejected);
    assert(read_text(target) == "original");

    options.allow_replace_existing = true;
    options.protected_source_paths = {path_to_utf8(target)};
    result = execute_atomic_export(*job, options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Rejected);
    assert(read_text(target) == "original");

    options.protected_source_paths.clear();
    result = execute_atomic_export(*job, options);
    if (result.report.status != ExportJobStatus::Complete) {
        std::cerr << "Overwrite export status: " << static_cast<int>(result.report.status) << '\n';
        for (const auto& diagnostic : result.report.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Complete);
    assert(read_text(target) == "validated PikDraw output");
}

void test_batch_keeps_committed_files_and_reports_failed_and_unstarted_items() {
    TempDirectory directory;
    const auto first = directory.path() / "page-1.svg";
    const auto second = directory.path() / "page-2.svg";
    const auto third = directory.path() / "page-3.svg";
    const auto job = make_job({first, second, third}, ExportScope::AllPages,
                              ExportTargetMode::PerPageBatch);
    auto options = options_with_text_writer();
    size_t call_count = 0;
    options.validate = [](const ExportOutputTarget&, std::istream& staged, uint64_t,
                          std::string& diagnostic) {
        if (read_stream(staged) != "page output") {
            diagnostic = "Unexpected page payload.";
            return false;
        }
        return true;
    };
    options.serialize = [&call_count](const ExportOutputTarget&, std::ostream& stream,
                                      std::string& diagnostic) {
        ++call_count;
        if (call_count == 2) {
            stream << "partial second page";
            diagnostic = "The second page writer failed.";
            return false;
        }
        stream << "page output";
        return stream.good();
    };

    const auto result = execute_atomic_export(*job, options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::WriteFailed);
    assert(result.report.files.size() == 3);
    assert(result.report.files[0].status == ExportFileStatus::Committed);
    assert(result.report.files[1].status == ExportFileStatus::Failed);
    assert(result.report.files[2].status == ExportFileStatus::Skipped);
    assert(std::filesystem::exists(first));
    assert(!std::filesystem::exists(second));
    assert(!std::filesystem::exists(third));
    assert(!has_staging_file(directory.path()));
}

void test_cancel_before_publish_removes_staged_output() {
    TempDirectory directory;
    const auto target = directory.path() / "cancelled.svg";
    auto options = options_with_text_writer();
    size_t checks = 0;
    options.is_cancelled = [&checks](uint64_t) { return ++checks >= 2; };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Cancelled);
    assert(result.report.files.front().status == ExportFileStatus::Cancelled);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_publish_gate_can_reject_a_stale_generation() {
    TempDirectory directory;
    const auto target = directory.path() / "stale.svg";
    auto options = options_with_text_writer();
    options.publish_if_current = [](uint64_t, const std::function<bool()>&) {
        return false;
    };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Cancelled);
    assert(result.report.files.front().status == ExportFileStatus::Cancelled);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_streaming_cancel_is_observed_between_bounded_writes() {
    TempDirectory directory;
    const auto target = directory.path() / "large-cancelled.svg";
    auto options = options_with_text_writer(std::string(2 * 1024 * 1024, 'z'));
    size_t cancellation_checks = 0;
    uint64_t last_progress_bytes = 0;
    size_t progress_events = 0;
    options.is_cancelled = [&cancellation_checks](uint64_t) {
        return ++cancellation_checks >= 3;
    };
    options.on_progress = [&](size_t target_index, size_t target_count,
                              uint64_t bytes_written, uint64_t byte_limit) {
        assert(target_index == 0 && target_count == 1);
        assert(byte_limit == options.max_output_bytes_per_file);
        last_progress_bytes = bytes_written;
        ++progress_events;
    };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Cancelled);
    assert(result.report.files.front().status == ExportFileStatus::Cancelled);
    assert(last_progress_bytes > 0 && last_progress_bytes < 2 * 1024 * 1024);
    assert(progress_events >= 2);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_per_file_byte_budget_stops_writer_and_cleans_staging() {
    TempDirectory directory;
    const auto target = directory.path() / "oversized.svg";
    auto options = options_with_text_writer(std::string(32, 'x'));
    options.max_output_bytes_per_file = 8;

    const auto result = execute_atomic_export(*make_job({target}), options);
    if (!result.has_valid_report())
        std::cerr << "Invalid resource-limit report: " << result.report_validation.message << '\n';
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::ResourceLimit);
    assert(result.report.files.front().status == ExportFileStatus::Failed);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

void test_extension_mismatch_is_rejected_before_writer_runs() {
    TempDirectory directory;
    const auto target = directory.path() / "misnamed.pdf";
    auto options = options_with_text_writer();
    bool writer_called = false;
    options.serialize = [&writer_called](const ExportOutputTarget&, std::ostream&,
                                         std::string&) {
        writer_called = true;
        return true;
    };

    const auto result = execute_atomic_export(*make_job({target}), options);
    assert(result.has_valid_report());
    assert(result.report.status == ExportJobStatus::Rejected);
    assert(result.report.diagnostics.front().code == "EXPORT_OUTPUT_EXTENSION_MISMATCH");
    assert(!writer_called);
    assert(!std::filesystem::exists(target));
    assert(!has_staging_file(directory.path()));
}

} // namespace

int main() {
    test_success_commits_validated_bytes_and_receipt();
    test_failed_writer_cleans_partial_staging_file();
    test_failed_validator_never_publishes_final_name();
    test_overwrite_requires_confirmation_and_protects_source();
    test_batch_keeps_committed_files_and_reports_failed_and_unstarted_items();
    test_cancel_before_publish_removes_staged_output();
    test_publish_gate_can_reject_a_stale_generation();
    test_streaming_cancel_is_observed_between_bounded_writes();
    test_per_file_byte_budget_stops_writer_and_cleans_staging();
    test_extension_mismatch_is_rejected_before_writer_runs();
    std::cout << "All atomic export tests passed.\n";
    return 0;
}
