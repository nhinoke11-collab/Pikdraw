#include <cstring>
#include "d2d_internal.hpp"

namespace pikdraw::rendering::d2d {

D2DGeometryCache::D2DGeometryCache() = default;

D2DGeometryCache::~D2DGeometryCache() = default;

void D2DGeometryCache::clear() {
  cache_.clear();
  last_revision_ = 0;
}

namespace {
uint64_t compute_path_hash(const geometry::Path2D& path) noexcept {
  uint64_t h = 14695981039346656037ULL;
  const auto& elems = path.elements();
  h ^= elems.size();
  h *= 1099511628211ULL;
  h ^= static_cast<uint64_t>(path.winding_rule());
  h *= 1099511628211ULL;
  for (const auto& el : elems) {
    h ^= static_cast<uint64_t>(el.verb);
    h *= 1099511628211ULL;
    uint64_t x_bits = 0, y_bits = 0;
    std::memcpy(&x_bits, &el.pt.x, sizeof(double));
    std::memcpy(&y_bits, &el.pt.y, sizeof(double));
    h ^= x_bits;
    h *= 1099511628211ULL;
    h ^= y_bits;
    h *= 1099511628211ULL;
  }
  return h;
}
} // namespace

void D2DGeometryCache::evict_stale(uint64_t current_revision) {
  if (cache_.size() > 4096) {
    for (auto it = cache_.begin(); it != cache_.end();) {
      if (current_revision > it->second.last_used_revision &&
          current_revision - it->second.last_used_revision > 50) {
        it = cache_.erase(it);
      } else {
        ++it;
      }
    }
  }
  last_revision_ = current_revision;
}

ComPtr<ID2D1PathGeometry> D2DGeometryCache::get_or_create_path(
    ID2D1Factory1 *factory,
    document::ObjectId id,
    uint64_t revision,
    const geometry::Path2D &path,
    uint8_t kind) {
  if (!factory)
    return nullptr;

  const bool even_odd = path.winding_rule() == geometry::WindingRule::EvenOdd;
  const uint64_t path_hash = compute_path_hash(path);
  CacheKey key{id, kind, even_odd};

  auto it = cache_.find(key);
  if (it != cache_.end()) {
    if (it->second.path_hash == path_hash && it->second.geometry) {
      it->second.last_used_revision = revision;
      return it->second.geometry;
    }
  }

  ComPtr<ID2D1PathGeometry> path_geo;
  HRESULT hr = factory->CreatePathGeometry(path_geo.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return nullptr;

  ComPtr<ID2D1GeometrySink> sink;
  hr = path_geo->Open(sink.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return nullptr;

  sink->SetFillMode(path.winding_rule() == geometry::WindingRule::EvenOdd
                        ? D2D1_FILL_MODE_ALTERNATE
                        : D2D1_FILL_MODE_WINDING);

  bool figure_open = false;

  for (const auto &el : path.elements()) {
    switch (el.verb) {
    case geometry::PathVerb::MoveTo: {
      if (figure_open) {
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
      }
      D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
      sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
      figure_open = true;
      break;
    }
    case geometry::PathVerb::LineTo: {
      if (!figure_open) {
        D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
        sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
        figure_open = true;
      } else {
        D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
        sink->AddLine(pt);
      }
      break;
    }
    case geometry::PathVerb::QuadTo: {
      if (!figure_open) {
        D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.ctrl1.x), static_cast<float>(el.ctrl1.y));
        sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
        figure_open = true;
      }
      D2D1_QUADRATIC_BEZIER_SEGMENT seg{};
      seg.point1 = D2D1::Point2F(static_cast<float>(el.ctrl1.x), static_cast<float>(el.ctrl1.y));
      seg.point2 = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
      sink->AddQuadraticBezier(&seg);
      break;
    }
    case geometry::PathVerb::CubicTo: {
      if (!figure_open) {
        D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.ctrl1.x), static_cast<float>(el.ctrl1.y));
        sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
        figure_open = true;
      }
      D2D1_BEZIER_SEGMENT seg{};
      seg.point1 = D2D1::Point2F(static_cast<float>(el.ctrl1.x), static_cast<float>(el.ctrl1.y));
      seg.point2 = D2D1::Point2F(static_cast<float>(el.ctrl2.x), static_cast<float>(el.ctrl2.y));
      seg.point3 = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
      sink->AddBezier(&seg);
      break;
    }
    case geometry::PathVerb::ArcTo: {
      if (!figure_open) {
        D2D1_POINT_2F pt = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
        sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
        figure_open = true;
      }
      D2D1_ARC_SEGMENT arc{};
      arc.point = D2D1::Point2F(static_cast<float>(el.pt.x), static_cast<float>(el.pt.y));
      arc.size = D2D1::SizeF(static_cast<float>(el.rx), static_cast<float>(el.ry));
      arc.rotationAngle = static_cast<float>(el.rotation_deg);
      arc.sweepDirection = el.sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                    : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
      arc.arcSize = el.large_arc ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
      sink->AddArc(&arc);
      break;
    }
    case geometry::PathVerb::Close: {
      if (figure_open) {
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        figure_open = false;
      }
      break;
    }
    }
  }

  if (figure_open) {
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
  }

  hr = sink->Close();
  if (FAILED(hr))
    return nullptr;

  if (cache_.size() >= 4096) {
    evict_stale(revision);
  }
  cache_[key] = GeometryEntry{path_geo, path_hash, revision};
  return path_geo;
}

} // namespace pikdraw::rendering::d2d
