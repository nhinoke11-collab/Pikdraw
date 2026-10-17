#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace pikdraw::rendering {

struct CulledObject {
  document::ObjectId id;
  document::ObjectVariant object;
  geometry::Rect2D bounds;
};

/// Borrowed entries remain valid while the input immutable snapshot is alive.
struct CulledObjectRef {
  document::ObjectId id;
  const document::ObjectVariant *object;
  geometry::Rect2D bounds;
};

/// Linear baseline query. Layer and object vectors define back-to-front order.
class SpatialCuller {
  template <class Map>
  static const document::ObjectVariant *lookup(const Map &map,
                                               document::ObjectId id) {
    if constexpr (requires { map.lookup_ref(id); }) {
      return map.lookup_ref(id);
    } else {
      // Source compatibility with pre-audit persistent map.
      const auto &values = map.all();
      const auto it = values.find(id);
      return it == values.end() ? nullptr : &it->second;
    }
  }

  [[nodiscard]] static bool finite_rect(const geometry::Rect2D &rect) {
    return rect.is_finite() && rect.width >= 0.0 && rect.height >= 0.0 &&
           std::isfinite(rect.right()) && std::isfinite(rect.bottom());
  }

  /// Expands all sides without allowing overflow to become a false cull.
  [[nodiscard]] static bool expand_rect(geometry::Rect2D &rect,
                                        double x_margin,
                                        double y_margin) {
    if (!finite_rect(rect) || !std::isfinite(x_margin) ||
        !std::isfinite(y_margin) || x_margin < 0.0 || y_margin < 0.0)
      return false;
    const double left = rect.x - x_margin;
    const double top = rect.y - y_margin;
    const double right = rect.right() + x_margin;
    const double bottom = rect.bottom() + y_margin;
    const double width = right - left;
    const double height = bottom - top;
    if (!std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(right) || !std::isfinite(bottom) ||
        !std::isfinite(width) || !std::isfinite(height) || width < 0.0 ||
        height < 0.0)
      return false;
    rect = {left, top, width, height};
    return true;
  }

  [[nodiscard]] static bool translate_rect(geometry::Rect2D &rect,
                                           double dx, double dy) {
    if (!finite_rect(rect) || !std::isfinite(dx) || !std::isfinite(dy))
      return false;
    const double x = rect.x + dx;
    const double y = rect.y + dy;
    if (!std::isfinite(x) || !std::isfinite(y))
      return false;
    rect.x = x;
    rect.y = y;
    return finite_rect(rect);
  }

  [[nodiscard]] static bool union_rects(const geometry::Rect2D &first,
                                        const geometry::Rect2D &second,
                                        geometry::Rect2D &result) {
    if (!finite_rect(first) || !finite_rect(second))
      return false;
    const double left = std::min(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double right = std::max(first.right(), second.right());
    const double bottom = std::max(first.bottom(), second.bottom());
    const double width = right - left;
    const double height = bottom - top;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0.0 ||
        height < 0.0)
      return false;
    result = {left, top, width, height};
    return finite_rect(result);
  }

  [[nodiscard]] static bool normalized_opacity(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
  }

  [[nodiscard]] static bool valid_effect_node(
      const document::EffectNode &node) {
    if (node.id == document::kInvalidEffectInstanceId ||
        node.schema_version != document::kBasicEffectNodeSchemaVersion ||
        node.schema_minor > document::kDropShadowBlendSchemaMinor)
      return false;
    return std::visit(
        [&node](const auto &effect) {
          using T = std::decay_t<decltype(effect)>;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            return node.schema_minor == document::kBasicEffectNodeSchemaMinor &&
                   std::isfinite(effect.sigma_x_mm) &&
                   std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm;
          } else {
            return std::isfinite(effect.dx_mm) &&
                   std::isfinite(effect.dy_mm) &&
                   std::abs(effect.dx_mm) <=
                       geometry::limits::kCanvasMaxMm &&
                   std::abs(effect.dy_mm) <=
                       geometry::limits::kCanvasMaxMm &&
                   std::isfinite(effect.sigma_x_mm) &&
                   std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() &&
                   normalized_opacity(effect.opacity) &&
                   static_cast<uint8_t>(effect.blend_mode) <=
                       static_cast<uint8_t>(document::BlendMode::Lighten) &&
                   (node.schema_minor == document::kDropShadowBlendSchemaMinor ||
                    effect.blend_mode == document::BlendMode::Normal);
          }
        },
        node.parameters);
  }

  [[nodiscard]] static bool valid_appearance(
      const document::Appearance &appearance) {
    if (appearance.schema_version != document::kAppearanceSchemaVersion ||
        appearance.schema_minor != document::kAppearanceSchemaMinor ||
        !normalized_opacity(appearance.object_opacity) ||
        !normalized_opacity(appearance.fill_opacity) ||
        !normalized_opacity(appearance.stroke_opacity) ||
        static_cast<uint8_t>(appearance.blend_mode) >
            static_cast<uint8_t>(document::BlendMode::Lighten) ||
        appearance.effects.size() > document::kMaxEffectsPerObject)
      return false;

    std::unordered_set<document::EffectInstanceId> effect_ids;
    for (size_t index = 0; index < appearance.effects.size(); ++index) {
      const auto &node = appearance.effects[index];
      if (!valid_effect_node(node) || !effect_ids.insert(node.id).second)
        return false;
      if (node.enabled) {
        const auto *shadow =
            std::get_if<document::DropShadowEffect>(&node.parameters);
        if (shadow && shadow->blend_mode != document::BlendMode::Normal) {
          for (size_t later = index + 1; later < appearance.effects.size();
               ++later) {
            if (appearance.effects[later].enabled) return false;
          }
        }
      }
    }
    return true;
  }

  [[nodiscard]] static bool max_linear_scale(
      const geometry::Transform2D &transform, double &scale) {
    const double trace = transform.a * transform.a +
                         transform.b * transform.b +
                         transform.c * transform.c +
                         transform.d * transform.d;
    const double determinant = transform.determinant();
    double discriminant = trace * trace - 4.0 * determinant * determinant;
    if (!std::isfinite(trace) || !std::isfinite(determinant) ||
        !std::isfinite(discriminant))
      return false;
    discriminant = std::max(0.0, discriminant);
    scale = std::sqrt((trace + std::sqrt(discriminant)) * 0.5);
    return std::isfinite(scale) && scale > 0.0;
  }

  [[nodiscard]] static bool filter_support_mm(double sigma_mm,
                                              double density,
                                              double &radius_mm) {
    if (!std::isfinite(sigma_mm) || sigma_mm < 0.0 ||
        !std::isfinite(density) || density <= 0.0)
      return false;
    const double sigma_px = sigma_mm * density;
    if (!std::isfinite(sigma_px))
      return false;
    const double support_px = std::ceil(4.0 * sigma_px);
    radius_mm = support_px / density;
    return std::isfinite(support_px) && support_px >= 0.0 &&
           std::isfinite(radius_mm);
  }

  [[nodiscard]] static bool path_output_bounds(
      const document::PathObject &object, geometry::Rect2D &world_bounds,
      double camera_zoom, double conservative_world_margin_mm) {
    auto local_bounds = object.path.bounds();
    if (!finite_rect(local_bounds) || !object.transform.is_finite() ||
        !object.transform.is_invertible() ||
        !std::isfinite(object.stroke_width_mm) ||
        object.stroke_width_mm < 0.0 ||
        !std::isfinite(conservative_world_margin_mm) ||
        conservative_world_margin_mm < 0.0)
      return false;

    if (!valid_appearance(object.appearance))
      return false;

    const bool has_enabled_effect = std::any_of(
        object.appearance.effects.begin(), object.appearance.effects.end(),
        [](const document::EffectNode &node) { return node.enabled; });
    double density = 0.0;
    if (has_enabled_effect) {
      // A legacy caller may not know the camera zoom. In that case retain the
      // object instead of risking an under-sized rounded filter bound.
      if (!std::isfinite(camera_zoom) || camera_zoom <= 0.0)
        return false;
      double transform_scale = 0.0;
      if (!max_linear_scale(object.transform, transform_scale))
        return false;
      density = camera_zoom * transform_scale;
      if (!std::isfinite(density) || density <= 0.0)
        return false;

      const double local_stroke_margin =
          object.has_stroke ? object.stroke_width_mm * 0.5 : 0.0;
      if (!expand_rect(local_bounds, local_stroke_margin,
                       local_stroke_margin))
        return false;
    }

    for (const auto &node : object.appearance.effects) {
      if (!node.enabled)
        continue;
      const bool applied = std::visit(
          [&](const auto &effect) {
            using T = std::decay_t<decltype(effect)>;
            double radius_x = 0.0;
            double radius_y = 0.0;
            if (!filter_support_mm(effect.sigma_x_mm, density, radius_x) ||
                !filter_support_mm(effect.sigma_y_mm, density, radius_y))
              return false;
            if constexpr (std::is_same_v<T,
                                         document::GaussianBlurEffect>) {
              return expand_rect(local_bounds, radius_x, radius_y);
            } else {
              auto shadow_bounds = local_bounds;
              if (!expand_rect(shadow_bounds, radius_x, radius_y) ||
                  !translate_rect(shadow_bounds, effect.dx_mm,
                                  effect.dy_mm))
                return false;
              geometry::Rect2D combined;
              if (!union_rects(local_bounds, shadow_bounds, combined))
                return false;
              local_bounds = combined;
              return true;
            }
          },
          node.parameters);
      if (!applied)
        return false;
    }

    if (has_enabled_effect &&
        !expand_rect(local_bounds, 1.0 / density, 1.0 / density))
      return false;

    world_bounds = object.transform.map_rect(local_bounds);
    if (!finite_rect(world_bounds))
      return false;

    // Retain a conservative world-space stroke margin after mapping. The
    // local margin above also keeps filter input bounds conservative.
    const auto &transform = object.transform;
    const double stroke_bound_scale =
        std::hypot(std::hypot(transform.a, transform.b),
                   std::hypot(transform.c, transform.d));
    const double world_stroke_margin =
        object.has_stroke && !has_enabled_effect
            ? object.stroke_width_mm * 0.5 * stroke_bound_scale
            : 0.0;
    return expand_rect(world_bounds, world_stroke_margin +
                                          conservative_world_margin_mm,
                       world_stroke_margin +
                           conservative_world_margin_mm);
  }

public:
  [[nodiscard]] static bool compute_object_bounds(
      const document::ObjectVariant &object,
      geometry::Rect2D &world_bounds,
      double camera_zoom,
      double conservative_world_margin_mm = 0.0) {
    return std::visit(
        [&](const auto &obj) -> bool {
          using T = std::decay_t<decltype(obj)>;
          if constexpr (std::is_same_v<T, document::PathObject>) {
            return path_output_bounds(obj, world_bounds, camera_zoom,
                                      conservative_world_margin_mm);
          } else if constexpr (std::is_same_v<T, document::ImageObject>) {
            world_bounds = obj.transform.map_rect(obj.bounds_rect);
            return finite_rect(world_bounds);
          } else {
            world_bounds = geometry::Rect2D{-10000.0, -10000.0, 20000.0, 20000.0};
            return true;
          }
        },
        object);
  }

  /// `camera_zoom` supplies the target pixel density; effect support is rounded
  /// with the same finite ceil(4*sigma_px) rule as the compositor. The final
  /// world-space margin covers target pixel-center sampling after transforms.
  [[nodiscard]] static std::vector<CulledObjectRef>
  query_visible_refs(const document::DocumentSnapshot &snapshot,
                     const geometry::Rect2D &visible_bounds_mm,
                     double camera_zoom,
                     double conservative_world_margin_mm = 0.0) {
    std::vector<CulledObjectRef> result;
    std::unordered_set<document::ObjectId> visited;
    const auto &objects = snapshot.objects();
    for (const auto &layer : snapshot.page().layers) {
      if (!layer.visible)
        continue;
      for (auto id : layer.object_ids) {
        const auto *object = lookup(objects, id);
        if (!object || !visited.insert(id).second)
          continue;
        geometry::Rect2D bounds;
        bool include = false;
        std::visit(
            [&](const auto &obj) {
              using T = std::decay_t<decltype(obj)>;
              if (!obj.visible)
                return;
              if constexpr (std::is_same_v<T, document::PathObject>) {
                const bool bounds_valid = path_output_bounds(
                    obj, bounds, camera_zoom,
                    conservative_world_margin_mm);
                // Invalid source/effect data must reach renderer validation
                // rather than disappear during a visibility query.
                include = !bounds_valid || !finite_rect(visible_bounds_mm) ||
                          visible_bounds_mm.intersects(bounds);
              } else {
                // Text bounds are only estimates; groups need hierarchical
                // handling.
                include = true;
              }
            },
            *object);
        if (include)
          result.push_back({id, object, bounds});
      }
    }
    return result;
  }
  /// Legacy visibility queries have no camera density. Effectful objects are
  /// retained conservatively; their `bounds` are unavailable, so callers that
  /// need output bounds should pass the camera zoom.
  [[nodiscard]] static std::vector<CulledObjectRef>
  query_visible_refs(const document::DocumentSnapshot &snapshot,
                     const geometry::Rect2D &visible_bounds_mm) {
    return query_visible_refs(snapshot, visible_bounds_mm, 0.0, 0.0);
  }
  // Compatibility copy API; frame rendering uses borrowed query_visible_refs.
  [[nodiscard]] static std::vector<CulledObject>
  query_visible(const document::DocumentSnapshot &snapshot,
                const geometry::Rect2D &bounds,
                double camera_zoom,
                double conservative_world_margin_mm = 0.0) {
    std::vector<CulledObject> result;
    for (const auto &item : query_visible_refs(
             snapshot, bounds, camera_zoom, conservative_world_margin_mm)) {
      result.push_back({item.id, *item.object, item.bounds});
    }
    return result;
  }
  /// Legacy visibility queries have no camera density. Effectful objects are
  /// retained conservatively; their `bounds` are unavailable, so callers that
  /// need output bounds should pass the camera zoom.
  [[nodiscard]] static std::vector<CulledObject>
  query_visible(const document::DocumentSnapshot &snapshot,
                const geometry::Rect2D &bounds) {
    return query_visible(snapshot, bounds, 0.0, 0.0);
  }
};

} // namespace pikdraw::rendering
