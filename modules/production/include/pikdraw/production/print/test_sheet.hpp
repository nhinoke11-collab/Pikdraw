#pragma once

#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/model/raster_edit_graph.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::production::print {

enum class PrintRoute : uint8_t {
    RgbFileExport,
    DriverManagedRgb,
    ApplicationManagedRgb,
    SystemManagedRgb,
    RipFile
};

enum class DriverColorState : uint8_t { Unknown, Managed, Disabled };

/// Frozen condition data for one output/test job. `profile` is the RGB source
/// ICC content; `destination_profile_sha256` identifies the file selected by
/// the output device/WCS when that identity is available and required.
struct OutputCondition {
    std::string id;
    uint32_t revision{1};
    std::string device_id;
    std::string media_id;
    std::string ink_set_id;
    std::string print_mode_id;
    std::shared_ptr<const color::ProfileResource> profile;
    std::string destination_profile_sha256;
    bool require_destination_profile_identity{false};
    PrintRoute route{PrintRoute::RgbFileExport};
    DriverColorState driver_color_state{DriverColorState::Unknown};
    uint32_t target_ppi_x{300};
    uint32_t target_ppi_y{300};
};

enum class OutputConditionError : uint8_t {
    None,
    InvalidIdentity,
    InvalidRevision,
    MissingRgbProfile,
    InvalidRgbProfile,
    MissingDevice,
    DriverColorOwnerUnknown,
    DriverColorOwnerMismatch,
    MissingDestinationProfileIdentity,
    UnsupportedRoute,
    InvalidResolution
};

struct OutputConditionValidation {
    OutputConditionError error{OutputConditionError::None};
    std::string diagnostic;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == OutputConditionError::None;
    }
};

[[nodiscard]] OutputConditionValidation validate_output_condition(
    const OutputCondition& condition);

enum class TestSheetKind : uint8_t {
    Diagnostic,
    OneToOneCrop,
    CropAndDiagnostic,
    VariationGrid
};

struct RasterSourceSnapshot {
    std::string document_id;
    uint64_t document_revision{0};
    document::ObjectId object_id{document::kInvalidObjectId};
    uint64_t source_revision{0};
    uint32_t width_px{0};
    uint32_t height_px{0};
};

struct PixelRect {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
    bool operator==(const PixelRect&) const = default;
};

struct VariationAxis {
    std::string parameter_id;
    std::string unit;
    std::vector<double> values;
};

struct TestSheetRequest {
    std::string run_id;
    TestSheetKind kind{TestSheetKind::Diagnostic};
    RasterSourceSnapshot source;
    document::RasterEditGraph edit_graph;
    OutputCondition condition;
    uint32_t evaluated_width_px{0};
    uint32_t evaluated_height_px{0};
    double page_width_mm{210.0};
    double page_height_mm{297.0};
    double margin_mm{10.0};
    double patch_size_mm{12.0};
    double patch_gap_mm{3.0};
    PixelRect crop;
    uint32_t variation_columns{3};
    uint32_t variation_rows{1};
    VariationAxis x_axis;
    VariationAxis y_axis;
};

enum class TestSheetElementKind : uint8_t {
    RunLabel,
    RasterCrop,
    ProcessColorPatch,
    GrayPatch,
    CalibrationRuler,
    VariationCell
};

struct TestSheetElement {
    TestSheetElementKind kind{TestSheetElementKind::RunLabel};
    std::string stable_id;
    std::string label;
    geometry::Rect2D bounds_mm;
    std::optional<PixelRect> source_crop;
    std::array<double, 3> source_srgb{};
    bool has_source_srgb{false};
    uint32_t row{0};
    uint32_t column{0};
    double x_value{0.0};
    double y_value{0.0};
    bool is_baseline{false};
};

struct PrintTestSheetPlan {
    uint32_t schema_version{1};
    std::string run_id;
    TestSheetKind kind{TestSheetKind::Diagnostic};
    RasterSourceSnapshot source;
    document::RasterEditGraph edit_graph;
    OutputCondition condition;
    uint32_t evaluated_width_px{0};
    uint32_t evaluated_height_px{0};
    double page_width_mm{0.0};
    double page_height_mm{0.0};
    uint32_t ppi_x{0};
    uint32_t ppi_y{0};
    VariationAxis x_axis;
    VariationAxis y_axis;
    double one_hundred_mm_ruler_mm{100.0};
    color::ProfileIdentifier chart_source_profile{"sRGB", color::ColorSpace::Rgb, {}};
    std::vector<TestSheetElement> elements;
};

enum class TestSheetPlanError : uint8_t {
    None,
    InvalidIdentity,
    InvalidSource,
    InvalidPageGeometry,
    InvalidPatchGeometry,
    InvalidCrop,
    InvalidVariationGrid,
    LayoutOverflow,
    TooManyElements,
    InvalidOutputCondition
};

struct TestSheetPlanResult {
    std::optional<PrintTestSheetPlan> plan;
    TestSheetPlanError error{TestSheetPlanError::None};
    std::string diagnostic;
    [[nodiscard]] explicit operator bool() const noexcept {
        return plan.has_value() && error == TestSheetPlanError::None;
    }
};

/// Creates a frozen, millimeter-accurate layout. It does not evaluate raster
/// pixels, convert profiles, render text, write files, or submit print jobs.
[[nodiscard]] TestSheetPlanResult plan_test_sheet(const TestSheetRequest& request);

} // namespace pikdraw::production::print
