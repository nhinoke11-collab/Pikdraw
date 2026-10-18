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
}
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
    reject_corrupt(mode_offset, static_cast<uint8_t>(color::DocumentColorMode::Rgb));
    reject_corrupt(mode_offset + 1 + 4 + doc.color_profile_name().size(), 0);
    Document fresh(*reopened);
    // New writes normalize even when introduced by an ordinary module command.
    check(fresh.set_active_page(fresh.pages().front().id), "Page activation failed");
    auto unlocked = fresh.pages();
    for (auto& page : unlocked) page.layout.layers.front().locked = false;
    DocumentSnapshot writable{1, 0, unlocked, unlocked.front().id, "color-writer"};
    Document writer(writable);
    PathObject shape; shape.id = generate_object_id();
    shape.path = geometry::Path2D::make_rect(1, 1, 2, 2);
    check(writer.execute_command(std::make_unique<AddObjectCommand>(shape)).success, "Add failed");
    all_paints(*writer.current_snapshot(), color::ColorSpace::Cmyk);
    check(writer.execute_command(std::make_unique<ConvertDocumentColorModeCommand>(
        color::DocumentColorMode::Rgb)).success, "Reverse conversion failed");
    all_paints(*writer.current_snapshot(), color::ColorSpace::Rgb);
}
