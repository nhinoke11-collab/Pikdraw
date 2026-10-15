#include "pikdraw/interop/adapters/svg_importer.hpp"
#include <cassert>
#include <iostream>

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

int main() {
    std::cout << "[test_svg_adapter] Running SVG importer tests...\n";
    test_svg_import_basic();
    test_svg_unsupported_filter_reported();
    std::cout << "[test_svg_adapter] All tests passed!\n";
    return 0;
}
