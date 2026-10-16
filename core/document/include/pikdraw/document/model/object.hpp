#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <algorithm>

namespace pikdraw::document {

enum class ObjectType {
    Path,
    Text,
    Group,
    Image
};

struct PathObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    geometry::Path2D path;
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    color::ColorValue fill{color::RGBColor{0.2, 0.4, 0.8, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double stroke_width_mm{1.0};
    bool has_fill{false};
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
    typography::TextAlignment alignment{typography::TextAlignment::Left};
    bool underline{false};
    geometry::Point2D position{0.0, 0.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const {
        if (text.empty()) {
            return transform.map_rect({position.x, position.y, 20.0, font.size_mm()});
        }
        // Calculate multi-line and single-line bounds accurately
        size_t lines = 1;
        size_t max_line_len = 0;
        size_t cur_len = 0;
        for (char c : text) {
            if (c == '\n') {
                if (cur_len > max_line_len) max_line_len = cur_len;
                cur_len = 0;
                lines++;
            } else {
                cur_len++;
            }
        }
        if (cur_len > max_line_len) max_line_len = cur_len;
        const double char_w = font.size_mm() * 0.55;
        const double line_h = font.size_mm() * 1.3;
        const double w = std::max(10.0, max_line_len * char_w);
        const double h = std::max(font.size_mm(), lines * line_h);
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

struct ImageObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    geometry::Rect2D bounds_rect{0.0, 0.0, 100.0, 80.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    int32_t width_px{160};
    int32_t height_px{120};
    std::vector<uint32_t> pixels_rgba;
    std::optional<geometry::Path2D> clip_path;
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const {
        return transform.map_rect(bounds_rect);
    }

    static ImageObject make_procedural_photo(ObjectId id, double x, double y, double w, double h, const std::string& name = "Photo Asset") {
        ImageObject obj;
        obj.id = id;
        obj.name = name;
        obj.bounds_rect = {x, y, w, h};
        obj.width_px = 160;
        obj.height_px = 120;
        obj.pixels_rgba.resize(static_cast<size_t>(obj.width_px) * obj.height_px);

        for (int row = 0; row < obj.height_px; ++row) {
            double v = static_cast<double>(row) / (obj.height_px - 1);
            for (int col = 0; col < obj.width_px; ++col) {
                double u = static_cast<double>(col) / (obj.width_px - 1);

                uint8_t r = 0, g = 0, b = 0;
                if (v < 0.65) {
                    double sky_t = v / 0.65;
                    r = static_cast<uint8_t>((1.0 - sky_t) * 35 + sky_t * 245);
                    g = static_cast<uint8_t>((1.0 - sky_t) * 30 + sky_t * 130);
                    b = static_cast<uint8_t>((1.0 - sky_t) * 115 + sky_t * 32);

                    double dx = (u - 0.62) * (obj.width_px / 120.0);
                    double dy = (v - 0.40);
                    double dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < 0.16) {
                        double glow = (1.0 - dist / 0.16);
                        r = static_cast<uint8_t>(std::min(255.0, r + glow * 255));
                        g = static_cast<uint8_t>(std::min(255.0, g + glow * 240));
                        b = static_cast<uint8_t>(std::min(255.0, b + glow * 180));
                    }
                } else {
                    double land_t = (v - 0.65) / 0.35;
                    double peak = 0.65 + 0.08 * std::sin(u * 9.4) + 0.04 * std::cos(u * 21.0);
                    if (v < peak) {
                        r = static_cast<uint8_t>(25 + land_t * 20);
                        g = static_cast<uint8_t>(38 + land_t * 25);
                        b = static_cast<uint8_t>(45 + land_t * 30);
                    } else {
                        r = static_cast<uint8_t>(50 + (1.0 - land_t) * 120);
                        g = static_cast<uint8_t>(40 + (1.0 - land_t) * 60);
                        b = static_cast<uint8_t>(70 + land_t * 40);
                    }
                }

                if (row >= obj.height_px - 8 && col < 64) {
                    int swatch = col / 8;
                    const uint8_t palette[8][3] = {
                        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {0, 242, 195},
                        {245, 158, 11}, {239, 68, 68}, {255, 255, 255}, {20, 20, 20}
                    };
                    r = palette[swatch][0];
                    g = palette[swatch][1];
                    b = palette[swatch][2];
                }

                uint32_t packed = (static_cast<uint32_t>(r) << 24) |
                                  (static_cast<uint32_t>(g) << 16) |
                                  (static_cast<uint32_t>(b) << 8) | 0xFF;
                obj.pixels_rgba[static_cast<size_t>(row) * obj.width_px + col] = packed;
            }
        }
        return obj;
    }
};

using ObjectVariant = std::variant<PathObject, TextObject, GroupObject, ImageObject>;

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
