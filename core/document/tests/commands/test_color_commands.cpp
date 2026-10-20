#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "../../src/storage/byte_codec.hpp"
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::document;
namespace {
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
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
    page2.layout.raster_dpi_explicit = true;

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

    // Test SetPageRasterDpiCommand
    check(doc.execute_command(std::make_unique<SetPageRasterDpiCommand>(id2, 150)).success,
          "SetPageRasterDpiCommand failed");
    check(doc.page_raster_dpi(id2) == 150, "Page 2 DPI not updated to 150");
    check(doc.page_raster_dpi(id1) == 300, "Page 1 DPI mutated unexpectedly");

    check(doc.undo(), "Undo Page 2 DPI failed");
    check(doc.page_raster_dpi(id2) == 72, "Page 2 DPI not restored to 72");

    // Test SetPageColorModeCommand
    check(doc.execute_command(std::make_unique<SetPageColorModeCommand>(
        id2, color::DocumentColorMode::Cmyk)).success, "SetPageColorModeCommand failed");
    check(doc.page_color_mode(id2) == color::DocumentColorMode::Cmyk, "Page 2 mode not converted to CMYK");
    check(doc.page_color_mode(id1) == color::DocumentColorMode::Cmyk, "Page 1 mode corrupted");

    check(doc.undo(), "Undo Page 2 color mode failed");
    check(doc.page_color_mode(id2) == color::DocumentColorMode::Rgb, "Page 2 mode not restored to RGB");
}
} // namespace

void test_document_color_commands() {
    legacy_v8_color_metadata_migration();
    std::vector<DocumentPage> pages;
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
        pages.push_back(std::move(page));
    }
    DocumentSnapshot input{1, 0, pages, pages[1].id, "color-document"};
    Document doc(input);
    const auto original = doc.current_snapshot();
    const auto original_bytes = NativeFormat::serialize(*original);
    check(doc.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Cmyk)).success, "Color conversion failed");
    check(doc.revision() == 1 && doc.color_mode() == color::DocumentColorMode::Cmyk,
          "Conversion was not one transaction");
    check(doc.raster_dpi() == 72 && doc.active_page_id() == original->active_page_id(),
          "Conversion changed independent DPI or active page");
    all_paints(*doc.current_snapshot(), color::ColorSpace::Cmyk);
    for (const auto& page : doc.pages()) {
        check(page.layout.color_profile_name == "U.S. Web Coated (SWOP) v2", "Profile not synchronized");
        const auto& path = std::get<PathObject>(*page.objects.lookup_ref(page.layout.layers[0].object_ids[0]));
        const auto& spot = std::get<color::SpotColor>(path.fill_gradient->stops.back().color.payload());
        check(spot.spot_name == "CutContour" && spot.tint == .65, "Spot identity/tint lost");
        check(path.fill.overprint().fill && path.fill_gradient->id != 0 && path.locked && !path.visible,
              "Conversion changed object semantics");
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

    test_per_page_color_commands();
}


