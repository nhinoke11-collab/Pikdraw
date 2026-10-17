#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/automation/json_value.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/shaping/font_resolver.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <optional>
#include <variant>
#include <vector>

namespace pikdraw::automation {

inline constexpr std::string_view kApiVersion = "0.1";

enum class Status {
    Ok,
    InvalidArgument,
    RevisionConflict,
    EpochMismatch,
    TargetNotFound,
    UnsupportedCapability,
    BusyInteraction,
    InternalError,
};

struct Capability {
    std::string id;
    std::string input_schema;
    std::vector<std::string> required_scopes;
    bool writes_document{false};
};

struct DocumentState {
    std::string document_id;
    uint64_t epoch{0};
    uint64_t revision{0};
    double page_width_mm{0.0};
    double page_height_mm{0.0};
    size_t object_count{0};
    size_t layer_count{0};
    bool dirty{false};
};

struct ObjectSummary {
    enum class BoundsQuality { SourceGeometry, TextApproximation, Unsupported };
    uint64_t id{0};
    std::string type;
    std::string name;
    geometry::Rect2D bounds{};
    BoundsQuality bounds_quality{BoundsQuality::Unsupported};
    size_t layer_index{0};
};

struct Result {
    Status status{Status::InternalError};
    std::string diagnostic;
    uint64_t revision_before{0};
    uint64_t revision_after{0};
    std::vector<uint64_t> created_ids;
    std::vector<size_t> created_operation_indices;
    std::vector<uint64_t> affected_ids;
    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
};

struct BatchValidation {
    bool valid{false};
    std::vector<std::string> diagnostics;
};

struct CreateRectangle {
    uint64_t expected_epoch{0};
    uint64_t expected_revision{0};
    size_t layer_index{0};
    geometry::Rect2D bounds_mm{};
    color::ColorValue fill{color::RGBColor{0.14, 0.47, 0.82, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double stroke_width_mm{1.0};
};

enum class CreateObjectKind { Rectangle, Ellipse, Text, Line, Polyline };

struct CreateObject {
    CreateObjectKind kind{CreateObjectKind::Rectangle};
    uint64_t expected_epoch{0};
    uint64_t expected_revision{0};
    size_t layer_index{0};
    geometry::Rect2D bounds_mm{};
    geometry::Point2D position_mm{};
    std::vector<geometry::Point2D> path_points_mm;
    bool path_closed{false};
    std::string name;
    std::string text;
    typography::FontIdentity font{typography::FontIdentity::make_default()};
    typography::TextAlignment text_alignment{typography::TextAlignment::Left};
    bool underline{false};
    color::ColorValue fill{color::RGBColor{0.14, 0.47, 0.82, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double stroke_width_mm{1.0};
};

struct TransformObjects {
    std::vector<uint64_t> target_ids;
    double translate_x_mm{0.0};
    double translate_y_mm{0.0};
    double scale_x{1.0};
    double scale_y{1.0};
    double rotation_degrees{0.0};
    geometry::Point2D pivot_mm{};
};

struct DuplicateObjects {
    std::vector<uint64_t> target_ids;
    double offset_x_mm{0.0};
    double offset_y_mm{0.0};
};

struct DeleteObjects {
    std::vector<uint64_t> target_ids;
};

struct SetObjectStyle {
    std::vector<uint64_t> target_ids;
    std::optional<color::ColorValue> fill;
    std::optional<color::ColorValue> stroke;
    std::optional<double> stroke_width_mm;
};

struct GroupObjects {
    std::vector<uint64_t> target_ids;
    std::string name;
};

struct UngroupObjects {
    std::vector<uint64_t> target_ids;
};

enum class AlignmentMode { Left, CenterX, Right, Top, CenterY, Bottom };

struct AlignObjects {
    uint64_t reference_id{document::kInvalidObjectId};
    std::vector<uint64_t> target_ids;
    AlignmentMode mode{AlignmentMode::Left};
};

enum class DistributionAxis { Horizontal, Vertical };

struct DistributeObjects {
    std::vector<uint64_t> target_ids;
    DistributionAxis axis{DistributionAxis::Horizontal};
};

using BatchOperation = std::variant<CreateObject, TransformObjects,
                                    DuplicateObjects, DeleteObjects, SetObjectStyle,
                                    GroupObjects, UngroupObjects, AlignObjects,
                                    DistributeObjects>;

struct TextMeasurement {
    typography::FontIdentity requested_font;
    typography::FontIdentity actual_font;
    geometry::Rect2D bounds_mm{};
    size_t line_count{0};
    size_t glyph_count{0};
    size_t missing_glyph_count{0};
    bool is_fallback{false};
    std::string warning;
};

struct ValidationDiagnostic {
    std::string code;
    std::string severity;
    uint64_t object_id{0};
    std::string message;
};

struct DocumentValidation {
    bool valid{false};
    std::vector<ValidationDiagnostic> diagnostics;
};

/// Service methods run on the document's owning writer thread. Transport adapters must
/// marshal onto that thread before invoking methods that access the mutable Document facade.
class AutomationService final {
public:
    explicit AutomationService(document::Document& document) noexcept : document_(document) {}

    [[nodiscard]] std::vector<Capability> capabilities() const;
    [[nodiscard]] DocumentState inspect() const;
    [[nodiscard]] std::vector<ObjectSummary> query_objects() const;
    [[nodiscard]] Result create_rectangle(const CreateRectangle& request);
    [[nodiscard]] BatchValidation validate_rectangles(const std::vector<CreateRectangle>& requests) const;
    [[nodiscard]] Result create_rectangles(uint64_t expected_epoch, uint64_t expected_revision,
                                          const std::vector<CreateRectangle>& requests);
    [[nodiscard]] BatchValidation validate_objects(const std::vector<CreateObject>& requests) const;
    [[nodiscard]] BatchValidation validate_batch(const std::vector<BatchOperation>& operations) const;
    [[nodiscard]] Result create_objects(uint64_t expected_epoch, uint64_t expected_revision,
                                        const std::vector<CreateObject>& requests);
    [[nodiscard]] Result apply_batch(uint64_t expected_epoch, uint64_t expected_revision,
                                     const std::vector<BatchOperation>& operations);
    [[nodiscard]] Result undo(uint64_t expected_epoch, uint64_t expected_revision);
    [[nodiscard]] Result redo(uint64_t expected_epoch, uint64_t expected_revision);
    [[nodiscard]] TextMeasurement measure_text(std::string_view text,
        const typography::FontIdentity& font,
        const typography::LayoutOptions& options = {}) const;
    [[nodiscard]] typography::ResolvedFont query_font(
        const typography::FontIdentity& font) const;
    [[nodiscard]] DocumentValidation validate_document(double safe_margin_mm = 0.0,
        const std::set<uint64_t>& safe_margin_exempt_ids = {}) const;
    [[nodiscard]] document::SnapshotPtr snapshot() const noexcept { return document_.current_snapshot(); }

private:
    document::Document& document_;
};

} // namespace pikdraw::automation
