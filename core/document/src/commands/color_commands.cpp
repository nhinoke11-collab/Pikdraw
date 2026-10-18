#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/document/document.hpp"

namespace pikdraw::document {
bool normalize_object_colors(ObjectVariant& object, color::DocumentColorMode mode,
                             const color::IccPreview* icc) {
    bool changed = false;
    auto convert = [&](color::ColorValue& value) {
        if (value.color_space() == color::ColorSpace::Spot ||
            (mode == color::DocumentColorMode::Cmyk && value.color_space() == color::ColorSpace::Cmyk) ||
            (mode == color::DocumentColorMode::Rgb && value.color_space() == color::ColorSpace::Rgb))
            return;
        value = color::convert_color_to_mode(value, mode, icc);
        changed = true;
    };
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
            convert(value.fill);
            if constexpr (std::is_same_v<T, PathObject>) {
                convert(value.stroke); // Also normalize disabled paints for later re-enabling.
                if (value.fill_gradient) {
                    auto gradient = *value.fill_gradient;
                    bool stops_changed = false;
                    for (auto& stop : gradient.stops) {
                        const auto space = stop.color.color_space();
                        convert(stop.color);
                        stops_changed |= space != stop.color.color_space();
                    }
                    if (stops_changed) value.fill_gradient =
                        std::make_shared<const GradientPaint>(std::move(gradient));
                }
            }
            for (auto& effect : value.appearance.effects)
                if (auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters))
                    convert(shadow->color); // Disabled effects included.
        }
    }, object);
    return changed;
}

bool object_colors_match_mode(const ObjectVariant& value, color::DocumentColorMode mode) {
        const auto needs_conversion = [mode](const color::ColorValue& paint) {
            return paint.color_space() != color::ColorSpace::Spot &&
                paint.color_space() != (mode == color::DocumentColorMode::Cmyk
                    ? color::ColorSpace::Cmyk : color::ColorSpace::Rgb);
        };
        const bool needed = std::visit([&](const auto& object) {
            using T = std::decay_t<decltype(object)>;
            if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
                if (needs_conversion(object.fill)) return true;
                if constexpr (std::is_same_v<T, PathObject>) {
                    if (needs_conversion(object.stroke)) return true;
                    if (object.fill_gradient)
                        for (const auto& stop : object.fill_gradient->stops)
                            if (needs_conversion(stop.color)) return true;
                }
                for (const auto& effect : object.appearance.effects)
                    if (const auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters))
                        if (needs_conversion(shadow->color)) return true;
            }
            return false;
        }, value);
        return !needed;
}

PersistentObjectMap normalize_process_colors(const PersistentObjectMap& source,
    color::DocumentColorMode mode, const color::IccPreview* icc) {
    auto result = source;
    for (const auto& [id, value] : source.all()) {
        // Raster bytes, ICC provenance and group hierarchy are never copied or converted.
        if (object_colors_match_mode(value, mode)) continue;
        auto converted = value;
        if (normalize_object_colors(converted, mode, icc))
            result = result.set(id, std::move(converted));
    }
    return result;
}

ConvertDocumentColorModeCommand::ConvertDocumentColorModeCommand(color::DocumentColorMode mode,
    std::string profile, std::shared_ptr<const color::IccPreview> icc)
    : mode_(mode), profile_(profile.empty() ? color::default_document_profile(mode) : std::move(profile)),
      icc_(std::move(icc)) {}

CommandResult ConvertDocumentColorModeCommand::execute(Document& doc) {
    if (!color::valid_document_color_mode(mode_) || profile_.empty() || profile_.size() > 256)
        return CommandResult::fail("Invalid document color mode or profile");
    const auto snapshot = doc.current_snapshot();
    if (snapshot->page().color_mode_explicit && snapshot->color_mode() == mode_ &&
        snapshot->color_profile_name() == profile_)
        return CommandResult::no_change();
    // This API selects the mode's default profile. Reassigning a profile in the
    // same mode needs a separate ICC profile conversion, not a metadata-only edit.
    if (snapshot->page().color_mode_explicit && snapshot->color_mode() == mode_ &&
        snapshot->color_profile_name() != profile_)
        return CommandResult::fail("Same-mode ICC profile reassignment is not supported");
    auto pages = doc.pages();
    for (auto& page : pages) {
        page.objects = normalize_process_colors(page.objects, mode_, icc_.get());
        page.layout.color_mode = mode_;
        page.layout.color_profile_name = profile_;
        page.layout.color_mode_explicit = true;
        // DPI is independent of color mode; conversion retains the user's setting.
    }
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}
CommandResult ConvertDocumentColorModeCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No color conversion to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

CommandResult SetDocumentRasterDpiCommand::execute(Document& doc) {
    if (dpi_ != 72 && dpi_ != 150 && dpi_ != 300)
        return CommandResult::fail("Raster effects DPI must be 72, 150 or 300");
    if (doc.raster_dpi() == dpi_ && (doc.page().color_mode_explicit || doc.page().raster_dpi_explicit))
        return CommandResult::no_change();
    before_ = doc.current_snapshot();
    auto pages = doc.pages();
    for (auto& page : pages) {
        page.layout.raster_dpi = dpi_;
        page.layout.raster_dpi_explicit = true;
    }
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}
CommandResult SetDocumentRasterDpiCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No raster DPI change to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}
} // namespace pikdraw::document
