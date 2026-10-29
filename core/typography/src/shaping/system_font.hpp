#pragma once
#include "pikdraw/typography/shaping/font_resolver.hpp"
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwrite.h>
#include <windows.h>
namespace pikdraw::typography::system_font {
template <class T> class ComPtr {
  public:
    ~ComPtr() {
        if (value_)
            value_->Release();
    }
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T *operator->() const {
        return value_;
    }
    T *get() const {
        return value_;
    }
    T **put() {
        return &value_;
    }

  private:
    T *value_{};
};
void check(HRESULT result, const char *operation);
IDWriteFactory *factory();
IDWriteFontCollection *system_font_collection();
std::wstring wide(std::string_view value);
std::string utf8(std::wstring_view value);
std::string family_name(IDWriteFont *font);
void load_font(const FontIdentity &identity, ComPtr<IDWriteFont> &font, ComPtr<IDWriteFontFace> &face);
FontIdentity describe(IDWriteFont *font, double size_pt);
std::vector<size_t> utf16_byte_offsets(std::string_view value);
} // namespace pikdraw::typography::system_font
#endif
