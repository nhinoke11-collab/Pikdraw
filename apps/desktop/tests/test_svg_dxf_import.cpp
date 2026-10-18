#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::desktop;

namespace {
constexpr char kSvgFixture[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="30mm" height="20mm" viewBox="0 0 30 20">
  <g id="art-layer"><rect id="panel" x="2" y="3" width="12" height="8" fill="#2575fc"/></g>
</svg>)SVG";
constexpr char kDxfFixture[] = R"DXF(0
SECTION
2
HEADER
9
$INSUNITS
70
4
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
2
0
LAYER
2
CUT
70
0
62
1
6
CONTINUOUS
0
LAYER
2
ENGRAVE
70
0
62
5
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
LINE
5
10
8
CUT
10
0
20
0
30
0
11
20
21
0
31
0
0
LWPOLYLINE
5
11
8
CUT
90
3
70
1
10
20
20
0
42
0.414213562373095
10
30
20
10
10
20
20
20
0
ARC
5
12
8
ENGRAVE
10
50
20
50
30
0
40
10
50
0
51
90
0
CIRCLE
5
13
8
ENGRAVE
10
80
20
50
30
0
40
5
0
ENDSEC
0
EOF
)DXF";

void test_isolated_import_and_atomic_document_commit() {
    const auto source_path = std::filesystem::temp_directory_path() /
                             L"pikdraw-desktop-import-fixture.svg";
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kSvgFixture, sizeof(kSvgFixture) - 1));
    }

    CompositionRoot root;
    root.initialize(900.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    const auto initial_page = root.document().current_snapshot()->page();
    const auto metadata = workflow.begin_import_job(1);
    const auto imported = run_import_worker_job(source_path.wstring(), ImportFormat::Svg,
                                                metadata, std::stop_token{});
    std::error_code ignored;
    std::filesystem::remove(source_path, ignored);
    assert(imported.success && !imported.cancelled);
    assert(imported.proposal.profile_id == "svg-hierarchy-v2");
    assert(imported.proposal.source_page_width_mm == 30.0);
    assert(imported.proposal.source_page_height_mm == 20.0);
    assert(imported.proposal.layers.size() == 1 && !imported.proposal.objects.empty());

    const size_t imported_count = imported.proposal.objects.size();
    const auto commit = workflow.commit_import_proposal(metadata, imported.proposal);
    assert(commit == document::ProposalStatus::Accepted);
    auto snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == imported_count);
    assert(snapshot->page().layers.size() == initial_page.layers.size() + 1);
    assert(snapshot->page().width_mm == initial_page.width_mm);
    assert(snapshot->page().height_mm == initial_page.height_mm);
    assert(root.document().undo());
    snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == 0);
    assert(snapshot->page().layers.size() == initial_page.layers.size());
    assert(root.document().redo());
    assert(root.document().current_snapshot()->object_count() == imported_count);

    const auto stale_metadata = workflow.begin_import_job(2);
    document::PathObject intervening;
    intervening.id = document::generate_object_id();
    intervening.path.move_to(0, 0);
    intervening.path.line_to(2, 2);
    assert(root.document().execute_command(
        std::make_unique<document::AddObjectCommand>(intervening)).success);
    const auto stale_commit = workflow.commit_import_proposal(stale_metadata, imported.proposal);
    assert(stale_commit == document::ProposalStatus::StaleRevision);
    assert(root.document().current_snapshot()->object_count() == imported_count + 1);
}

void test_dxf_profile_and_source_signature_rejection() {
    const auto dxf_path = std::filesystem::temp_directory_path() / L"pikdraw-desktop-import-fixture.dxf";
    {
        std::ofstream source(dxf_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kDxfFixture, sizeof(kDxfFixture) - 1));
    }
    CompositionRoot root;
    root.initialize(800.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    const auto metadata = workflow.begin_import_job(1);
    const auto imported = run_import_worker_job(dxf_path.wstring(), ImportFormat::Dxf,
                                                metadata, std::stop_token{});
    std::error_code ignored;
    std::filesystem::remove(dxf_path, ignored);
    if (!imported.success) std::cerr << "DXF import failed: " << imported.error_message << '\n';
    assert(imported.success && imported.proposal.profile_id == "dxf-entities-v1");
    assert(imported.proposal.layers.size() == 2);
    assert(imported.proposal.layers.front().name == "CUT");
    assert(imported.proposal.source_page_width_mm > 0.0);
    assert(imported.proposal.source_page_height_mm > 0.0);
    assert(workflow.commit_import_proposal(metadata, imported.proposal) ==
           document::ProposalStatus::Accepted);
    const auto snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == 4);
    assert(snapshot->page().layers[snapshot->page().layers.size() - 2].name == "CUT");
    assert(snapshot->page().layers.back().name == "ENGRAVE");
    assert(root.document().undo() && root.document().current_snapshot()->object_count() == 0);

    const auto mismatch_path = std::filesystem::temp_directory_path() / L"pikdraw-import-signature-mismatch.dxf";
    {
        std::ofstream source(mismatch_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kSvgFixture, sizeof(kSvgFixture) - 1));
    }
    const auto mismatch_metadata = workflow.begin_import_job(2);
    const auto mismatch = run_import_worker_job(mismatch_path.wstring(), ImportFormat::Dxf,
                                                mismatch_metadata, std::stop_token{});
    std::filesystem::remove(mismatch_path, ignored);
    assert(!mismatch.success && !mismatch.cancelled);
    assert(mismatch.error_message.find("signature") != std::string::npos);
    assert(root.document().current_snapshot()->object_count() == 0);
    workflow.cancel_import_job(mismatch_metadata.job_id);

    const auto cancel_metadata = workflow.begin_import_job(3);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = run_import_worker_job(mismatch_path.wstring(), ImportFormat::Dxf,
                                                 cancel_metadata, cancellation.get_token());
    assert(cancelled.cancelled && !cancelled.success);
    assert(root.document().current_snapshot()->object_count() == 0);
    workflow.cancel_import_job(cancel_metadata.job_id);
}
}

int main() {
    test_isolated_import_and_atomic_document_commit();
    test_dxf_profile_and_source_signature_rejection();
    std::cout << "Desktop isolated SVG/DXF import workflow tests passed\n";
    return 0;
}
