#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::interop;
using namespace pikdraw::document;

void test_svg_export_and_roundtrip() {
    Document doc("doc-svg-export");

    PathObject po;
    po.id = generate_object_id();
    po.name = "Box";
    po.path.move_to(50, 50);
    po.path.line_to(150, 50);
    po.path.line_to(150, 150);
    po.path.close();
    doc.execute_command(std::make_unique<AddObjectCommand>(po));

    TextObject to;
    to.id = generate_object_id();
    to.name = "Vietnamese Text";
    to.text = "PikDraw Vector Editor Việt Nam & <xưởng>";
    to.font.family_name = "PikDrawSans";
    to.font.size_pt = 32.0;
    to.position = {100.0, 300.0};
    doc.execute_command(std::make_unique<AddObjectCommand>(to));

    auto snap = doc.current_snapshot();
    auto export_result = SvgExporter::export_with_report(*snap);
    assert(export_result.success && export_result.report.is_clean());
    std::string svg_out = export_result.svg;
    assert(!svg_out.empty());
    assert(svg_out.find("<svg") != std::string::npos);
    assert(svg_out.find("PikDraw Vector Editor Việt Nam &amp; &lt;xưởng&gt;") !=
           std::string::npos);
    assert(svg_out.find("font-size=\"11.288888888888888\"") != std::string::npos);

    // Re-import exported SVG to prove roundtrip fidelity
    auto import_res = SvgImporter::import_from_string(svg_out);
    assert(import_res.success);
    assert(import_res.objects.size() == 2);

    const auto& re_text = std::get<TextObject>(import_res.objects[1]);
    assert(re_text.text == "PikDraw Vector Editor Việt Nam & <xưởng>");
    assert(re_text.font.family_name == "PikDrawSans");
    assert(std::abs(re_text.font.size_pt - 32.0) < 1e-12);
}

void test_svg_analytic_arc_and_loss_report() {
    Document doc("svg-arc-source");
    PathObject object;
    object.id = generate_object_id();
    object.path.move_to({100, 0});
    object.path.arc_to(100, 50, 0, false, true, 0, 50);
    object.fill = color::CMYKColor{0.1, 0.8, 0.7, 0.0, 1.0};
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);

    const auto exported = SvgExporter::export_with_report(*doc.current_snapshot());
    assert(exported.success && exported.svg.find("A ") != std::string::npos);
    assert(!exported.report.is_clean());
    assert(exported.report.issues().front().severity == FidelitySeverity::Warning);
    const auto imported = SvgImporter::import_from_string(exported.svg);
    assert(imported.success && imported.objects.size() == 1);
    const auto restored = std::get<PathObject>(imported.objects.front());
    assert(restored.authoring_source &&
           restored.authoring_source->subpaths.front().segments.front().kind ==
               PathSegmentKind::EllipticArc);

    Document grouped("svg-unsupported-group");
    GroupObject group;
    group.id = generate_object_id();
    assert(grouped.execute_command(std::make_unique<AddObjectCommand>(group)).success);
    const auto blocked = SvgExporter::export_with_report(*grouped.current_snapshot());
    assert(!blocked.success && blocked.svg.empty() &&
           blocked.report.has_unsupported());
}

int main() {
    std::cout << "[test_svg_exporter] Running SVG exporter tests...\n";
    test_svg_export_and_roundtrip();
    test_svg_analytic_arc_and_loss_report();
    std::cout << "[test_svg_exporter] All tests passed!\n";
    return 0;
}
