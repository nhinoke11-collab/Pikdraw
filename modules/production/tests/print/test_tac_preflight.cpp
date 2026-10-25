#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/production/print/tac_preflight.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::production::print;

int main() {
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    const auto srgb = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
    assert(srgb);
    TacPreflightOptions invalid_options;
    invalid_options.limit_percent = 300.0;
    const std::array<uint8_t, 4> one_pixel{128, 64, 32, 255};
    const raster::RasterPixelView one_pixel_view{one_pixel, 1, 1, 4,
        raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
    const auto wrong_destination = analyze_raster_tac(
        one_pixel_view, srgb.resource, srgb.resource, invalid_options);
    assert(wrong_destination.status == TacPreflightStatus::UnsupportedProfile);

    const auto cmyk_path = srgb_path.parent_path() / L"RSWOP.icm";
    if (std::filesystem::is_regular_file(cmyk_path)) {
        const auto cmyk = color::ProfileResource::load_file(cmyk_path, color::ColorSpace::Cmyk);
        assert(cmyk);
        const std::array<uint8_t, 8> pixels{
            128, 64, 32, 0, 255, 0, 0, 255
        };
        const raster::RasterPixelView view{
            pixels, 2, 1, 8, raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
        TacPreflightOptions options;
        options.limit_percent = 300.0;
        options.block_pixels = 2;
        const auto report = analyze_raster_tac(view, srgb.resource, cmyk.resource, options);
        assert(report.succeeded() && report.pixels_analyzed == 2);
        assert(report.maximum_tac_percent <= 400.0 && report.mean_tac_percent >= 0.0);
        assert(std::abs(report.over_limit_coverage_percent -
            50.0 * report.pixels_over_limit) < 1e-9);
        assert(report.output_profile_sha256 == cmyk.resource->identity().checksum_sha256);

        options.cancelled = [] { return true; };
        const auto cancelled = analyze_raster_tac(view, srgb.resource, cmyk.resource, options);
        assert(cancelled.status == TacPreflightStatus::Cancelled);
    }

    std::cout << "P6 ICC-based bounded TAC preflight checks passed\n";
}
