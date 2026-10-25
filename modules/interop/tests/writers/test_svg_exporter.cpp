#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::interop;
using namespace pikdraw::document;

void test_svg_explicit_bounds_set_artboard_viewport() {
    Document document;
    SvgExportOptions options;
    options.bounds = geometry::Rect2D{25.0, 40.0, 125.0, 80.0};
    const auto exported = SvgExporter::export_with_report(
        *document.current_snapshot(), options);
    assert(exported.success);
    assert(exported.svg.find("width=\"125mm\" height=\"80mm\"") != std::string::npos);
    assert(exported.svg.find("viewBox=\"25 40 125 80\"") != std::string::npos);
}

void test_profile_bound_rgb_is_converted_to_srgb_for_svg() {
#ifdef _WIN32
    using namespace color;
    const auto srgb = ProfileResource::load_file(
        IccPreview::system_srgb_profile(), ColorSpace::Rgb);
    assert(srgb);

    const auto read_be32 = [](const std::vector<uint8_t>& bytes, size_t offset) {
        assert(offset + 4 <= bytes.size());
        return (static_cast<uint32_t>(bytes[offset]) << 24) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
            static_cast<uint32_t>(bytes[offset + 3]);
    };
    const auto write_be32 = [](std::vector<uint8_t>& bytes, size_t offset,
                               uint32_t value) {
        assert(offset + 4 <= bytes.size());
        bytes[offset] = static_cast<uint8_t>(value >> 24);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
        bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 3] = static_cast<uint8_t>(value);
    };
    auto alternate_bytes = srgb.resource->bytes();
    std::fill(alternate_bytes.begin() + 84, alternate_bytes.begin() + 100, 0);
    const auto tag_count = read_be32(alternate_bytes, 128);
    std::optional<size_t> red_xyz;
    std::optional<size_t> green_xyz;
    for (uint32_t index = 0; index < tag_count; ++index) {
        const size_t record = 132u + static_cast<size_t>(index) * 12u;
        if (record + 12 > alternate_bytes.size()) break;
        const std::string signature(alternate_bytes.begin() +
                static_cast<ptrdiff_t>(record),
            alternate_bytes.begin() + static_cast<ptrdiff_t>(record + 4));
        const auto offset = read_be32(alternate_bytes, record + 4);
        const auto size = read_be32(alternate_bytes, record + 8);
        if (offset > alternate_bytes.size() ||
            size > alternate_bytes.size() - offset || size < 20 ||
            std::string(alternate_bytes.begin() + offset,
                alternate_bytes.begin() + offset + 4) != "XYZ ")
            continue;
        if (signature == "rXYZ") red_xyz = offset;
        else if (signature == "gXYZ") green_xyz = offset;
    }
    assert(red_xyz && green_xyz);
    for (const auto [component, delta] :
         {std::pair<size_t, int32_t>{8, 6554}, {16, 3277}}) {
        const auto red = static_cast<int32_t>(
            read_be32(alternate_bytes, *red_xyz + component));
        const auto green = static_cast<int32_t>(
            read_be32(alternate_bytes, *green_xyz + component));
        write_be32(alternate_bytes, *red_xyz + component,
                   static_cast<uint32_t>(red + delta));
        write_be32(alternate_bytes, *green_xyz + component,
                   static_cast<uint32_t>(green - delta));
    }
    const auto alternate = ProfileResource::create(
        "PikDraw SVG alternate primaries", ColorSpace::Rgb,
        std::move(alternate_bytes));
    assert(alternate);

    Document doc("svg-profile-bound-rgb");
    const auto page_id = doc.current_snapshot()->active_page_id();
    assert(doc.execute_command(std::make_unique<AssignPageColorProfileCommand>(
        page_id, alternate.resource)).success);

    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(10, 10, 40, 30);
    path.fill = RGBColor{0.50, 0.40, 0.30, 0.75};
    assert(path.fill.set_profile_resource(alternate.resource));
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);

    PathObject inherited_profile = path;
    inherited_profile.id = generate_object_id();
    inherited_profile.path = geometry::Path2D::make_rect(60, 10, 40, 30);
    inherited_profile.fill = RGBColor{0.50, 0.40, 0.30, 0.75};
    assert(doc.execute_command(
        std::make_unique<AddObjectCommand>(inherited_profile)).success);

    PathObject explicit_srgb = path;
    explicit_srgb.id = generate_object_id();
    explicit_srgb.path = geometry::Path2D::make_rect(110, 10, 40, 30);
    explicit_srgb.fill = RGBColor{0.50, 0.40, 0.30, 0.75};
    assert(explicit_srgb.fill.set_profile_resource(srgb.resource));
    assert(doc.execute_command(
        std::make_unique<AddObjectCommand>(explicit_srgb)).success);

    const auto oracle = ProfileColorTransform::create(
        alternate.resource, srgb.resource);
    assert(oracle);
    ColorValue expected;
    assert(oracle.transform->convert(path.fill, expected));
    const auto expected_device = to_preview_rgba(expected).to_device();
    const auto source_device = to_preview_rgba(path.fill).to_device();
    std::ostringstream expected_hex;
    expected_hex << '#' << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(expected_device.r) << std::setw(2)
        << static_cast<int>(expected_device.g) << std::setw(2)
        << static_cast<int>(expected_device.b);
    std::ostringstream source_hex;
    source_hex << '#' << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(source_device.r) << std::setw(2)
        << static_cast<int>(source_device.g) << std::setw(2)
        << static_cast<int>(source_device.b);
    assert(expected_hex.str() != source_hex.str());

    const auto exported = SvgExporter::export_with_report(*doc.current_snapshot());
    assert(exported.success);
    assert(exported.svg.find("fill=\"" + expected_hex.str() + "\"") !=
           std::string::npos);
    const auto inherited_hex = exported.svg.find(
        "fill=\"" + expected_hex.str() + "\"",
        exported.svg.find("fill=\"" + expected_hex.str() + "\"") + 1);
    assert(inherited_hex != std::string::npos);
    assert(exported.svg.find("fill=\"" + source_hex.str() + "\"") !=
           std::string::npos);
    assert(exported.svg.find("fill-opacity=\"0.75\"") != std::string::npos);
    assert(!exported.report.is_clean());
    assert(exported.report.issues().front().severity == FidelitySeverity::Warning);
    assert(exported.report.issues().front().feature == "fill color");
#endif
}

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
    const auto group_export = SvgExporter::export_with_report(*grouped.current_snapshot());
    assert(group_export.success && group_export.svg.find("<g")!=std::string::npos);
    ImageObject image;image.id=generate_object_id();image.width_px=1;image.height_px=1;image.pixels_rgba={0xff112233};
    assert(grouped.execute_command(std::make_unique<AddObjectCommand>(image)).success);
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

void test_svg_export_clipping_mask_roundtrip() {
    PathObject mask;
    mask.id = generate_object_id();
    mask.name = "Frame";
    mask.path = geometry::Path2D::make_rect(20, 20, 60, 60);
    mask.fill = color::RGBColor{0.2, 0.4, 0.8, 1.0};
    mask.stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0};
    mask.stroke_width_mm = 1.0;

    PathObject content;
    content.id = generate_object_id();
    content.name = "Content";
    content.path = geometry::Path2D::make_rect(10, 10, 80, 80);
    content.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};

    GroupObject group;
    group.id = generate_object_id();
    group.name = "PowerClip Group";
    group.children_ids = {mask.id, content.id};
    ClipProperties clip{};
    clip.mask_object_id = mask.id;
    clip.lock_contents_to_frame = true;
    clip.preserve_frame_appearance = true;
    clip.fit_mode = ClipContentFitMode::CurrentPosition;
    group.clip = clip;

    Page page = Page::make_default();
    page.width_mm = 100.0;
    page.height_mm = 100.0;
    page.layers.front().object_ids = {group.id};

    PersistentObjectMap objects;
    objects = objects.set(mask.id, mask);
    objects = objects.set(content.id, content);
    objects = objects.set(group.id, group);

    DocumentSnapshot snapshot{1, 0, std::move(page), std::move(objects)};

    auto result = SvgExporter::export_with_report(snapshot);
    assert(result.success && result.report.is_clean());
    assert(result.svg.find("<clipPath id=\"clip_") != std::string::npos);
    assert(result.svg.find("<g clip-path=\"url(#clip_") != std::string::npos);

    auto imported = SvgImporter::import_from_string(result.svg);
    assert(imported.success);
    bool found_clipping = false;
    for (const auto& obj : imported.objects) {
        if (const auto* grp = std::get_if<GroupObject>(&obj)) {
            if (grp->is_clipping_container()) {
                found_clipping = true;
                assert(grp->clip.has_value());
                assert(grp->clip->mask_object_id != kInvalidObjectId);
            }
        }
    }
    assert(found_clipping);
}

void test_svg_nested_clips_preserve_hierarchy_and_winding() {
    PathObject mask;mask.id=generate_object_id();mask.path=geometry::Path2D::make_rect(0,0,30,30);
    mask.path.move_to(5,5);mask.path.line_to(20,5);mask.path.line_to(20,20);mask.path.line_to(5,20);mask.path.close();
    mask.path.set_winding_rule(geometry::WindingRule::EvenOdd);
    mask.has_stroke=false;
    PathObject content;content.id=generate_object_id();content.path=geometry::Path2D::make_rect(-10,-10,50,50);content.has_stroke=false;
    GroupObject clip;clip.id=generate_object_id();clip.children_ids={mask.id,content.id};clip.clip=ClipProperties{};
    clip.clip->mask_object_id=mask.id;clip.clip->preserve_frame_appearance=false;
    clip.transform=geometry::Transform2D::translation(7,9);
    GroupObject parent;parent.id=generate_object_id();parent.children_ids={clip.id};parent.transform=geometry::Transform2D::rotation_degrees(30);
    auto page=Page::make_default();page.layers[0].object_ids={parent.id};
    auto objects=PersistentObjectMap{}.set(mask.id,mask).set(content.id,content).set(clip.id,clip).set(parent.id,parent);
    DocumentSnapshot snapshot{1,0,page,objects};
    const auto output=SvgExporter::export_with_report(snapshot);
    assert(output.success);assert(output.svg.find("clip-rule=\"evenodd\"")!=std::string::npos);
    assert(output.svg.find("transform=")!=std::string::npos);
    auto imported=SvgImporter::import_from_string(output.svg);
    if(!imported.success)std::cerr<<imported.error_message<<"\n";
    assert(imported.success);
    PersistentObjectMap restored_objects;for(const auto& object:imported.objects)restored_objects=restored_objects.set(std::visit([](const auto& value){return value.id;},object),object);
    DocumentSnapshot restored{1,0,imported.page,restored_objects};
    const auto expected=snapshot.hierarchy().find(clip.id)->bounds;
    for(const auto& object:imported.objects)if(const auto* g=std::get_if<GroupObject>(&object);g&&g->clip){
        const auto actual=restored.hierarchy().find(g->id)->bounds;
        assert(actual&&std::abs(actual->x-expected->x)<1e-6&&std::abs(actual->y-expected->y)<1e-6);
    }
    assert(std::count_if(imported.objects.begin(),imported.objects.end(),[](const auto& object){
        const auto* group=std::get_if<GroupObject>(&object);return group&&group->clip;})>=1);
    std::ostringstream selected;
    const std::vector<ObjectId> scope{parent.id};
    const auto scoped=SvgExporter::write_to_stream(snapshot,selected,{},&scope);
    assert(scoped.success);assert(selected.str().find("clip-path")!=std::string::npos);
}

int main() {
    test_svg_explicit_bounds_set_artboard_viewport();
    test_svg_nested_clips_preserve_hierarchy_and_winding();
    std::cout << "[test_svg_exporter] Running SVG exporter tests...\n";
    test_profile_bound_rgb_is_converted_to_srgb_for_svg();
    test_svg_export_and_roundtrip();
    test_svg_analytic_arc_and_loss_report();
    test_svg_path_and_text_opacity_mapping();
    test_svg_unsupported_appearance_is_rejected_before_output();
    test_svg_stream_export_respects_resolved_object_scope();
    test_svg_export_clipping_mask_roundtrip();
    std::cout << "[test_svg_exporter] All tests passed!\n";
    return 0;
}
