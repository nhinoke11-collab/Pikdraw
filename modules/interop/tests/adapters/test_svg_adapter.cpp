#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "../../src/path_adapter/svg_path_adapter.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::interop;
using namespace pikdraw::document;

void test_svg_import_basic() {
    std::string svg = R"(
        <svg xmlns="http://www.w3.org/2000/svg" width="500" height="400">
            <rect x="50" y="60" width="200" height="150" fill="#224488" stroke="#000000" stroke-width="2" />
            <path d="M 10 20 L 100 20 Q 150 50 200 20 Z" fill="#ff0000" />
            <text x="30" y="300" font-family="PikDrawSans" font-size="24">Hà Nội - Thủ đô ngàn năm văn hiến</text>
        </svg>
    )";

    auto res = SvgImporter::import_from_string(svg);
    assert(res.success);
    assert(res.report.is_clean());
    assert(res.page.width_mm == 500.0);
    assert(res.page.height_mm == 400.0);
    assert(res.objects.size() == 3);

    // Verify Rect
    const auto& rect_obj = std::get<PathObject>(res.objects[0]);
    assert(rect_obj.id != kInvalidObjectId);
    assert(rect_obj.stroke_width_mm == 2.0);

    // Verify Vietnamese text
    const auto& text_obj = std::get<TextObject>(res.objects[2]);
    assert(text_obj.text == "Hà Nội - Thủ đô ngàn năm văn hiến");
    assert(text_obj.font.family_name == "PikDrawSans");
    assert(text_obj.font.size_pt == 24.0);
}

void test_svg_unsupported_filter_reported() {
    std::string svg_with_filter = R"(
        <svg width="200" height="200">
            <filter id="f1"><feGaussianBlur stdDeviation="3" /></filter>
            <rect x="10" y="10" width="50" height="50" />
        </svg>
    )";

    auto res = SvgImporter::import_from_string(svg_with_filter);
    assert(res.success);
    assert(!res.report.is_clean());
    assert(res.report.has_unsupported());
    assert(res.objects.size() == 1);
}

void test_lossless_curve_path_adapter() {
    geometry::Path2D path;
    path.move_to({10, 10});
    path.quad_to(20, 0, 30, 10);
    path.cubic_to(35, 20, 45, 20, 50, 10);
    path.close();
    std::string issue;
    const auto source = adapters::SvgPathAdapter::from_path2d(path, &issue);
    assert(source.has_value() && issue.empty());
    assert(source->subpaths.front().segments.size() == 3);
    assert(source->subpaths.front().segments[0].kind ==
           document::PathSegmentKind::QuadraticBezier);
    assert(source->subpaths.front().segments[1].kind ==
           document::PathSegmentKind::CubicBezier);
    const auto data = adapters::SvgPathAdapter::to_path_data(*source, &issue);
    assert(data.has_value() && issue.empty());
    assert(data->find("Q ") != std::string::npos);
    assert(data->find("C ") != std::string::npos);
    assert(data->find("Z ") != std::string::npos);

    geometry::Path2D arc_path;
    arc_path.move_to({100, 0});
    arc_path.arc_to(100, 50, 0, false, true, 0, 50);
    const auto arc_source =
        adapters::SvgPathAdapter::from_path2d(arc_path, &issue);
    assert(arc_source.has_value() && issue.empty());
    assert(arc_source->subpaths.front().segments.front().kind ==
           document::PathSegmentKind::EllipticArc);
    const auto arc_data =
        adapters::SvgPathAdapter::to_path_data(*arc_source, &issue);
    assert(arc_data.has_value() && arc_data->find("A ") != std::string::npos);

    geometry::Path2D invalid_arc;
    invalid_arc.move_to({1, 1});
    invalid_arc.arc_to(0, 5, 0, false, true, 10, 10);
    assert(!adapters::SvgPathAdapter::from_path2d(invalid_arc, &issue));
    assert(!issue.empty());
}

int main() {
    std::cout << "[test_svg_adapter] Running SVG importer tests...\n";
    test_svg_import_basic();
    test_svg_unsupported_filter_reported();
    test_lossless_curve_path_adapter();
    std::cout << "[test_svg_adapter] All tests passed!\n";
    return 0;
}
