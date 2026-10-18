#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

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

void test_svg_path_and_text_opacity_mapping() {
    // AC-BF-SVG-01 — Given source paint alpha plus component/object opacity,
    // When exporting a Path and Text object, Then paint opacity is multiplied
    // exactly and object opacity is emitted at the element compositing level.
    Document doc("svg-opacity-mapping");
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(10, 10, 40, 30);
    path.fill = color::RGBColor{0.2, 0.4, 0.6, 0.5};
    path.stroke = color::RGBColor{0.1, 0.2, 0.3, 0.75};
    path.stroke_width_mm = 1.25;
    path.appearance.fill_opacity = 0.5;
    path.appearance.stroke_opacity = 0.5;
    path.appearance.object_opacity = 0.25;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);

    TextObject text;
    text.id = generate_object_id();
    text.text = "opacity";
    text.fill = color::RGBColor{0.3, 0.5, 0.7, 0.5};
    text.appearance.fill_opacity = 0.5;
    text.appearance.object_opacity = 0.5;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(text)).success);

    const auto exported = SvgExporter::export_with_report(*doc.current_snapshot());
    assert(exported.success);
    assert(exported.svg.find("fill-opacity=\"0.25\"") != std::string::npos);
    assert(exported.svg.find("stroke-opacity=\"0.375\"") != std::string::npos);
    assert(exported.svg.find("opacity=\"0.25\"") != std::string::npos);
    assert(exported.svg.find("opacity=\"0.5\"") != std::string::npos);
}

void test_svg_unsupported_appearance_is_rejected_before_output() {
    // AC-BF-SVG-02 — Given an unrepresentable blend or effect node, When SVG
    // export is requested, Then it returns no bytes and identifies the loss.
    Document blend_doc("svg-unsupported-blend");
    PathObject blended;
    blended.id = generate_object_id();
    blended.path = geometry::Path2D::make_rect(0, 0, 20, 20);
    blended.appearance.blend_mode = BlendMode::Multiply;
    assert(blend_doc.execute_command(std::make_unique<AddObjectCommand>(blended)).success);
    const auto blend_result =
        SvgExporter::export_with_report(*blend_doc.current_snapshot());
    assert(!blend_result.success && blend_result.svg.empty());
    assert(blend_result.report.has_unsupported());
    assert(blend_result.report.issues().front().feature == "Object blend mode");

    Document effect_doc("svg-unsupported-effect");
    PathObject effected;
    effected.id = generate_object_id();
    effected.path = geometry::Path2D::make_rect(0, 0, 20, 20);
    EffectNode disabled_blur;
    disabled_blur.id = generate_effect_instance_id();
    disabled_blur.enabled = false;
    disabled_blur.parameters = GaussianBlurEffect{1.0, 1.0};
    effected.appearance.effects.push_back(disabled_blur);
    assert(effect_doc.execute_command(std::make_unique<AddObjectCommand>(effected)).success);
    const auto effect_result =
        SvgExporter::export_with_report(*effect_doc.current_snapshot());
    assert(!effect_result.success && effect_result.svg.empty());
    assert(effect_result.report.has_unsupported());
    assert(effect_result.report.issues().front().feature == "Appearance effects");
    assert(SvgExporter::export_to_string(*effect_doc.current_snapshot()).empty());
}

void test_svg_stream_export_respects_resolved_object_scope() {
    Document doc("svg-scoped-stream");
    PathObject included;
    included.id = generate_object_id();
    included.path = geometry::Path2D::make_rect(10, 10, 20, 20);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(included)).success);

    PathObject excluded;
    excluded.id = generate_object_id();
    excluded.path = geometry::Path2D::make_rect(100, 100, 20, 20);
    excluded.appearance.blend_mode = BlendMode::Multiply;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(excluded)).success);

    const std::vector<ObjectId> scope{included.id};
    std::ostringstream output;
    const auto result = SvgExporter::write_to_stream(
        *doc.current_snapshot(), output, SvgExportOptions{}, &scope);
    assert(result.success && result.report.is_clean());
    const auto text = output.str();
    assert(text.find("<path") != std::string::npos);
    assert(text.find("<path", text.find("<path") + 1) == std::string::npos);
    const auto roundtrip = SvgImporter::import_from_string(text);
    assert(roundtrip.success && roundtrip.objects.size() == 1);
    assert(std::get<PathObject>(roundtrip.objects.front()).path.bounds().x == 10.0);

    const std::vector<ObjectId> unsupported_scope{excluded.id};
    std::ostringstream rejected_output;
    const auto rejected = SvgExporter::write_to_stream(
        *doc.current_snapshot(), rejected_output, SvgExportOptions{},
        &unsupported_scope);
    assert(!rejected.success && rejected.report.has_unsupported());
    assert(rejected_output.str().empty());
}

int main() {
    std::cout << "[test_svg_exporter] Running SVG exporter tests...\n";
    test_svg_export_and_roundtrip();
    test_svg_analytic_arc_and_loss_report();
    test_svg_path_and_text_opacity_mapping();
    test_svg_unsupported_appearance_is_rejected_before_output();
    test_svg_stream_export_respects_resolved_object_scope();
    std::cout << "[test_svg_exporter] All tests passed!\n";
    return 0;
}
