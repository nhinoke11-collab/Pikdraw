#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "../../src/path_adapter/svg_path_adapter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

using namespace pikdraw;
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
    assert(rect_obj.authoring_source);
    assert(std::get<PathObject>(res.objects[1]).authoring_source);

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
    assert(std::abs(path.path.elements()[0].pt.y - 65.0) < 1e-9);
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
    assert(std::abs(elements[0].pt.y - (100.0 - 10.0 * 25.4 / 96.0)) < 1e-9);
    assert(std::abs(elements[2].pt.y - (100.0 - 15.0 * 25.4 / 96.0)) < 1e-9);
    assert(std::abs(elements[3].pt.y - (100.0 - 14.0 * 25.4 / 96.0)) < 1e-9);
    assert(std::abs(elements[3].pt.x - 13.0 * 25.4 / 96.0) < 1e-9);
    assert(elements[4].verb == PathVerb::Close);
}

void test_group_and_object_transforms_are_applied_to_page_coordinates() {
    const std::string svg = R"svg(<svg width="100mm" height="100mm">
        <g transform="translate(5 7)"><rect id="moved" x="1" y="2" width="3" height="4"
            transform="scale(2)"/></g>
        <path id="rotated" transform="rotate(90)" d="M10 0L20 0"/>
        <path id="combined" transform="translate(5 7) scale(2)" d="M1 2L2 2"/>
    </svg>)svg";
    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success && result.objects.size() == 4);
    const auto& moved = std::get<PathObject>(result.objects[0]).path.elements();
    assert(std::abs(moved[0].pt.x - 7.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(moved[0].pt.y - (100.0 - 11.0 * 25.4 / 96.0)) < 1e-9);
    const auto& rotated = std::get<PathObject>(result.objects[2]).path.elements();
    assert(std::abs(rotated[0].pt.x) < 1e-9);
    assert(std::abs(rotated[0].pt.y - (100.0 - 10.0 * 25.4 / 96.0)) < 1e-9);
    const auto& combined = std::get<PathObject>(result.objects[3]).path.elements();
    assert(std::abs(combined[0].pt.x - 7.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(combined[0].pt.y - (100.0 - 11.0 * 25.4 / 96.0)) < 1e-9);
}

void test_arc_path_command_is_preserved() {
    const std::string svg = R"(<svg width="100mm" height="100mm"><path id="arc" d="M0 0 A2 2 0 0 1 4 4"/></svg>)";
    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success && result.objects.size() == 1);
    const auto& path = std::get<PathObject>(result.objects.front()).path;
    assert(path.size() == 2);
    const auto& arc = path.elements()[1];
    assert(arc.verb == PathVerb::ArcTo);
    // The SVG-to-document viewport reflects the Y axis. Transform the whole
    // arc so its direction and ellipse parameters follow the mapped geometry.
    assert(!arc.large_arc && !arc.sweep);
    assert(std::abs(arc.pt.x - 4.0 * 25.4 / 96.0) < 1e-9);
    assert(std::abs(arc.pt.y - (100.0 - 4.0 * 25.4 / 96.0)) < 1e-9);
}

void test_nested_groups_and_inkscape_layers_are_preserved() {
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg"
        xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="100mm" height="80mm">
        <g id="layer-cut" inkscape:groupmode="layer" inkscape:label="Cut">
            <g id="outer" inkscape:label="Outer group">
                <rect id="rect-a" width="10mm" height="5mm"/>
                <g id="inner"><path id="path-a" d="M0 0L5 5"/></g>
            </g>
        </g>
        <g id="layer-print" inkscape:groupmode="layer" inkscape:label="Print">
            <path id="path-b" d="M1 1L2 2"/>
        </g>
    </svg>)";

    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success);
    assert(result.page.layers.size() == 2);
    assert(result.layer_source_ids == std::vector<std::string>({"layer-cut", "layer-print"}));
    assert(result.page.layers[0].name == "Cut" && result.page.layers[1].name == "Print");
    assert(result.objects.size() == 5 && result.source_ids.size() == 5);

    std::unordered_map<std::string, size_t> object_by_source;
    for (size_t index = 0; index < result.source_ids.size(); ++index) {
        object_by_source.emplace(result.source_ids[index], index);
    }
    const auto& outer = std::get<GroupObject>(result.objects[object_by_source.at("outer")]);
    assert(result.page.layers[0].object_ids == std::vector<ObjectId>{outer.id});
    assert(outer.children_ids.size() == 2);
    const auto& rect = std::get<PathObject>(result.objects[object_by_source.at("rect-a")]);
    const auto& inner = std::get<GroupObject>(result.objects[object_by_source.at("inner")]);
    const auto& path_a = std::get<PathObject>(result.objects[object_by_source.at("path-a")]);
    const auto& path_b = std::get<PathObject>(result.objects[object_by_source.at("path-b")]);
    assert(outer.children_ids[0] == rect.id && outer.children_ids[1] == inner.id);
    assert(inner.children_ids == std::vector<ObjectId>{path_a.id});
    assert(result.page.layers[1].object_ids == std::vector<ObjectId>{path_b.id});
}

void test_rejects_active_external_and_malformed_content() {
    for (const auto& svg : {
        R"(<svg width="10mm" height="10mm"><script>alert(1)</script></svg>)",
        R"(<!DOCTYPE svg [<!ENTITY x SYSTEM "file:///secret">]><svg width="10mm" height="10mm"/>)",
        R"(<svg width="10mm" height="10mm"><image href="https://example.invalid/x.png"/></svg>)",
        R"(<svg width="10mm" height="10mm"><g opacity="0.5"><rect width="2" height="2"/></g></svg>)",
        R"(<svg width="10mm" height="10mm"><g style="opacity:0.5"><rect width="2" height="2"/></g></svg>)",
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

void test_element_opacity_maps_to_appearance() {
    const std::string svg = R"(<svg width="10mm" height="10mm">
        <path id="path-opacity" d="M0 0 L4 0 L4 4 Z" fill="#ff000080"
            fill-opacity="25%" stroke="#0000ff" stroke-opacity="50%" opacity="75%"/>
        <rect id="rect-opacity" width="2" height="2" opacity="1.2"
            fill-opacity="-0.25" stroke-opacity="200%"/>
        <text id="text-opacity" x="0" y="1" opacity="0.4" fill-opacity="0.6">x</text>
    </svg>)";
    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success);
    assert(!result.report.has_unsupported());
    assert(result.objects.size() == 3);

    const auto& path = std::get<PathObject>(result.objects[0]);
    assert(std::abs(path.appearance.object_opacity - 0.75) < 1e-12);
    assert(std::abs(path.appearance.fill_opacity - 0.25) < 1e-12);
    assert(std::abs(path.appearance.stroke_opacity - 0.5) < 1e-12);
    const auto* path_fill = std::get_if<color::RGBColor>(&path.fill.payload());
    assert(path_fill && std::abs(path_fill->a - 128.0 / 255.0) < 1e-12);

    const auto& rect = std::get<PathObject>(result.objects[1]);
    assert(rect.appearance.object_opacity == 1.0);
    assert(rect.appearance.fill_opacity == 0.0);
    assert(rect.appearance.stroke_opacity == 1.0);

    const auto& text = std::get<TextObject>(result.objects[2]);
    assert(std::abs(text.appearance.object_opacity - 0.4) < 1e-12);
    assert(std::abs(text.appearance.fill_opacity - 0.6) < 1e-12);
}

void test_invalid_element_opacity_rejects_the_whole_source() {
    for (const auto& element : {
        R"(<path d="M0 0 L1 1" opacity="not-a-number"/>)",
        R"(<rect width="1" height="1" fill-opacity="1e999"/>)",
        R"(<text x="0" y="1" fill-opacity="auto">x</text>)",
    }) {
        const auto svg = std::string(
            R"(<svg width="10mm" height="10mm">)") + element + "</svg>";
        const auto result = SvgImporter::import_from_string(svg);
        assert(!result.success);
        assert(result.objects.empty());
        assert(result.report.has_unsupported());
        assert(result.report.issue_count() == 1);
        assert(result.report.issues().front().feature == "invalid_opacity");
    }
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

void test_svg_clip_path_import() {
    const std::string svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm">
        <defs>
            <clipPath id="my-clip">
                <rect x="10" y="10" width="80" height="80" />
            </clipPath>
        </defs>
        <g clip-path="url(#my-clip)">
            <rect id="content-rect" x="0" y="0" width="100" height="100" fill="#ff0000" />
        </g>
    </svg>)svg";

    const auto result = SvgImporter::import_from_string(svg);
    assert(result.success);
    assert(result.objects.size() >= 2);

    bool found_clipping_group = false;
    for (const auto& var : result.objects) {
        if (const auto* grp = std::get_if<GroupObject>(&var)) {
            if (grp->is_clipping_container()) {
                found_clipping_group = true;
                assert(grp->clip.has_value());
                assert(grp->clip->mask_object_id != kInvalidObjectId);
                assert(!grp->children_ids.empty());
                assert(grp->children_ids.front() == grp->clip->mask_object_id);
            }
        }
    }
    assert(found_clipping_group);
}

int main() {
    std::cout << "[test_svg_adapter] Running SVG importer tests...\n";
    test_svg_import_basic();
    test_svg_unsupported_filter_reported();
    test_viewbox_units_and_path_commands();
    test_relative_horizontal_vertical_path_commands();
    test_group_and_object_transforms_are_applied_to_page_coordinates();
    test_arc_path_command_is_preserved();
    test_nested_groups_and_inkscape_layers_are_preserved();
    test_rejects_active_external_and_malformed_content();
    test_rejects_source_limits_and_bad_utf8();
    test_element_opacity_maps_to_appearance();
    test_invalid_element_opacity_rejects_the_whole_source();
    test_lossless_curve_path_adapter();
    test_svg_clip_path_import();
    std::cout << "[test_svg_adapter] All tests passed!\n";
    return 0;
}
