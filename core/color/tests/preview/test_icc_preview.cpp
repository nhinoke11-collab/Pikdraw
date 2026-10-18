#include "pikdraw/color/preview/icc_preview.hpp"
#include <iostream>
#include <stdexcept>
#define REQUIRE(value)                                                                                       \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error("Failed: " #value);                                                     \
    } while (false)

int main() {
    using namespace pikdraw::color;
#ifdef _WIN32
    try {
        std::string error;
        const auto srgb = IccPreview::system_srgb_profile();
        REQUIRE(!srgb.empty() && std::filesystem::is_regular_file(srgb));
        const auto preview = IccPreview::create({}, {}, &error);
        REQUIRE(preview && error.empty());
        const auto rgb = ColorValue::make_rgb(.2, .4, .8, .35);
        const auto converted = preview->convert(rgb);
        REQUIRE(std::abs(converted.r - .2) < .002 && std::abs(converted.g - .4) < .002);
        REQUIRE(std::abs(converted.b - .8) < .002 && std::abs(converted.a - .35) < 1e-6);
        REQUIRE(!IccPreview::create(srgb.parent_path() / "missing-pikdraw-profile.icc", {}, &error));
        REQUIRE(!error.empty());
        REQUIRE(!IccPreview::create({}, srgb, &error)); // reject RGB in the CMYK slot
        REQUIRE(!error.empty());
        const auto swop = srgb.parent_path() / "RSWOP.icm";
        if (std::filesystem::is_regular_file(swop)) {
            const auto print = IccPreview::create({}, swop, &error);
            REQUIRE(print && print->has_cmyk_transform());
            const auto source = ColorValue::make_cmyk(0, 1, 1, 0, .6);
            const auto approximate = to_preview_rgba(source);
            const auto actual = print->convert(source);
            const double difference = std::abs(actual.r - approximate.r) +
                                      std::abs(actual.g - approximate.g) + std::abs(actual.b - approximate.b);
            REQUIRE(difference > .02); // proves a real CMYK ICC transform is exercised
            REQUIRE(std::abs(actual.a - .6) < 1e-6);
            REQUIRE(source.color_space() == ColorSpace::Cmyk);
            const auto &channels = std::get<CMYKColor>(source.payload());
            REQUIRE(channels.c == 0 && channels.m == 1 && channels.y == 1 && channels.k == 0);
            const auto different_alpha = print->convert(ColorValue::make_cmyk(0, 1, 1, 0, .2));
            REQUIRE(std::abs(different_alpha.a - .2) < 1e-6);
            auto rgb_source = ColorValue::make_rgb(.17, .42, .83, .37);
            rgb_source.set_overprint({true, true});
            const auto cmyk = convert_color_to_mode(rgb_source, DocumentColorMode::Cmyk, print.get());
            const auto& process = std::get<CMYKColor>(cmyk.payload());
            REQUIRE(process.a == .37 && cmyk.overprint().fill && cmyk.overprint().stroke);
            const auto fallback = convert_color_to_mode(rgb_source, DocumentColorMode::Cmyk);
            REQUIRE(!process.approx_equal(std::get<CMYKColor>(fallback.payload()), .001));
            const auto restored_rgb = convert_color_to_mode(cmyk, DocumentColorMode::Rgb, print.get());
            REQUIRE(std::get<RGBColor>(restored_rgb.payload()).a == .37);
            const auto black = convert_color_to_mode(ColorValue::make_rgb(0, 0, 0), DocumentColorMode::Cmyk, print.get());
            REQUIRE((std::get<CMYKColor>(black.payload()).approx_equal({0, 0, 0, 1, 1}, 0)));
            std::cout
                << "[PASS] Installed SWOP CMYK -> sRGB ICC, independent channels, alpha cache; difference="
                << difference << '\n';
        } else {
            std::cout << "[NOT_MEASURED] No installed SWOP CMYK fixture\n";
        }
        std::cout << "[PASS] System sRGB ICC and invalid/wrong-channel profile rejection\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
#else
    std::cout << "[NOT_MEASURED] Windows ICC backend unavailable\n";
#endif
    return 0;
}
