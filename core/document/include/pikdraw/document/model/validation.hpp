#pragma once
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document {

inline void validate_page(const Page& page) {
    if (!color::valid_document_color_mode(page.color_mode) ||
        page.color_profile_name.empty() || page.color_profile_name.size() > 256 ||
        (page.raster_dpi != 72 && page.raster_dpi != 150 && page.raster_dpi != 300))
        throw std::invalid_argument("Invalid document color settings or raster effects DPI");
    if (!std::isfinite(page.width_mm) || !std::isfinite(page.height_mm) ||
        page.width_mm <= 0 || page.height_mm <= 0 ||
        page.width_mm > geometry::limits::kCanvasMaxMm ||
        page.height_mm > geometry::limits::kCanvasMaxMm)
        throw std::invalid_argument("Page dimensions must be positive finite millimeters within canvas limits");
}

inline void validate_geometry_path(const geometry::Path2D& path) {
    using geometry::GeometryStatus;
    for (const auto& element : path.elements()) {
        if (geometry::validate_point(element.pt) != GeometryStatus::Ok ||
            geometry::validate_point(element.ctrl1) != GeometryStatus::Ok ||
            geometry::validate_point(element.ctrl2) != GeometryStatus::Ok ||
            !std::isfinite(element.rx) || !std::isfinite(element.ry) ||
            !std::isfinite(element.rotation_deg) || element.rx < 0 || element.ry < 0)
            throw std::invalid_argument("Invalid or out-of-range source path coordinate");
    }
}

inline void validate_path_source(const PathObject& object) {
    if (!object.fill.is_valid() || !object.stroke.is_valid() ||
        !std::isfinite(object.stroke_width_mm) || object.stroke_width_mm < 0)
        throw std::invalid_argument("Invalid source color or stroke width");
    validate_geometry_path(object.path);
    if (object.fill_gradient) {
        if (const auto error = object.fill_gradient->validation_error()) {
            throw std::invalid_argument("Invalid gradient fill: " + *error);
        }
    }
    if (object.authoring_source) {
        if (const auto error = object.authoring_source->validation_error())
            throw std::invalid_argument(*error);
        if (!editable_path_matches_adapter(*object.authoring_source, object.path))
            throw std::invalid_argument("Editable path and derived geometry adapter disagree");
        for (const auto& subpath : object.authoring_source->subpaths) {
            if (subpath.id.value() == object.id)
                throw std::invalid_argument("Subpath ID collides with its object ID");
            for (const auto& node : subpath.nodes)
                if (node.id.value() == object.id)
                    throw std::invalid_argument("Node ID collides with its object ID");
            for (const auto& segment : subpath.segments)
                if (segment.id.value() == object.id)
                    throw std::invalid_argument("Segment ID collides with its object ID");
        }
    }
}

inline void validate_text_source(const TextObject& object) {
    if (!std::isfinite(object.frame_width_mm) || object.frame_width_mm < 0 ||
        object.frame_width_mm > geometry::limits::kCanvasMaxMm ||
        !std::isfinite(object.line_spacing_ratio) || object.line_spacing_ratio < 0.25 ||
        object.line_spacing_ratio > 10 ||
        (object.alignment != typography::TextAlignment::Left &&
         object.alignment != typography::TextAlignment::Center &&
         object.alignment != typography::TextAlignment::Right))
        throw std::invalid_argument("Invalid paragraph width, spacing or alignment");
    if (!object.fill.is_valid() || object.font.family_name.empty() ||
        !std::isfinite(object.font.size_pt) || object.font.size_pt <= 0 ||
        object.font.size_mm() > geometry::limits::kCanvasMaxMm ||
        object.font.weight == 0 || object.font.weight > 999 ||
        geometry::validate_point(object.position) != geometry::GeometryStatus::Ok)
        throw std::invalid_argument("Invalid text color, font properties or source position");
}

inline void validate_appearance(const Appearance& appearance) {
    const auto normalized = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (appearance.schema_version != kAppearanceSchemaVersion ||
        appearance.schema_minor != kAppearanceSchemaMinor ||
        !normalized(appearance.object_opacity) ||
        !normalized(appearance.fill_opacity) ||
        !normalized(appearance.stroke_opacity) ||
        static_cast<uint8_t>(appearance.blend_mode) >
            static_cast<uint8_t>(BlendMode::Lighten) ||
        appearance.effects.size() > kMaxEffectsPerObject)
        throw std::invalid_argument("Invalid appearance schema, opacity, blend mode or effect count");

    std::unordered_set<EffectInstanceId> effect_ids;
    effect_ids.reserve(appearance.effects.size());
    for (size_t index = 0; index < appearance.effects.size(); ++index) {
        const auto& node = appearance.effects[index];
        if (node.id == kInvalidEffectInstanceId ||
            node.schema_version != kBasicEffectNodeSchemaVersion ||
            node.schema_minor > kDropShadowBlendSchemaMinor ||
            !effect_ids.insert(node.id).second)
            throw std::invalid_argument("Invalid or repeated appearance effect ID/schema");
        std::visit([&](const auto& effect) {
            using T = std::decay_t<decltype(effect)>;
            if constexpr (std::is_same_v<T, GaussianBlurEffect>) {
                if (node.schema_minor != kBasicEffectNodeSchemaMinor ||
                    !std::isfinite(effect.sigma_x_mm) ||
                    !std::isfinite(effect.sigma_y_mm) || effect.sigma_x_mm < 0.0 ||
                    effect.sigma_y_mm < 0.0 ||
                    effect.sigma_x_mm > geometry::limits::kCanvasMaxMm ||
                    effect.sigma_y_mm > geometry::limits::kCanvasMaxMm)
                    throw std::invalid_argument("Gaussian blur sigma must be finite and within document limits");
            } else {
                if (!std::isfinite(effect.dx_mm) || !std::isfinite(effect.dy_mm) ||
                    std::abs(effect.dx_mm) > geometry::limits::kCanvasMaxMm ||
                    std::abs(effect.dy_mm) > geometry::limits::kCanvasMaxMm ||
                    !std::isfinite(effect.sigma_x_mm) ||
                    !std::isfinite(effect.sigma_y_mm) || effect.sigma_x_mm < 0.0 ||
                    effect.sigma_y_mm < 0.0 ||
                    effect.sigma_x_mm > geometry::limits::kCanvasMaxMm ||
                    effect.sigma_y_mm > geometry::limits::kCanvasMaxMm ||
                    !effect.color.is_valid() || !normalized(effect.opacity) ||
                    static_cast<uint8_t>(effect.blend_mode) >
                        static_cast<uint8_t>(BlendMode::Lighten) ||
                    (node.schema_minor == kBasicEffectNodeSchemaMinor &&
                     effect.blend_mode != BlendMode::Normal))
                    throw std::invalid_argument("Drop shadow parameters are invalid or outside document limits");
                if (effect.blend_mode != BlendMode::Normal) {
                    for (size_t later = index + 1;
                         later < appearance.effects.size(); ++later) {
                        if (appearance.effects[later].enabled)
                            throw std::invalid_argument(
                                "A non-Normal shadow blend must be the last enabled effect");
                    }
                }
            }
        }, node.parameters);
    }
}

inline void validate_image_source(const ImageObject& object) {
    if (geometry::validate_rect(object.bounds_rect) != geometry::GeometryStatus::Ok ||
        object.bounds_rect.width <= 0 || object.bounds_rect.height <= 0 ||
        object.width_px <= 0 || object.height_px <= 0)
        throw std::invalid_argument("Invalid image dimensions or bounds");
    const auto expected_pixels = static_cast<uint64_t>(object.width_px) *
                                 static_cast<uint64_t>(object.height_px);
    if (expected_pixels != object.pixels_rgba.size())
        throw std::invalid_argument("Image pixel count does not match its dimensions");
    const auto& source = object.source;
    if ((!source.media_type.empty() &&
         source.media_type != "image/png" &&
         source.media_type != "image/jpeg" &&
         source.media_type != "image/tiff") ||
        source.original_bytes.size() > RasterSourceMetadata::kMaxSourceBytes ||
        source.icc_profile.size() > RasterSourceMetadata::kMaxIccProfileBytes ||
        !std::isfinite(source.dpi_x) || !std::isfinite(source.dpi_y) ||
        source.dpi_x <= 0.0 || source.dpi_y <= 0.0 ||
        source.dpi_x > 1000000.0 || source.dpi_y > 1000000.0 ||
        source.frame_count == 0 || source.frame_count > 256 ||
        source.frame_index >= source.frame_count ||
        source.source_bits_per_pixel > 128 ||
        source.exif_orientation == 0 || source.exif_orientation > 8 ||
        (!source.original_bytes.empty() && source.media_type.empty()))
        throw std::invalid_argument("Invalid raster source metadata or embedded asset budget");
    if (object.clip_path)
        validate_geometry_path(*object.clip_path);
}

inline void validate_object_source(ObjectId key, const ObjectVariant& value) {
    std::visit([&](const auto& object) {
        if (key == kInvalidObjectId || object.id != key)
            throw std::invalid_argument("Object map key must match a nonzero stable ID");
        if (geometry::validate_transform(object.transform) != geometry::GeometryStatus::Ok)
            throw std::invalid_argument("Invalid or singular object transform");
        using T = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<T, PathObject>) {
            validate_path_source(object);
            validate_appearance(object.appearance);
        }
        else if constexpr (std::is_same_v<T, TextObject>) {
            validate_text_source(object);
            validate_appearance(object.appearance);
        }
        else if constexpr (std::is_same_v<T, GroupObject>) {
            std::unordered_set<ObjectId> unique;
            for (auto child : object.children_ids)
                if (child == kInvalidObjectId || child == key || !unique.insert(child).second)
                    throw std::invalid_argument("Invalid or repeated group child ID");
        } else if constexpr (std::is_same_v<T, ImageObject>) validate_image_source(object);
    }, value);
}

} // namespace pikdraw::document
