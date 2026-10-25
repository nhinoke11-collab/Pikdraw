#pragma once

#include "pikdraw/desktop/shell/native_window.hpp"

#include <string_view>

namespace pikdraw::desktop {

enum class NumericQuantityKind {
    None,
    Length,
    AngleDegrees,
    Count,
    Percent,
    Scalar,
    FontPoint,
    Expression
};

struct NumericFieldDescriptor {
    std::string_view semantic_key;
    NumericQuantityKind quantity{NumericQuantityKind::None};
};

[[nodiscard]] inline constexpr NumericFieldDescriptor describe_numeric_field(
    NativeWindow::NumericFieldId field) noexcept {
    using Field = NativeWindow::NumericFieldId;
    using Quantity = NumericQuantityKind;
    switch (field) {
    case Field::PageWidth: return {"page.width", Quantity::Length};
    case Field::PageHeight: return {"page.height", Quantity::Length};
    case Field::RasterExposureEv: return {"raster.exposure_ev", Quantity::Scalar};
    case Field::RasterColorBalanceRedCyan:
        return {"raster.color_balance.red_cyan", Quantity::Percent};
    case Field::RasterColorBalanceGreenMagenta:
        return {"raster.color_balance.green_magenta", Quantity::Percent};
    case Field::RasterColorBalanceBlueYellow:
        return {"raster.color_balance.blue_yellow", Quantity::Percent};
    case Field::RasterLightContrast:
        return {"raster.light.contrast", Quantity::Percent};
    case Field::RasterLightHighlights:
        return {"raster.light.highlights", Quantity::Percent};
    case Field::RasterLightShadows:
        return {"raster.light.shadows", Quantity::Percent};
    case Field::RasterLightWhites:
        return {"raster.light.whites", Quantity::Percent};
    case Field::RasterLightBlacks:
        return {"raster.light.blacks", Quantity::Percent};
    case Field::RasterHslHue:
        return {"raster.hsl.hue", Quantity::AngleDegrees};
    case Field::RasterHslSaturation:
        return {"raster.hsl.saturation", Quantity::Percent};
    case Field::RasterHslLightness:
        return {"raster.hsl.lightness", Quantity::Percent};
    case Field::RasterVibranceAmount:
        return {"raster.vibrance.amount", Quantity::Percent};
    case Field::RasterLevelsBlackIn:
        return {"raster.levels.black_in", Quantity::Percent};
    case Field::RasterLevelsWhiteIn:
        return {"raster.levels.white_in", Quantity::Percent};
    case Field::RasterLevelsGamma:
        return {"raster.levels.gamma", Quantity::Scalar};
    case Field::RasterLevelsBlackOut:
        return {"raster.levels.black_out", Quantity::Percent};
    case Field::RasterLevelsWhiteOut:
        return {"raster.levels.white_out", Quantity::Percent};
    case Field::RasterWhiteBalanceTemperature:
        return {"raster.white_balance.temperature_shift", Quantity::Percent};
    case Field::RasterWhiteBalanceTint:
        return {"raster.white_balance.tint", Quantity::Percent};
    case Field::RasterCurvePoint0:
        return {"raster.curves.point_0", Quantity::Percent};
    case Field::RasterCurvePoint1:
        return {"raster.curves.point_1", Quantity::Percent};
    case Field::RasterCurvePoint2:
        return {"raster.curves.point_2", Quantity::Percent};
    case Field::RasterCurvePoint3:
        return {"raster.curves.point_3", Quantity::Percent};
    case Field::RasterCurvePoint4:
        return {"raster.curves.point_4", Quantity::Percent};
    case Field::PolygonSides: return {"shape.polygon.sides", Quantity::Count};
    case Field::PolygonRotation: return {"shape.polygon.rotation", Quantity::AngleDegrees};
    case Field::ObjectX: return {"selection.x", Quantity::Length};
    case Field::ObjectY: return {"selection.y", Quantity::Length};
    case Field::ObjectWidth: return {"selection.width", Quantity::Length};
    case Field::ObjectHeight: return {"selection.height", Quantity::Length};
    case Field::ObjectRotation: return {"selection.rotation", Quantity::AngleDegrees};
    case Field::ObjectOpacity: return {"selection.opacity", Quantity::Percent};
    case Field::FillOpacity: return {"paint.fill.opacity", Quantity::Percent};
    case Field::StrokeOpacity: return {"paint.stroke.opacity", Quantity::Percent};
    case Field::BlurSigmaX: return {"effect.blur.sigma_x", Quantity::Length};
    case Field::BlurSigmaY: return {"effect.blur.sigma_y", Quantity::Length};
    case Field::ShadowFeatherX: return {"effect.shadow.feather_x", Quantity::Length};
    case Field::ShadowFeatherY: return {"effect.shadow.feather_y", Quantity::Length};
    case Field::TextFontSize: return {"text.font.size", Quantity::FontPoint};
    case Field::TextFrameWidth: return {"text.frame.width", Quantity::Length};
    case Field::TextLineSpacing: return {"text.line_spacing", Quantity::Percent};
    case Field::ShadowOffsetX: return {"effect.shadow.offset_x", Quantity::Length};
    case Field::ShadowOffsetY: return {"effect.shadow.offset_y", Quantity::Length};
    case Field::ShadowOpacity: return {"effect.shadow.opacity", Quantity::Percent};
    case Field::DistributeSpacing: return {"arrange.distribute.spacing", Quantity::Length};
    case Field::ContourSpacing: return {"contour.spacing", Quantity::Length};
    case Field::ContourSteps: return {"contour.steps", Quantity::Count};
    case Field::ContourTolerance: return {"contour.arc_tolerance", Quantity::Length};
    case Field::ContourMiterLimit: return {"contour.miter_limit", Quantity::Scalar};
    case Field::GradientStopOffset: return {"gradient.stop.offset", Quantity::Percent};
    case Field::GradientStopOpacity: return {"gradient.stop.opacity", Quantity::Percent};
    case Field::GradientStopMidpoint: return {"gradient.stop.midpoint", Quantity::Percent};
    case Field::GradientAngle: return {"gradient.angle", Quantity::AngleDegrees};
    case Field::GradientLength: return {"gradient.length", Quantity::Length};
    case Field::GradientCenterX: return {"gradient.center_x", Quantity::Length};
    case Field::GradientCenterY: return {"gradient.center_y", Quantity::Length};
    case Field::PenExpression: return {"path.numeric_expression", Quantity::Expression};
    case Field::PenLength: return {"path.length", Quantity::Length};
    case Field::PenAngle: return {"path.angle", Quantity::AngleDegrees};
    case Field::ModalWidth: return {"document.new.width", Quantity::Length};
    case Field::ModalHeight: return {"document.new.height", Quantity::Length};
    case Field::RectCornerRadiusTL: return {"shape.rectangle.corner_tl", Quantity::Length};
    case Field::RectCornerRadiusTR: return {"shape.rectangle.corner_tr", Quantity::Length};
    case Field::RectCornerRadiusBR: return {"shape.rectangle.corner_br", Quantity::Length};
    case Field::RectCornerRadiusBL: return {"shape.rectangle.corner_bl", Quantity::Length};
    case Field::TacLimitPercent: return {"color.tac.limit", Quantity::Percent};
    case Field::GridSpacing: return {"snap.grid.spacing", Quantity::Length};
    case Field::ArtboardX: return {"artboard.x", Quantity::Length};
    case Field::ArtboardY: return {"artboard.y", Quantity::Length};
    case Field::ArtboardWidth: return {"artboard.width", Quantity::Length};
    case Field::ArtboardHeight: return {"artboard.height", Quantity::Length};
    case Field::SnapAcquireRadius: return {"snap.acquire_radius", Quantity::Scalar};
    case Field::SnapReleaseRadius: return {"snap.release_radius", Quantity::Scalar};
    case Field::None: return {};
    }
    return {};
}

[[nodiscard]] inline constexpr bool numeric_field_is_length(
    NativeWindow::NumericFieldId field) noexcept {
    return describe_numeric_field(field).quantity == NumericQuantityKind::Length;
}

} // namespace pikdraw::desktop
