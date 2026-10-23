#include "pikdraw/typography/layout/text_on_path.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "../shaping/system_font.hpp"
#include <cmath>
#include <limits>
#include <algorithm>

#ifdef _WIN32
#include <d2d1.h>
#endif

namespace pikdraw::typography {

namespace {

#ifdef _WIN32
class SingleGlyphSink final : public IDWriteGeometrySink {
public:
    explicit SingleGlyphSink(geometry::Path2D& path, const geometry::Transform2D& tx)
        : path_(path), tx_(tx) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteGeometrySink)) {
            *object = static_cast<IDWriteGeometrySink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) noexcept override {
        path_.set_winding_rule(mode == D2D1_FILL_MODE_ALTERNATE
            ? geometry::WindingRule::EvenOdd
            : geometry::WindingRule::NonZero);
    }

    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}

    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) noexcept override {
        guard([&] {
            auto pt = tx_.map_point({start.x, start.y});
            path_.move_to(pt.x, pt.y);
        });
    }

    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) noexcept override {
        guard([&] {
            for (UINT32 i = 0; i < count; ++i) {
                auto pt = tx_.map_point({points[i].x, points[i].y});
                path_.line_to(pt.x, pt.y);
            }
        });
    }

    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* segments, UINT32 count) noexcept override {
        guard([&] {
            for (UINT32 i = 0; i < count; ++i) {
                const auto& s = segments[i];
                auto p1 = tx_.map_point({s.point1.x, s.point1.y});
                auto p2 = tx_.map_point({s.point2.x, s.point2.y});
                auto p3 = tx_.map_point({s.point3.x, s.point3.y});
                path_.cubic_to(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
            }
        });
    }

    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) noexcept override {
        guard([&] {
            if (end == D2D1_FIGURE_END_CLOSED) path_.close();
        });
    }

    HRESULT STDMETHODCALLTYPE Close() noexcept override {
        return failed_ ? E_OUTOFMEMORY : S_OK;
    }

private:
    template <class F> void guard(F operation) noexcept {
        if (failed_) return;
        try { operation(); } catch (...) { failed_ = true; }
    }
    geometry::Path2D& path_;
    geometry::Transform2D tx_;
    bool failed_{false};
};
#endif

struct ArcLengthTable {
    std::vector<geometry::Point2D> points;
    std::vector<double> cumulative_lengths;
    double total_length{0.0};

    [[nodiscard]] static ArcLengthTable build(const std::vector<geometry::Point2D>& pts) {
        ArcLengthTable table;
        if (pts.size() < 2) return table;
        table.points = pts;
        table.cumulative_lengths.resize(pts.size(), 0.0);
        double sum = 0.0;
        for (size_t i = 1; i < pts.size(); ++i) {
            double dx = pts[i].x - pts[i - 1].x;
            double dy = pts[i].y - pts[i - 1].y;
            sum += std::sqrt(dx * dx + dy * dy);
            table.cumulative_lengths[i] = sum;
        }
        table.total_length = sum;
        return table;
    }

    struct Sample {
        geometry::Point2D point;
        geometry::Point2D tangent; // Normalized
        double angle_radians{0.0};
        bool valid{false};
    };

    [[nodiscard]] Sample sample(double s) const {
        Sample smp;
        if (points.size() < 2 || total_length <= 1e-9) return smp;
        s = std::clamp(s, 0.0, total_length);

        auto it = std::upper_bound(cumulative_lengths.begin(), cumulative_lengths.end(), s);
        size_t idx = (it == cumulative_lengths.end()) ? (points.size() - 1) : static_cast<size_t>(std::distance(cumulative_lengths.begin(), it));
        if (idx == 0) idx = 1;

        double s0 = cumulative_lengths[idx - 1];
        double s1 = cumulative_lengths[idx];
        double seg_len = s1 - s0;
        double t = (seg_len > 1e-9) ? ((s - s0) / seg_len) : 0.0;
        t = std::clamp(t, 0.0, 1.0);

        const auto& p0 = points[idx - 1];
        const auto& p1 = points[idx];
        smp.point = { (1.0 - t) * p0.x + t * p1.x, (1.0 - t) * p0.y + t * p1.y };

        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-9) {
            smp.tangent = { dx / len, dy / len };
        } else {
            smp.tangent = { 1.0, 0.0 };
        }
        smp.angle_radians = std::atan2(smp.tangent.y, smp.tangent.x);
        smp.valid = true;
        return smp;
    }
};

} // namespace

TextOnPathLayoutResult layout_text_on_path(
    const geometry::Path2D& path,
    std::string_view utf8_text,
    const FontIdentity& font,
    const TextOnPathOptions& options
) {
    TextOnPathLayoutResult result;
    if (utf8_text.empty() || path.elements().empty()) return result;

    // 1. Flatten path to polyline
    auto flattened = geometry::flatten_path(path);
    if (flattened.empty() || flattened.front().points.size() < 2) return result;

    // Pick longest subpath if multiple
    size_t best_subpath_idx = 0;
    size_t max_pts = 0;
    for (size_t i = 0; i < flattened.size(); ++i) {
        if (flattened[i].points.size() > max_pts) {
            max_pts = flattened[i].points.size();
            best_subpath_idx = i;
        }
    }

    auto pts = flattened[best_subpath_idx].points;
    if (options.reversed) {
        std::reverse(pts.begin(), pts.end());
    }

    auto arc_table = ArcLengthTable::build(pts);
    result.path_length_mm = arc_table.total_length;
    if (result.path_length_mm <= 1e-6) return result;

    // 2. Shape the single-line text
    LayoutOptions layout_opts;
    layout_opts.max_width_mm = 0.0;
    auto layout = TextLayout::layout_text(utf8_text, font, layout_opts);
    if (layout.lines().empty()) return result;

    const auto& line = layout.lines().front();
    double total_text_w = line.run.total_advance_mm;
    result.text_length_mm = total_text_w;

    // 3. Compute starting arc length
    double start_s = options.start_offset_mm;
    if (options.alignment == TextAlignment::Center) {
        start_s += (result.path_length_mm - total_text_w) * 0.5;
    } else if (options.alignment == TextAlignment::Right) {
        start_s += (result.path_length_mm - total_text_w);
    }

    double current_s = start_s;

#ifdef _WIN32
    using namespace system_font;
    ComPtr<IDWriteFont> dwrite_font;
    ComPtr<IDWriteFontFace> dwrite_face;
    try {
        load_font(font, dwrite_font, dwrite_face);
    } catch (...) {
        // Fallback or ignore if DWrite fails in non-GUI context
    }
#endif

    result.glyphs.reserve(line.run.glyphs.size());
    for (const auto& glyph : line.run.glyphs) {
        double advance = glyph.x_advance_mm;
        double glyph_mid_s = current_s + advance * 0.5;

        PlacedGlyphOnPath placed;
        placed.advance_mm = advance;

        if (glyph_mid_s < 0.0 || glyph_mid_s > result.path_length_mm) {
            placed.is_visible = false;
            result.fits_on_path = false;
        } else {
            auto smp = arc_table.sample(glyph_mid_s);
            if (smp.valid) {
                // Normal vector perpendicular to tangent
                geometry::Point2D normal{ -smp.tangent.y, smp.tangent.x };
                if (options.reversed) {
                    normal.x = -normal.x;
                    normal.y = -normal.y;
                }

                placed.position = {
                    smp.point.x + normal.x * options.baseline_offset_mm,
                    smp.point.y + normal.y * options.baseline_offset_mm
                };
                placed.rotation_radians = smp.angle_radians;
                placed.is_visible = true;

#ifdef _WIN32
                if (dwrite_face.get() != nullptr && glyph.glyph_id > 0 && glyph.glyph_id < dwrite_face->GetGlyphCount()) {
                    UINT16 g_id = static_cast<UINT16>(glyph.glyph_id);
                    FLOAT g_adv = static_cast<FLOAT>(glyph.x_advance_mm);
                    DWRITE_GLYPH_OFFSET g_off{
                        static_cast<FLOAT>(glyph.offset_x_mm - advance * 0.5),
                        static_cast<FLOAT>(-glyph.offset_y_mm)
                    };

                    // Combined transform: Rotate -> Translate
                    auto glyph_tx = geometry::Transform2D::translation(placed.position.x, placed.position.y) *
                                    geometry::Transform2D::rotation_radians(placed.rotation_radians);

                    SingleGlyphSink sink(result.combined_outlines, glyph_tx);
                    (void)dwrite_face->GetGlyphRunOutline(
                        static_cast<FLOAT>(font.size_mm()),
                        &g_id, &g_adv, &g_off, 1,
                        FALSE, FALSE, &sink
                    );
                    (void)sink.Close();
                }
#endif
            }
        }

        result.glyphs.push_back(placed);
        current_s += advance;
    }

    result.bounds = result.combined_outlines.bounds();
    return result;
}

} // namespace pikdraw::typography
