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
            const auto black_pure = convert_color_to_mode(ColorValue::make_rgb(0, 0, 0),
                DocumentColorMode::Cmyk, print.get(), BlackGeneration::PureK100);
            REQUIRE((std::get<CMYKColor>(black_pure.payload()).approx_equal({0, 0, 0, 1, 1}, 0)));
            REQUIRE(black_pure.overprint().fill && black_pure.overprint().stroke);

            const auto black_rich = convert_color_to_mode(ColorValue::make_rgb(0, 0, 0),
                DocumentColorMode::Cmyk, print.get(), BlackGeneration::RichBlack);
            const auto& rich_cmyk = std::get<CMYKColor>(black_rich.payload());
            REQUIRE(rich_cmyk.c == 0.40 && rich_cmyk.m == 0.30 && rich_cmyk.y == 0.30 && rich_cmyk.k == 1.00);

            // Test Gamut Warning
            const auto neon_green = ColorValue::make_rgb(0.0, 1.0, 0.0);
            const auto neutral_gray = ColorValue::make_rgb(0.5, 0.5, 0.5);

            REQUIRE(print->is_out_of_gamut(neon_green));
            REQUIRE(!print->is_out_of_gamut(neutral_gray));

            PreviewOptions gamut_opts;
            gamut_opts.gamut_warning = true;
            const auto warned_neon = print->convert(neon_green, gamut_opts);
            REQUIRE(std::abs(warned_neon.r - gamut_opts.gamut_warning_color.r) < 1e-4);
            REQUIRE(std::abs(warned_neon.g - gamut_opts.gamut_warning_color.g) < 1e-4);
            REQUIRE(std::abs(warned_neon.b - gamut_opts.gamut_warning_color.b) < 1e-4);

            // Test Soft-Proofing
            PreviewOptions proof_opts;
            proof_opts.proof_colors = true;
            const auto direct_rgb = print->convert(neon_green);
            const auto proofed_rgb = print->convert(neon_green, proof_opts);
            // Soft-proofed neon green is desaturated compared to direct sRGB preview
            REQUIRE(proofed_rgb.g < direct_rgb.g);

            std::cout
                << "[PASS] Installed SWOP CMYK -> sRGB ICC, independent channels, alpha cache; difference="
                << difference << '\n';
        } else {
            std::cout << "[NOT_MEASURED] No installed SWOP CMYK fixture\n";
        }

        // Test TAC calculation and clamping
        const CMYKColor heavy_cmyk{1.0, 1.0, 1.0, 1.0};
        REQUIRE(calculate_tac(heavy_cmyk) == 400.0);
        const auto clamped_cmyk = clamp_tac(heavy_cmyk, 320.0);
        const double clamped_tac = calculate_tac(clamped_cmyk);
        REQUIRE(clamped_tac <= 320.0001);
        REQUIRE(clamped_cmyk.k == 1.0); // K plate preserved
        REQUIRE(std::abs(clamped_cmyk.c - 2.20 / 3.0) < 1e-4);

        // Test Overprint Simulation
        const PreviewRGBA yellow_bg{1.0f, 1.0f, 0.0f, 1.0f};
        const PreviewRGBA cyan_fg{0.0f, 1.0f, 1.0f, 1.0f};
        const auto green_result = simulate_overprint(yellow_bg, cyan_fg);
        REQUIRE(std::abs(green_result.r - 0.0f) < 1e-6);
        REQUIRE(std::abs(green_result.g - 1.0f) < 1e-6);
        REQUIRE(std::abs(green_result.b - 0.0f) < 1e-6);

        std::cout << "[PASS] System sRGB ICC, TAC control, and overprint simulation\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
#else
    std::cout << "[NOT_MEASURED] Windows ICC backend unavailable\n";
#endif
    return 0;
}
