#include "pikdraw/typography/shaping/shaper.hpp"
#include "system_font.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#ifdef _WIN32
namespace pikdraw::typography {
namespace {
class GlyphCollector final : public IDWriteTextRenderer {
  public:
    GlyphCollector(ShapedRun &run, const std::vector<size_t> &offsets) : run_(run), offsets_(offsets) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextRenderer) ||
            iid == __uuidof(IDWritePixelSnapping)) {
            *object = static_cast<IDWriteTextRenderer *>(this);
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
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) override {
        *disabled = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *value) override {
        *value = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *value) override {
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT baseline_x, FLOAT baseline_y, DWRITE_MEASURING_MODE,
                                           const DWRITE_GLYPH_RUN *glyph_run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION *description,
                                           IUnknown *) override {
        try {
            collect(baseline_x, baseline_y, glyph_run, description);
            return S_OK;
        } catch (...) {
            error = std::current_exception();
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *,
                                            IUnknown *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *,
                                                IUnknown *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, FLOAT, FLOAT, IDWriteInlineObject *, BOOL, BOOL,
                                               IUnknown *) override {
        return E_NOTIMPL;
    }
    std::exception_ptr error;

  private:
    void collect(FLOAT baseline_x, FLOAT baseline_y, const DWRITE_GLYPH_RUN *glyph_run,
                 const DWRITE_GLYPH_RUN_DESCRIPTION *description) {
        using namespace system_font;
        if (!description || glyph_run->isSideways || glyph_run->bidiLevel != 0)
            throw std::runtime_error(
                "Directional/sideways typography is unsupported by the current run model");
        IDWriteFontCollection *collection = system_font_collection();
        ComPtr<IDWriteFont> actual;
        check(collection->GetFontFromFontFace(glyph_run->fontFace, actual.put()), "GetFontFromFontFace");
        const auto identity = describe(actual.get(), run_.font.size_pt);
        if (identity.postscript_name != run_.font.postscript_name ||
            glyph_run->fontFace->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
            throw std::runtime_error("Mixed-font glyph fallback is unsupported by the current run model");
        std::vector<size_t> clusters(glyph_run->glyphCount, std::numeric_limits<size_t>::max());
        for (UINT32 i = 0; i < description->stringLength; ++i) {
            const auto glyph = description->clusterMap[i];
            if (glyph >= clusters.size() || description->textPosition + i >= offsets_.size())
                throw std::runtime_error("Invalid DirectWrite cluster map");
            clusters[glyph] = std::min(clusters[glyph], offsets_[description->textPosition + i]);
        }
        size_t cluster = 0;
        if (!have_baseline_) {
            first_baseline_ = baseline_y;
            have_baseline_ = true;
        }
        const double origin_correction = baseline_x - run_.total_advance_mm;
        for (UINT32 i = 0; i < glyph_run->glyphCount; ++i) {
            if (clusters[i] != std::numeric_limits<size_t>::max())
                cluster = clusters[i];
            if (glyph_run->glyphIndices[i] == 0)
                throw std::runtime_error("Font lacks required glyph");
            const auto offset = glyph_run->glyphOffsets ? glyph_run->glyphOffsets[i] : DWRITE_GLYPH_OFFSET{};
            run_.glyphs.push_back({glyph_run->glyphIndices[i], cluster, glyph_run->glyphAdvances[i], 0,
                                   offset.advanceOffset + origin_correction,
                                   baseline_y - first_baseline_ - offset.ascenderOffset});
            run_.total_advance_mm += glyph_run->glyphAdvances[i];
        }
    }
    ShapedRun &run_;
    const std::vector<size_t> &offsets_;
    bool have_baseline_{};
    FLOAT first_baseline_{};
};
} // namespace
ShapedRun shape_text(std::string_view text, const FontIdentity &requested) {
    using namespace system_font;
    const auto unicode = wide(text);
    if (unicode.find_first_of(L"\r\n\t") != std::wstring::npos)
        throw std::invalid_argument("Shape one plain line; tabs require explicit tab stops");
    const auto resolved = FontResolver::instance().resolve(requested);
    ShapedRun result;
    result.font = resolved.actual;
    result.requested_font = requested;
    result.is_fallback = resolved.is_fallback;
    result.warning_message = resolved.warning_message;
    result.text_utf8 = text;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
    load_font(result.font, font, face);
    DWRITE_FONT_METRICS metrics{};
    face->GetMetrics(&metrics);
    result.ascent_mm = result.font.size_mm() * metrics.ascent / metrics.designUnitsPerEm;
    result.descent_mm = result.font.size_mm() * metrics.descent / metrics.designUnitsPerEm;
    if (text.empty())
        return result;
    ComPtr<IDWriteTextFormat> format;
    check(factory()->CreateTextFormat(wide(result.font.family_name).c_str(), nullptr, font->GetWeight(),
                                      font->GetStyle(), font->GetStretch(),
                                      static_cast<FLOAT>(result.font.size_mm()), L"vi-vn", format.put()),
          "CreateTextFormat");
    check(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "SetWordWrapping");
    ComPtr<IDWriteTextLayout> layout;
    check(factory()->CreateTextLayout(unicode.data(), static_cast<UINT32>(unicode.size()), format.get(),
                                      10000000, 10000000, layout.put()),
          "CreateTextLayout");
    const auto offsets = utf16_byte_offsets(text);
    GlyphCollector collector(result, offsets);
    const auto hr = layout->Draw(nullptr, &collector, 0, 0);
    if (collector.error)
        std::rethrow_exception(collector.error);
    check(hr, "Shape glyph runs");
    return result;
}
} // namespace pikdraw::typography
#else
namespace pikdraw::typography {
ShapedRun shape_text(std::string_view, const FontIdentity &) {
    throw std::runtime_error("Typography system backend unavailable");
}
} // namespace pikdraw::typography
#endif
