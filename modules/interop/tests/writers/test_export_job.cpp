#include "pikdraw/interop/export/export_job.hpp"

#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::interop;

namespace {

DocumentPage page_with_objects(const char* name, std::vector<ObjectId> ids) {
    auto page = make_document_page(name, Page::make_default());
    for (const auto id : ids) {
        PathObject path;
        path.id = id;
        path.name = name;
        path.path = geometry::Path2D::make_rect(10.0, 20.0, 30.0, 40.0);
        page.objects = page.objects.set(id, path);
        page.layout.layers.front().object_ids.push_back(id);
    }
    return page;
}

SnapshotPtr make_snapshot() {
    auto first = page_with_objects("Page 1", {101, 102});
    auto second = page_with_objects("Page 2", {201});
    const auto active_id = first.id;
    std::vector<DocumentPage> pages;
    pages.push_back(std::move(first));
    pages.push_back(std::move(second));
    return std::make_shared<const DocumentSnapshot>(4, 17, std::move(pages), active_id,
                                                    "export-test-document");
}

ExportJobRequest request_for(const char* job_id) {
    ExportJobRequest request;
    request.job_id = job_id;
    request.adapter_id = "svg-static-v1";
    request.output_version = "1.1";
    request.profile_id = "design";
    request.output_paths = {"C:/exports/output.svg"};
    ExportOptionsSchema schema;
    schema.adapter_id = request.adapter_id;
    schema.profile_id = request.profile_id;
    const auto options = build_export_options(schema, {});
    assert(options);
    request.options = options.snapshot;
    return request;
}

void test_active_page_scope_captures_exact_revision() {
    const auto snapshot = make_snapshot();
    auto request = request_for("active-page");
    const auto built = build_export_job(snapshot, std::move(request));
    assert(built && built.error == ExportJobError::None);
    assert(built.job->snapshot() == snapshot);
    assert(built.job->options() != nullptr);
    assert(built.job->options_schema_version() == 1);
    assert(built.job->options()->adapter_id() == built.job->adapter_id());
    assert(built.job->document_id() == "export-test-document");
    assert(built.job->snapshot_revision() == 17);
    assert(built.job->pages().size() == 1);
    assert(built.job->pages().front().page_id == snapshot->active_page_id());
    assert((built.job->pages().front().object_ids == std::vector<ObjectId>{101, 102}));
    assert(built.job->output_targets().size() == 1);
    assert((built.job->output_targets().front().page_ids ==
            std::vector<PageId>{snapshot->active_page_id()}));
    assert((built.job->output_targets().front().object_ids ==
            std::vector<ObjectId>{101, 102}));
}

void test_artboard_scope_captures_id_bounds_and_shared_page_objects() {
    const auto snapshot = make_snapshot();
    const auto& board = snapshot->active_page().artboards.front();
    auto request = request_for("active-artboard");
    request.scope = ExportScope::Artboard;
    request.bounds_policy = ExportBoundsPolicy::Artboard;
    request.artboard_id = board.id;
    const auto built = build_export_job(snapshot, std::move(request));
    assert(built && built.job->scope() == ExportScope::Artboard);
    assert(built.job->artboard_id() == board.id);
    assert(built.job->pages().size() == 1);
    assert((built.job->pages().front().object_ids == std::vector<ObjectId>{101, 102}));
    assert(built.job->output_targets().size() == 1);
    const auto& target = built.job->output_targets().front();
    assert(target.artboard_id == board.id);
    assert(target.bounds.has_value());
    assert(target.bounds->x == board.x_mm && target.bounds->y == board.y_mm);
    assert(target.bounds->width == board.width_mm && target.bounds->height == board.height_mm);

    auto missing = request_for("missing-artboard");
    missing.scope = ExportScope::Artboard;
    missing.bounds_policy = ExportBoundsPolicy::Artboard;
    missing.artboard_id = std::numeric_limits<ArtboardId>::max();
    const auto rejected = build_export_job(snapshot, std::move(missing));
    assert(!rejected && rejected.error == ExportJobError::InvalidArtboard);
}

void test_selection_scope_preserves_selection_order_and_batch_targets() {
    const auto snapshot = make_snapshot();
    auto request = request_for("selection");
    request.scope = ExportScope::Selection;
    request.selected_object_ids = {102, 101};
    request.target_mode = ExportTargetMode::PerAssetBatch;
    request.output_paths = {"C:/exports/second.svg", "C:/exports/first.svg"};
    request.bounds_policy = ExportBoundsPolicy::Selection;
    const auto built = build_export_job(snapshot, std::move(request));
    assert(built);
    assert(built.job->pages().size() == 1);
    assert((built.job->pages().front().object_ids == std::vector<ObjectId>{102, 101}));
    assert(built.job->target_mode() == ExportTargetMode::PerAssetBatch);
    assert(built.job->output_targets().size() == 2);
    assert((built.job->output_targets()[0].object_ids == std::vector<ObjectId>{102}));
    assert((built.job->output_targets()[1].object_ids == std::vector<ObjectId>{101}));
}

void test_ordered_page_ranges_and_all_artwork() {
    const auto snapshot = make_snapshot();
    auto range = request_for("range");
    range.scope = ExportScope::PageRange;
    range.page_ids = {snapshot->pages()[1].id, snapshot->pages()[0].id};
    range.target_mode = ExportTargetMode::PerPageBatch;
    range.output_paths = {"C:/exports/page-2.svg", "C:/exports/page-1.svg"};
    const auto range_built = build_export_job(snapshot, std::move(range));
    assert(range_built && range_built.job->pages().size() == 2);
    assert(range_built.job->pages()[0].page_id == snapshot->pages()[1].id);
    assert(range_built.job->pages()[1].page_id == snapshot->pages()[0].id);
    assert(range_built.job->output_targets()[0].path == "C:/exports/page-2.svg");
    assert((range_built.job->output_targets()[0].page_ids ==
            std::vector<PageId>{snapshot->pages()[1].id}));

    auto all_artwork = request_for("all-artwork");
    all_artwork.scope = ExportScope::AllArtwork;
    all_artwork.target_mode = ExportTargetMode::PerAssetBatch;
    all_artwork.output_paths = {"C:/exports/a.svg", "C:/exports/b.svg", "C:/exports/c.svg"};
    all_artwork.clip_to_page = false;
    const auto all_built = build_export_job(snapshot, std::move(all_artwork));
    assert(all_built && all_built.job->pages().size() == 2);
    assert(all_built.job->pages()[0].object_ids.size() == 2);
    assert(all_built.job->pages()[1].object_ids.size() == 1);
    assert(!all_built.job->clip_to_page());
    assert(all_built.job->output_targets().size() == 3);
    assert(all_built.job->output_targets()[2].path == "C:/exports/c.svg");
    assert((all_built.job->output_targets()[2].page_ids ==
            std::vector<PageId>{snapshot->pages()[1].id}));
    assert((all_built.job->output_targets()[2].object_ids ==
            std::vector<ObjectId>{201}));
}

void test_rejects_invalid_scopes_bounds_and_target_collisions() {
    const auto snapshot = make_snapshot();

    auto empty_selection = request_for("empty-selection");
    empty_selection.scope = ExportScope::Selection;
    auto rejected = build_export_job(snapshot, std::move(empty_selection));
    assert(!rejected && rejected.error == ExportJobError::InvalidSelection);

    auto bad_page = request_for("bad-page");
    bad_page.scope = ExportScope::PageRange;
    bad_page.page_ids = {9999};
    bad_page.target_mode = ExportTargetMode::PerPageBatch;
    rejected = build_export_job(snapshot, std::move(bad_page));
    assert(!rejected && rejected.error == ExportJobError::InvalidPage);

    auto bad_bounds = request_for("bad-bounds");
    bad_bounds.bounds_policy = ExportBoundsPolicy::ExplicitRectangle;
    bad_bounds.explicit_bounds = geometry::Rect2D{0, 0, 0, 100};
    rejected = build_export_job(snapshot, std::move(bad_bounds));
    assert(!rejected && rejected.error == ExportJobError::InvalidBounds);

    auto collision = request_for("collision");
    collision.target_mode = ExportTargetMode::PerPageBatch;
    collision.output_paths = {"C:/exports/./file.svg", "c:\\exports\\FILE.svg"};
    rejected = build_export_job(snapshot, std::move(collision));
    assert(!rejected && rejected.error == ExportJobError::InvalidOutputTargets);

    auto wrong_options = request_for("wrong-options");
    ExportOptionsSchema other_schema;
    other_schema.adapter_id = "another-adapter";
    other_schema.profile_id = "design";
    const auto other_options = build_export_options(other_schema, {});
    assert(other_options);
    wrong_options.options = other_options.snapshot;
    rejected = build_export_job(snapshot, std::move(wrong_options));
    assert(!rejected && rejected.error == ExportJobError::InvalidOptionsSchema);

    auto wrong_profile = request_for("wrong-profile-options");
    other_schema.adapter_id = wrong_profile.adapter_id;
    other_schema.profile_id = "print";
    const auto print_options = build_export_options(other_schema, {});
    assert(print_options);
    wrong_profile.options = print_options.snapshot;
    rejected = build_export_job(snapshot, std::move(wrong_profile));
    assert(!rejected && rejected.error == ExportJobError::InvalidOptionsSchema);
}

} // namespace

int main() {
    test_active_page_scope_captures_exact_revision();
    test_artboard_scope_captures_id_bounds_and_shared_page_objects();
    test_selection_scope_preserves_selection_order_and_batch_targets();
    test_ordered_page_ranges_and_all_artwork();
    test_rejects_invalid_scopes_bounds_and_target_collisions();
    std::cout << "All export job tests passed.\n";
    return 0;
}
