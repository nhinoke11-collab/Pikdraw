#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
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
    to.text = "PikDraw Vector Editor Việt Nam";
    to.font.family_name = "PikDrawSans";
    to.font.size_pt = 32.0;
    to.position = {100.0, 300.0};
    doc.execute_command(std::make_unique<AddObjectCommand>(to));

    auto snap = doc.current_snapshot();
    std::string svg_out = SvgExporter::export_to_string(*snap);
    assert(!svg_out.empty());
    assert(svg_out.find("<svg") != std::string::npos);
    assert(svg_out.find("PikDraw Vector Editor Việt Nam") != std::string::npos);

    // Re-import exported SVG to prove roundtrip fidelity
    auto import_res = SvgImporter::import_from_string(svg_out);
    assert(import_res.success);
    assert(import_res.objects.size() == 2);

    const auto& re_text = std::get<TextObject>(import_res.objects[1]);
    assert(re_text.text == "PikDraw Vector Editor Việt Nam");
    assert(re_text.font.family_name == "PikDrawSans");
    assert(re_text.font.size_pt == 32.0);
}

int main() {
    std::cout << "[test_svg_exporter] Running SVG exporter tests...\n";
    test_svg_export_and_roundtrip();
    std::cout << "[test_svg_exporter] All tests passed!\n";
    return 0;
}
