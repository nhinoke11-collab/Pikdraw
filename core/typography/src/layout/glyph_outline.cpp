#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "../shaping/system_font.hpp"
#include <cmath>
#include <limits>
#ifdef _WIN32
#include <d2d1.h>
namespace pikdraw::typography {
namespace {
class OutlineSink final : public IDWriteGeometrySink {
  public:
    OutlineSink(geometry::Path2D &path, geometry::Point2D origin) : path_(path), origin_(origin) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteGeometrySink)) {
            *object = static_cast<IDWriteGeometrySink *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) noexcept override {
        path_.set_winding_rule(mode == D2D1_FILL_MODE_ALTERNATE ? geometry::WindingRule::EvenOdd
                                                                : geometry::WindingRule::NonZero);
    }
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) noexcept override {
        guard([&] { path_.move_to(origin_.x + start.x, origin_.y + start.y); });
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F *points, UINT32 count) noexcept override {
        guard([&] {
            for (UINT32 i = 0; i < count; ++i)
                path_.line_to(origin_.x + points[i].x, origin_.y + points[i].y);
        });
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT *segments, UINT32 count) noexcept override {
        guard([&] {
            for (UINT32 i = 0; i < count; ++i) {
                const auto &s = segments[i];
                path_.cubic_to(origin_.x + s.point1.x, origin_.y + s.point1.y, origin_.x + s.point2.x,
                               origin_.y + s.point2.y, origin_.x + s.point3.x, origin_.y + s.point3.y);
            }
        });
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) noexcept override {
        guard([&] {
            if (end == D2D1_FIGURE_END_CLOSED)
                path_.close();
        });
    }
    HRESULT STDMETHODCALLTYPE Close() noexcept override {
        return failed_ ? E_OUTOFMEMORY : S_OK;
    }

  private:
    template <class F> void guard(F operation) noexcept {
        if (failed_)
            return;
        try {
            operation();
        } catch (...) {
            failed_ = true;
        }
    }
    geometry::Path2D &path_;
    geometry::Point2D origin_;
    bool failed_{};
};
} // namespace
geometry::Path2D text_to_path_outlines(const TextLayout &layout, bool underline) {
    using namespace system_font;
    geometry::Path2D result;
    for (const auto &line : layout.lines()) {
        if (!std::isfinite(line.baseline_origin.x) || !std::isfinite(line.baseline_origin.y))
            throw std::invalid_argument("Invalid glyph baseline");
        ComPtr<IDWriteFont> font;
        ComPtr<IDWriteFontFace> face;
        load_font(line.run.font, font, face);
        const auto actual = describe(font.get(), line.run.font.size_pt);
        if (actual.postscript_name != line.run.font.postscript_name)
            throw std::runtime_error("Shaped font identity no longer available");
        std::vector<UINT16> indices;
        std::vector<FLOAT> advances;
        std::vector<DWRITE_GLYPH_OFFSET> offsets;
        for (const auto &glyph : line.run.glyphs) {
            if (!std::isfinite(glyph.x_advance_mm) || !std::isfinite(glyph.offset_x_mm) ||
                !std::isfinite(glyph.offset_y_mm) || glyph.y_advance_mm != 0)
                throw std::invalid_argument("Invalid glyph placement");
            const double limit = std::numeric_limits<FLOAT>::max();
            if (std::abs(glyph.x_advance_mm) > limit || std::abs(glyph.offset_x_mm) > limit ||
                std::abs(glyph.offset_y_mm) > limit)
                throw std::invalid_argument("Glyph placement exceeds backend range");
            if (glyph.glyph_id == 0 || glyph.glyph_id >= face->GetGlyphCount())
                throw std::invalid_argument("Invalid glyph identity");
            indices.push_back(static_cast<UINT16>(glyph.glyph_id));
            advances.push_back(static_cast<FLOAT>(glyph.x_advance_mm));
            offsets.push_back(
                {static_cast<FLOAT>(glyph.offset_x_mm), static_cast<FLOAT>(-glyph.offset_y_mm)});
        }
        if (indices.empty())
            continue;
        OutlineSink sink(result, line.baseline_origin);
        check(face->GetGlyphRunOutline(static_cast<FLOAT>(line.run.font.size_mm()), indices.data(),
                                       advances.data(), offsets.data(), static_cast<UINT32>(indices.size()),
                                       FALSE, FALSE, &sink),
              "GetGlyphRunOutline");
        check(sink.Close(), "Outline sink");
        if (underline && line.run.total_advance_mm > 0) {
            const double x = line.baseline_origin.x;
            const double y = line.baseline_origin.y + line.run.font.size_mm() * 0.1;
            const double w = line.run.total_advance_mm;
            const double h = std::max(0.04, line.run.font.size_mm() * 0.055);
            result.move_to(x, y);
            result.line_to(x + w, y);
            result.line_to(x + w, y + h);
            result.line_to(x, y + h);
            result.close();
        }
    }
    return result;
}
} // namespace pikdraw::typography
#else
namespace pikdraw::typography {
geometry::Path2D text_to_path_outlines(const TextLayout &, bool) {
    throw std::runtime_error("Typography outlines unavailable");
}
} // namespace pikdraw::typography
#endif
