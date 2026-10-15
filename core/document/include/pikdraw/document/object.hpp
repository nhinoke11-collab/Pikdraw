#pragma once

#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include "pikdraw/document/types.hpp"
#include "pikdraw/document/color.hpp"
#include "pikdraw/geometry/curve.hpp"
#include "pikdraw/geometry/transform.hpp"
#include "pikdraw/geometry/rect.hpp"

namespace pikdraw::document {

enum class ObjectType : uint8_t {
    Path = 0,
    Text = 1,
    Group = 2
};

class DocumentObject {
public:
    ObjectId id{INVALID_OBJECT_ID};
    std::string name;
    pikdraw::geometry::Transform2D transform{pikdraw::geometry::Transform2D::identity()};
    ColorData stroke_color{ColorData::make_rgb(0, 0, 0)};
    ColorData fill_color{ColorData::make_rgb(1, 1, 1)};
    double stroke_width_mm{0.25};
    bool visible{true};
    bool locked{false};

    virtual ~DocumentObject() = default;
    [[nodiscard]] virtual ObjectType type() const = 0;
    [[nodiscard]] virtual std::shared_ptr<DocumentObject> clone() const = 0;
    [[nodiscard]] virtual pikdraw::geometry::Rect2D bounds() const = 0;
};

class PathObject : public DocumentObject {
public:
    pikdraw::geometry::SourcePath path;

    [[nodiscard]] ObjectType type() const override { return ObjectType::Path; }
    [[nodiscard]] std::shared_ptr<DocumentObject> clone() const override {
        return std::make_shared<PathObject>(*this);
    }
    [[nodiscard]] pikdraw::geometry::Rect2D bounds() const override {
        const auto local = path.compute_bounds();
        return local.transformed(transform);
    }
};

class TextObject : public DocumentObject {
public:
    std::string utf8_text;
    std::string font_family{"Noto Sans"};
    double font_size_pt{12.0};
    bool is_bold{false};
    bool is_italic{false};

    [[nodiscard]] ObjectType type() const override { return ObjectType::Text; }
    [[nodiscard]] std::shared_ptr<DocumentObject> clone() const override {
        return std::make_shared<TextObject>(*this);
    }
    [[nodiscard]] pikdraw::geometry::Rect2D bounds() const override {
        // Approximation for bounds based on font size and text length
        const double width = utf8_text.size() * (font_size_pt * 0.352778 * 0.6);
        const double height = font_size_pt * 0.352778;
        const auto local = pikdraw::geometry::Rect2D::from_xywh(0, 0, width, height);
        return local.transformed(transform);
    }
};

class GroupObject : public DocumentObject {
public:
    std::vector<ObjectId> child_ids;

    [[nodiscard]] ObjectType type() const override { return ObjectType::Group; }
    [[nodiscard]] std::shared_ptr<DocumentObject> clone() const override {
        return std::make_shared<GroupObject>(*this);
    }
    [[nodiscard]] pikdraw::geometry::Rect2D bounds() const override {
        return pikdraw::geometry::Rect2D(0, 0, 0, 0);
    }
};

// Bounded source subset for this spike. This is not a production file schema.
inline void validate_source_object(const DocumentObject& object) {
    if (!object.transform.is_finite() || !std::isfinite(object.stroke_width_mm) || object.stroke_width_mm<0 ||
        object.name.size()>1024*1024) throw std::invalid_argument("Invalid object metadata");
    for (const auto* color : {&object.stroke_color,&object.fill_color}) {
        if (color->model>ColorModel::Spot || !std::isfinite(color->c0) || !std::isfinite(color->c1) ||
            !std::isfinite(color->c2) || !std::isfinite(color->c3) || !std::isfinite(color->spot_tint) ||
            color->spot_name.size()>1024*1024 || color->icc_profile_name.size()>1024*1024)
            throw std::invalid_argument("Invalid source color metadata");
    }
    if (const auto* path=dynamic_cast<const PathObject*>(&object)) {
        if (path->path.segments.size()>250000) throw std::invalid_argument("Path segment budget exceeded");
        for (const auto& segment:path->path.segments) {
            if (segment.type>pikdraw::geometry::SegmentType::Close) throw std::invalid_argument("Unknown segment");
            for (auto point : {segment.p0,segment.p1,segment.p2,segment.p3}) {
                const auto world=object.transform.apply(point);
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x)>1e7 || std::abs(point.y)>1e7 ||
                    !std::isfinite(world.x) || !std::isfinite(world.y)) throw std::invalid_argument("Invalid curve coordinates");
            }
        }
    } else if (const auto* text=dynamic_cast<const TextObject*>(&object)) {
        if (!std::isfinite(text->font_size_pt) || text->font_size_pt<=0 || text->utf8_text.size()>1024*1024 ||
            text->font_family.size()>1024*1024) throw std::invalid_argument("Invalid text metadata");
    } else {
        throw std::invalid_argument("Groups and custom source objects unsupported until bounds/topology are implemented");
    }
    if (!object.bounds().is_finite()) throw std::invalid_argument("Nonfinite source bounds");
}

} // namespace pikdraw::document
