#include "pikdraw/production/print/test_sheet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace pikdraw::production::print {
namespace {
constexpr double kMmPerInch = 25.4;
constexpr double kHeaderHeightMm = 8.0;
constexpr double kFooterHeightMm = 6.0;
constexpr double kLayoutGapMm = 6.0;
constexpr size_t kMaxPlanElements = 256;

TestSheetPlanResult failure(TestSheetPlanError error, std::string diagnostic) {
    return {std::nullopt, error, std::move(diagnostic)};
}

bool valid_identifier(std::string_view value, size_t max_bytes = 256) {
    if (value.empty() || value.size() > max_bytes) return false;
    for (const unsigned char byte : value)
        if (byte == 0 || byte < 0x20) return false;
    return true;
}

bool finite_positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

bool valid_sha256(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
        [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

TestSheetElement element(TestSheetElementKind kind, std::string id,
                         std::string label, geometry::Rect2D bounds) {
    TestSheetElement result;
    result.kind = kind;
    result.stable_id = std::move(id);
    result.label = std::move(label);
    result.bounds_mm = bounds;
    return result;
}

std::vector<std::pair<std::string, std::array<double, 3>>> diagnostic_colors() {
    std::vector<std::pair<std::string, std::array<double, 3>>> patches;
    patches.reserve(19);
    for (uint32_t step = 0; step <= 10; ++step) {
        const double value = static_cast<double>(step) / 10.0;
        patches.emplace_back("gray-" + std::to_string(step * 10),
                             std::array<double, 3>{value, value, value});
    }
    patches.emplace_back("red", std::array<double, 3>{1.0, 0.0, 0.0});
    patches.emplace_back("green", std::array<double, 3>{0.0, 1.0, 0.0});
    patches.emplace_back("blue", std::array<double, 3>{0.0, 0.0, 1.0});
    patches.emplace_back("cyan", std::array<double, 3>{0.0, 1.0, 1.0});
    patches.emplace_back("magenta", std::array<double, 3>{1.0, 0.0, 1.0});
    patches.emplace_back("yellow", std::array<double, 3>{1.0, 1.0, 0.0});
    patches.emplace_back("black", std::array<double, 3>{0.0, 0.0, 0.0});
    patches.emplace_back("white", std::array<double, 3>{1.0, 1.0, 1.0});
    return patches;
}

void append_chart(PrintTestSheetPlan& plan, double x, double y,
                  double patch_size, double patch_gap) {
    constexpr uint32_t columns = 6;
    const auto patches = diagnostic_colors();
    for (size_t index = 0; index < patches.size(); ++index) {
        const uint32_t row = static_cast<uint32_t>(index / columns);
        const uint32_t column = static_cast<uint32_t>(index % columns);
        const double left = x + column * (patch_size + patch_gap);
        const double top = y + row * (patch_size + patch_gap);
        const auto kind = index < 11 ? TestSheetElementKind::GrayPatch
                                     : TestSheetElementKind::ProcessColorPatch;
        auto patch = element(kind, "chart." + patches[index].first,
            patches[index].first,
            {left, top, patch_size, patch_size});
        patch.source_srgb = patches[index].second;
        patch.has_source_srgb = true;
        patch.row = row;
        patch.column = column;
        plan.elements.push_back(std::move(patch));
    }
}

bool crop_is_valid(const PixelRect& crop, const RasterSourceSnapshot& source) {
    if (crop.width == 0 || crop.height == 0 || source.width_px == 0 ||
        source.height_px == 0 || crop.x > source.width_px ||
        crop.y > source.height_px)
        return false;
    return crop.width <= source.width_px - crop.x &&
           crop.height <= source.height_px - crop.y;
}

bool valid_axis(const VariationAxis& axis, uint32_t cells) {
    if (!valid_identifier(axis.parameter_id, 128) ||
        !valid_identifier(axis.unit, 32) || axis.values.size() != cells)
        return false;
    return std::all_of(axis.values.begin(), axis.values.end(),
                       [](double value) { return std::isfinite(value); }) &&
           axis.values[cells / 2] == 0.0;
}

} // namespace

OutputConditionValidation validate_output_condition(
    const OutputCondition& condition) {
    switch (condition.route) {
    case PrintRoute::RgbFileExport:
    case PrintRoute::DriverManagedRgb:
    case PrintRoute::ApplicationManagedRgb:
    case PrintRoute::SystemManagedRgb:
        break;
    case PrintRoute::RipFile:
        return {OutputConditionError::UnsupportedRoute,
                "The current test-sheet renderer only supports RGB file and printer routes."};
    default:
        return {OutputConditionError::UnsupportedRoute,
                "Unknown print route cannot be used for a test sheet."};
    }
    if (!valid_identifier(condition.id, 128))
        return {OutputConditionError::InvalidIdentity,
                "Output condition ID is missing, too long, or contains control bytes."};
    if (condition.revision == 0)
        return {OutputConditionError::InvalidRevision,
                "Output condition revision must be positive."};
    if (!condition.profile)
        return {OutputConditionError::MissingRgbProfile,
                "An RGB ICC resource is required for this test-sheet route."};
    const auto& identity = condition.profile->identity();
    if (identity.color_space != color::ColorSpace::Rgb ||
        identity.checksum_sha256.empty() || condition.profile->bytes().empty())
        return {OutputConditionError::InvalidRgbProfile,
                "Output profile must be a validated, byte-backed RGB ICC resource."};
    if (condition.target_ppi_x == 0 || condition.target_ppi_y == 0 ||
        condition.target_ppi_x > 9600 || condition.target_ppi_y > 9600)
        return {OutputConditionError::InvalidResolution,
                "Target PPI must be between 1 and 9600 on both axes."};
    if (condition.route == PrintRoute::DriverManagedRgb) {
        if (!valid_identifier(condition.device_id, 256))
            return {OutputConditionError::MissingDevice,
                    "Driver-managed output requires an identified device."};
        if (condition.driver_color_state == DriverColorState::Unknown)
            return {OutputConditionError::DriverColorOwnerUnknown,
                    "Driver color ownership is unknown; this route cannot be marked verified."};
        if (condition.driver_color_state != DriverColorState::Managed)
            return {OutputConditionError::DriverColorOwnerMismatch,
                    "Driver-managed output requires the driver color-management state to be enabled."};
    }
    if (condition.route == PrintRoute::ApplicationManagedRgb &&
        condition.driver_color_state != DriverColorState::Disabled) {
        return {condition.driver_color_state == DriverColorState::Unknown
                    ? OutputConditionError::DriverColorOwnerUnknown
                    : OutputConditionError::DriverColorOwnerMismatch,
                "Application-managed output requires a verified driver color-management-off state."};
    }
    if (condition.route == PrintRoute::SystemManagedRgb) {
        if (!valid_identifier(condition.device_id, 256))
            return {OutputConditionError::MissingDevice,
                    "System-managed output requires an identified device."};
        if (condition.driver_color_state != DriverColorState::Disabled)
            return {condition.driver_color_state == DriverColorState::Unknown
                        ? OutputConditionError::DriverColorOwnerUnknown
                        : OutputConditionError::DriverColorOwnerMismatch,
                    "System-managed output requires driver ICM to be disabled to avoid a second conversion."};
    }
    if (condition.require_destination_profile_identity &&
        !valid_sha256(condition.destination_profile_sha256))
        return {OutputConditionError::MissingDestinationProfileIdentity,
                "The output route requires a 64-character SHA-256 identity for the destination profile."};
    return {};
}

TestSheetPlanResult plan_test_sheet(const TestSheetRequest& request) {
    switch (request.kind) {
    case TestSheetKind::Diagnostic:
    case TestSheetKind::OneToOneCrop:
    case TestSheetKind::CropAndDiagnostic:
    case TestSheetKind::VariationGrid:
        break;
    default:
        return failure(TestSheetPlanError::InvalidIdentity,
                       "Unknown test-sheet kind.");
    }
    if (!valid_identifier(request.run_id, 128) ||
        !valid_identifier(request.source.document_id, 128) ||
        request.source.document_revision == 0 ||
        request.source.object_id == document::kInvalidObjectId ||
        request.source.source_revision == 0)
        return failure(TestSheetPlanError::InvalidIdentity,
                       "Run, document, object, and source revision identities are required.");
    if (request.source.width_px == 0 || request.source.height_px == 0)
        return failure(TestSheetPlanError::InvalidSource,
                       "The frozen raster source dimensions must be positive.");
    const auto condition = validate_output_condition(request.condition);
    if (!condition)
        return failure(TestSheetPlanError::InvalidOutputCondition,
                       condition.diagnostic);
    if (!finite_positive(request.page_width_mm) ||
        !finite_positive(request.page_height_mm) ||
        !finite_positive(request.margin_mm) ||
        request.page_width_mm > 10000.0 || request.page_height_mm > 10000.0 ||
        request.margin_mm * 2.0 >= request.page_width_mm ||
        request.margin_mm * 2.0 >= request.page_height_mm)
        return failure(TestSheetPlanError::InvalidPageGeometry,
                       "Page dimensions and margins must be finite and leave a positive printable area.");
    if (!finite_positive(request.patch_size_mm) ||
        !std::isfinite(request.patch_gap_mm) || request.patch_gap_mm < 0.0 ||
        request.patch_size_mm > 100.0 || request.patch_gap_mm > 100.0)
        return failure(TestSheetPlanError::InvalidPatchGeometry,
                       "Patch size must be positive and the gap must be finite and non-negative.");

    PrintTestSheetPlan plan;
    plan.run_id = request.run_id;
    plan.kind = request.kind;
    plan.source = request.source;
    plan.edit_graph = request.edit_graph;
    plan.condition = request.condition;
    plan.evaluated_width_px = request.evaluated_width_px == 0
        ? request.source.width_px : request.evaluated_width_px;
    plan.evaluated_height_px = request.evaluated_height_px == 0
        ? request.source.height_px : request.evaluated_height_px;
    plan.page_width_mm = request.page_width_mm;
    plan.page_height_mm = request.page_height_mm;
    plan.ppi_x = request.condition.target_ppi_x;
    plan.ppi_y = request.condition.target_ppi_y;
    if (request.kind == TestSheetKind::VariationGrid) {
        plan.x_axis = request.x_axis;
        plan.y_axis = request.y_axis;
    }
    plan.elements.reserve(32);
    plan.elements.push_back(element(TestSheetElementKind::RunLabel,
        "run.label", "PikDraw test run " + request.run_id,
        {request.margin_mm, request.margin_mm,
         request.page_width_mm - request.margin_mm * 2.0, kHeaderHeightMm}));

    const double content_left = request.margin_mm;
    const double content_top = request.margin_mm + kHeaderHeightMm + kLayoutGapMm;
    const double content_right = request.page_width_mm - request.margin_mm;
    const double content_bottom = request.page_height_mm - request.margin_mm - kFooterHeightMm;
    const double content_width = content_right - content_left;
    const double content_height = content_bottom - content_top;
    if (content_width <= 0.0 || content_height <= 0.0)
        return failure(TestSheetPlanError::InvalidPageGeometry,
                       "Page is too small for the header, footer, and printable margins.");

    if (request.kind == TestSheetKind::VariationGrid) {
        const bool has_y_axis = !request.y_axis.parameter_id.empty();
        const uint32_t columns = request.variation_columns;
        const uint32_t rows = has_y_axis ? request.variation_rows : 1u;
        if ((columns != 3 && columns != 5 && columns != 7) ||
            (has_y_axis && rows != 3 && rows != 5 && rows != 7) ||
            (!has_y_axis && request.variation_rows != 1) ||
            !valid_axis(request.x_axis, columns) ||
            (has_y_axis && (!valid_axis(request.y_axis, rows) ||
                            request.x_axis.parameter_id == request.y_axis.parameter_id)))
            return failure(TestSheetPlanError::InvalidVariationGrid,
                           "Variation axes require distinct parameter IDs, finite values, and a zero-valued center cell.");
        if (static_cast<size_t>(columns) * rows > kMaxPlanElements)
            return failure(TestSheetPlanError::TooManyElements,
                           "Variation grid exceeds the plan element budget.");
        const double cell_width = content_width / columns;
        const double cell_height = content_height / rows;
        for (uint32_t row = 0; row < rows; ++row) {
            for (uint32_t column = 0; column < columns; ++column) {
                const double cell_left = content_left + column * cell_width;
                const double cell_top = content_top + row * cell_height;
                const double cell_right = column + 1 == columns
                    ? content_right : cell_left + cell_width;
                const double cell_bottom = row + 1 == rows
                    ? content_bottom : cell_top + cell_height;
                auto cell = element(TestSheetElementKind::VariationCell,
                    "variant." + std::to_string(row) + "." +
                        std::to_string(column),
                    request.x_axis.parameter_id + "=" +
                        std::to_string(request.x_axis.values[column]),
                    {cell_left, cell_top, cell_right - cell_left,
                     cell_bottom - cell_top});
                cell.row = row;
                cell.column = column;
                cell.x_value = request.x_axis.values[column];
                cell.y_value = has_y_axis ? request.y_axis.values[row] : 0.0;
                cell.is_baseline = column == columns / 2 && row == rows / 2;
                if (has_y_axis)
                    cell.label += ", " + request.y_axis.parameter_id + "=" +
                        std::to_string(request.y_axis.values[row]);
                if (cell.is_baseline) cell.label = "Baseline · " + cell.label;
                plan.elements.push_back(std::move(cell));
            }
        }
    } else {
        const bool has_crop = request.kind == TestSheetKind::OneToOneCrop ||
                              request.kind == TestSheetKind::CropAndDiagnostic;
        const bool has_chart = request.kind == TestSheetKind::Diagnostic ||
                               request.kind == TestSheetKind::CropAndDiagnostic;
        double crop_width_mm = 0.0;
        double crop_height_mm = 0.0;
        if (has_crop) {
            const RasterSourceSnapshot evaluated_source{
                request.source.document_id, request.source.document_revision,
                request.source.object_id, request.source.source_revision,
                plan.evaluated_width_px, plan.evaluated_height_px};
            if (!crop_is_valid(request.crop, evaluated_source))
                return failure(TestSheetPlanError::InvalidCrop,
                               "Crop rectangle must be positive and remain within the frozen source pixels.");
            crop_width_mm = static_cast<double>(request.crop.width) * kMmPerInch /
                            plan.ppi_x;
            crop_height_mm = static_cast<double>(request.crop.height) * kMmPerInch /
                             plan.ppi_y;
        }
        if (has_chart) {
            constexpr uint32_t chart_columns = 6;
            constexpr uint32_t chart_rows = 4;
            const double chart_width = chart_columns * request.patch_size_mm +
                                       (chart_columns - 1) * request.patch_gap_mm;
            const double chart_height = chart_rows * request.patch_size_mm +
                                        (chart_rows - 1) * request.patch_gap_mm;
            double chart_x = content_left;
            double chart_y = content_top;
            if (has_crop) {
                if (crop_width_mm + kLayoutGapMm + chart_width <= content_width) {
                    chart_x = content_left + crop_width_mm + kLayoutGapMm;
                } else {
                    chart_y = content_top + crop_height_mm + kLayoutGapMm;
                }
            }
            if (chart_x + chart_width > content_right ||
                chart_y + chart_height > content_bottom)
                return failure(TestSheetPlanError::LayoutOverflow,
                               "Crop and diagnostic chart do not fit the requested sheet at the selected PPI.");
            append_chart(plan, chart_x, chart_y,
                         request.patch_size_mm, request.patch_gap_mm);
        }
        double ruler_y = content_top;
        if (has_crop) {
            auto crop = element(TestSheetElementKind::RasterCrop, "source.crop",
                "Source crop at 1:1", {content_left, content_top,
                                       crop_width_mm, crop_height_mm});
            crop.source_crop = request.crop;
            plan.elements.push_back(std::move(crop));
            ruler_y = content_top + crop_height_mm + kLayoutGapMm;
        }
        if (has_chart) {
            constexpr uint32_t chart_rows = 4;
            const double chart_height = chart_rows * request.patch_size_mm +
                                        (chart_rows - 1) * request.patch_gap_mm;
            double chart_y = content_top;
            if (has_crop) {
                const double chart_width = 6 * request.patch_size_mm +
                                           5 * request.patch_gap_mm;
                if (crop_width_mm + kLayoutGapMm + chart_width > content_width)
                    chart_y = content_top + crop_height_mm + kLayoutGapMm;
            }
            ruler_y = (std::max)(ruler_y, chart_y + chart_height + kLayoutGapMm);
        }
        if (content_left + plan.one_hundred_mm_ruler_mm > content_right ||
            ruler_y + request.patch_size_mm > content_bottom)
            return failure(TestSheetPlanError::LayoutOverflow,
                           "A 100 mm calibration ruler and its labels must fit inside the printable area.");
        plan.elements.push_back(element(TestSheetElementKind::CalibrationRuler,
            "calibration.ruler.100mm", "100 mm scale",
            {content_left, ruler_y, plan.one_hundred_mm_ruler_mm,
             request.patch_size_mm}));
    }

    std::unordered_set<std::string> element_ids;
    for (const auto& item : plan.elements) {
        if (!item.bounds_mm.is_finite() || item.bounds_mm.is_empty() ||
            item.bounds_mm.x < request.margin_mm ||
            item.bounds_mm.y < request.margin_mm ||
            item.bounds_mm.right() > request.page_width_mm - request.margin_mm ||
            item.bounds_mm.bottom() > request.page_height_mm - request.margin_mm ||
            !element_ids.insert(item.stable_id).second)
            return failure(TestSheetPlanError::LayoutOverflow,
                           "Generated sheet element is outside the page or has a duplicate stable ID.");
    }
    if (plan.elements.size() > kMaxPlanElements)
        return failure(TestSheetPlanError::TooManyElements,
                       "Test sheet exceeds the element count budget.");
    return {std::move(plan), TestSheetPlanError::None, {}};
}

} // namespace pikdraw::production::print
