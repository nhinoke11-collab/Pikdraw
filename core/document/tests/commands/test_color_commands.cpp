#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/document/commands/tac_commands.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "../../src/storage/byte_codec.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::document;
namespace {
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void write_u32_be(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}
std::shared_ptr<const color::ProfileResource> make_test_profile(
    std::string name, color::ColorSpace space, uint8_t marker) {
    std::vector<uint8_t> bytes(148, 0);
    write_u32_be(bytes, 0, static_cast<uint32_t>(bytes.size()));
    const char* color_signature = space == color::ColorSpace::Cmyk ? "CMYK" : "RGB ";
    for (size_t i = 0; i < 4; ++i) bytes[16 + i] = static_cast<uint8_t>(color_signature[i]);
    for (size_t i = 0; i < 4; ++i) bytes[20 + i] = static_cast<uint8_t>("XYZ "[i]);
    for (size_t i = 0; i < 4; ++i) bytes[36 + i] = static_cast<uint8_t>("acsp"[i]);
    write_u32_be(bytes, 128, 1);
    for (size_t i = 0; i < 4; ++i) bytes[132 + i] = static_cast<uint8_t>("test"[i]);
    write_u32_be(bytes, 136, 144);
    write_u32_be(bytes, 140, 4);
    bytes[144] = marker;
    auto created = color::ProfileResource::create(std::move(name), space, std::move(bytes));
    check(static_cast<bool>(created), "Synthetic ICC profile resource failed validation");
    return std::move(created.resource);
}
void process(const color::ColorValue& value, color::ColorSpace space) {
    check(value.color_space() == space || value.color_space() == color::ColorSpace::Spot,
          "Opposite-mode process paint survived document conversion");
}
void all_paints(const DocumentSnapshot& snapshot, color::ColorSpace space) {
    for (const auto& page : snapshot.pages())
        for (const auto& [id, object] : page.objects.all())
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
                    process(value.fill, space);
                    if constexpr (std::is_same_v<T, PathObject>) {
                        process(value.stroke, space);
                        if (value.fill_gradient)
                            for (const auto& stop : value.fill_gradient->stops) process(stop.color, space);
                    }
                    for (const auto& effect : value.appearance.effects)
                        if (const auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters))
                            process(shadow->color, space);
                }
            }, object);
}
void legacy_v8_color_metadata_migration() {
    using namespace storage_detail;
    Writer payload(true);
    payload.str("legacy-v8-color");
    for (auto value : {8u, 1u, 0u, 1u, 0u, 1u, 0u}) payload.u32(value);
    payload.u32(1); payload.u64(987);
    payload.u64(987); payload.str("Legacy page");
    payload.f64(210); payload.f64(297);
    payload.u32(1); payload.str("Legacy layer"); payload.flag(true); payload.flag(false);
    payload.u32(0); payload.u32(0); // Layer refs and objects.
    Writer container;
    for (auto byte : kPikDrawMagic) container.u8(byte);
    container.u32(8); container.u32(0); container.u64(1); container.u64(0);
    container.u32(static_cast<uint32_t>(payload.bytes.size())); container.u32(0);
    container.append(payload.bytes);
    const auto crc = NativeFormat::calculate_crc32(container.bytes.data(), container.bytes.size());
    for (unsigned i = 0; i < 4; ++i) container.bytes[32 + i] = static_cast<uint8_t>(crc >> (i * 8));
    std::string error;
    const auto migrated = NativeFormat::deserialize(container.bytes.data(), container.bytes.size(), &error);
    check(migrated && migrated->color_mode() == color::DocumentColorMode::Rgb &&
        migrated->color_profile_name() == "sRGB" && migrated->raster_dpi() == 72 &&
        !migrated->page().color_mode_explicit, "Legacy v8 migration invented an explicit mode");
    Document legacy(*migrated);
    check(legacy.execute_command(std::make_unique<SetDocumentRasterDpiCommand>(72)).success &&
        legacy.revision() == 1 && legacy.page().raster_dpi_explicit && !legacy.page().color_mode_explicit,
        "Setting legacy DPI must enable effect sampling without converting source colors");
}

void test_explicit_tac_validation_and_fix() {
    auto active_page = make_document_page("TAC active");
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(1, 1, 20, 20);
    path.fill = color::ColorValue::make_cmyk(.8, .8, .5, .2, .4);
    path.fill.set_overprint({true, false});
    path.stroke = color::ColorValue::make_cmyk(.1, .1, .1, .1);
    auto gradient = GradientPaint::make_default_linear(
        allocate_gradient_paint_id(), path.bounds());
    gradient.stops.front().color = color::ColorValue::make_cmyk(.7, .7, .5, .1, .6);
    gradient.stops.back().color = color::ColorValue::make_spot("CutContour", .75);
    path.fill_gradient = std::make_shared<const GradientPaint>(std::move(gradient));
    EffectNode shadow;
    shadow.id = generate_effect_instance_id();
    shadow.enabled = false;
    DropShadowEffect shadow_value;
    shadow_value.color = color::ColorValue::make_cmyk(.8, .8, .7, .2, .7);
    shadow_value.color.set_overprint({false, true});
    shadow.parameters = shadow_value;
    path.appearance.effects.push_back(shadow);
    active_page.layout.layers.front().object_ids.push_back(path.id);
    active_page.objects = active_page.objects.set(path.id, path);

    TextObject text;
    text.id = generate_object_id();
    text.fill = color::ColorValue::make_cmyk(.9, .7, .7, .1, .5);
    active_page.layout.layers.front().object_ids.push_back(text.id);
    active_page.objects = active_page.objects.set(text.id, text);

    ImageObject image;
    image.id = generate_object_id();
    image.width_px = 1;
    image.height_px = 1;
    image.pixels_rgba = {0xffffffffu};
    active_page.layout.layers.front().object_ids.push_back(image.id);
    active_page.objects = active_page.objects.set(image.id, image);

    auto other_page = make_document_page("TAC other");
    PathObject other_path;
    other_path.id = generate_object_id();
    other_path.path = geometry::Path2D::make_rect(2, 2, 10, 10);
    other_path.fill = color::ColorValue::make_cmyk(.9, .9, .8, .4);
    other_page.layout.layers.front().object_ids.push_back(other_path.id);
    other_page.objects = other_page.objects.set(other_path.id, other_path);

    const auto active_id = active_page.id;
    const auto other_id = other_page.id;
    DocumentSnapshot initial{1, 0, {active_page, other_page}, active_id, "tac-document"};
    Document doc(initial);
    const color::TACOutputCondition output{"press-profile-200", 200.0};

    const auto before_validation_revision = doc.revision();
    const auto validation = validate_document_tac(*doc.current_snapshot(), output,
        TACScope::ActivePage);
    check(validation.success && validation.summary.output_condition_id == output.identity &&
          validation.summary.scope == "active-page" &&
          validation.summary.process_colors_examined == 5 &&
          validation.summary.process_colors_over_limit == 3 &&
          validation.summary.spot_colors_not_assessed == 1 &&
          validation.summary.raster_objects_not_assessed == 1 &&
          validation.summary.maximum_before_percent == 250.0 &&
          validation.summary.maximum_after_percent == 250.0 &&
          doc.revision() == before_validation_revision,
          "Read-only TAC validation summary or mutation boundary was incorrect");

    const auto original_active_path = doc.current_snapshot()->find_page(active_id)->objects.lookup_ref(path.id);
    const auto original_other_path = doc.current_snapshot()->find_page(other_id)->objects.lookup_ref(other_path.id);
    const auto fixed = doc.execute_command(std::make_unique<FixDocumentTacCommand>(
        output, TACScope::ActivePage));
    check(fixed.success && !fixed.is_no_change && fixed.tac_summary &&
          fixed.tac_summary->process_colors_changed == 3 &&
          fixed.tac_summary->fix_method.find("K-preserving proportional CMY") != std::string::npos &&
          fixed.tac_summary->maximum_after_percent <= 200.000001 &&
          doc.revision() == before_validation_revision + 1,
          "TAC fix did not make one bounded, reported transaction");

    const auto& fixed_path = std::get<PathObject>(
        *doc.current_snapshot()->find_page(active_id)->objects.lookup_ref(path.id));
    const auto& fixed_fill = std::get<color::CMYKColor>(fixed_path.fill.payload());
    check(std::abs(color::calculate_tac(fixed_fill) - 200.0) < 1e-8 &&
          fixed_fill.k == .2 && fixed_fill.a == .4 &&
          fixed_path.fill.overprint().fill && !fixed_path.fill.overprint().stroke,
          "TAC fix changed K, alpha or fill overprint metadata");
    const auto& fixed_shadow = std::get<DropShadowEffect>(
        fixed_path.appearance.effects.front().parameters).color;
    check(fixed_shadow.overprint().stroke &&
          std::get<color::CMYKColor>(fixed_shadow.payload()).a == .7,
          "TAC fix changed shadow alpha or overprint metadata");
    check(doc.current_snapshot()->find_page(other_id)->objects.lookup_ref(other_path.id) ==
              original_other_path,
          "Active-page TAC fix modified another page");
    check(doc.undo() &&
          doc.current_snapshot()->find_page(active_id)->objects.lookup_ref(path.id) ==
              original_active_path,
          "One undo did not restore exact source objects after TAC fix");

    const auto selection_validation = validate_document_tac(*doc.current_snapshot(), output,
        TACScope::Selection, {path.id, path.id});
    check(!selection_validation.success,
          "TAC validation accepted duplicate selection IDs");
    const auto selected_fix = doc.execute_command(std::make_unique<FixDocumentTacCommand>(
        output, TACScope::Selection, std::vector<ObjectId>{path.id}));
    check(selected_fix.success && selected_fix.tac_summary &&
          selected_fix.tac_summary->scope == "selection" &&
          selected_fix.tac_summary->process_colors_changed == 2,
          "Selection TAC fix changed paint outside the selected object");
    check(doc.undo(), "Selection TAC fix undo failed");

    const auto invalid_revision = doc.revision();
    const auto invalid = doc.execute_command(std::make_unique<FixDocumentTacCommand>(
        color::TACOutputCondition{"", 200.0}));
    check(!invalid.success && doc.revision() == invalid_revision,
          "Invalid output condition changed document history");

    const auto document_validation = validate_document_tac(*doc.current_snapshot(), output);
    check(document_validation.success && document_validation.summary.scope == "document" &&
          document_validation.summary.process_colors_over_limit == 4,
          "Document TAC validation did not include non-active pages");
}

void test_per_page_color_commands() {
    auto page1 = make_document_page("Page 1 - CMYK Print");
    page1.layout.color_mode = color::DocumentColorMode::Cmyk;
    page1.layout.color_profile_name = "U.S. Web Coated (SWOP) v2";
    page1.layout.raster_dpi = 300;
    page1.layout.color_mode_explicit = true;
    page1.layout.raster_dpi_explicit = true;

    PathObject obj1;
    obj1.id = generate_object_id();
    obj1.path = geometry::Path2D::make_rect(10, 10, 50, 40);
    obj1.fill = color::CMYKColor{0.1, 0.2, 0.3, 0.4};
    page1.layout.layers.front().object_ids.push_back(obj1.id);
    page1.objects = page1.objects.set(obj1.id, obj1);

    auto page2 = make_document_page("Page 2 - RGB Web");
    page2.layout.color_mode = color::DocumentColorMode::Rgb;
    page2.layout.color_profile_name = "sRGB IEC61966-2.1";
    page2.layout.raster_dpi = 72;
    page2.layout.color_mode_explicit = true;
    page2.layout.raster_dpi_explicit = false;

    PathObject obj2;
    obj2.id = generate_object_id();
    obj2.path = geometry::Path2D::make_rect(20, 20, 40, 40);
    obj2.fill = color::RGBColor{0.8, 0.2, 0.5};
    page2.layout.layers.front().object_ids.push_back(obj2.id);
    page2.objects = page2.objects.set(obj2.id, obj2);

    const auto id1 = page1.id;
    const auto id2 = page2.id;
    std::vector<DocumentPage> pages;
    pages.push_back(std::move(page1));
    pages.push_back(std::move(page2));

    DocumentSnapshot snap{1, 0, pages, id1, "hybrid-color-doc"};
    check(snap.page_color_mode(id1) == color::DocumentColorMode::Cmyk, "Page 1 mode not CMYK");
    check(snap.page_raster_dpi(id1) == 300, "Page 1 DPI not 300");
    check(snap.page_color_mode(id2) == color::DocumentColorMode::Rgb, "Page 2 mode not RGB");
    check(snap.page_raster_dpi(id2) == 72, "Page 2 DPI not 72");

    Document doc(snap);
    check(doc.color_mode() == color::DocumentColorMode::Cmyk, "Active page 1 mode not CMYK");
    check(doc.set_active_page(id2), "Failed to switch to Page 2");
    check(doc.color_mode() == color::DocumentColorMode::Rgb, "Active page 2 mode not RGB");
    check(doc.raster_dpi() == 72, "Active page 2 DPI not 72");

    const auto rgb_profile = make_test_profile(
        "Embedded RGB profile", color::ColorSpace::Rgb, 0x31);
    const auto profile_assign_revision = doc.revision();
    check(doc.execute_command(std::make_unique<AssignPageColorProfileCommand>(
              id2, rgb_profile)).success,
          "Assigning a matching page ICC resource failed");
    const auto* assigned_page = doc.current_snapshot()->find_page(id2);
    check(assigned_page && assigned_page->layout.color_profile_resource == rgb_profile &&
              assigned_page->layout.color_profile_name == "Embedded RGB profile" &&
              doc.revision() == profile_assign_revision + 1,
          "Page ICC assignment did not bind exact bytes as one document command");
    check(!doc.execute_command(std::make_unique<AssignPageColorProfileCommand>(
              id1, rgb_profile)).success,
          "RGB ICC resource was assigned to a CMYK page");
    check(doc.undo(), "Undo page ICC assignment failed");
    const auto* undone_profile_page = doc.current_snapshot()->find_page(id2);
    check(undone_profile_page && !undone_profile_page->layout.color_profile_resource &&
              undone_profile_page->layout.color_profile_name == "sRGB IEC61966-2.1",
          "Undo did not restore the unresolved page profile state");
    check(doc.redo(), "Redo page ICC assignment failed");
    check(doc.current_snapshot()->find_page(id2)->layout.color_profile_resource == rgb_profile,
          "Redo did not restore the exact immutable ICC resource");

    // Color mode and raster DPI are independent metadata. An explicit mode
    // must not make the implicit default DPI look like an explicit no-op.
    const auto before_default_dpi = doc.revision();
    const auto set_default_dpi = doc.execute_command(
        std::make_unique<SetPageRasterDpiCommand>(id2, 72));
    check(set_default_dpi.success && !set_default_dpi.is_no_change &&
          doc.revision() == before_default_dpi + 1 && doc.page().raster_dpi_explicit,
          "Setting the default page DPI must persist independently of explicit color mode");
    check(doc.undo() && !doc.page().raster_dpi_explicit,
          "Undo must restore the page's implicit DPI state");

    // Test SetPageRasterDpiCommand
    check(doc.execute_command(std::make_unique<SetPageRasterDpiCommand>(id2, 150)).success,
          "SetPageRasterDpiCommand failed");
    check(doc.page_raster_dpi(id2) == 150, "Page 2 DPI not updated to 150");
    check(doc.page_raster_dpi(id1) == 300, "Page 1 DPI mutated unexpectedly");

    check(doc.undo(), "Undo Page 2 DPI failed");
    check(doc.page_raster_dpi(id2) == 72, "Page 2 DPI not restored to 72");

    check(doc.execute_command(std::make_unique<SetPageRasterDpiCommand>(id2, 72)).success &&
          doc.page().raster_dpi_explicit, "Page 2 should have explicit 72 DPI before document-wide edit");
    const auto set_document_dpi = doc.execute_command(
        std::make_unique<SetDocumentRasterDpiCommand>(72));
    check(set_document_dpi.success && !set_document_dpi.is_no_change &&
          doc.page_raster_dpi(id1) == 72 && doc.page_raster_dpi(id2) == 72,
          "Document DPI command must update other pages when the active page already matches");
    check(doc.undo() && doc.page_raster_dpi(id1) == 300 && doc.page_raster_dpi(id2) == 72,
          "Undo must restore every page after document-wide DPI update");

    // Test SetPageColorModeCommand
    check(doc.execute_command(std::make_unique<SetPageColorModeCommand>(
        id2, color::DocumentColorMode::Cmyk)).success, "SetPageColorModeCommand failed");
    check(doc.page_color_mode(id2) == color::DocumentColorMode::Cmyk, "Page 2 mode not converted to CMYK");
    check(doc.page_color_mode(id1) == color::DocumentColorMode::Cmyk, "Page 1 mode corrupted");

    check(doc.undo(), "Undo Page 2 color mode failed");
    check(doc.page_color_mode(id2) == color::DocumentColorMode::Rgb, "Page 2 mode not restored to RGB");
}

void test_page_icc_conversion_command() {
#ifdef _WIN32
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    const auto cmyk_path = srgb_path.parent_path() / L"RSWOP.icm";
    if (srgb_path.empty() || !std::filesystem::is_regular_file(cmyk_path)) {
        std::cout << "[NOT_MEASURED] Page ICC conversion requires installed sRGB and SWOP profiles\n";
        return;
    }
    const auto source = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
    const auto destination = color::ProfileResource::load_file(cmyk_path, color::ColorSpace::Cmyk);
    check(source && destination, "Installed ICC resources failed validation");
    const auto transform_result = color::ProfileColorTransform::create(
        source.resource, destination.resource,
        {color::IccRenderingIntent::Perceptual, false});
    check(static_cast<bool>(transform_result), "Windows CMM could not create an RGB-to-CMYK transform");

    auto page = make_document_page("ICC conversion");
    page.layout.color_mode = color::DocumentColorMode::Rgb;
    page.layout.color_mode_explicit = true;
    page.layout.color_profile_name = source.resource->identity().name;
    page.layout.color_profile_resource = source.resource;
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(5, 5, 25, 18);
    path.fill = color::ColorValue::make_rgb(.17, .42, .83, .37);
    path.fill.set_overprint({true, false});
    path.stroke = color::ColorValue::make_rgb(.8, .3, .1, .6);
    check(path.stroke.set_profile_resource(source.resource),
          "Could not bind test stroke to its explicit source ICC profile");
    path.has_stroke = true;
    path.stroke_width_mm = .4;
    auto gradient = GradientPaint::make_default_linear(allocate_gradient_paint_id(), path.bounds());
    gradient.stops.front().color = color::ColorValue::make_rgb(.1, .2, .3);
    gradient.stops.back().color = color::ColorValue::make_spot("CutContour", .7);
    path.fill_gradient = std::make_shared<const GradientPaint>(std::move(gradient));
    EffectNode shadow;
    shadow.id = generate_effect_instance_id();
    shadow.enabled = false;
    DropShadowEffect shadow_value;
    shadow_value.color = color::ColorValue::make_rgb(.6, .5, .4, .8);
    shadow_value.color.set_overprint({false, true});
    shadow.parameters = shadow_value;
    path.appearance.effects.push_back(shadow);
    page.layout.layers.front().object_ids.push_back(path.id);
    page.objects = page.objects.set(path.id, path);
    TextObject text;
    text.id = generate_object_id();
    text.fill = color::ColorValue::make_rgb(.2, .5, .7, .45);
    page.layout.layers.front().object_ids.push_back(text.id);
    page.objects = page.objects.set(text.id, text);

    const auto page_id = page.id;
    Document doc{DocumentSnapshot{1, 0, {page}, page_id, "page-icc-conversion"}};
    const auto original_fill = path.fill;
    const auto revision = doc.revision();
    const auto converted = doc.execute_command(std::make_unique<ConvertPageColorProfileCommand>(
        page_id, transform_result.transform));
    check(converted.success && !converted.is_no_change && converted.color_conversion &&
          converted.color_conversion->icc_managed == 5 && doc.revision() == revision + 1,
          "Page ICC conversion must convert supported paints in one command");
    const auto* converted_page = doc.current_snapshot()->find_page(page_id);
    const auto* converted_path_value = converted_page->objects.lookup_ref(path.id);
    const auto* converted_path = converted_path_value ? std::get_if<PathObject>(converted_path_value) : nullptr;
    const auto* converted_text_value = converted_page->objects.lookup_ref(text.id);
    const auto* converted_text = converted_text_value ? std::get_if<TextObject>(converted_text_value) : nullptr;
    check(converted_page->layout.color_mode == color::DocumentColorMode::Cmyk &&
          converted_page->layout.color_profile_resource == destination.resource &&
          converted_path && converted_text &&
          converted_path->fill.color_space() == color::ColorSpace::Cmyk &&
          converted_path->stroke.color_space() == color::ColorSpace::Cmyk &&
          converted_path->stroke.profile_resource() == destination.resource &&
          !converted_path->fill.profile_resource() &&
          converted_path->fill_gradient->stops.front().color.color_space() == color::ColorSpace::Cmyk &&
          converted_path->fill_gradient->stops.back().color.color_space() == color::ColorSpace::Spot &&
          converted_text->fill.color_space() == color::ColorSpace::Cmyk,
          "ICC conversion must update process paints and preserve spot paints plus page binding");
    check(converted_path->fill.overprint().fill && !converted_path->fill.overprint().stroke &&
          std::abs(std::get<color::CMYKColor>(converted_path->fill.payload()).a - .37) < 1e-6 &&
          std::get<DropShadowEffect>(converted_path->appearance.effects.front().parameters)
              .color.overprint().stroke,
          "ICC conversion must preserve alpha and overprint on converted paints");
    check(doc.undo() && doc.current_snapshot()->find_page(page_id)->layout.color_profile_resource == source.resource &&
          std::get<color::RGBColor>(std::get<PathObject>(
              *doc.current_snapshot()->find_page(page_id)->objects.lookup_ref(path.id)).fill.payload())
              .approx_equal(std::get<color::RGBColor>(original_fill.payload()), 0.0),
          "One undo must restore exact pre-conversion profile and paint channels");

    const auto wrong_source = make_test_profile("Wrong source", color::ColorSpace::Rgb, 0x41);
    check(doc.execute_command(std::make_unique<AssignPageColorProfileCommand>(page_id, wrong_source)).success,
          "Could not prepare mismatched source-profile regression");
    const auto stale_revision = doc.revision();
    const auto rejected = doc.execute_command(std::make_unique<ConvertPageColorProfileCommand>(
        page_id, transform_result.transform));
    check(!rejected.success && doc.revision() == stale_revision,
          "ICC conversion must reject stale source binding without mutation");
#endif
}

void test_selected_paint_icc_assign_convert_and_undo() {
#ifdef _WIN32
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    const auto cmyk_path = srgb_path.empty() ? std::filesystem::path{} :
        srgb_path.parent_path() / L"RSWOP.icm";
    if (srgb_path.empty() || !std::filesystem::is_regular_file(cmyk_path)) {
        std::cout << "[NOT_MEASURED] Selection ICC conversion requires installed sRGB and SWOP profiles\n";
        return;
    }
    const auto source = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
    const auto destination = color::ProfileResource::load_file(cmyk_path, color::ColorSpace::Cmyk);
    check(source && destination, "Selection ICC resources failed validation");
    const auto created = color::ProfileColorTransform::create(
        source.resource, destination.resource,
        {color::IccRenderingIntent::RelativeColorimetric, false});
    check(static_cast<bool>(created), "Windows CMM could not create a selection ICC transform");

    auto page = make_document_page("Selection ICC");
    page.layout.color_mode = color::DocumentColorMode::Rgb;
    page.layout.color_mode_explicit = true;
    page.layout.color_profile_name = source.resource->identity().name;
    page.layout.color_profile_resource = source.resource;
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(0, 0, 20, 10);
    path.fill = color::ColorValue::make_rgb(.2, .4, .6, .7);
    path.stroke = color::ColorValue::make_rgb(.8, .3, .1, .5);
    path.stroke_width_mm = .3;
    DropShadowEffect shadow;
    shadow.color = color::ColorValue::make_rgb(.6, .5, .4, .8);
    path.appearance.effects.push_back(EffectNode{
        generate_effect_instance_id(), 1, 1, true, shadow});
    page.layout.layers.front().object_ids.push_back(path.id);
    page.objects = page.objects.set(path.id, path);
    TextObject text;
    text.id = generate_object_id();
    text.fill = color::ColorValue::make_rgb(.22, .48, .73, .65);
    page.layout.layers.front().object_ids.push_back(text.id);
    page.objects = page.objects.set(text.id, text);
    const auto page_id = page.id;
    Document doc{DocumentSnapshot{1, 0, {page}, page_id, "selected-paint-icc"}};

    const auto original_fill = path.fill;
    const auto assigned = doc.execute_command(
        std::make_unique<AssignObjectsColorProfileCommand>(
            std::vector<ObjectId>{path.id, text.id}, source.resource));
    check(assigned.success && doc.revision() == 1,
          "Selected paint ICC assignment did not commit once");
    const auto* assigned_path_page = doc.current_snapshot()->find_page(page_id);
    const auto* assigned_path_value = assigned_path_page->objects.lookup_ref(path.id);
    const auto* assigned_path = assigned_path_value
        ? std::get_if<PathObject>(assigned_path_value) : nullptr;
    check(assigned_path && assigned_path->fill.profile_resource() == source.resource &&
              assigned_path->stroke.profile_resource() == source.resource &&
              std::get<color::RGBColor>(assigned_path->fill.payload()).approx_equal(
                  std::get<color::RGBColor>(original_fill.payload()), 0.0),
          "Assigning a selection ICC changed channels or failed to bind its paints");

    const auto converted = doc.execute_command(
        std::make_unique<ConvertObjectsColorProfileCommand>(
            std::vector<ObjectId>{path.id, text.id}, page_id, created.transform));
    check(converted.success && converted.color_conversion &&
              converted.color_conversion->icc_managed == 4 && doc.revision() == 2,
          "Selected paints were not converted together in one command");
    const auto* converted_page = doc.current_snapshot()->find_page(page_id);
    const auto* converted_path_value = converted_page->objects.lookup_ref(path.id);
    const auto* converted_path = converted_path_value
        ? std::get_if<PathObject>(converted_path_value) : nullptr;
    const auto* converted_text_value = converted_page->objects.lookup_ref(text.id);
    const auto* converted_text = converted_text_value
        ? std::get_if<TextObject>(converted_text_value) : nullptr;
    check(converted_page->layout.color_profile_resource == source.resource &&
              converted_path && converted_text &&
              converted_path->fill.color_space() == color::ColorSpace::Cmyk &&
              converted_path->fill.profile_resource() == destination.resource &&
              converted_path->stroke.profile_resource() == destination.resource &&
              converted_text->fill.profile_resource() == destination.resource,
          "Selection conversion did not bind converted paints while keeping the page unchanged");
    check(doc.undo(), "Undo selected ICC conversion failed");
    const auto* after_conversion_undo = doc.current_snapshot()->find_page(page_id);
    const auto* undone_path = std::get_if<PathObject>(
        after_conversion_undo->objects.lookup_ref(path.id));
    check(undone_path && undone_path->fill.profile_resource() == source.resource &&
              undone_path->fill.color_space() == color::ColorSpace::Rgb &&
              doc.undo(),
          "Undo did not restore the assigned source profile exactly");
    const auto* after_assign_undo = doc.current_snapshot()->find_page(page_id);
    const auto* original_path = std::get_if<PathObject>(
        after_assign_undo->objects.lookup_ref(path.id));
    check(original_path && !original_path->fill.profile_resource(),
          "Undo of profile assignment did not restore the inherited page binding");

    const auto wrong_source = make_test_profile(
        "Other selection source", color::ColorSpace::Rgb, 0x72);
    const auto wrong_assignment = doc.execute_command(
        std::make_unique<AssignObjectsColorProfileCommand>(
            std::vector<ObjectId>{path.id}, wrong_source));
    check(wrong_assignment.success, "Could not prepare mismatched paint profile fixture");
    const auto before_rejected_revision = doc.revision();
    const auto rejected = doc.execute_command(
        std::make_unique<ConvertObjectsColorProfileCommand>(
            std::vector<ObjectId>{path.id}, page_id, created.transform));
    check(!rejected.success && doc.revision() == before_rejected_revision,
          "Selection conversion accepted a paint bound to a different source ICC");
#endif
}

void test_document_icc_conversion_command() {
#ifdef _WIN32
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    const auto cmyk_path = srgb_path.empty() ? std::filesystem::path{} :
        srgb_path.parent_path() / L"RSWOP.icm";
    if (srgb_path.empty() || !std::filesystem::is_regular_file(cmyk_path)) {
        std::cout << "[NOT_MEASURED] Document ICC conversion requires installed sRGB and SWOP profiles\n";
        return;
    }
    const auto source = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
    const auto destination = color::ProfileResource::load_file(cmyk_path, color::ColorSpace::Cmyk);
    check(source && destination, "Document ICC resources failed validation");
    const auto transform = color::ProfileColorTransform::create(
        source.resource, destination.resource,
        {color::IccRenderingIntent::RelativeColorimetric, false});
    check(static_cast<bool>(transform), "Windows CMM could not create a document ICC transform");

    auto source_page = make_document_page("Page from sRGB");
    source_page.layout.color_mode = color::DocumentColorMode::Rgb;
    source_page.layout.color_mode_explicit = true;
    source_page.layout.color_profile_name = source.resource->identity().name;
    source_page.layout.color_profile_resource = source.resource;
    TextObject text;
    text.id = generate_object_id();
    text.fill = color::ColorValue::make_rgb(.22, .48, .73, .65);
    source_page.layout.layers.front().object_ids.push_back(text.id);
    source_page.objects = source_page.objects.set(text.id, text);

    auto already_destination = make_document_page("Already SWOP");
    already_destination.layout.color_mode = color::DocumentColorMode::Cmyk;
    already_destination.layout.color_mode_explicit = true;
    already_destination.layout.color_profile_name = destination.resource->identity().name;
    already_destination.layout.color_profile_resource = destination.resource;
    const auto active_page_id = already_destination.id;
    Document doc{DocumentSnapshot{1, 0, {source_page, already_destination},
        active_page_id, "document-icc-conversion"}};
    const auto before_revision = doc.revision();
    const auto converted = doc.execute_command(
        std::make_unique<ConvertDocumentColorProfileCommand>(destination.resource,
            std::vector<std::shared_ptr<const color::ProfileColorTransform>>{transform.transform}));
    check(converted.success && !converted.is_no_change && converted.color_conversion &&
          converted.color_conversion->icc_managed == 1 && doc.revision() == before_revision + 1,
          "Document ICC conversion must process unconverted pages even when the active page is already at destination");
    const auto* converted_source_page = doc.current_snapshot()->find_page(source_page.id);
    const auto* converted_text_value = converted_source_page
        ? converted_source_page->objects.lookup_ref(text.id) : nullptr;
    const auto* converted_text = converted_text_value
        ? std::get_if<TextObject>(converted_text_value) : nullptr;
    check(converted_source_page && converted_text &&
          converted_source_page->layout.color_profile_resource == destination.resource &&
          converted_source_page->layout.color_mode == color::DocumentColorMode::Cmyk &&
          converted_text->fill.color_space() == color::ColorSpace::Cmyk &&
          doc.active_page_id() == active_page_id,
          "Document ICC conversion must update every page binding and preserve active page identity");
    check(doc.undo() && doc.current_snapshot()->find_page(source_page.id)->layout.color_profile_resource == source.resource &&
          std::get<TextObject>(*doc.current_snapshot()->find_page(source_page.id)->objects.lookup_ref(text.id))
              .fill.color_space() == color::ColorSpace::Rgb,
          "One undo must restore all page ICC bindings and source channels");
    check(doc.redo() && doc.current_snapshot()->find_page(source_page.id)->layout.color_profile_resource == destination.resource,
          "Redo must reapply the document ICC conversion");

    auto unresolved_page = make_document_page("Unresolved legacy source");
    const auto unresolved_id = unresolved_page.id;
    Document atomic_doc{DocumentSnapshot{1, 0, {source_page, unresolved_page},
        unresolved_id, "document-icc-atomic-preflight"}};
    const auto atomic_revision = atomic_doc.revision();
    const auto rejected = atomic_doc.execute_command(
        std::make_unique<ConvertDocumentColorProfileCommand>(destination.resource,
            std::vector<std::shared_ptr<const color::ProfileColorTransform>>{transform.transform}));
    check(!rejected.success && atomic_doc.revision() == atomic_revision &&
          atomic_doc.current_snapshot()->find_page(source_page.id)->layout.color_profile_resource == source.resource,
          "An unresolved page must reject document ICC conversion without partially changing earlier pages");

    Document no_op_doc{DocumentSnapshot{1, 0, {already_destination}, active_page_id,
        "document-icc-no-op"}};
    const auto no_op_revision = no_op_doc.revision();
    const auto no_op = no_op_doc.execute_command(
        std::make_unique<ConvertDocumentColorProfileCommand>(destination.resource,
            std::vector<std::shared_ptr<const color::ProfileColorTransform>>{}));
    check(no_op.success && no_op.is_no_change && no_op_doc.revision() == no_op_revision,
          "An already-converted document must remain a true no-op without a transform");
#endif
}
} // namespace

void test_document_color_commands() {
    legacy_v8_color_metadata_migration();
    std::vector<DocumentPage> pages;
    std::vector<ObjectId> unmarked_black_ids;
    for (int p = 0; p < 3; ++p) {
        auto page = make_document_page("Color test");
        page.layout.layers.front().visible = false;
        page.layout.layers.front().locked = true;
        for (int i = 0; i < 100; ++i) {
            PathObject path;
            path.id = generate_object_id();
            path.path = geometry::Path2D::make_rect(i, 1, 5, 4);
            path.fill = color::RGBColor{.123456789, .45, .7, .35};
            path.stroke = color::GrayscaleColor{.2, .6};
            path.fill.set_overprint({true, false});
            path.visible = false;
            path.locked = true;
            auto gradient = GradientPaint::make_default_linear(allocate_gradient_paint_id(), path.bounds());
            gradient.stops.back().color = color::ColorValue::make_spot("CutContour", .65);
            path.fill_gradient = std::make_shared<const GradientPaint>(std::move(gradient));
            EffectNode effect;
            effect.id = generate_effect_instance_id();
            effect.enabled = false;
            effect.parameters = DropShadowEffect{};
            path.appearance.effects.push_back(effect);
            page.layout.layers.front().object_ids.push_back(path.id);
            page.objects = page.objects.set(path.id, path);
        }
        TextObject text;
        text.id = generate_object_id();
        text.text = "Màu tiếng Việt";
        page.layout.layers.front().object_ids.push_back(text.id);
        page.objects = page.objects.set(text.id, text);

        PathObject unmarked_black;
        unmarked_black.id = generate_object_id();
        unmarked_black.path = geometry::Path2D::make_rect(210, 1, 5, 4);
        unmarked_black.fill = color::ColorValue::make_rgb(0, 0, 0);
        unmarked_black.stroke = color::ColorValue::make_rgb(0, 0, 0);
        unmarked_black_ids.push_back(unmarked_black.id);
        page.layout.layers.front().object_ids.push_back(unmarked_black.id);
        page.objects = page.objects.set(unmarked_black.id, unmarked_black);
        pages.push_back(std::move(page));
    }
    DocumentSnapshot input{1, 0, pages, pages[1].id, "color-document"};
    Document doc(input);
    const auto original = doc.current_snapshot();
    const auto original_bytes = NativeFormat::serialize(*original);
    const auto conversion_result = doc.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Cmyk));
    check(conversion_result.success, "Color conversion failed");
    check(conversion_result.color_conversion && conversion_result.color_conversion->approximate > 0,
          "Color conversion did not report its approximate fallback paints");
    check(doc.revision() == 1 && doc.color_mode() == color::DocumentColorMode::Cmyk,
          "Conversion was not one transaction");
    check(doc.raster_dpi() == 72 && doc.active_page_id() == original->active_page_id(),
          "Conversion changed independent DPI or active page");
    all_paints(*doc.current_snapshot(), color::ColorSpace::Cmyk);
    const auto converted_pages = doc.pages();
    for (size_t page_index = 0; page_index < converted_pages.size(); ++page_index) {
        const auto& page = converted_pages[page_index];
        check(page.layout.color_profile_name == "U.S. Web Coated (SWOP) v2", "Profile not synchronized");
        const auto& path = std::get<PathObject>(*page.objects.lookup_ref(page.layout.layers[0].object_ids[0]));
        const auto& spot = std::get<color::SpotColor>(path.fill_gradient->stops.back().color.payload());
        check(spot.spot_name == "CutContour" && spot.tint == .65, "Spot identity/tint lost");
        check(path.fill.overprint().fill && path.fill_gradient->id != 0 && path.locked && !path.visible,
              "Conversion changed object semantics");
        const auto& unmarked_black = std::get<PathObject>(
            *page.objects.lookup_ref(unmarked_black_ids[page_index]));
        check(unmarked_black.fill.color_space() == color::ColorSpace::Cmyk &&
              !unmarked_black.fill.overprint().fill && !unmarked_black.fill.overprint().stroke &&
              !unmarked_black.stroke.overprint().fill && !unmarked_black.stroke.overprint().stroke,
              "Generic conversion forced Pure K overprint onto an unmarked fill/stroke");
    }
    const auto converted = doc.current_snapshot();
    check(doc.undo(), "Conversion undo failed");
    for (size_t p = 0; p < pages.size(); ++p)
        for (const auto& [id, value] : original->pages()[p].objects.all())
            check(doc.pages()[p].objects.lookup_ref(id) == &value,
                  "Undo did not restore exact immutable source objects");
    check(!doc.page().color_mode_explicit && doc.color_profile_name() == "sRGB", "Undo metadata lost");
    check(doc.redo(), "Conversion redo failed");
    check(doc.execute_command(std::make_unique<SetDocumentRasterDpiCommand>(150)).success, "DPI failed");
    check(doc.raster_dpi() == 150 && doc.undo() && doc.raster_dpi() == 72 && doc.redo(), "DPI history failed");
    const auto revision = doc.revision();
    check(!doc.execute_command(std::make_unique<SetDocumentRasterDpiCommand>(0)).success &&
          doc.revision() == revision, "Invalid DPI changed document");
    check(doc.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Cmyk)).is_no_change && doc.revision() == revision, "No-op adds history");

    auto explicit_mode_only = make_document_page("Explicit mode, implicit DPI");
    explicit_mode_only.layout.color_mode = color::DocumentColorMode::Cmyk;
    explicit_mode_only.layout.color_mode_explicit = true;
    explicit_mode_only.layout.raster_dpi = 72;
    explicit_mode_only.layout.raster_dpi_explicit = false;
    const auto explicit_mode_id = explicit_mode_only.id;
    Document explicit_mode_document{DocumentSnapshot{
        1, 0, {explicit_mode_only}, explicit_mode_id, "explicit-mode-implicit-dpi"}};
    const auto document_dpi = explicit_mode_document.execute_command(
        std::make_unique<SetDocumentRasterDpiCommand>(72));
    check(document_dpi.success && !document_dpi.is_no_change &&
          explicit_mode_document.page().raster_dpi_explicit,
          "Document DPI command must not use explicit color mode as its no-op signal");
    check(explicit_mode_document.undo() && !explicit_mode_document.page().raster_dpi_explicit,
          "Document DPI undo must restore implicit DPI metadata");

    // A target-mode active page must not hide pages that still need conversion.
    auto target_page = make_document_page("Already CMYK");
    target_page.layout.color_mode = color::DocumentColorMode::Cmyk;
    target_page.layout.color_mode_explicit = true;
    target_page.layout.color_profile_name = "U.S. Web Coated (SWOP) v2";
    auto pending_page = make_document_page("Needs conversion");
    PathObject pending_path;
    pending_path.id = generate_object_id();
    pending_path.path = geometry::Path2D::make_rect(0, 0, 4, 4);
    pending_path.fill = color::ColorValue::make_rgb(.2, .4, .6);
    pending_page.layout.layers.front().object_ids.push_back(pending_path.id);
    pending_page.objects = pending_page.objects.set(pending_path.id, pending_path);
    DocumentSnapshot mixed_input{1, 0, {target_page, pending_page}, target_page.id, "mixed-pages"};
    Document mixed_pages(mixed_input);
    check(mixed_pages.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Cmyk)).success,
        "Active target-mode page incorrectly short-circuited document conversion");
    const auto* converted_pending = mixed_pages.pages()[1].objects.lookup_ref(pending_path.id);
    check(converted_pending && std::get<PathObject>(*converted_pending).fill.color_space() ==
          color::ColorSpace::Cmyk, "Non-active page was not converted");

    auto wrong_profile_page = make_document_page("Profile identity");
    wrong_profile_page.layout.color_mode = color::DocumentColorMode::Cmyk;
    wrong_profile_page.layout.color_mode_explicit = true;
    wrong_profile_page.layout.color_profile_name = "Another CMYK profile";
    DocumentSnapshot profile_input{1, 0, {wrong_profile_page}, wrong_profile_page.id, "profile-page"};
    Document profile_page(profile_input);
    const auto profile_revision = profile_page.revision();
    check(!profile_page.execute_command(std::make_unique<SetPageColorModeCommand>(
        wrong_profile_page.id, color::DocumentColorMode::Cmyk)).success &&
        profile_page.revision() == profile_revision,
        "Same-mode profile conversion silently relabeled the page");

    auto bytes = NativeFormat::serialize(*doc.current_snapshot());
    std::string error;
    auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    check(reopened && reopened->color_mode() == color::DocumentColorMode::Cmyk &&
          reopened->raster_dpi() == 150 && reopened->page_count() == 3 &&
          reopened->color_profile_name() == "U.S. Web Coated (SWOP) v2", "Native color settings lost");
    const size_t mode_offset = 36 + 4 + doc.id().size() + 7 * 4;
    const auto valid_bytes = bytes;
    auto reject_corrupt = [&](size_t offset, uint8_t value) {
        bytes = valid_bytes; bytes[offset] = value;
        std::fill(bytes.begin() + 32, bytes.begin() + 36, 0);
        const auto crc = NativeFormat::calculate_crc32(bytes.data(), bytes.size());
        for (unsigned i = 0; i < 4; ++i) bytes[32 + i] = static_cast<uint8_t>(crc >> (i * 8));
        check(!NativeFormat::deserialize(bytes.data(), bytes.size(), &error), "Malformed native color settings accepted");
    };
    reject_corrupt(mode_offset, 99);
    reject_corrupt(mode_offset + 1 + 4 + doc.color_profile_name().size(), 0);
    Document fresh(*reopened);
    check(fresh.set_active_page(fresh.pages().front().id), "Page activation failed");
    auto unlocked = fresh.pages();
    for (auto& page : unlocked) page.layout.layers.front().locked = false;
    DocumentSnapshot writable{1, 0, unlocked, unlocked.front().id, "color-writer"};
    Document writer(writable);

    // Single page supports both CMYK and RGB objects simultaneously:
    PathObject cmyk_shape; cmyk_shape.id = generate_object_id();
    cmyk_shape.path = geometry::Path2D::make_rect(1, 1, 2, 2);
    cmyk_shape.fill = color::ColorValue::make_cmyk(0.1, 0.8, 0.9, 0.0);
    check(writer.execute_command(std::make_unique<AddObjectCommand>(cmyk_shape)).success, "Add CMYK shape failed");

    PathObject rgb_shape; rgb_shape.id = generate_object_id();
    rgb_shape.path = geometry::Path2D::make_rect(3, 3, 2, 2);
    rgb_shape.fill = color::ColorValue::make_rgb(0.2, 0.5, 0.8);
    check(writer.execute_command(std::make_unique<AddObjectCommand>(rgb_shape)).success, "Add RGB shape failed");

    const auto* stored_cmyk = writer.objects().lookup_ref(cmyk_shape.id);
    const auto* stored_rgb = writer.objects().lookup_ref(rgb_shape.id);
    check(stored_cmyk && std::get<PathObject>(*stored_cmyk).fill.color_space() == color::ColorSpace::Cmyk,
          "CMYK object lost its CMYK color space on single page");
    check(stored_rgb && std::get<PathObject>(*stored_rgb).fill.color_space() == color::ColorSpace::Rgb,
          "RGB object lost its RGB color space on single page");

    auto mixed_bytes = NativeFormat::serialize(*writer.current_snapshot());
    auto reopened_mixed = NativeFormat::deserialize(mixed_bytes.data(), mixed_bytes.size(), &error);
    check(reopened_mixed != nullptr, "Failed to deserialize mixed-color document");
    const auto* reopened_cmyk = reopened_mixed->objects().lookup_ref(cmyk_shape.id);
    const auto* reopened_rgb = reopened_mixed->objects().lookup_ref(rgb_shape.id);
    check(reopened_cmyk && std::get<PathObject>(*reopened_cmyk).fill.color_space() == color::ColorSpace::Cmyk,
          "CMYK object lost its color space across serialization");
    check(reopened_rgb && std::get<PathObject>(*reopened_rgb).fill.color_space() == color::ColorSpace::Rgb,
          "RGB object lost its color space across serialization");

    check(writer.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Rgb)).success, "Reverse conversion failed");
    all_paints(*writer.current_snapshot(), color::ColorSpace::Rgb);

    // Test converting specific selected objects to CMYK or RGB:
    const auto obj1_id = rgb_shape.id;
    const auto obj2_id = cmyk_shape.id; // converted to RGB above
    check(writer.execute_command(std::make_unique<ConvertObjectsColorModeCommand>(
        std::vector<ObjectId>{obj1_id}, color::DocumentColorMode::Cmyk)).success,
        "Convert selected objects to CMYK failed");
    const auto* converted_obj1 = writer.objects().lookup_ref(obj1_id);
    const auto* untouched_obj2 = writer.objects().lookup_ref(obj2_id);
    check(converted_obj1 && std::get<PathObject>(*converted_obj1).fill.color_space() == color::ColorSpace::Cmyk,
          "Target object was not converted to CMYK");
    check(untouched_obj2 && std::get<PathObject>(*untouched_obj2).fill.color_space() == color::ColorSpace::Rgb,
          "Untouched object was unexpectedly converted");

    check(writer.undo(), "Undo convert objects failed");
    const auto* undone_obj1 = writer.objects().lookup_ref(obj1_id);
    check(undone_obj1 && std::get<PathObject>(*undone_obj1).fill.color_space() == color::ColorSpace::Rgb,
          "Undo did not restore original color space");

    test_explicit_tac_validation_and_fix();
    test_per_page_color_commands();
    test_page_icc_conversion_command();
    test_selected_paint_icc_assign_convert_and_undo();
    test_document_icc_conversion_command();
}


