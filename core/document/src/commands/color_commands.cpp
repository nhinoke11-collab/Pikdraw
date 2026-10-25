#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <unordered_map>

namespace pikdraw::document {
namespace {
bool convert_profile_paint(color::ColorValue& paint,
    const color::ProfileColorTransform& transform, color::ColorSpace source_space,
    color::ColorSpace destination_space, bool& changed, std::size_t& conversions,
    bool bind_destination_always = false) {
    if (paint.color_space() == color::ColorSpace::Spot ||
        paint.color_space() == color::ColorSpace::Grayscale)
        return true;
    if (paint.color_space() != source_space)
        return false;
    if (paint.profile_resource() &&
        paint.profile_resource()->identity() != transform.source_profile().identity())
        return false;
    color::ColorValue converted = paint;
    if (!transform.convert(paint, converted) || converted.color_space() != destination_space)
        return false;
    if (bind_destination_always &&
        !converted.set_profile_resource(transform.destination_resource()))
        return false;
    paint = std::move(converted);
    changed = true;
    ++conversions;
    return true;
}

bool convert_object_profile_paints(ObjectVariant& object,
    const color::ProfileColorTransform& transform, color::ColorSpace source_space,
    color::ColorSpace destination_space, bool& changed, std::size_t& conversions,
    bool bind_destination_always = false) {
    bool valid = true;
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
            valid = convert_profile_paint(value.fill, transform, source_space,
                                          destination_space, changed, conversions,
                                          bind_destination_always);
            if constexpr (std::is_same_v<T, PathObject>) {
                valid = valid && convert_profile_paint(value.stroke, transform,
                    source_space, destination_space, changed, conversions,
                    bind_destination_always);
                if (valid && value.fill_gradient) {
                    auto gradient = *value.fill_gradient;
                    bool gradient_changed = false;
                    for (auto& stop : gradient.stops) {
                        if (!convert_profile_paint(stop.color, transform, source_space,
                                destination_space, gradient_changed, conversions,
                                bind_destination_always)) {
                            valid = false;
                            break;
                        }
                    }
                    if (valid && gradient_changed) {
                        value.fill_gradient = std::make_shared<const GradientPaint>(std::move(gradient));
                        changed = true;
                    }
                }
            }
            if (valid) {
                for (auto& effect : value.appearance.effects) {
                    auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters);
                    if (shadow && !convert_profile_paint(shadow->color, transform,
                            source_space, destination_space, changed, conversions,
                            bind_destination_always)) {
                        valid = false;
                        break;
                    }
                }
            }
        }
    }, object);
    return valid;
}

color::DocumentColorMode document_mode_for(color::ColorSpace space) noexcept {
    return space == color::ColorSpace::Cmyk
        ? color::DocumentColorMode::Cmyk : color::DocumentColorMode::Rgb;
}

std::string profile_key(const color::ProfileIdentifier& identity) {
    return std::to_string(static_cast<unsigned>(identity.color_space)) + ":" +
        identity.checksum_sha256;
}
} // namespace

namespace {
void assign_profile(color::ColorValue& paint,
                    const std::shared_ptr<const color::ProfileResource>& resource,
                    bool& changed) {
    if (paint.color_space() == color::ColorSpace::Spot ||
        paint.color_space() != resource->identity().color_space)
        return;
    if (paint.profile_resource() &&
        paint.profile_resource()->identity() == resource->identity())
        return;
    if (!paint.set_profile_resource(resource))
        throw std::invalid_argument("ICC profile color space does not match the selected paint");
    changed = true;
}

void assign_object_profiles(ObjectVariant& object,
    const std::shared_ptr<const color::ProfileResource>& resource, bool& changed) {
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
            assign_profile(value.fill, resource, changed);
            if constexpr (std::is_same_v<T, PathObject>) {
                assign_profile(value.stroke, resource, changed);
                if (value.fill_gradient) {
                    auto gradient = *value.fill_gradient;
                    bool gradient_changed = false;
                    for (auto& stop : gradient.stops)
                        assign_profile(stop.color, resource, gradient_changed);
                    if (gradient_changed) {
                        value.fill_gradient =
                            std::make_shared<const GradientPaint>(std::move(gradient));
                        changed = true;
                    }
                }
            }
            for (auto& effect : value.appearance.effects)
                if (auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters))
                    assign_profile(shadow->color, resource, changed);
        }
    }, object);
}

bool convert_selected_paint(color::ColorValue& paint,
    const std::shared_ptr<const color::ProfileResource>& page_profile,
    const std::shared_ptr<const color::ProfileResource>& destination,
    const std::unordered_map<std::string,
        std::shared_ptr<const color::ProfileColorTransform>>& transforms,
    bool& changed, std::size_t& converted_count) {
    if (paint.color_space() == color::ColorSpace::Spot ||
        paint.color_space() == color::ColorSpace::Grayscale)
        return true;
    if (paint.color_space() != color::ColorSpace::Rgb &&
        paint.color_space() != color::ColorSpace::Cmyk)
        return false;
    const auto source = paint.profile_resource()
        ? paint.profile_resource() : page_profile;
    if (!source)
        return false;
    if (source->identity() == destination->identity())
        return paint.color_space() == destination->identity().color_space;
    if (paint.color_space() != source->identity().color_space)
        return false;
    const auto transform = transforms.find(profile_key(source->identity()));
    if (transform == transforms.end())
        return false;
    color::ColorValue converted = paint;
    if (!transform->second->convert(paint, converted) ||
        converted.color_space() != destination->identity().color_space ||
        !converted.set_profile_resource(destination))
        return false;
    paint = std::move(converted);
    changed = true;
    ++converted_count;
    return true;
}

bool convert_selected_object_profiles(ObjectVariant& object,
    const std::shared_ptr<const color::ProfileResource>& page_profile,
    const std::shared_ptr<const color::ProfileResource>& destination,
    const std::unordered_map<std::string,
        std::shared_ptr<const color::ProfileColorTransform>>& transforms,
    bool& changed, std::size_t& converted_count) {
    bool valid = true;
    std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PathObject> || std::is_same_v<T, TextObject>) {
            valid = convert_selected_paint(value.fill, page_profile, destination,
                transforms, changed, converted_count);
            if constexpr (std::is_same_v<T, PathObject>) {
                valid = valid && convert_selected_paint(value.stroke, page_profile,
                    destination, transforms, changed, converted_count);
                if (valid && value.fill_gradient) {
                    auto gradient = *value.fill_gradient;
                    bool gradient_changed = false;
                    for (auto& stop : gradient.stops) {
                        if (!convert_selected_paint(stop.color, page_profile,
                                destination, transforms, gradient_changed,
                                converted_count)) {
                            valid = false;
                            break;
                        }
                    }
                    if (valid && gradient_changed) {
                        value.fill_gradient =
                            std::make_shared<const GradientPaint>(std::move(gradient));
                        changed = true;
                    }
                }
            }
            if (valid) {
                for (auto& effect : value.appearance.effects) {
                    auto* shadow = std::get_if<DropShadowEffect>(&effect.parameters);
                    if (shadow && !convert_selected_paint(shadow->color, page_profile,
                            destination, transforms, changed, converted_count)) {
                        valid = false;
                        break;
                    }
                }
            }
        }
    }, object);
    return valid;
}
} // namespace

bool normalize_object_colors(ObjectVariant& object, color::DocumentColorMode mode,
                             const color::IccPreview* icc,
                             color::ColorConversionSummary* summary) {
    bool changed = false;
    auto convert = [&](color::ColorValue& value) {
        if (value.color_space() == color::ColorSpace::Spot ||
            (mode == color::DocumentColorMode::Cmyk && value.color_space() == color::ColorSpace::Cmyk) ||
            (mode == color::DocumentColorMode::Rgb && value.color_space() == color::ColorSpace::Rgb))
            return;
        auto report = color::convert_color_to_mode_report(value, mode, icc);
        if (summary) summary->record(report);
        if (report.changed) {
            value = std::move(report.color);
            changed = true;
        }
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

bool page_colors_match_mode(const DocumentPage& page, color::DocumentColorMode mode) {
    for (const auto& [id, object] : page.objects.all()) {
        (void)id;
        if (!object_colors_match_mode(object, mode))
            return false;
    }
    return true;
}

PersistentObjectMap normalize_process_colors(const PersistentObjectMap& source,
    color::DocumentColorMode mode, const color::IccPreview* icc,
    color::ColorConversionSummary* summary) {
    auto result = source;
    for (const auto& [id, value] : source.all()) {
        // Raster bytes, ICC provenance and group hierarchy are never copied or converted.
        if (object_colors_match_mode(value, mode)) continue;
        auto converted = value;
        if (normalize_object_colors(converted, mode, icc, summary))
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
    const bool every_page_is_current = std::all_of(snapshot->pages().begin(), snapshot->pages().end(),
        [&](const DocumentPage& page) {
            return page.layout.color_mode_explicit && page.layout.color_mode == mode_ &&
                page.layout.color_profile_name == profile_ && page_colors_match_mode(page, mode_);
        });
    if (every_page_is_current) return CommandResult::no_change();
    // This API selects the mode's default profile. Reassigning a profile in the
    // same mode needs a separate ICC profile conversion, not a metadata-only edit.
    auto pages = doc.pages();
    if (std::any_of(pages.begin(), pages.end(), [&](const DocumentPage& page) {
            return page.layout.color_mode_explicit && page.layout.color_mode == mode_ &&
                page.layout.color_profile_name != profile_;
        }))
        return CommandResult::fail("Same-mode ICC profile reassignment is not supported");
    color::ColorConversionSummary summary;
    for (auto& page : pages) {
        page.objects = normalize_process_colors(page.objects, mode_, icc_.get(), &summary);
        if (page.layout.color_mode != mode_ || page.layout.color_profile_name != profile_)
            page.layout.color_profile_resource.reset();
        page.layout.color_mode = mode_;
        page.layout.color_profile_name = profile_;
        page.layout.color_mode_explicit = true;
        // DPI is independent of color mode; conversion retains the user's setting.
    }
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok(summary);
}
CommandResult ConvertDocumentColorModeCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No color conversion to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

CommandResult SetDocumentRasterDpiCommand::execute(Document& doc) {
    if (dpi_ != 72 && dpi_ != 150 && dpi_ != 300)
        return CommandResult::fail("Raster effects DPI must be 72, 150 or 300");
    const auto snapshot = doc.current_snapshot();
    const bool every_page_is_current = std::all_of(snapshot->pages().begin(), snapshot->pages().end(),
        [this](const DocumentPage& page) {
            return page.layout.raster_dpi == dpi_ && page.layout.raster_dpi_explicit;
        });
    if (every_page_is_current)
        return CommandResult::no_change();
    before_ = snapshot;
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

SetPageColorModeCommand::SetPageColorModeCommand(PageId page_id, color::DocumentColorMode mode,
    std::string profile, std::shared_ptr<const color::IccPreview> icc)
    : page_id_(page_id), mode_(mode),
      profile_(profile.empty() ? color::default_document_profile(mode) : std::move(profile)),
      icc_(std::move(icc)) {}

CommandResult SetPageColorModeCommand::execute(Document& doc) {
    if (!color::valid_document_color_mode(mode_) || profile_.empty() || profile_.size() > 256)
        return CommandResult::fail("Invalid page color mode or profile");
    const auto snapshot = doc.current_snapshot();
    const auto* target_page = snapshot->find_page(page_id_);
    if (!target_page) return CommandResult::fail("Page not found");
    if (target_page->layout.color_mode_explicit && target_page->layout.color_mode == mode_ &&
        target_page->layout.color_profile_name != profile_)
        return CommandResult::fail("Same-mode ICC profile reassignment is not supported");
    if (target_page->layout.color_mode_explicit && target_page->layout.color_mode == mode_ &&
        target_page->layout.color_profile_name == profile_ && page_colors_match_mode(*target_page, mode_))
        return CommandResult::no_change();
    auto pages = doc.pages();
    auto it = std::find_if(pages.begin(), pages.end(), [this](const auto& p) { return p.id == page_id_; });
    if (it == pages.end()) return CommandResult::fail("Page not found in document");
    color::ColorConversionSummary summary;
    it->objects = normalize_process_colors(it->objects, mode_, icc_.get(), &summary);
    if (it->layout.color_mode != mode_ || it->layout.color_profile_name != profile_)
        it->layout.color_profile_resource.reset();
    it->layout.color_mode = mode_;
    it->layout.color_profile_name = profile_;
    it->layout.color_mode_explicit = true;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok(summary);
}

CommandResult SetPageColorModeCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No page color change to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

AssignPageColorProfileCommand::AssignPageColorProfileCommand(
    PageId page_id, std::shared_ptr<const color::ProfileResource> resource)
    : page_id_(page_id), resource_(std::move(resource)) {}

CommandResult AssignPageColorProfileCommand::execute(Document& doc) {
    if (!resource_)
        return CommandResult::fail("ICC profile resource is missing");
    const auto snapshot = doc.current_snapshot();
    const auto* target_page = snapshot->find_page(page_id_);
    if (!target_page)
        return CommandResult::fail("Page not found");
    const auto expected_space = target_page->layout.color_mode == color::DocumentColorMode::Cmyk
        ? color::ColorSpace::Cmyk : color::ColorSpace::Rgb;
    if (resource_->identity().color_space != expected_space)
        return CommandResult::fail("ICC profile color space does not match the page color mode");
    if (target_page->layout.color_profile_resource &&
        target_page->layout.color_profile_resource->identity() == resource_->identity() &&
        target_page->layout.color_profile_name == resource_->identity().name &&
        target_page->layout.color_mode_explicit)
        return CommandResult::no_change();

    auto pages = doc.pages();
    auto it = std::find_if(pages.begin(), pages.end(), [this](const auto& page) {
        return page.id == page_id_;
    });
    if (it == pages.end())
        return CommandResult::fail("Page not found in document");
    it->layout.color_profile_name = resource_->identity().name;
    it->layout.color_profile_resource = resource_;
    it->layout.color_mode_explicit = true;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}

CommandResult AssignPageColorProfileCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No page profile assignment to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

AssignObjectsColorProfileCommand::AssignObjectsColorProfileCommand(
    std::vector<ObjectId> object_ids,
    std::shared_ptr<const color::ProfileResource> resource)
    : object_ids_(std::move(object_ids)), resource_(std::move(resource)) {}

CommandResult AssignObjectsColorProfileCommand::execute(Document& doc) {
    if (!resource_)
        return CommandResult::fail("ICC profile resource is missing");
    if (resource_->identity().color_space == color::ColorSpace::Spot)
        return CommandResult::fail("An ICC profile cannot be assigned to a spot paint");
    if (object_ids_.empty())
        return CommandResult::no_change();
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return CommandResult::fail("Document is unavailable");
    auto objects = snapshot->objects();
    bool any_changed = false;
    for (const auto id : object_ids_) {
        const auto* existing = objects.lookup_ref(id);
        if (!existing)
            return CommandResult::fail("A selected object is no longer on the active page");
        auto replacement = *existing;
        bool changed = false;
        assign_object_profiles(replacement, resource_, changed);
        if (changed) {
            objects = objects.set(id, std::move(replacement));
            any_changed = true;
        }
    }
    if (!any_changed)
        return CommandResult::no_change();
    before_ = snapshot;
    doc.apply_mutation(snapshot->page(), std::move(objects));
    return CommandResult::ok();
}

CommandResult AssignObjectsColorProfileCommand::undo(Document& doc) {
    if (!before_)
        return CommandResult::fail("No selected paint ICC assignment to undo");
    doc.apply_mutation(before_->page(), before_->objects());
    return CommandResult::ok();
}

ConvertPageColorProfileCommand::ConvertPageColorProfileCommand(PageId page_id,
    std::shared_ptr<const color::ProfileColorTransform> transform)
    : page_id_(page_id), transform_(std::move(transform)) {}

CommandResult ConvertPageColorProfileCommand::execute(Document& doc) {
    if (!transform_)
        return CommandResult::fail("ICC color transform is missing");
    const auto snapshot = doc.current_snapshot();
    const auto* target_page = snapshot->find_page(page_id_);
    if (!target_page)
        return CommandResult::fail("Page not found");
    const auto& source_profile = transform_->source_profile();
    const auto& destination_profile = transform_->destination_profile();
    const auto source_space = source_profile.identity().color_space;
    const auto destination_space = destination_profile.identity().color_space;
    if ((source_space != color::ColorSpace::Rgb && source_space != color::ColorSpace::Cmyk) ||
        (destination_space != color::ColorSpace::Rgb && destination_space != color::ColorSpace::Cmyk))
        return CommandResult::fail("Page ICC conversion supports RGB and CMYK profiles only");
    if (target_page->layout.color_profile_resource &&
        target_page->layout.color_profile_resource->identity() == destination_profile.identity() &&
        target_page->layout.color_mode == document_mode_for(destination_space))
        return CommandResult::no_change();
    if (!target_page->layout.color_mode_explicit || !target_page->layout.color_profile_resource ||
        target_page->layout.color_profile_resource->identity() != source_profile.identity() ||
        target_page->layout.color_mode != document_mode_for(source_space))
        return CommandResult::fail("Page source ICC binding does not match the transform source profile");

    auto pages = doc.pages();
    auto page = std::find_if(pages.begin(), pages.end(), [this](const auto& value) {
        return value.id == page_id_;
    });
    if (page == pages.end())
        return CommandResult::fail("Page not found in document");

    color::ColorConversionSummary summary;
    std::size_t converted_paint_count = 0;
    auto converted_objects = page->objects;
    for (const auto& [id, value] : page->objects.all()) {
        auto converted = value;
        bool changed = false;
        if (!convert_object_profile_paints(converted, *transform_, source_space,
                                           destination_space, changed, converted_paint_count))
            return CommandResult::fail("Page contains a process paint that does not match its source ICC profile");
        if (changed)
            converted_objects = converted_objects.set(id, std::move(converted));
    }
    page->objects = std::move(converted_objects);
    page->layout.color_mode = document_mode_for(destination_space);
    page->layout.color_mode_explicit = true;
    page->layout.color_profile_name = destination_profile.identity().name;
    page->layout.color_profile_resource = transform_->destination_resource();
    summary.icc_managed = converted_paint_count;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok(summary);
}

CommandResult ConvertPageColorProfileCommand::undo(Document& doc) {
    if (!before_)
        return CommandResult::fail("No page ICC conversion to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

ConvertObjectsColorProfileCommand::ConvertObjectsColorProfileCommand(
    std::vector<ObjectId> object_ids, PageId page_id,
    std::shared_ptr<const color::ProfileColorTransform> transform)
    : object_ids_(std::move(object_ids)), page_id_(page_id),
      destination_(transform ? transform->destination_resource() : nullptr) {
    if (transform)
        transforms_.push_back(std::move(transform));
}

ConvertObjectsColorProfileCommand::ConvertObjectsColorProfileCommand(
    std::vector<ObjectId> object_ids, PageId page_id,
    std::shared_ptr<const color::ProfileResource> destination,
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms)
    : object_ids_(std::move(object_ids)), page_id_(page_id),
      destination_(std::move(destination)), transforms_(std::move(transforms)) {}

CommandResult ConvertObjectsColorProfileCommand::execute(Document& doc) {
    if (!destination_ ||
        (destination_->identity().color_space != color::ColorSpace::Rgb &&
         destination_->identity().color_space != color::ColorSpace::Cmyk))
        return CommandResult::fail("A resolved RGB or CMYK destination ICC profile is required");
    if (object_ids_.empty())
        return CommandResult::no_change();
    std::unordered_map<std::string,
        std::shared_ptr<const color::ProfileColorTransform>> by_source;
    by_source.reserve(transforms_.size());
    for (const auto& transform : transforms_) {
        if (!transform || transform->destination_profile().identity() != destination_->identity())
            return CommandResult::fail("Every selection transform must use the destination profile");
        const auto source_space = transform->source_profile().identity().color_space;
        if (source_space != color::ColorSpace::Rgb && source_space != color::ColorSpace::Cmyk)
            return CommandResult::fail("Selected ICC conversion supports RGB and CMYK sources only");
        if (!by_source.emplace(profile_key(transform->source_profile().identity()), transform).second)
            return CommandResult::fail("Duplicate source ICC transform supplied");
    }
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return CommandResult::fail("Document is unavailable");
    const auto* page = snapshot->find_page(page_id_);
    if (!page || page_id_ != snapshot->active_page_id())
        return CommandResult::fail("Selected paint page is no longer active");
    auto objects = page->objects;
    std::size_t converted_count = 0;
    for (const auto id : object_ids_) {
        const auto* existing = objects.lookup_ref(id);
        if (!existing)
            return CommandResult::fail("A selected object is no longer on the active page");
        auto replacement = *existing;
        bool changed = false;
        if (!convert_selected_object_profiles(replacement,
                page->layout.color_profile_resource, destination_, by_source,
                changed, converted_count))
            return CommandResult::fail(
                "A selected process paint has no matching source ICC transform");
        if (changed)
            objects = objects.set(id, std::move(replacement));
    }
    if (converted_count == 0)
        return CommandResult::no_change();
    color::ColorConversionSummary summary;
    summary.icc_managed = converted_count;
    before_ = snapshot;
    doc.apply_mutation(page->layout, std::move(objects));
    return CommandResult::ok(summary);
}

CommandResult ConvertObjectsColorProfileCommand::undo(Document& doc) {
    if (!before_)
        return CommandResult::fail("No selected paint ICC conversion to undo");
    doc.apply_mutation(before_->page(), before_->objects());
    return CommandResult::ok();
}

ConvertDocumentColorProfileCommand::ConvertDocumentColorProfileCommand(
    std::shared_ptr<const color::ProfileResource> destination,
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms)
    : destination_(std::move(destination)), transforms_(std::move(transforms)) {}

CommandResult ConvertDocumentColorProfileCommand::execute(Document& doc) {
    if (!destination_ ||
        (destination_->identity().color_space != color::ColorSpace::Rgb &&
         destination_->identity().color_space != color::ColorSpace::Cmyk))
        return CommandResult::fail("A resolved RGB or CMYK destination ICC profile is required");

    std::unordered_map<std::string,
        std::shared_ptr<const color::ProfileColorTransform>> by_source;
    by_source.reserve(transforms_.size());
    std::optional<color::IccTransformOptions> common_options;
    for (const auto& transform : transforms_) {
        if (!transform || transform->destination_profile().identity() != destination_->identity())
            return CommandResult::fail("Every page transform must use the requested destination profile");
        const auto options = transform->options();
        if (common_options && (options.intent != common_options->intent ||
                               options.black_point_compensation != common_options->black_point_compensation))
            return CommandResult::fail("Document conversion must use one ICC policy for every page");
        common_options = options;
        const auto key = profile_key(transform->source_profile().identity());
        if (!by_source.emplace(key, transform).second)
            return CommandResult::fail("Duplicate source ICC transform supplied");
    }

    const auto snapshot = doc.current_snapshot();
    if (!snapshot || snapshot->pages().empty())
        return CommandResult::fail("Document has no pages to convert");
    auto pages = doc.pages();
    std::size_t converted_pages = 0;
    std::size_t converted_paints = 0;

    // Build every replacement in local snapshots first. Any unresolved page,
    // unsupported paint, or stale transform rejects the whole operation.
    for (auto& page : pages) {
        if (page.layout.color_profile_resource &&
            page.layout.color_profile_resource->identity() == destination_->identity() &&
            page.layout.color_mode == document_mode_for(destination_->identity().color_space) &&
            page.layout.color_profile_name == destination_->identity().name &&
            page.layout.color_mode_explicit)
            continue;
        if (page.layout.color_profile_resource &&
            page.layout.color_profile_resource->identity() == destination_->identity()) {
            if (page.layout.color_mode != document_mode_for(destination_->identity().color_space))
                return CommandResult::fail("Page color mode does not match its bound destination ICC profile");
            page.layout.color_profile_name = destination_->identity().name;
            page.layout.color_mode_explicit = true;
            ++converted_pages;
            continue;
        }
        if (!page.layout.color_profile_resource)
            return CommandResult::fail("Every page needs a resolved source ICC profile before document conversion");

        const auto& source = page.layout.color_profile_resource;
        const auto transform_it = by_source.find(profile_key(source->identity()));
        if (transform_it == by_source.end())
            return CommandResult::fail("A page source ICC profile has no matching destination transform");
        const auto& transform = *transform_it->second;
        const auto source_space = source->identity().color_space;
        const auto destination_space = destination_->identity().color_space;
        if (page.layout.color_mode != document_mode_for(source_space))
            return CommandResult::fail("Page color mode does not match its bound source ICC profile");

        auto converted_objects = page.objects;
        std::size_t page_paints = 0;
        for (const auto& [id, object] : page.objects.all()) {
            auto converted = object;
            bool changed = false;
            if (!convert_object_profile_paints(converted, transform, source_space,
                    destination_space, changed, page_paints))
                return CommandResult::fail("A page contains a process paint that does not match its source ICC profile");
            if (changed)
                converted_objects = converted_objects.set(id, std::move(converted));
        }
        page.objects = std::move(converted_objects);
        page.layout.color_mode = document_mode_for(destination_space);
        page.layout.color_mode_explicit = true;
        page.layout.color_profile_name = destination_->identity().name;
        page.layout.color_profile_resource = destination_;
        converted_paints += page_paints;
        ++converted_pages;
    }

    if (converted_pages == 0)
        return CommandResult::no_change();

    color::ColorConversionSummary summary;
    summary.icc_managed = converted_paints;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), snapshot->active_page_id());
    return CommandResult::ok(summary);
}

CommandResult ConvertDocumentColorProfileCommand::undo(Document& doc) {
    if (!before_)
        return CommandResult::fail("No document ICC conversion to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

CommandResult SetPageRasterDpiCommand::execute(Document& doc) {
    if (dpi_ != 72 && dpi_ != 150 && dpi_ != 300)
        return CommandResult::fail("Raster effects DPI must be 72, 150 or 300");
    const auto snapshot = doc.current_snapshot();
    const auto* target_page = snapshot->find_page(page_id_);
    if (!target_page) return CommandResult::fail("Page not found");
    if (target_page->layout.raster_dpi == dpi_ && target_page->layout.raster_dpi_explicit)
        return CommandResult::no_change();
    auto pages = doc.pages();
    auto it = std::find_if(pages.begin(), pages.end(), [this](const auto& p) { return p.id == page_id_; });
    if (it == pages.end()) return CommandResult::fail("Page not found in document");
    it->layout.raster_dpi = dpi_;
    it->layout.raster_dpi_explicit = true;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}

CommandResult SetPageRasterDpiCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No page raster DPI change to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

ConvertObjectsColorModeCommand::ConvertObjectsColorModeCommand(std::vector<ObjectId> object_ids,
    color::DocumentColorMode mode, std::string profile, std::shared_ptr<const color::IccPreview> icc)
    : object_ids_(std::move(object_ids)), mode_(mode),
      profile_(profile.empty() ? color::default_document_profile(mode) : std::move(profile)),
      icc_(std::move(icc)) {}

CommandResult ConvertObjectsColorModeCommand::execute(Document& doc) {
    if (!color::valid_document_color_mode(mode_))
        return CommandResult::fail("Invalid target color mode");
    if (object_ids_.empty())
        return CommandResult::no_change();

    auto objects = doc.objects();
    bool any_changed = false;
    color::ColorConversionSummary summary;
    for (const auto id : object_ids_) {
        const auto* existing = objects.lookup_ref(id);
        if (!existing) continue;
        if (object_colors_match_mode(*existing, mode_)) continue;
        auto converted = *existing;
        if (normalize_object_colors(converted, mode_, icc_.get(), &summary)) {
            objects = objects.set(id, std::move(converted));
            any_changed = true;
        }
    }
    if (!any_changed) return CommandResult::no_change();
    before_ = doc.current_snapshot();
    doc.apply_mutation(doc.page(), std::move(objects));
    return CommandResult::ok(summary);
}

CommandResult ConvertObjectsColorModeCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No object color conversion to undo");
    doc.apply_mutation(before_->page(), before_->objects());
    return CommandResult::ok();
}
} // namespace pikdraw::document
