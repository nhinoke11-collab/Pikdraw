#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/production/print/test_sheet_render.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::production::print;

namespace {
std::shared_ptr<const color::ProfileResource> system_srgb() {
    const auto result = color::ProfileResource::load_file(
        color::IccPreview::system_srgb_profile(), color::ColorSpace::Rgb);
    assert(result && "test fixture requires an installed system sRGB profile");
    return result.resource;
}

TestSheetRequest request(TestSheetKind kind) {
    TestSheetRequest result;
    result.run_id = "render-run-20261024";
    result.kind = kind;
    result.source = {"document-frozen", 7, 42, 11, 2, 2};
    result.condition.id = "sRGB-file";
    result.condition.profile = system_srgb();
    result.condition.target_ppi_x = 72;
    result.condition.target_ppi_y = 72;
    result.page_width_mm = 210;
    result.page_height_mm = 297;
    result.margin_mm = 10;
    result.patch_size_mm = 10;
    result.patch_gap_mm = 2;
    return result;
}

uint8_t pixel_channel(const raster::RasterPixelBuffer& image, uint32_t x,
                      uint32_t y, size_t channel) {
    return image.bytes[(static_cast<size_t>(y) * image.width + x) * 4 + channel];
}
}

int main() {
    const std::array<uint8_t, 16> source_bytes{
        40, 80, 120, 255, 180, 100, 60, 255,
        20, 40, 60, 255, 220, 220, 220, 255
    };
    const raster::RasterPixelView source{
        source_bytes, 2, 2, 8, raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
    document::RasterEditGraph graph;
    graph.nodes.push_back({{5}, true, document::RasterExposureParams{0.25}, 1});

    {
        auto sheet_request = request(TestSheetKind::CropAndDiagnostic);
        sheet_request.crop = {0, 0, 2, 2};
        const auto planned = plan_test_sheet(sheet_request);
        assert(planned);
        const auto rendered = render_test_sheet(*planned.plan, source);
        assert(rendered.succeeded());
        assert(rendered.sheet.width_px == 595 && rendered.sheet.height_px == 842);
        assert(rendered.sheet.image.encoding == raster::RasterPixelEncoding::SRgbRgba8StraightAlpha);
        assert(rendered.sheet.image.reservation && rendered.sheet.image.reservation->bytes() ==
               rendered.sheet.image.bytes.size());
        const double expected_ruler_mm = std::round(100.0 * 72.0 / 25.4) * 25.4 / 72.0;
        assert(std::abs(rendered.sheet.one_hundred_mm_ruler_actual_mm - expected_ruler_mm) < 1e-9);
        for (size_t alpha = 3; alpha < rendered.sheet.image.bytes.size(); alpha += 4)
            assert(rendered.sheet.image.bytes[alpha] == 255);
    }

    {
        auto sheet_request = request(TestSheetKind::VariationGrid);
        sheet_request.variation_columns = 3;
        sheet_request.variation_rows = 1;
        sheet_request.x_axis = {"exposure", "EV", {-1.0, 0.0, 1.0}};
        sheet_request.edit_graph = graph;
        const auto planned = plan_test_sheet(sheet_request);
        assert(planned && planned.plan->x_axis.values.size() == 3);
        const auto rendered = render_test_sheet(*planned.plan, source);
        assert(rendered.succeeded());
        std::array<uint8_t, 3> center_samples{};
        for (const auto& item : planned.plan->elements) {
            if (item.kind != TestSheetElementKind::VariationCell) continue;
            const auto x = static_cast<uint32_t>(std::lround(
                (item.bounds_mm.x + item.bounds_mm.width / 2.0) * 72.0 / 25.4));
            const auto y = static_cast<uint32_t>(std::lround(
                (item.bounds_mm.y + item.bounds_mm.height * 0.65) * 72.0 / 25.4));
            center_samples[item.column] = pixel_channel(rendered.sheet.image, x, y, 0);
        }
        assert(center_samples[0] < center_samples[1] && center_samples[1] < center_samples[2]);
    }

    {
        auto sheet_request = request(TestSheetKind::VariationGrid);
        sheet_request.variation_columns = 3;
        sheet_request.variation_rows = 1;
        sheet_request.x_axis = {"exposure", "EV", {-11.0, 0.0, 11.0}};
        const auto planned = plan_test_sheet(sheet_request);
        assert(planned);
        const auto rendered = render_test_sheet(*planned.plan, source);
        assert(rendered.status == TestSheetRenderStatus::UnsupportedVariation);
    }

    std::cout << "P4 deterministic test-sheet raster rendering checks passed\n";
}
