#pragma once
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document {

inline void validate_page(const Page& page) {
    if (!color::valid_document_color_mode(page.color_mode) ||
        page.color_profile_name.empty() || page.color_profile_name.size() > 256 ||
        (page.raster_dpi != 72 && page.raster_dpi != 150 && page.raster_dpi != 300))
        throw std::invalid_argument("Invalid document color settings or raster effects DPI");
    if (page.color_profile_resource) {
        const auto& identity = page.color_profile_resource->identity();
        const auto expected_space = page.color_mode == color::DocumentColorMode::Cmyk
            ? color::ColorSpace::Cmyk : color::ColorSpace::Rgb;
        if (identity.color_space != expected_space ||
            identity.checksum_sha256.size() != 64)
            throw std::invalid_argument("Page ICC resource does not match its color mode");
    }
    if (!geometry::units::is_physical_length_unit(page.display_length_unit))
        throw std::invalid_argument("Page display unit must be a physical length unit");
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
        !std::isfinite(object.frame_height_mm) || object.frame_height_mm < 0 ||
        object.frame_height_mm > geometry::limits::kCanvasMaxMm ||
        !std::isfinite(object.line_spacing_ratio) || object.line_spacing_ratio < 0.25 ||
        object.line_spacing_ratio > 10 || !std::isfinite(object.letter_spacing_mm) ||
        std::abs(object.letter_spacing_mm) > geometry::limits::kCanvasMaxMm ||
        !std::isfinite(object.path_offset_mm) ||
        std::abs(object.path_offset_mm) > geometry::limits::kCanvasMaxMm ||
        !std::isfinite(object.path_baseline_offset_mm) ||
        std::abs(object.path_baseline_offset_mm) > geometry::limits::kCanvasMaxMm ||
        (object.alignment != typography::TextAlignment::Left &&
         object.alignment != typography::TextAlignment::Center &&
         object.alignment != typography::TextAlignment::Right &&
         object.alignment != typography::TextAlignment::Justify))
        throw std::invalid_argument("Invalid paragraph width, line/letter spacing or alignment");
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
            node.schema_minor > kExtendedEffectNodeSchemaMinor ||
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
            } else if constexpr (std::is_same_v<T, DropShadowEffect>) {
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
            } else if constexpr (std::is_same_v<T, FeatherEffect>) {
                if (node.schema_minor != kExtendedEffectNodeSchemaMinor ||
                    !std::isfinite(effect.radius_mm) || effect.radius_mm < 0.0 ||
                    effect.radius_mm > geometry::limits::kCanvasMaxMm)
                    throw std::invalid_argument("Feather radius must be finite and within document limits");
            } else if constexpr (std::is_same_v<T, OuterGlowEffect>) {
                if (node.schema_minor != kExtendedEffectNodeSchemaMinor ||
                    !std::isfinite(effect.radius_mm) || effect.radius_mm < 0.0 ||
                    effect.radius_mm > geometry::limits::kCanvasMaxMm ||
                    !effect.color.is_valid() || !normalized(effect.opacity))
                    throw std::invalid_argument("Outer glow parameters are invalid or outside document limits");
            } else if constexpr (std::is_same_v<T, InnerGlowEffect>) {
                if (node.schema_minor != kExtendedEffectNodeSchemaMinor ||
                    !std::isfinite(effect.radius_mm) || effect.radius_mm < 0.0 ||
                    effect.radius_mm > geometry::limits::kCanvasMaxMm ||
                    !effect.color.is_valid() || !normalized(effect.opacity))
                    throw std::invalid_argument("Inner glow parameters are invalid or outside document limits");
            } else if constexpr (std::is_same_v<T, InnerShadowEffect>) {
                if (node.schema_minor != kExtendedEffectNodeSchemaMinor ||
                    !std::isfinite(effect.dx_mm) || !std::isfinite(effect.dy_mm) ||
                    std::abs(effect.dx_mm) > geometry::limits::kCanvasMaxMm ||
                    std::abs(effect.dy_mm) > geometry::limits::kCanvasMaxMm ||
                    !std::isfinite(effect.sigma_x_mm) ||
                    !std::isfinite(effect.sigma_y_mm) || effect.sigma_x_mm < 0.0 ||
                    effect.sigma_y_mm < 0.0 ||
                    effect.sigma_x_mm > geometry::limits::kCanvasMaxMm ||
                    effect.sigma_y_mm > geometry::limits::kCanvasMaxMm ||
                    !effect.color.is_valid() || !normalized(effect.opacity) ||
                    static_cast<uint8_t>(effect.blend_mode) >
                        static_cast<uint8_t>(BlendMode::Lighten))
                    throw std::invalid_argument("Inner shadow parameters are invalid or outside document limits");
            } else if constexpr (std::is_same_v<T, ColorOverlayEffect>) {
                if (node.schema_minor != kExtendedEffectNodeSchemaMinor ||
                    !effect.color.is_valid() || !normalized(effect.opacity) ||
                    static_cast<uint8_t>(effect.blend_mode) >
                        static_cast<uint8_t>(BlendMode::Lighten))
                    throw std::invalid_argument("Color overlay parameters are invalid or outside document limits");
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
    if (expected_pixels != object.rgba_pixels().size())
        throw std::invalid_argument("Image pixel count does not match its dimensions");
    const auto high_precision_pixels = object.rgba16le_pixels();
    if (!high_precision_pixels.empty()) {
        if (expected_pixels > std::numeric_limits<uint64_t>::max() / 8u ||
            expected_pixels * 8u > std::numeric_limits<uint32_t>::max() ||
            high_precision_pixels.size() != expected_pixels * 8u)
            throw std::invalid_argument(
                "RGBA16LE image bytes do not match its dimensions or native storage limit");
    }
    const auto& source = object.source;
    if ((!source.media_type.empty() &&
         source.media_type != "image/png" &&
         source.media_type != "image/jpeg" &&
         source.media_type != "image/tiff" &&
         source.media_type != "image/bmp" &&
         source.media_type != "application/x-pikdraw-rgba8") ||
        source.original_data().size() > RasterSourceMetadata::kMaxSourceBytes ||
        source.icc_profile.size() > RasterSourceMetadata::kMaxIccProfileBytes ||
        !std::isfinite(source.dpi_x) || !std::isfinite(source.dpi_y) ||
        source.dpi_x <= 0.0 || source.dpi_y <= 0.0 ||
        source.dpi_x > 1000000.0 || source.dpi_y > 1000000.0 ||
        source.frame_count == 0 ||
        source.frame_index >= source.frame_count ||
        source.source_bits_per_pixel > 128 ||
        source.exif_orientation == 0 || source.exif_orientation > 8 ||
        static_cast<uint8_t>(source.preview_pixel_color_basis) >
            static_cast<uint8_t>(RasterPixelColorBasis::EmbeddedProfileUnapplied) ||
        (!source.original_data().empty() && source.media_type.empty()))
        throw std::invalid_argument("Invalid raster source metadata or embedded asset budget");
    if (object.clip_path)
        validate_geometry_path(*object.clip_path);
    if (object.asset_source) {
        const auto& asset = *object.asset_source;
        const bool fingerprint_valid = asset.fingerprint_sha256.empty() ||
            (asset.fingerprint_sha256.size() == 64 &&
             std::all_of(asset.fingerprint_sha256.begin(), asset.fingerprint_sha256.end(),
                [](unsigned char c) { return (c >= '0' && c <= '9') ||
                    (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }));
        if (!asset.asset_id.valid() || asset.revision == 0 ||
            asset.encoded_byte_length == 0 || asset.media_type.empty() ||
            asset.media_type.size() > 256 ||
            (asset.media_type != "image/png" && asset.media_type != "image/jpeg" &&
             asset.media_type != "image/tiff" && asset.media_type != "image/bmp") ||
            static_cast<uint8_t>(asset.mode) > static_cast<uint8_t>(RasterStorageMode::Linked) ||
            (asset.mode == RasterStorageMode::Linked && asset.locator.empty()) ||
            asset.locator.size() > 32768 || !fingerprint_valid)
            throw std::invalid_argument("Invalid raster asset identity or source descriptor");
    }
    if (source.asset_provider &&
        (!object.asset_source || source.asset_provider->size_bytes() !=
            object.asset_source->encoded_byte_length ||
         (!source.original_data().empty() && source.original_data().size() !=
            object.asset_source->encoded_byte_length)))
        throw std::invalid_argument(
            "Raster source provider must match its persistent asset descriptor");
    if (object.edit_graph.schema_version != RasterEditGraph::kSchemaVersion ||
        object.edit_graph.nodes.size() > RasterEditGraph::kMaxNodes)
        throw std::invalid_argument("Invalid raster edit graph schema or node count");
    std::unordered_set<uint64_t> node_ids;
    std::unordered_set<uint64_t> mask_ids;
    uint32_t raster_width = static_cast<uint32_t>(object.width_px);
    uint32_t raster_height = static_cast<uint32_t>(object.height_px);
    node_ids.reserve(object.edit_graph.nodes.size());
    mask_ids.reserve(object.edit_graph.nodes.size());
    for (const auto& node : object.edit_graph.nodes) {
        if (!node.id.valid() || !node_ids.insert(node.id.value).second)
            throw std::invalid_argument("Raster edit node IDs must be nonzero and unique");
        if (node.algorithm_version != 1)
            throw std::invalid_argument("Unsupported raster edit algorithm version");
        if (node.enabled && node.mask &&
            (node.mask->width_px != raster_width ||
             node.mask->height_px != raster_height))
            throw std::invalid_argument(
                "Raster mask dimensions must match the graph image at that node");
        std::visit([](const auto& params) {
            using T = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<T, RasterCropParams>) {
                if (!std::isfinite(params.x) || !std::isfinite(params.y) ||
                    !std::isfinite(params.width) || !std::isfinite(params.height) ||
                    params.x < 0.0 || params.y < 0.0 || params.width <= 0.0 ||
                    params.height <= 0.0 || params.x + params.width > 1.0 ||
                    params.y + params.height > 1.0)
                    throw std::invalid_argument("Raster crop must be a finite normalized source rectangle");
            } else if constexpr (std::is_same_v<T, RasterRotateParams>) {
                if (!std::isfinite(params.degrees) || std::abs(params.degrees) > 360000.0)
                    throw std::invalid_argument("Raster rotation must be finite and within supported range");
            } else if constexpr (std::is_same_v<T, RasterExposureParams>) {
                if (!std::isfinite(params.exposure_ev) || std::abs(params.exposure_ev) > 10.0)
                    throw std::invalid_argument("Raster exposure must be finite and within +/-10 EV");
            } else if constexpr (std::is_same_v<T, RasterLevelsParams>) {
                const auto channel = static_cast<uint8_t>(params.channel);
                if (channel > static_cast<uint8_t>(RasterToneChannel::Blue) ||
                    !std::isfinite(params.black_in) || !std::isfinite(params.white_in) ||
                    !std::isfinite(params.gamma) || !std::isfinite(params.black_out) ||
                    !std::isfinite(params.white_out) || params.black_in < 0.0 ||
                    params.white_in > 1.0 || params.black_in >= params.white_in ||
                    params.gamma < 0.1 || params.gamma > 10.0 ||
                    params.black_out < 0.0 || params.white_out > 1.0 ||
                    params.black_out > params.white_out)
                    throw std::invalid_argument("Raster Levels parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterHslParams>) {
                if (!std::isfinite(params.hue_degrees) ||
                    std::abs(params.hue_degrees) > 180.0 ||
                    !std::isfinite(params.saturation_percent) ||
                    std::abs(params.saturation_percent) > 100.0 ||
                    !std::isfinite(params.lightness_percent) ||
                    std::abs(params.lightness_percent) > 100.0)
                    throw std::invalid_argument("Raster HSL parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterCurvesParams>) {
                if (static_cast<uint8_t>(params.channel) >
                    static_cast<uint8_t>(RasterToneChannel::Blue) ||
                    std::any_of(params.output_points.begin(), params.output_points.end(),
                        [](double value) {
                            return !std::isfinite(value) || value < 0.0 || value > 1.0;
                        }))
                    throw std::invalid_argument("Raster Curves parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterWhiteBalanceParams>) {
                if (!std::isfinite(params.temperature_shift_percent) ||
                    std::abs(params.temperature_shift_percent) > 100.0 ||
                    !std::isfinite(params.tint_percent) ||
                    std::abs(params.tint_percent) > 100.0)
                    throw std::invalid_argument("Raster White Balance parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterVibranceParams>) {
                if (!std::isfinite(params.amount_percent) ||
                    std::abs(params.amount_percent) > 100.0)
                    throw std::invalid_argument("Raster Vibrance amount is invalid");
            } else if constexpr (std::is_same_v<T, RasterSelectiveHslParams>) {
                if (static_cast<uint8_t>(params.range) >
                        static_cast<uint8_t>(RasterHueRange::Magentas) ||
                    !std::isfinite(params.hue_degrees) ||
                    std::abs(params.hue_degrees) > 30.0 ||
                    !std::isfinite(params.saturation_percent) ||
                    std::abs(params.saturation_percent) > 100.0 ||
                    !std::isfinite(params.lightness_percent) ||
                    std::abs(params.lightness_percent) > 100.0 ||
                    !std::isfinite(params.feather_percent) ||
                    params.feather_percent < 0.0 || params.feather_percent > 100.0)
                    throw std::invalid_argument("Raster selective HSL parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterLightParams>) {
                if (!std::isfinite(params.exposure_ev) ||
                    std::abs(params.exposure_ev) > 5.0 ||
                    !std::isfinite(params.contrast_percent) ||
                    std::abs(params.contrast_percent) > 100.0 ||
                    !std::isfinite(params.highlights_percent) ||
                    std::abs(params.highlights_percent) > 100.0 ||
                    !std::isfinite(params.shadows_percent) ||
                    std::abs(params.shadows_percent) > 100.0 ||
                    !std::isfinite(params.whites_percent) ||
                    std::abs(params.whites_percent) > 100.0 ||
                    !std::isfinite(params.blacks_percent) ||
                    std::abs(params.blacks_percent) > 100.0)
                    throw std::invalid_argument("Raster Light parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterColorBalanceParams>) {
                if (static_cast<uint8_t>(params.range) >
                        static_cast<uint8_t>(RasterTonalRange::Highlights) ||
                    !std::isfinite(params.red_cyan_percent) ||
                    std::abs(params.red_cyan_percent) > 100.0 ||
                    !std::isfinite(params.green_magenta_percent) ||
                    std::abs(params.green_magenta_percent) > 100.0 ||
                    !std::isfinite(params.blue_yellow_percent) ||
                    std::abs(params.blue_yellow_percent) > 100.0)
                    throw std::invalid_argument("Raster Color Balance parameters are invalid");
            } else if constexpr (std::is_same_v<T, RasterChannelMixerParams>) {
                if (std::any_of(params.coefficients.begin(), params.coefficients.end(),
                    [](double value) { return !std::isfinite(value) ||
                        value < -2.0 || value > 2.0; }))
                    throw std::invalid_argument("Raster Channel Mixer coefficients are invalid");
            } else if constexpr (std::is_same_v<T, RasterCloneStampParams>) {
                if (!std::isfinite(params.source_x) || params.source_x < 0.0 ||
                    params.source_x > 1.0 || !std::isfinite(params.source_y) ||
                    params.source_y < 0.0 || params.source_y > 1.0 ||
                    !std::isfinite(params.target_x) || params.target_x < 0.0 ||
                    params.target_x > 1.0 || !std::isfinite(params.target_y) ||
                    params.target_y < 0.0 || params.target_y > 1.0 ||
                    !std::isfinite(params.radius_percent) ||
                    params.radius_percent <= 0.0 || params.radius_percent > 25.0 ||
                    !std::isfinite(params.hardness_percent) ||
                    params.hardness_percent < 0.0 || params.hardness_percent > 100.0 ||
                    !std::isfinite(params.opacity_percent) ||
                    params.opacity_percent < 0.0 || params.opacity_percent > 100.0)
                    throw std::invalid_argument("Raster Clone Stamp parameters are invalid");
            }
        }, node.parameters);
        if (node.mask) {
            const auto& mask = *node.mask;
            const bool pointwise =
                std::holds_alternative<RasterExposureParams>(node.parameters) ||
                std::holds_alternative<RasterLevelsParams>(node.parameters) ||
                std::holds_alternative<RasterHslParams>(node.parameters) ||
                std::holds_alternative<RasterCurvesParams>(node.parameters) ||
                std::holds_alternative<RasterWhiteBalanceParams>(node.parameters) ||
                std::holds_alternative<RasterVibranceParams>(node.parameters) ||
                std::holds_alternative<RasterSelectiveHslParams>(node.parameters) ||
                std::holds_alternative<RasterLightParams>(node.parameters) ||
                std::holds_alternative<RasterColorBalanceParams>(node.parameters) ||
                std::holds_alternative<RasterChannelMixerParams>(node.parameters);
            const uint64_t mask_pixels =
                static_cast<uint64_t>(mask.width_px) * mask.height_px;
            const bool valid_payload =
                mask.kind == RasterAdjustmentMaskKind::Bitmap
                    ? mask_pixels == mask.coverage.size()
                    : (mask.kind == RasterAdjustmentMaskKind::Rectangle ||
                       mask.kind == RasterAdjustmentMaskKind::LinearGradient ||
                       mask.kind == RasterAdjustmentMaskKind::RadialGradient) &&
                      mask.coverage.empty() && std::isfinite(mask.left) &&
                      std::isfinite(mask.top) && std::isfinite(mask.width) &&
                      std::isfinite(mask.height) &&
                      std::isfinite(mask.feather_px) && mask.left >= 0.0 &&
                      mask.top >= 0.0 && mask.width > 0.0 && mask.height > 0.0 &&
                      mask.left + mask.width <= 1.0 &&
                      mask.top + mask.height <= 1.0 && mask.feather_px >= 0.0 &&
                      mask.feather_px <= (std::max)(mask.width_px, mask.height_px) &&
                      (mask.kind == RasterAdjustmentMaskKind::LinearGradient
                          ? std::isfinite(mask.gradient_angle_degrees) &&
                                std::abs(mask.gradient_angle_degrees) <= 36000.0
                          : mask.gradient_angle_degrees == 0.0);
            if (!pointwise || mask.id == 0 || !mask_ids.insert(mask.id).second ||
                mask.width_px == 0 || mask.height_px == 0 ||
                mask.density_percent > 100 ||
                (mask.kind != RasterAdjustmentMaskKind::LinearGradient &&
                 mask.gradient_angle_degrees != 0.0) || !valid_payload)
                throw std::invalid_argument("Invalid raster adjustment mask");
        }
        if (node.enabled) {
            if (const auto* crop = std::get_if<RasterCropParams>(&node.parameters)) {
                const auto boundary = [](double normalized, uint32_t extent) {
                    const auto rounded = std::llround(normalized * extent);
                    return static_cast<uint32_t>(std::clamp<long long>(
                        rounded, 0, static_cast<long long>(extent)));
                };
                const auto left = boundary(crop->x, raster_width);
                const auto top = boundary(crop->y, raster_height);
                auto right = boundary(crop->x + crop->width, raster_width);
                auto bottom = boundary(crop->y + crop->height, raster_height);
                if (right <= left) right = (std::min)(raster_width, left + 1);
                if (bottom <= top) bottom = (std::min)(raster_height, top + 1);
                raster_width = right - left;
                raster_height = bottom - top;
            } else if (const auto* rotate =
                           std::get_if<RasterRotateParams>(&node.parameters)) {
                double degrees = std::fmod(rotate->degrees, 360.0);
                if (degrees < 0.0) degrees += 360.0;
                if (degrees == 90.0 || degrees == 270.0) {
                    std::swap(raster_width, raster_height);
                } else if (degrees != 0.0 && degrees != 180.0) {
                    const double radians = degrees * (std::acos(-1.0) / 180.0);
                    const double width = std::ceil(
                        std::abs(raster_width * std::cos(radians)) +
                        std::abs(raster_height * std::sin(radians)));
                    const double height = std::ceil(
                        std::abs(raster_width * std::sin(radians)) +
                        std::abs(raster_height * std::cos(radians)));
                    if (!std::isfinite(width) || !std::isfinite(height) ||
                        width < 1.0 || height < 1.0 ||
                        width > std::numeric_limits<uint32_t>::max() ||
                        height > std::numeric_limits<uint32_t>::max())
                        throw std::invalid_argument(
                            "Raster rotation output dimensions overflow");
                    raster_width = static_cast<uint32_t>(width);
                    raster_height = static_cast<uint32_t>(height);
                }
            }
        }
    }
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
            if (object.contour_paints) {
                if (!object.contour_spec.starts_with("contour:"))
                    throw std::invalid_argument("Contour paints require a contour feature specification");
                if (!object.contour_paints->outline.is_valid() ||
                    !object.contour_paints->fill.is_valid())
                    throw std::invalid_argument("Invalid live contour source paint");
            }
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
            if (object.clip.has_value()) {
                if (object.clip->mask_object_id == kInvalidObjectId || object.clip->mask_object_id == key)
                    throw std::invalid_argument("Invalid clipping mask object ID");
                if (static_cast<uint8_t>(object.clip->fit_mode) > static_cast<uint8_t>(ClipContentFitMode::StretchToFill))
                    throw std::invalid_argument("Invalid clipping fit mode");
            }
        } else if constexpr (std::is_same_v<T, ImageObject>) validate_image_source(object);
    }, value);
}

} // namespace pikdraw::document
