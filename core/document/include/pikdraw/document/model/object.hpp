#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace pikdraw::document {

enum class ObjectType {
    Path,
    Text,
    Group
};

struct PathObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    geometry::Path2D path;
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    color::ColorValue fill{color::RGBColor{0.2, 0.4, 0.8, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double stroke_width_mm{1.0};
    bool has_fill{true};
    bool has_stroke{true};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const {
        return path.transformed(transform).bounds();
    }
};

struct TextObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    std::string text;
    typography::FontIdentity font{typography::FontIdentity::make_default()};
    color::ColorValue fill{color::RGBColor{0.9, 0.9, 0.9, 1.0}};
    geometry::Point2D position{0.0, 0.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const {
        // Approximate text bounds from length and font size in mm
        const double w = text.length() * (font.size_mm() * 0.55);
        const double h = font.size_mm();
        geometry::Rect2D r{position.x, position.y, w, h};
        return transform.map_rect(r);
    }
};

struct GroupObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    std::vector<ObjectId> children_ids;
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};
};

using ObjectVariant = std::variant<PathObject, TextObject, GroupObject>;

struct Layer {
    std::string name{"Layer 1"};
    bool visible{true};
    bool locked{false};
    std::vector<ObjectId> object_ids;
};

struct Page {
    double width_mm{20000.0}; // Default 20-meter billboard capable
    double height_mm{1000.0};
    std::vector<Layer> layers;

    static Page make_default() {
        Page p;
        p.width_mm = 20000.0;
        p.height_mm = 1000.0;
        p.layers.push_back(Layer{"Layer 1", true, false, {}});
        return p;
    }
};

} // namespace pikdraw::document
