#pragma once
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/document/effects/effect_stack.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"
#include "pikdraw/document/model/raster_edit_graph.hpp"
#include "pikdraw/document/model/raster_source_provider.hpp"
#include <string>
#include <vector>
#include <memory>
#include <limits>
#include <variant>
#include <stdexcept>
#include <algorithm>
#include <span>

namespace pikdraw::document {

enum class ObjectType {
    Path,
    Text,
    Group,
    Image
};

enum class RasterPixelColorBasis : uint8_t {
    LegacyUnknown = 0,
    AssumedSRgb = 1,
    ConvertedToSRgb = 2,
    EmbeddedProfileUnapplied = 3
};

struct PathContourPaints {
    color::ColorValue outline;
    color::ColorValue fill;
};

struct PathObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    // Renderer/interchange adapter. When authoring_source exists, edits must replace that source
    // and regenerate this value; the immutable source is authoritative.
    geometry::Path2D path;
    std::shared_ptr<const EditablePath> authoring_source;
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    color::ColorValue fill{color::RGBColor{0.2, 0.4, 0.8, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double stroke_width_mm{0.0};
    bool has_fill{true};
    bool has_stroke{true};
    std::shared_ptr<const GradientPaint> fill_gradient{nullptr};
    Appearance appearance;
    bool visible{true};
    bool locked{false};
    std::string contour_spec;
    std::optional<PathContourPaints> contour_paints;

    [[nodiscard]] EffectiveFillPaint effective_fill_paint() const {
        if (!has_fill) {
            return {FillPaintKind::None, fill, nullptr};
        }
        if (fill_gradient) {
            return {FillPaintKind::Gradient, fill, fill_gradient};
        }
        return {FillPaintKind::Solid, fill, nullptr};
    }


    void set_authoring_source(EditablePath source) {
        if (const auto error = source.validation_error())
            throw std::invalid_argument(*error);
        path = editable_path_to_path2d(source);
        for (const auto& subpath : source.subpaths) {
            reserve_path_element_id(subpath.id);
            for (const auto& node : subpath.nodes) reserve_path_element_id(node.id);
            for (const auto& segment : subpath.segments) reserve_path_element_id(segment.id);
        }
        authoring_source = std::make_shared<const EditablePath>(std::move(source));
    }

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
    Appearance appearance;
    typography::TextAlignment alignment{typography::TextAlignment::Left};
    bool underline{false};
    bool strikethrough{false};
    double frame_width_mm{0.0}; // 0: point text; >0: editable wrapping paragraph.
    double frame_height_mm{0.0}; // 0: auto height; >0: fixed frame height with text flow overflow.
    ObjectId next_frame_id{kInvalidObjectId}; // Linked frame receiving overflow text
    ObjectId prev_frame_id{kInvalidObjectId}; // Linked frame feeding text into this frame
    bool has_overflow{false}; // Overflow indicator (text truncated in chain)
    ObjectId path_binding_id{kInvalidObjectId}; // Text on Path binding
    double path_offset_mm{0.0}; // Offset along curve
    bool path_reversed{false}; // Invert curve direction / flip across curve
    double path_baseline_offset_mm{0.0}; // Distance offset perpendicular to curve
    double line_spacing_ratio{1.2};
    double letter_spacing_mm{0.0};
    geometry::Point2D position{0.0, 0.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] bool is_linked() const noexcept {
        return next_frame_id != kInvalidObjectId || prev_frame_id != kInvalidObjectId;
    }

    [[nodiscard]] bool is_text_on_path() const noexcept {
        return path_binding_id != kInvalidObjectId;
    }

    [[nodiscard]] typography::TextLayout layout() const {
        typography::LayoutOptions options;
        options.alignment = alignment;
        options.max_width_mm = frame_width_mm;
        options.line_spacing_ratio = line_spacing_ratio;
        options.letter_spacing_mm = letter_spacing_mm;
        return typography::TextLayout::layout_text(text, font, options);
    }

    [[nodiscard]] geometry::Rect2D bounds() const {
        auto box = layout().bounds();
        if (frame_width_mm > 0) { box.x = 0; box.width = frame_width_mm; }
        if (frame_height_mm > 0) { box.height = std::max(box.height, frame_height_mm); }
        // Keep an empty line selectable without a fake width based on UTF-8 bytes.
        box.width = std::max(box.width, font.size_mm() * 0.2);
        if (underline) box.height += font.size_mm() * 0.12;
        return (transform * geometry::Transform2D::translation(position.x, position.y) *
                geometry::Transform2D::scale(1, -1)).map_rect(box);
    }
};

enum class ClipContentFitMode : uint8_t {
    CurrentPosition = 0,
    CenterContents,
    FitProportionally,
    FillProportionally,
    StretchToFill
};

struct ClipProperties {
    ObjectId mask_object_id{kInvalidObjectId};
    bool lock_contents_to_frame{true};
    bool preserve_frame_appearance{true};
    ClipContentFitMode fit_mode{ClipContentFitMode::CurrentPosition};
    bool is_empty_frame{false};
};

struct GroupObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    std::vector<ObjectId> children_ids;
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    std::optional<ClipProperties> clip;
    bool visible{true};
    bool locked{false};

    [[nodiscard]] bool is_clipping_container() const noexcept {
        return clip.has_value() && clip->mask_object_id != kInvalidObjectId;
    }
};

/// Raster source identity and metadata kept beside the editable RGBA preview.
/// Embedded source bytes may be held inline for legacy callers or through a
/// runtime provider so large documents do not retain a second RAM copy.
struct RasterSourceMetadata {
    // Native storage currently uses a U32 blob length; this is a format bound,
    // not an import quota. Import workers stop at the system RAM pressure gate.
    static constexpr size_t kMaxSourceBytes = std::numeric_limits<uint32_t>::max();
    static constexpr size_t kMaxIccProfileBytes = 1u * 1024u * 1024u;

    std::string media_type;
    std::vector<uint8_t> original_bytes;
    std::shared_ptr<const std::vector<uint8_t>> shared_original_bytes;
    double dpi_x{96.0};
    double dpi_y{96.0};
    bool dpi_from_source{false};
    uint32_t frame_index{0};
    uint32_t frame_count{1};
    uint32_t source_bits_per_pixel{0};
    uint16_t exif_orientation{1};
    bool orientation_applied{false};
    std::vector<uint8_t> icc_profile;
    RasterPixelColorBasis preview_pixel_color_basis{RasterPixelColorBasis::AssumedSRgb};
    // Runtime-only source access. Native storage persists the descriptor and
    // source bytes, never this provider or its open file handle. When present,
    // the provider may be the sole runtime owner of embedded source bytes.
    std::shared_ptr<const RasterSourceProvider> asset_provider;
    // Optional admission lease for inline encoded bytes retained by older
    // callers. Disk-backed providers do not retain this lease. It is not serialized.
    std::shared_ptr<const void> source_memory_lease;

    [[nodiscard]] const std::vector<uint8_t>& original_data() const noexcept {
        return shared_original_bytes ? *shared_original_bytes : original_bytes;
    }
};

struct ImageObject {
    ObjectId id{kInvalidObjectId};
    std::string name;
    geometry::Rect2D bounds_rect{0.0, 0.0, 100.0, 80.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    int32_t width_px{160};
    int32_t height_px{120};
    std::vector<uint32_t> pixels_rgba;
    // Imported placements may share immutable decoded pixels while retaining
    // independent bounds, transforms, names, and selection IDs.
    std::shared_ptr<const std::vector<uint32_t>> shared_pixels_rgba;
    // Optional high-precision working source in straight-alpha, encoded sRGB
    // RGBA16 little-endian order. pixels_rgba remains the renderer-facing
    // 8-bit preview; color edits use this source when it is present.
    std::shared_ptr<const std::vector<uint8_t>> shared_pixels_rgba16le;
    RasterSourceMetadata source;
    std::optional<RasterSourceDescriptor> asset_source;
    RasterEditGraph edit_graph;
    std::optional<geometry::Path2D> clip_path;
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const {
        return transform.map_rect(bounds_rect);
    }

    [[nodiscard]] std::span<const uint32_t> rgba_pixels() const noexcept {
        if (shared_pixels_rgba) return *shared_pixels_rgba;
        return pixels_rgba;
    }

    [[nodiscard]] std::span<const uint8_t> rgba16le_pixels() const noexcept {
        return shared_pixels_rgba16le
            ? std::span<const uint8_t>(*shared_pixels_rgba16le)
            : std::span<const uint8_t>{};
    }

    static ImageObject make_procedural_photo(ObjectId id, double x, double y, double w, double h, const std::string& name = "Photo Asset") {
        ImageObject obj;
        obj.id = id;
        obj.name = name;
        obj.bounds_rect = {x, y, w, h};
        obj.width_px = 160;
        obj.height_px = 120;
        obj.pixels_rgba.resize(static_cast<size_t>(obj.width_px) * obj.height_px);

        auto rgba16 = std::make_shared<std::vector<uint8_t>>(
            static_cast<size_t>(obj.width_px) * static_cast<size_t>(obj.height_px) * 8u);
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

                const size_t idx = static_cast<size_t>(row) * obj.width_px + col;
                uint32_t packed = (static_cast<uint32_t>(r) << 24) |
                                  (static_cast<uint32_t>(g) << 16) |
                                  (static_cast<uint32_t>(b) << 8) | 0xFF;
                obj.pixels_rgba[idx] = packed;
                const size_t o16 = idx * 8u;
                (*rgba16)[o16 + 0] = r; (*rgba16)[o16 + 1] = r;
                (*rgba16)[o16 + 2] = g; (*rgba16)[o16 + 3] = g;
                (*rgba16)[o16 + 4] = b; (*rgba16)[o16 + 5] = b;
                (*rgba16)[o16 + 6] = 0xFFu; (*rgba16)[o16 + 7] = 0xFFu;
            }
        }
        obj.shared_pixels_rgba16le = std::move(rgba16);
        obj.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
        return obj;
    }
};

using ObjectVariant = std::variant<PathObject, TextObject, GroupObject, ImageObject>;

inline constexpr uint32_t kDefaultLayerPalette[8] = {
    0x00F2C3FF, // Cyan / Teal
    0xEF4444FF, // Coral Red
    0x3B82F6FF, // Royal Blue
    0xF59E0BFF, // Amber Orange
    0x8B5CF6FF, // Purple / Violet
    0x10B981FF, // Emerald Green
    0xEC4899FF, // Pink
    0xF97316FF  // Deep Orange
};

inline uint32_t default_layer_color(size_t index) noexcept {
    return kDefaultLayerPalette[index % 8];
}

using LayerId = uint32_t;
inline constexpr LayerId kInvalidLayerId = 0;

enum class LayerRole : uint8_t {
    Artwork = 0,
    AnnotationGroupHeader = 1,
    AnnotationDetailView = 2,
    AnnotationNotes = 3,
    AnnotationSymbols = 4
};

struct Layer {
    std::string name{"Layer 1"};
    bool visible{true};
    bool locked{false};
    bool printable{true};
    uint32_t color_rgba{0x00F2C3FF};
    std::vector<ObjectId> object_ids;
    LayerId id{kInvalidLayerId};
    LayerId group_id{kInvalidLayerId};
    LayerRole role{LayerRole::Artwork};

    Layer() = default;
    Layer(std::string name_in, bool visible_in, bool locked_in, std::vector<ObjectId> ids_in)
        : name(std::move(name_in)), visible(visible_in), locked(locked_in),
          printable(true), color_rgba(0x00F2C3FF), object_ids(std::move(ids_in)) {}
    Layer(std::string name_in, bool visible_in, bool locked_in, bool printable_in,
          uint32_t color_in, std::vector<ObjectId> ids_in)
        : name(std::move(name_in)), visible(visible_in), locked(locked_in),
          printable(printable_in), color_rgba(color_in), object_ids(std::move(ids_in)) {}
    Layer(std::string name_in, bool visible_in, bool locked_in, bool printable_in,
          uint32_t color_in, std::vector<ObjectId> ids_in, LayerId id_in, LayerId group_id_in, LayerRole role_in)
        : name(std::move(name_in)), visible(visible_in), locked(locked_in),
          printable(printable_in), color_rgba(color_in), object_ids(std::move(ids_in)),
          id(id_in), group_id(group_id_in), role(role_in) {}
};

struct Page {
    double width_mm{20000.0}; // Default 20-meter billboard capable
    double height_mm{1000.0};
    std::vector<Layer> layers;
    color::DocumentColorMode color_mode{color::DocumentColorMode::Rgb};
    std::string color_profile_name{"sRGB"};
    int32_t raster_dpi{72};
    // v2-v8 never declared a process mode. Preserve those source channels until
    // explicit conversion; new desktop documents enable normalization immediately.
    bool color_mode_explicit{false};
    bool raster_dpi_explicit{false};
    std::shared_ptr<const color::ProfileResource> color_profile_resource;
    geometry::units::LengthUnit display_length_unit{geometry::units::LengthUnit::Millimeter};
    bool display_length_unit_explicit{false};

    [[nodiscard]] bool effective_layer_visible(size_t index) const {
        if (index >= layers.size()) return false;
        const auto& lyr = layers[index];
        if (lyr.group_id == kInvalidLayerId) return lyr.visible;
        for (const auto& other : layers) {
            if (other.id == lyr.group_id) {
                return other.visible && lyr.visible;
            }
        }
        return lyr.visible;
    }

    [[nodiscard]] bool effective_layer_locked(size_t index) const {
        if (index >= layers.size()) return true;
        const auto& lyr = layers[index];
        if (lyr.group_id == kInvalidLayerId) return lyr.locked;
        for (const auto& other : layers) {
            if (other.id == lyr.group_id) {
                return other.locked || lyr.locked;
            }
        }
        return lyr.locked;
    }

    [[nodiscard]] bool effective_layer_printable(size_t index) const {
        if (index >= layers.size()) return false;
        const auto& lyr = layers[index];
        if (lyr.group_id == kInvalidLayerId) return lyr.printable;
        for (const auto& other : layers) {
            if (other.id == lyr.group_id) {
                return other.printable && lyr.printable;
            }
        }
        return lyr.printable;
    }

    [[nodiscard]] std::optional<size_t> find_layer_by_id(LayerId id) const {
        for (size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].id == id) return i;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<size_t> find_layer_by_role(LayerRole role) const {
        for (size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].role == role) return i;
        }
        return std::nullopt;
    }

    [[nodiscard]] LayerId generate_next_layer_id() const noexcept {
        LayerId max_id = 0;
        for (const auto& l : layers) {
            if (l.id > max_id) max_id = l.id;
        }
        return max_id + 1;
    }

    static Page make_default() {
        Page p;
        p.width_mm = 20000.0;
        p.height_mm = 1000.0;
        p.layers.push_back(Layer{"Layer 1", true, false, true, 0x00F2C3FF, {}, 1, 0, LayerRole::Artwork});
        return p;
    }
};

} // namespace pikdraw::document
