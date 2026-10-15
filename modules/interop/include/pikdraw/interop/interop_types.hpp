#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <array>

namespace pikdraw::interop {

struct Point2D {
    double x{0.0};
    double y{0.0};

    [[nodiscard]] bool IsFinite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }

    [[nodiscard]] bool operator==(const Point2D& other) const noexcept {
        return std::abs(x - other.x) < 1e-9 && std::abs(y - other.y) < 1e-9;
    }
};

// 2D affine transform: x' = a*x + c*y + tx; y' = b*x + d*y + ty
struct Transform2D {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double tx{0.0};
    double ty{0.0};

    [[nodiscard]] static Transform2D Identity() noexcept {
        return {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    }

    [[nodiscard]] bool IsFinite() const noexcept {
        return std::isfinite(a) && std::isfinite(b) &&
               std::isfinite(c) && std::isfinite(d) &&
               std::isfinite(tx) && std::isfinite(ty);
    }

    [[nodiscard]] Point2D Apply(const Point2D& p) const noexcept {
        return {
            a * p.x + c * p.y + tx,
            b * p.x + d * p.y + ty
        };
    }

    // compose(parent, local) applies local first: parent * local
    [[nodiscard]] static Transform2D Compose(const Transform2D& parent, const Transform2D& local) noexcept {
        return {
            parent.a * local.a + parent.c * local.b,
            parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d,
            parent.b * local.c + parent.d * local.d,
            parent.a * local.tx + parent.c * local.ty + parent.tx,
            parent.b * local.tx + parent.d * local.ty + parent.ty
        };
    }
};

enum class ColorSpaceType : uint8_t {
    RGB = 0,
    CMYK = 1,
    Spot = 2
};

struct ColorRGB {
    double r{0.0};
    double g{0.0};
    double b{0.0};
    double a{1.0};

    [[nodiscard]] bool IsFinite() const noexcept {
        return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
    }
};

struct ColorCMYK {
    double c{0.0};
    double m{0.0};
    double y{0.0};
    double k{0.0};
    double a{1.0};

    [[nodiscard]] bool IsFinite() const noexcept {
        return std::isfinite(c) && std::isfinite(m) && std::isfinite(y) && std::isfinite(k) && std::isfinite(a);
    }
};

struct ColorSpot {
    std::string spot_name;
    double tint{1.0};
    ColorCMYK fallback_cmyk{};

    [[nodiscard]] bool IsFinite() const noexcept {
        return std::isfinite(tint) && fallback_cmyk.IsFinite();
    }
};

struct PathSegment {
    enum class Type : uint8_t {
        MoveTo = 0,
        LineTo = 1,
        CubicTo = 2,
        QuadTo = 3,
        Close = 4
    } type{Type::MoveTo};

    Point2D p1{};
    Point2D p2{};
    Point2D p3{};

    [[nodiscard]] bool IsFinite() const noexcept {
        return p1.IsFinite() && p2.IsFinite() && p3.IsFinite();
    }
};

struct VectorGraphicObject {
    std::string id;
    Transform2D transform{};
    std::vector<PathSegment> segments;
    ColorRGB stroke_color{};
    ColorRGB fill_color{};
    double stroke_width_mm{0.1};
    uint32_t depth{0};

    [[nodiscard]] bool IsFinite() const noexcept {
        if (!transform.IsFinite() || !stroke_color.IsFinite() || !fill_color.IsFinite() || !std::isfinite(stroke_width_mm)) {
            return false;
        }
        for (const auto& seg : segments) {
            if (!seg.IsFinite()) return false;
        }
        return true;
    }
};

} // namespace pikdraw::interop
