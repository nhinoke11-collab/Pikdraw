#include "pikdraw/document/commands/appearance_commands.hpp"

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/validation.hpp"
#include <algorithm>
#include <type_traits>
#include <unordered_set>

namespace pikdraw::document {
namespace {

bool same_color_payload(const color::ColorValue& lhs,
                        const color::ColorValue& rhs) {
  if (lhs.payload().index() != rhs.payload().index())
    return false;
  return std::visit([&](const auto& left) {
    using T = std::decay_t<decltype(left)>;
    const auto* right = std::get_if<T>(&rhs.payload());
    if (!right)
      return false;
    if constexpr (std::is_same_v<T, color::RGBColor>) {
      return left.r == right->r && left.g == right->g && left.b == right->b &&
             left.a == right->a;
    } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
      return left.c == right->c && left.m == right->m && left.y == right->y &&
             left.k == right->k && left.a == right->a;
    } else if constexpr (std::is_same_v<T, color::SpotColor>) {
      return left.spot_name == right->spot_name && left.tint == right->tint &&
             left.alternate_rgb.r == right->alternate_rgb.r &&
             left.alternate_rgb.g == right->alternate_rgb.g &&
             left.alternate_rgb.b == right->alternate_rgb.b &&
             left.alternate_rgb.a == right->alternate_rgb.a &&
             left.overprint == right->overprint;
    } else {
      return left.gray == right->gray && left.a == right->a;
    }
  }, lhs.payload()) && lhs.overprint().fill == rhs.overprint().fill &&
         lhs.overprint().stroke == rhs.overprint().stroke;
}

bool same_parameters(const EffectParameters& lhs,
                     const EffectParameters& rhs) {
  if (lhs.index() != rhs.index())
    return false;
  if (const auto* left = std::get_if<GaussianBlurEffect>(&lhs)) {
    const auto& right = std::get<GaussianBlurEffect>(rhs);
    return left->sigma_x_mm == right.sigma_x_mm &&
           left->sigma_y_mm == right.sigma_y_mm;
  }
  const auto& left = std::get<DropShadowEffect>(lhs);
  const auto& right = std::get<DropShadowEffect>(rhs);
  return left.dx_mm == right.dx_mm && left.dy_mm == right.dy_mm &&
         left.sigma_x_mm == right.sigma_x_mm &&
         left.sigma_y_mm == right.sigma_y_mm &&
         left.opacity == right.opacity && left.blend_mode == right.blend_mode &&
         same_color_payload(left.color, right.color);
}

bool same_appearance(const Appearance& lhs, const Appearance& rhs) {
  if (lhs.schema_version != rhs.schema_version ||
      lhs.object_opacity != rhs.object_opacity ||
      lhs.fill_opacity != rhs.fill_opacity ||
      lhs.stroke_opacity != rhs.stroke_opacity ||
      lhs.blend_mode != rhs.blend_mode || lhs.effects.size() != rhs.effects.size())
    return false;
  for (size_t i = 0; i < lhs.effects.size(); ++i) {
    const auto& a = lhs.effects[i];
    const auto& b = rhs.effects[i];
    if (a.id != b.id || a.schema_version != b.schema_version ||
        a.schema_minor != b.schema_minor ||
        a.enabled != b.enabled || !same_parameters(a.parameters, b.parameters))
      return false;
  }
  return true;
}

bool editable_appearance_object(const ObjectVariant& object) {
  return std::holds_alternative<PathObject>(object) ||
         std::holds_alternative<TextObject>(object);
}

bool locked(const ObjectVariant& object) {
  return std::visit([](const auto& value) { return value.locked; }, object);
}

const Appearance& get_appearance(const ObjectVariant& object) {
  if (const auto* path = std::get_if<PathObject>(&object))
    return path->appearance;
  return std::get<TextObject>(object).appearance;
}

void set_appearance(ObjectVariant& object, const Appearance& appearance) {
  if (auto* path = std::get_if<PathObject>(&object))
    path->appearance = appearance;
  else
    std::get<TextObject>(object).appearance = appearance;
}

} // namespace

SetAppearanceCommand::SetAppearanceCommand(
    std::vector<AppearanceReplacement> replacements)
    : replacements_(std::move(replacements)) {}

CommandResult SetAppearanceCommand::execute(Document& doc) {
  if (applied_)
    return CommandResult::fail("Appearance command is already applied");
  if (replacements_.empty())
    return CommandResult::fail("Appearance command has no target objects");

  std::unordered_set<ObjectId> targets;
  targets.reserve(replacements_.size());
  std::vector<ObjectVariant> updated;
  std::vector<Appearance> current_values;
  updated.reserve(replacements_.size());
  current_values.reserve(replacements_.size());
  bool changed = false;
  for (const auto& replacement : replacements_) {
    if (replacement.object_id == kInvalidObjectId ||
        !targets.insert(replacement.object_id).second)
      return CommandResult::fail("Appearance targets must be unique nonzero IDs");
    const auto current = doc.objects().get(replacement.object_id);
    if (!current)
      return CommandResult::fail("Appearance target object no longer exists");
    if (!editable_appearance_object(*current))
      return CommandResult::fail("Appearance is supported only on Path/Text objects");
    if (locked(*current))
      return CommandResult::fail("A locked object prevents the atomic appearance update");
    try {
      validate_appearance(replacement.appearance);
    } catch (const std::exception& error) {
      return CommandResult::fail(error.what());
    }
    current_values.push_back(get_appearance(*current));
    auto next = *current;
    set_appearance(next, replacement.appearance);
    changed = changed || !same_appearance(current_values.back(), replacement.appearance);
    updated.push_back(std::move(next));
  }
  if (!changed)
    return CommandResult::no_change();

  if (previous_.empty())
    previous_ = current_values;
  auto objects = doc.objects();
  for (size_t i = 0; i < replacements_.size(); ++i)
    objects = objects.set(replacements_[i].object_id, std::move(updated[i]));
  doc.apply_mutation(doc.page(), std::move(objects));
  applied_ = true;
  return CommandResult::ok();
}

CommandResult SetAppearanceCommand::undo(Document& doc) {
  if (!applied_ || previous_.size() != replacements_.size())
    return CommandResult::fail("Cannot undo an appearance command that did not apply");
  auto objects = doc.objects();
  std::vector<ObjectVariant> restored;
  restored.reserve(replacements_.size());
  for (size_t i = 0; i < replacements_.size(); ++i) {
    const auto current = objects.get(replacements_[i].object_id);
    if (!current || !editable_appearance_object(*current))
      return CommandResult::fail("Appearance target is unavailable while undoing");
    auto next = *current;
    set_appearance(next, previous_[i]);
    restored.push_back(std::move(next));
  }
  for (size_t i = 0; i < replacements_.size(); ++i)
    objects = objects.set(replacements_[i].object_id, std::move(restored[i]));
  doc.apply_mutation(doc.page(), std::move(objects));
  applied_ = false;
  return CommandResult::ok();
}

} // namespace pikdraw::document
