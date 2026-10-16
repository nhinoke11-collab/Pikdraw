#include "pikdraw/interop/adapters/svg_importer.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw::interop;
using namespace pikdraw::document;
using namespace pikdraw::geometry;

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
    assert(res.report.issue_count() == 1);
    assert(!res.report.has_unsupported());
    assert(std::abs(res.page.width_mm - 500.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(res.page.height_mm - 400.0 * 25.4 / 96.0) < 1e-9);
    assert(res.objects.size() == 3);
    assert(res.source_ids.size() == 3);

    // Verify Rect
    const auto& rect_obj = std::get<PathObject>(res.objects[0]);
    assert(rect_obj.id != kInvalidObjectId);
    assert(std::abs(rect_obj.stroke_width_mm - 2.0 * 25.4 / 96.0) < 1e-9);

    // Verify Vietnamese text
    const auto& text_obj = std::get<TextObject>(res.objects[2]);
    assert(text_obj.text == "Hà Nội - Thủ đô ngàn năm văn hiến");
    assert(text_obj.font.family_name == "PikDrawSans");
    assert(text_obj.font.size_pt == 18.0); // SVG px are CSS pixels at 96 DPI.
}

void test_svg_unsupported_filter_reported() {
    std::string svg_with_filter = R"(
        <svg width="200" height="200">
            <filter id="f1"><feGaussianBlur stdDeviation="3" /></filter>
            <rect x="10" y="10" width="50" height="50" />
        </svg>
    )";

    auto res = SvgImporter::import_from_string(svg_with_filter);
    assert(!res.success);
    assert(!res.report.is_clean());
    assert(res.report.has_unsupported());
    assert(res.objects.empty());
}

void test_viewbox_units_and_path_commands() {
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 200 100">
        <path id="curve" d="M10 20 C10 0 10 10 20 10 S30 20 40 10 Q50 0 60 10 T80 10 Z" fill="#f00" fill-rule="evenodd"/>
    </svg>)";
    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success);
    assert(result.page.width_mm == 100.0 && result.page.height_mm == 100.0);
    assert(result.objects.size() == 1 && result.source_ids[0] == "curve");
    const auto& path = std::get<PathObject>(result.objects[0]);
    assert(path.path.winding_rule() == WindingRule::EvenOdd);
    assert(path.path.size() == 6);
    assert(std::abs(path.path.elements()[0].pt.x - 5.0) < 1e-9);
    assert(std::abs(path.path.elements()[0].pt.y - 35.0) < 1e-9);
    assert(std::abs(path.path.elements()[1].ctrl2.x - 5.0) < 1e-9);
}

void test_relative_horizontal_vertical_path_commands() {
    const std::string svg = R"(<svg width="100mm" height="100mm"><path id="relative" d="m10 10 h5 v5 l-2 -1 z"/></svg>)";
    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success && result.objects.size() == 1);
    const auto& path = std::get<PathObject>(result.objects[0]);
    assert(path.path.size() == 5);
    const auto& elements = path.path.elements();
    assert(std::abs(elements[0].pt.x - 10.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(elements[1].pt.x - 15.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(elements[2].pt.y - 15.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(elements[3].pt.x - 13.0 * 25.4 / 96.0) < 1e-9);
    assert(elements[4].verb == PathVerb::Close);
}

void test_rejects_active_external_and_malformed_content() {
    for (const auto& svg : {
        R"(<svg width="10mm" height="10mm"><script>alert(1)</script></svg>)",
        R"(<!DOCTYPE svg [<!ENTITY x SYSTEM "file:///secret">]><svg width="10mm" height="10mm"/>)",
        R"(<svg width="10mm" height="10mm"><image href="https://example.invalid/x.png"/></svg>)",
        R"(<svg width="10mm" height="10mm"><g><rect width="2" height="2"/></g></svg>)",
        R"(<svg width="10mm" height="10mm"><path d="M 0 0 A 2 2 0 0 1 4 4"/></svg>)",
        R"(<svg width="10mm" height="10mm"><path style="fill:red" d="M0 0L1 1"/></svg>)",
        R"(<svg width="10mm" height="10mm"><path d="M0 0L1"/></svg>)",
        R"(<?xml version="1.0" encoding="UTF-16"?><svg width="10mm" height="10mm"/>)",
        R"(<svg width="10mm" height="10mm"><rect note="raw < character" width="1" height="1"/></svg>)",
        "<svg width=\"10mm\" height=\"10mm\">\x01</svg>"
    }) {
        const auto result = SvgImporter::import_from_string(svg);
        assert(!result.success);
        assert(result.report.has_unsupported());
        assert(result.objects.empty());
    }
}

void test_rejects_source_limits_and_bad_utf8() {
    std::string oversized(SvgImporter::kMaxSourceBytes + 1, ' ');
    const auto over_limit = SvgImporter::import_from_string(oversized);
    assert(!over_limit.success && over_limit.report.has_unsupported());

    std::string invalid = "<svg width=\"1mm\" height=\"1mm\">";
    invalid.push_back(static_cast<char>(0xff));
    invalid += "</svg>";
    const auto bad_encoding = SvgImporter::import_from_string(invalid);
    assert(!bad_encoding.success && bad_encoding.objects.empty());
}

int main() {
    std::cout << "[test_svg_adapter] Running SVG importer tests...\n";
    test_svg_import_basic();
    test_svg_unsupported_filter_reported();
    test_viewbox_units_and_path_commands();
    test_relative_horizontal_vertical_path_commands();
    test_rejects_active_external_and_malformed_content();
    test_rejects_source_limits_and_bad_utf8();
    std::cout << "[test_svg_adapter] All tests passed!\n";
    return 0;
}
