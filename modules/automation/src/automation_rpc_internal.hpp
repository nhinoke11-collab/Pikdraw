#pragma once

#include "pikdraw/automation/automation_rpc.hpp"
#include "pikdraw/automation/automation_service.hpp"
#include "pikdraw/automation/json_value.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"

#include <charconv>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::automation {

inline std::string id_string(uint64_t value) { return std::to_string(value); }

inline const JsonValue* field(const JsonValue& value, std::string_view name) { return value.get(name); }
inline const std::string* string_field(const JsonValue& value, std::string_view name) {
    const auto* item = field(value, name); return item ? item->string() : nullptr;
}
inline const double* number_field(const JsonValue& value, std::string_view name) {
    const auto* item = field(value, name); return item ? item->number() : nullptr;
}

bool only_fields(const JsonValue& value, std::initializer_list<std::string_view> allowed);

JsonValue rect_json(const geometry::Rect2D& bounds);
JsonValue color_json(const color::ColorValue& value);
std::string bounds_quality(ObjectSummary::BoundsQuality quality);
JsonValue object_json(const document::ObjectVariant& value, size_t layer_index);

bool parse_color(const JsonValue* input, color::ColorValue& color_out);
bool parse_target_ids(const JsonValue& value, std::vector<uint64_t>& ids,
                      size_t maximum, std::string& diagnostic);
bool parse_object_id(const JsonValue& value, std::string_view field_name, uint64_t& id,
                     std::string& diagnostic);
[[nodiscard]] std::optional<geometry::units::LengthUnit> parse_physical_length_unit(
    std::string_view suffix) noexcept;

struct ObjectLocation {
    size_t layer_index{0};
    bool top_level{false};
    std::optional<uint64_t> parent_group_id;
};

std::optional<ObjectLocation> find_object_location(const document::DocumentSnapshot& snapshot,
                                                   uint64_t target_id);

bool parse_object_operations(const JsonValue* input, uint64_t epoch, uint64_t revision,
                             std::vector<BatchOperation>& parsed_result,
                             std::vector<std::string>& client_refs, std::string& diagnostic);

bool read_revision_pair(const JsonValue& request, uint64_t& epoch, uint64_t& revision);

bool parse_font(const JsonValue& request, typography::FontIdentity& font,
                std::string& diagnostic);

} // namespace pikdraw::automation
