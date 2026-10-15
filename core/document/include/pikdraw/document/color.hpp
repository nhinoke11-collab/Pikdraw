#pragma once

#include <string>
#include <cstdint>

namespace pikdraw::document {

enum class ColorModel : uint8_t {
    RGB = 0,
    CMYK = 1,
    Spot = 2
};

struct ColorData {
    ColorModel model{ColorModel::RGB};
    float c0{0.0f}; // R or C
    float c1{0.0f}; // G or M
    float c2{0.0f}; // B or Y
    float c3{0.0f}; // 0 or K
    std::string spot_name;
    float spot_tint{1.0f};
    std::string icc_profile_name;
    bool overprint{false};

    static ColorData make_rgb(float r, float g, float b, const std::string& profile = "sRGB") {
        ColorData c;
        c.model = ColorModel::RGB;
        c.c0 = r; c.c1 = g; c.c2 = b; c.c3 = 0.0f;
        c.icc_profile_name = profile;
        return c;
    }

    static ColorData make_cmyk(float c, float m, float y, float k, const std::string& profile = "ISO Coated v2") {
        ColorData col;
        col.model = ColorModel::CMYK;
        col.c0 = c; col.c1 = m; col.c2 = y; col.c3 = k;
        col.icc_profile_name = profile;
        return col;
    }

    static ColorData make_spot(const std::string& name, float tint, const std::string& profile = "") {
        ColorData col;
        col.model = ColorModel::Spot;
        col.spot_name = name;
        col.spot_tint = tint;
        col.icc_profile_name = profile;
        return col;
    }
};

} // namespace pikdraw::document
