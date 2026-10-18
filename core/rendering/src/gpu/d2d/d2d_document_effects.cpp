#include "d2d_internal.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::d2d {
bool D2DEffectsPipeline::apply_document_effects(ID2D1DeviceContext* context, ID2D1Image* source,
    const document::Appearance& appearance, document::ObjectId id,
    double viewport_density, int32_t raster_dpi, ComPtr<ID2D1Image>& output,
    std::vector<RenderDiagnostic>& diagnostics, const color::IccPreview* preview) {
    const bool has_effect = std::any_of(appearance.effects.begin(), appearance.effects.end(),
        [](const auto& node) { return node.enabled; });
    if (raster_dpi == 0 || !has_effect)
        return apply_effects(context, source, appearance, id, viewport_density, output, diagnostics, preview);
    auto fail = [&](const char* message) { diagnostics.push_back({id, true, message}); return false; };
    const double density = raster_dpi / 25.4;
    const double scale = density / viewport_density;
    D2D1_RECT_F bounds{};
    if (!std::isfinite(scale) || scale <= 0 || FAILED(context->GetImageLocalBounds(source, &bounds)))
        return fail("Cannot measure document raster effects source");
    const double left = std::floor(bounds.left * scale) - 2;
    const double top = std::floor(bounds.top * scale) - 2;
    const double width = std::ceil(bounds.right * scale) - left + 2;
    const double height = std::ceil(bounds.bottom * scale) - top + 2;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        width > 16384 || height > 16384 || width * height > 16'777'216)
        return fail("Document raster effects source exceeds texture budget");

    // Rasterize just the source bounds at the document density on a separate
    // context. Viewport zoom changes presentation, never this sampling grid.
    ComPtr<ID2D1Device> device;
    context->GetDevice(device.ReleaseAndGetAddressOf());
    ComPtr<ID2D1DeviceContext> local;
    if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, local.ReleaseAndGetAddressOf())))
        return fail("Cannot create document raster effects context");
    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(local->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
        nullptr, 0, &properties, bitmap.ReleaseAndGetAddressOf())))
        return fail("Cannot allocate document raster effects buffer");
    local->SetDpi(96, 96);
    local->SetTarget(bitmap.Get());
    local->BeginDraw();
    local->Clear(D2D1::ColorF(0, 0));
    local->SetTransform(D2D1::Matrix3x2F(static_cast<float>(scale), 0, 0, static_cast<float>(scale),
        static_cast<float>(-left), static_cast<float>(-top)));
    local->DrawImage(source);
    const auto rendered = local->EndDraw();
    local->SetTarget(nullptr);
    if (FAILED(rendered)) return fail("Document raster effects source rendering failed");
    ComPtr<ID2D1Image> filtered;
    if (!apply_effects(local.Get(), bitmap.Get(), appearance, id, density, filtered, diagnostics, preview))
        return false;
    ComPtr<ID2D1Effect> placement;
    if (FAILED(context->CreateEffect(CLSID_D2D12DAffineTransform, placement.ReleaseAndGetAddressOf())))
        return fail("Cannot place document raster effects output");
    placement->SetInput(0, filtered.Get());
    const auto matrix = D2D1::Matrix3x2F(static_cast<float>(1 / scale), 0, 0, static_cast<float>(1 / scale),
        static_cast<float>(left / scale), static_cast<float>(top / scale));
    if (FAILED(placement->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, matrix)))
        return fail("Cannot configure document raster effects output");
    placement->GetOutput(output.ReleaseAndGetAddressOf());
    return true;
}
} // namespace pikdraw::rendering::d2d
