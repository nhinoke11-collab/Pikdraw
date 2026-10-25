#include "pikdraw/production/print/test_sheet_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pikdraw::production::print {
namespace {
constexpr double kMmPerInch = 25.4;
constexpr uint64_t kMaximumRenderMemory = 2ull * 1024ull * 1024ull * 1024ull;

TestSheetRenderResult fail(TestSheetRenderStatus status, std::string message) {
    return {status, {}, std::move(message)};
}

bool is_cancelled(const TestSheetRenderOptions& options) {
    try { return options.cancelled && options.cancelled(); }
    catch (...) { return true; }
}

std::optional<uint32_t> millimeters_to_pixels(double mm, uint32_t ppi) {
    if (!std::isfinite(mm) || mm <= 0.0 || ppi == 0) return std::nullopt;
    const double value = mm * static_cast<double>(ppi) / kMmPerInch;
    if (!std::isfinite(value) || value < 1.0 ||
        value > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        return std::nullopt;
    return static_cast<uint32_t>(std::llround(value));
}

bool checked_surface_bytes(uint32_t width, uint32_t height, uint64_t& bytes) {
    if (width == 0 || height == 0 ||
        static_cast<uint64_t>(width) > std::numeric_limits<uint64_t>::max() / height)
        return false;
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > std::numeric_limits<uint64_t>::max() / 4) return false;
    bytes = pixels * 4;
    return true;
}

uint32_t coordinate_px(double mm, uint32_t ppi) {
    const double px = mm * static_cast<double>(ppi) / kMmPerInch;
    if (!std::isfinite(px) || px <= 0.0) return 0;
    return static_cast<uint32_t>(std::llround(px));
}

void fill_rect(std::span<uint8_t> rgba, uint32_t width, uint32_t height,
               int64_t left, int64_t top, int64_t right, int64_t bottom,
               std::array<uint8_t, 3> color) {
    left = (std::max<int64_t>)(0, left);
    top = (std::max<int64_t>)(0, top);
    right = (std::min<int64_t>)(width, right);
    bottom = (std::min<int64_t>)(height, bottom);
    if (right <= left || bottom <= top) return;
    for (int64_t y = top; y < bottom; ++y) {
        size_t offset = (static_cast<size_t>(y) * width +
                         static_cast<size_t>(left)) * 4;
        for (int64_t x = left; x < right; ++x, offset += 4) {
            rgba[offset] = color[0];
            rgba[offset + 1] = color[1];
            rgba[offset + 2] = color[2];
            rgba[offset + 3] = 255;
        }
    }
}

std::array<uint8_t, 3> byte_color(const std::array<double, 3>& color) {
    std::array<uint8_t, 3> result{};
    for (size_t channel = 0; channel < result.size(); ++channel) {
        const double value = std::clamp(color[channel], 0.0, 1.0);
        result[channel] = static_cast<uint8_t>(std::lround(value * 255.0));
    }
    return result;
}

std::array<uint8_t, 7> glyph(char ch) {
    // Small deterministic 5x7 ASCII subset used only for print-sheet labels.
    switch (ch) {
    case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15}; case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14}; case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
    case '-': return {0,0,0,31,0,0,0}; case '_': return {0,0,0,0,0,0,31};
    case '.': return {0,0,0,0,0,12,12}; case ':': return {0,12,12,0,12,12,0};
    case '/': return {1,2,2,4,8,8,16}; case '=': return {0,31,0,31,0,0,0};
    case '%': return {17,2,4,8,17,0,0}; case '+': return {0,4,4,31,4,4,0};
    default: return {31,17,5,9,4,0,4};
    }
}

void draw_text(std::span<uint8_t> rgba, uint32_t width, uint32_t height,
               uint32_t x, uint32_t y, std::string_view text,
               uint32_t scale = 2) {
    const uint32_t advance = 6 * scale;
    for (const unsigned char raw : text) {
        const char ch = raw >= 'a' && raw <= 'z'
            ? static_cast<char>(raw - 'a' + 'A') : static_cast<char>(raw);
        const auto rows = glyph(ch);
        for (uint32_t row = 0; row < rows.size(); ++row) {
            for (uint32_t col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) == 0) continue;
                fill_rect(rgba, width, height,
                    static_cast<int64_t>(x) + col * scale,
                    static_cast<int64_t>(y) + row * scale,
                    static_cast<int64_t>(x) + (col + 1) * scale,
                    static_cast<int64_t>(y) + (row + 1) * scale,
                    {20, 20, 20});
            }
        }
        if (advance > width - (std::min)(x, width)) break;
        x += advance;
    }
}

uint16_t read_u16le(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

double srgb_to_linear(double value) {
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value <= 0.0031308 ? value * 12.92
                              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

bool source_sample(const raster::RasterPixelView& source, uint32_t x, uint32_t y,
                   std::array<double, 4>& rgba) {
    if (x >= source.width || y >= source.height) return false;
    const bool sixteen = source.encoding == raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha;
    const size_t bytes_per_pixel = sixteen ? 8 : 4;
    const uint64_t offset = static_cast<uint64_t>(y) * source.row_stride +
                            static_cast<uint64_t>(x) * bytes_per_pixel;
    if (offset > source.bytes.size() || bytes_per_pixel > source.bytes.size() - offset)
        return false;
    if (sixteen) {
        for (size_t channel = 0; channel < 4; ++channel)
            rgba[channel] = read_u16le(source.bytes.data() + offset + channel * 2) / 65535.0;
    } else {
        for (size_t channel = 0; channel < 4; ++channel)
            rgba[channel] = source.bytes[offset + channel] / 255.0;
    }
    return true;
}

std::array<uint8_t, 3> sampled_on_white(const raster::RasterPixelBuffer& source,
                                        uint32_t x, uint32_t y) {
    std::array<double, 4> rgba{};
    if (!source_sample({source.bytes, source.width, source.height, source.row_stride,
                        source.encoding}, x, y, rgba))
        return {255, 255, 255};
    const double alpha = std::clamp(rgba[3], 0.0, 1.0);
    std::array<uint8_t, 3> result{};
    for (size_t channel = 0; channel < 3; ++channel) {
        const double linear = srgb_to_linear(rgba[channel]) * alpha + (1.0 - alpha);
        result[channel] = static_cast<uint8_t>(std::lround(linear_to_srgb(linear) * 255.0));
    }
    return result;
}

bool draw_image(std::span<uint8_t> destination, uint32_t dst_width,
                uint32_t dst_height, uint32_t left, uint32_t top,
                uint32_t draw_width, uint32_t draw_height,
                const raster::RasterPixelBuffer& source,
                std::optional<PixelRect> crop = std::nullopt) {
    const PixelRect region = crop.value_or(PixelRect{0, 0, source.width, source.height});
    if (region.width == 0 || region.height == 0 || region.x > source.width ||
        region.y > source.height || region.width > source.width - region.x ||
        region.height > source.height - region.y || draw_width == 0 || draw_height == 0)
        return false;
    const uint32_t clipped_width = left >= dst_width ? 0 :
        (std::min)(draw_width, dst_width - left);
    const uint32_t clipped_height = top >= dst_height ? 0 :
        (std::min)(draw_height, dst_height - top);
    for (uint32_t y = 0; y < clipped_height; ++y) {
        const double fy = ((static_cast<double>(y) + 0.5) * region.height / draw_height) - 0.5;
        const double clamped_y = std::clamp(fy, 0.0, static_cast<double>(region.height - 1));
        const uint32_t y0 = static_cast<uint32_t>(clamped_y);
        const uint32_t y1 = (std::min)(y0 + 1, region.height - 1);
        const double wy = clamped_y - y0;
        for (uint32_t x = 0; x < clipped_width; ++x) {
            const double fx = ((static_cast<double>(x) + 0.5) * region.width / draw_width) - 0.5;
            const double clamped_x = std::clamp(fx, 0.0, static_cast<double>(region.width - 1));
            const uint32_t x0 = static_cast<uint32_t>(clamped_x);
            const uint32_t x1 = (std::min)(x0 + 1, region.width - 1);
            const double wx = clamped_x - x0;
            const auto p00 = sampled_on_white(source, region.x + x0, region.y + y0);
            const auto p10 = sampled_on_white(source, region.x + x1, region.y + y0);
            const auto p01 = sampled_on_white(source, region.x + x0, region.y + y1);
            const auto p11 = sampled_on_white(source, region.x + x1, region.y + y1);
            const size_t out = (static_cast<size_t>(top + y) * dst_width + left + x) * 4;
            for (size_t c = 0; c < 3; ++c) {
                const double top_value = p00[c] * (1.0 - wx) + p10[c] * wx;
                const double bottom_value = p01[c] * (1.0 - wx) + p11[c] * wx;
                destination[out + c] = static_cast<uint8_t>(std::lround(
                    top_value * (1.0 - wy) + bottom_value * wy));
            }
            destination[out + 3] = 255;
        }
    }
    return true;
}

enum class VariationParameter : uint8_t { Exposure, Hue, Saturation, Lightness, Vibrance, Temperature, Tint };

std::optional<VariationParameter> parse_parameter(const VariationAxis& axis) {
    const auto id = std::string_view(axis.parameter_id);
    const auto unit = std::string_view(axis.unit);
    if (id == "exposure" && unit == "EV") return VariationParameter::Exposure;
    if (id == "hue" && (unit == "degree" || unit == "degrees")) return VariationParameter::Hue;
    if (id == "saturation" && unit == "%") return VariationParameter::Saturation;
    if (id == "lightness" && unit == "%") return VariationParameter::Lightness;
    if (id == "vibrance" && unit == "%") return VariationParameter::Vibrance;
    if (id == "temperature" && unit == "%") return VariationParameter::Temperature;
    if (id == "tint" && unit == "%") return VariationParameter::Tint;
    return std::nullopt;
}

struct VariationValues {
    std::optional<double> exposure;
    std::optional<double> hue;
    std::optional<double> saturation;
    std::optional<double> lightness;
    std::optional<double> vibrance;
    std::optional<double> temperature;
    std::optional<double> tint;
};

bool apply_variation_value(VariationValues& values, VariationParameter parameter,
                           double value) {
    switch (parameter) {
    case VariationParameter::Exposure:
        if (value < -10.0 || value > 10.0) return false;
        values.exposure = value; return true;
    case VariationParameter::Hue:
        if (value < -180.0 || value > 180.0) return false;
        values.hue = value; return true;
    case VariationParameter::Saturation:
        if (value < -100.0 || value > 100.0) return false;
        values.saturation = value; return true;
    case VariationParameter::Lightness:
        if (value < -100.0 || value > 100.0) return false;
        values.lightness = value; return true;
    case VariationParameter::Vibrance:
        if (value < -100.0 || value > 100.0) return false;
        values.vibrance = value; return true;
    case VariationParameter::Temperature:
        if (value < -100.0 || value > 100.0) return false;
        values.temperature = value; return true;
    case VariationParameter::Tint:
        if (value < -100.0 || value > 100.0) return false;
        values.tint = value; return true;
    }
    return false;
}

bool variation_graph(const document::RasterEditGraph& base,
                     const TestSheetElement& cell,
                     const VariationAxis& x_axis,
                     const VariationAxis& y_axis,
                     document::RasterEditGraph& output) {
    const auto x_parameter = parse_parameter(x_axis);
    const auto y_parameter = y_axis.parameter_id.empty()
        ? std::optional<VariationParameter>{} : parse_parameter(y_axis);
    if (!x_parameter || (!y_axis.parameter_id.empty() && !y_parameter)) return false;
    VariationValues values;
    if (!apply_variation_value(values, *x_parameter, cell.x_value) ||
        (y_parameter && !apply_variation_value(values, *y_parameter, cell.y_value)))
        return false;
    output = base;
    uint64_t next_id = 1;
    for (const auto& node : output.nodes) {
        if (!node.id.valid() || node.id.value == std::numeric_limits<uint64_t>::max()) return false;
        next_id = (std::max)(next_id, node.id.value + 1);
    }
    auto append = [&](document::RasterEditParameters parameters) {
        if (next_id == std::numeric_limits<uint64_t>::max() ||
            output.nodes.size() >= document::RasterEditGraph::kMaxNodes) return false;
        output.nodes.push_back({{next_id++}, true, std::move(parameters), 1});
        return true;
    };
    if (values.exposure && !append(document::RasterExposureParams{*values.exposure})) return false;
    if ((values.hue || values.saturation || values.lightness) &&
        !append(document::RasterHslParams{values.hue.value_or(0.0),
            values.saturation.value_or(0.0), values.lightness.value_or(0.0)})) return false;
    if (values.vibrance && !append(document::RasterVibranceParams{*values.vibrance})) return false;
    if ((values.temperature || values.tint) &&
        !append(document::RasterWhiteBalanceParams{values.temperature.value_or(0.0),
            values.tint.value_or(0.0)})) return false;
    output.schema_version = document::RasterEditGraph::kSchemaVersion;
    return true;
}

raster::RasterEvaluationResult evaluate(
    const raster::RasterPixelView& source, const document::RasterEditGraph& graph,
    raster::RasterMemoryBudget& budget, const TestSheetRenderOptions& options) {
    raster::RasterEvaluationOptions evaluation_options;
    evaluation_options.cancelled = options.cancelled;
    evaluation_options.progress = [&](const raster::RasterEvaluationProgress& progress) {
        if (options.progress)
            options.progress({0, 0, progress.completed_rows, progress.total_rows});
    };
    return raster::RasterEditEvaluator::evaluate(source, graph, budget, evaluation_options);
}

void draw_ruler(std::span<uint8_t> pixels, uint32_t width, uint32_t height,
                uint32_t x, uint32_t y, uint32_t width_px, uint32_t ppi) {
    const uint32_t baseline = y + (std::min)(static_cast<uint32_t>(std::lround(ppi * 0.8)), height - (std::min)(y, height));
    fill_rect(pixels, width, height, x, y, static_cast<int64_t>(x) + width_px, baseline + 1, {0, 0, 0});
    for (uint32_t mm = 0; mm <= 100; ++mm) {
        const uint32_t px = x + static_cast<uint32_t>(std::llround(
            static_cast<double>(mm) * ppi / kMmPerInch));
        const uint32_t tick_height = mm % 10 == 0 ? ppi / 2 : mm % 5 == 0 ? ppi / 3 : ppi / 5;
        const uint32_t tick_top = tick_height > baseline ? 0 : baseline - tick_height;
        fill_rect(pixels, width, height, px, tick_top,
                  static_cast<int64_t>(px) + (std::max)(1u, ppi / 150), baseline + 1, {0, 0, 0});
    }
    draw_text(pixels, width, height, x, y,
              "100MM RULER " + std::to_string(width_px) + "PX", (std::max)(1u, ppi / 300));
}

} // namespace

TestSheetRenderResult render_test_sheet(const PrintTestSheetPlan& plan,
    const raster::RasterPixelView& source,
    const TestSheetRenderOptions& options) {
    try {
    if (options.memory_limit_bytes == 0 || options.memory_limit_bytes > kMaximumRenderMemory ||
        plan.schema_version != 1 || !validate_output_condition(plan.condition) ||
        !std::isfinite(plan.page_width_mm) || !std::isfinite(plan.page_height_mm) ||
        plan.page_width_mm <= 0.0 || plan.page_height_mm <= 0.0 ||
        plan.ppi_x == 0 || plan.ppi_y == 0 || plan.elements.empty())
        return fail(TestSheetRenderStatus::InvalidPlan,
                    "Test-sheet plan has unsupported schema or invalid output geometry.");
    if (source.width != plan.source.width_px || source.height != plan.source.height_px ||
        plan.source.document_id.empty() || plan.source.object_id == document::kInvalidObjectId ||
        plan.source.document_revision == 0 || plan.source.source_revision == 0)
        return fail(TestSheetRenderStatus::SourceMismatch,
                    "Raster source dimensions or immutable revision do not match the frozen test plan.");
    if (is_cancelled(options))
        return fail(TestSheetRenderStatus::Cancelled, "Test-sheet render was cancelled.");

    const auto width = millimeters_to_pixels(plan.page_width_mm, plan.ppi_x);
    const auto height = millimeters_to_pixels(plan.page_height_mm, plan.ppi_y);
    if (!width || !height)
        return fail(TestSheetRenderStatus::InvalidPlan,
                    "Page dimensions cannot be represented at the requested resolution.");
    uint64_t page_bytes = 0;
    if (!checked_surface_bytes(*width, *height, page_bytes) ||
        page_bytes > options.memory_limit_bytes)
        return fail(TestSheetRenderStatus::MemoryLimit,
                    "Page raster exceeds the explicit test-sheet memory budget.");

    TestSheetRaster sheet;
    sheet.width_px = *width;
    sheet.height_px = *height;
    sheet.ppi_x = plan.ppi_x;
    sheet.ppi_y = plan.ppi_y;
    sheet.image.width = *width;
    sheet.image.height = *height;
    sheet.image.row_stride = static_cast<uint64_t>(*width) * 4;
    sheet.image.encoding = raster::RasterPixelEncoding::SRgbRgba8StraightAlpha;
    raster::RasterMemoryBudget budget(options.memory_limit_bytes,
        options.wait_for_memory_headroom);
    auto page_reservation = budget.try_reserve(page_bytes);
    if (!page_reservation)
        return fail(TestSheetRenderStatus::MemoryLimit,
                    "Page raster reservation was rejected by the memory budget.");
    sheet.image.reservation = page_reservation;
    try {
        sheet.image.bytes.resize(static_cast<size_t>(page_bytes), 255);
    } catch (...) {
        return fail(TestSheetRenderStatus::AllocationFailure,
                    "Could not allocate the bounded test-sheet page raster.");
    }

    uint32_t total_rows = 0;
    for (const auto& item : plan.elements) {
        total_rows += coordinate_px(item.bounds_mm.height, plan.ppi_y);
        if (item.kind == TestSheetElementKind::VariationCell)
            total_rows += (plan.source.height_px > 0 ? 1u : 0u);
    }
    uint32_t completed_rows = 0;
    uint32_t completed_elements = 0;
    const uint32_t total_elements = static_cast<uint32_t>(plan.elements.size());

    const bool needs_pixels = std::any_of(plan.elements.begin(), plan.elements.end(),
        [](const auto& item) {
            return item.kind == TestSheetElementKind::RasterCrop ||
                   item.kind == TestSheetElementKind::VariationCell;
        });
    std::optional<raster::RasterPixelBuffer> base_image;
    if (needs_pixels && plan.kind != TestSheetKind::VariationGrid) {
        auto evaluated = evaluate(source, plan.edit_graph, budget, options);
        if (!evaluated.succeeded())
            return fail(evaluated.status == raster::RasterEvaluationStatus::Cancelled
                ? TestSheetRenderStatus::Cancelled
                : evaluated.status == raster::RasterEvaluationStatus::InsufficientMemory
                    ? TestSheetRenderStatus::MemoryLimit : TestSheetRenderStatus::EvaluationFailed,
                "Could not evaluate the frozen image graph for the test sheet: " + evaluated.diagnostic);
        if (evaluated.image.width != plan.evaluated_width_px ||
            evaluated.image.height != plan.evaluated_height_px)
            return fail(TestSheetRenderStatus::SourceMismatch,
                "Frozen output dimensions do not match the evaluated image graph.");
        base_image = std::move(evaluated.image);
    }

    for (const auto& item : plan.elements) {
        if (is_cancelled(options))
            return fail(TestSheetRenderStatus::Cancelled, "Test-sheet render was cancelled.");
        const uint32_t left = coordinate_px(item.bounds_mm.x, plan.ppi_x);
        const uint32_t top = coordinate_px(item.bounds_mm.y, plan.ppi_y);
        const uint32_t item_width = coordinate_px(item.bounds_mm.width, plan.ppi_x);
        const uint32_t item_height = coordinate_px(item.bounds_mm.height, plan.ppi_y);
        const int64_t right = static_cast<int64_t>(left) + item_width;
        const int64_t bottom = static_cast<int64_t>(top) + item_height;
        switch (item.kind) {
        case TestSheetElementKind::RunLabel:
            fill_rect(sheet.image.bytes, *width, *height, left, top, right, bottom, {238, 238, 238});
            draw_text(sheet.image.bytes, *width, *height, left + 3, top + 3,
                "PIKDRAW RUN " + plan.run_id + " CONDITION " + plan.condition.id,
                (std::max)(1u, plan.ppi_x / 300));
            break;
        case TestSheetElementKind::GrayPatch:
        case TestSheetElementKind::ProcessColorPatch:
            fill_rect(sheet.image.bytes, *width, *height, left, top, right, bottom,
                      byte_color(item.source_srgb));
            if (item_width > 80 && item_height > 22)
                draw_text(sheet.image.bytes, *width, *height, left + 3, top + 3,
                          item.label, (std::max)(1u, plan.ppi_x / 300));
            break;
        case TestSheetElementKind::RasterCrop: {
            if (!base_image || !item.source_crop)
                return fail(TestSheetRenderStatus::InvalidPlan,
                            "The crop element is missing its immutable source rectangle.");
            if (!draw_image(sheet.image.bytes, *width, *height, left, top,
                            item_width, item_height, *base_image, item.source_crop))
                return fail(TestSheetRenderStatus::EvaluationFailed,
                            "The evaluated source does not contain the frozen 1:1 crop rectangle.");
            break;
        }
        case TestSheetElementKind::CalibrationRuler: {
            const uint32_t ruler_width = (std::min)(item_width, *width - (std::min)(left, *width));
            const uint32_t ruler_height = (std::min)(item_height, *height - (std::min)(top, *height));
            draw_ruler(sheet.image.bytes, *width, *height, left, top,
                       ruler_width, plan.ppi_x);
            sheet.one_hundred_mm_ruler_actual_mm =
                static_cast<double>(ruler_width) * kMmPerInch / plan.ppi_x;
            draw_text(sheet.image.bytes, *width, *height, left, top + ruler_height / 2,
                      "MEASURE BETWEEN END TICKS", (std::max)(1u, plan.ppi_x / 300));
            break;
        }
        case TestSheetElementKind::VariationCell: {
            document::RasterEditGraph variant;
            if (!variation_graph(plan.edit_graph, item, plan.x_axis, plan.y_axis, variant)) {
                return fail(TestSheetRenderStatus::UnsupportedVariation,
                    "Variation axis is not a supported raster control or exceeds its declared range.");
            }
            auto evaluated = evaluate(source, variant, budget, options);
            if (!evaluated.succeeded())
                return fail(evaluated.status == raster::RasterEvaluationStatus::Cancelled
                    ? TestSheetRenderStatus::Cancelled
                    : evaluated.status == raster::RasterEvaluationStatus::InsufficientMemory
                        ? TestSheetRenderStatus::MemoryLimit : TestSheetRenderStatus::EvaluationFailed,
                    "Could not evaluate a variation cell: " + evaluated.diagnostic);
            if (evaluated.image.width != plan.evaluated_width_px ||
                evaluated.image.height != plan.evaluated_height_px)
                return fail(TestSheetRenderStatus::SourceMismatch,
                    "Variation graph dimensions differ from the frozen test plan.");
            if (item_width < 20 || item_height < 24)
                return fail(TestSheetRenderStatus::InvalidPlan,
                            "Variation cells are too small for an identifying label.");
            fill_rect(sheet.image.bytes, *width, *height, left, top, right, bottom,
                      item.is_baseline ? std::array<uint8_t, 3>{242, 242, 242}
                                       : std::array<uint8_t, 3>{248, 248, 248});
            const uint32_t label_height = (std::max)(18u, plan.ppi_y / 8);
            fill_rect(sheet.image.bytes, *width, *height, left, top, right,
                      static_cast<int64_t>(top) + label_height, {230, 230, 230});
            draw_text(sheet.image.bytes, *width, *height, left + 3, top + 3,
                      item.is_baseline ? "BASELINE" : item.label,
                      (std::max)(1u, plan.ppi_x / 300));
            const uint32_t available_width = (std::min)(item_width,
                *width - (std::min)(*width, left));
            const uint32_t available_height = item_height > label_height
                ? item_height - label_height : 0;
            if (available_width == 0 || available_height == 0)
                return fail(TestSheetRenderStatus::InvalidPlan,
                            "Variation cell has no image area after its label.");
            const double scale = (std::min)(
                static_cast<double>(available_width) / evaluated.image.width,
                static_cast<double>(available_height) / evaluated.image.height);
            const uint32_t draw_width = (std::max)(1u, static_cast<uint32_t>(
                std::floor(evaluated.image.width * scale)));
            const uint32_t draw_height = (std::max)(1u, static_cast<uint32_t>(
                std::floor(evaluated.image.height * scale)));
            const uint32_t image_left = left + (available_width - draw_width) / 2;
            const uint32_t image_top = top + label_height +
                (available_height - draw_height) / 2;
            if (!draw_image(sheet.image.bytes, *width, *height, image_left, image_top,
                            draw_width, draw_height, evaluated.image))
                return fail(TestSheetRenderStatus::EvaluationFailed,
                            "Could not place the evaluated variation image into its cell.");
            break;
        }
        }
        ++completed_elements;
        completed_rows += coordinate_px(item.bounds_mm.height, plan.ppi_y);
        if (options.progress) {
            try { options.progress({completed_elements, total_elements,
                                    completed_rows, total_rows}); }
            catch (...) { }
        }
    }
    sheet.one_hundred_mm_ruler_actual_mm = sheet.one_hundred_mm_ruler_actual_mm == 0.0
        ? 0.0 : sheet.one_hundred_mm_ruler_actual_mm;
    return {TestSheetRenderStatus::Success, std::move(sheet), {}};
    } catch (const std::bad_alloc&) {
        return fail(TestSheetRenderStatus::AllocationFailure,
                    "Could not allocate bounded test-sheet render state.");
    } catch (const std::exception& error) {
        return fail(TestSheetRenderStatus::EvaluationFailed,
                    std::string("Test-sheet rendering failed: ") + error.what());
    } catch (...) {
        return fail(TestSheetRenderStatus::EvaluationFailed,
                    "Unexpected test-sheet rendering failure.");
    }
}

} // namespace pikdraw::production::print
