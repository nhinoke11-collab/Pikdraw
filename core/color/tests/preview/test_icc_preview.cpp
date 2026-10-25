#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#define REQUIRE(value)                                                                                       \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error("Failed: " #value);                                                     \
    } while (false)

namespace {

uint16_t read_u16le(const std::vector<uint8_t>& bytes, size_t offset) {
    REQUIRE(offset + 1 < bytes.size());
    return static_cast<uint16_t>(bytes[offset] |
        (static_cast<uint16_t>(bytes[offset + 1]) << 8));
}

uint16_t to_u16(double value) {
    return static_cast<uint16_t>(std::lround(
        std::clamp(value, 0.0, 1.0) * 65535.0));
}

} // namespace

int main() {
    using namespace pikdraw::color;
#ifdef _WIN32
    try {
        std::string error;
        const auto srgb = IccPreview::system_srgb_profile();
        REQUIRE(!srgb.empty() && std::filesystem::is_regular_file(srgb));
        const auto srgb_resource = ProfileResource::load_file(srgb, ColorSpace::Rgb);
        REQUIRE(srgb_resource && srgb_resource.resource->identity().checksum_sha256.size() == 64);
        const auto same_profile = ProfileColorTransform::create(
            srgb_resource.resource, srgb_resource.resource);
        REQUIRE(!same_profile && same_profile.diagnostic.find("no conversion") != std::string::npos);
        const auto display_bound_preview = IccPreview::create_from_resources(
            srgb_resource.resource, {}, srgb_resource.resource, &error);
        REQUIRE(display_bound_preview && error.empty());
        const auto display_bound_sample = display_bound_preview->convert(
            ColorValue::make_rgb(.18, .43, .76, .29));
        REQUIRE(std::isfinite(display_bound_sample.r) &&
                std::isfinite(display_bound_sample.g) &&
                std::isfinite(display_bound_sample.b) &&
                std::abs(display_bound_sample.a - .29f) < 1e-5f);

        std::shared_ptr<const ProfileResource> alternate_resource;
        ProfileColorTransformResult transform;
        const IccTransformOptions options{IccRenderingIntent::Perceptual, false};
        std::error_code list_error;
        const auto preferred_cmyk = srgb.parent_path() / L"RSWOP.icm";
        if (std::filesystem::is_regular_file(preferred_cmyk)) {
            const auto candidate = ProfileResource::load_file(preferred_cmyk, ColorSpace::Cmyk);
            REQUIRE(candidate);
            auto attempt = ProfileColorTransform::create(
                srgb_resource.resource, candidate.resource, options);
            REQUIRE(attempt);
            alternate_resource = candidate.resource;
            transform = std::move(attempt);
        }
        for (const auto& entry : std::filesystem::directory_iterator(srgb.parent_path(), list_error)) {
            if (transform || list_error || !entry.is_regular_file()) continue;
            const auto extension = entry.path().extension().wstring();
            if (extension != L".icc" && extension != L".ICC" &&
                extension != L".icm" && extension != L".ICM") continue;
            for (const auto space : {ColorSpace::Rgb, ColorSpace::Cmyk}) {
                const auto candidate = ProfileResource::load_file(entry.path(), space);
                if (!candidate || candidate.resource->identity() == srgb_resource.resource->identity()) continue;
                auto attempt = ProfileColorTransform::create(
                    srgb_resource.resource, candidate.resource, options);
                if (attempt) {
                    alternate_resource = candidate.resource;
                    transform = std::move(attempt);
                    break;
                }
            }
        }
        if (alternate_resource) {
            REQUIRE(transform && transform.transform->options().intent == IccRenderingIntent::Perceptual);
            auto transform_input = ColorValue::make_rgb(.21, .44, .82, .37);
            transform_input.set_overprint({true, false});
            ColorValue transform_output;
            REQUIRE(transform.transform->convert(transform_input, transform_output));
            REQUIRE(transform_output.is_valid());
            REQUIRE(!transform_output.profile_resource());
            REQUIRE(transform_output.overprint().fill && !transform_output.overprint().stroke);
            if (const auto* rgb_output = std::get_if<RGBColor>(&transform_output.payload()))
                REQUIRE(std::abs(rgb_output->a - .37) < 1e-6);
            else if (const auto* cmyk_output = std::get_if<CMYKColor>(&transform_output.payload()))
                REQUIRE(std::abs(cmyk_output->a - .37) < 1e-6);
            else REQUIRE(false);
            auto bound_transform_input = transform_input;
            REQUIRE(bound_transform_input.set_profile_resource(srgb_resource.resource));
            ColorValue bound_transform_output;
            REQUIRE(transform.transform->convert(bound_transform_input, bound_transform_output));
            REQUIRE(bound_transform_output.profile_resource() == alternate_resource);
            if (alternate_resource->identity().color_space == ColorSpace::Cmyk) {
                const auto gamut_a = transform.transform->is_in_gamut(bound_transform_input);
                const auto gamut_b = transform.transform->is_in_gamut(bound_transform_input);
                REQUIRE(gamut_a.has_value() == gamut_b.has_value());
                if (gamut_a) REQUIRE(*gamut_a == *gamut_b);
                const auto proof_preview = IccPreview::create_from_resources(
                    srgb_resource.resource, alternate_resource, &error);
                REQUIRE(proof_preview && error.empty());
                if (proof_preview->supports_gamut_check())
                    REQUIRE(proof_preview->is_out_of_gamut(bound_transform_input).has_value());
            }

            const std::vector<uint8_t> rgb16_input{
                0x21, 0x10, 0x43, 0x32, 0x65, 0x54, 0x87, 0x76};
            const size_t destination_channels = alternate_resource->identity().color_space ==
                ColorSpace::Rgb ? 3u : 4u;
            std::vector<uint8_t> process16_output((destination_channels + 1) * 2);
            REQUIRE(transform.transform->convert_process16le(
                rgb16_input, process16_output, true, true));
            REQUIRE(read_u16le(process16_output, destination_channels * 2) == 0x7687);
            auto scalar16_input = ColorValue::make_rgb(
                0x1021 / 65535.0, 0x3243 / 65535.0, 0x5465 / 65535.0,
                0x7687 / 65535.0);
            ColorValue scalar16_output;
            REQUIRE(transform.transform->convert(scalar16_input, scalar16_output));
            if (const auto* rgb = std::get_if<RGBColor>(&scalar16_output.payload())) {
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 0)) -
                                 static_cast<int>(to_u16(rgb->r))) <= 1);
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 2)) -
                                 static_cast<int>(to_u16(rgb->g))) <= 1);
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 4)) -
                                 static_cast<int>(to_u16(rgb->b))) <= 1);
            } else if (const auto* cmyk = std::get_if<CMYKColor>(
                           &scalar16_output.payload())) {
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 0)) -
                                 static_cast<int>(to_u16(cmyk->c))) <= 1);
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 2)) -
                                 static_cast<int>(to_u16(cmyk->m))) <= 1);
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 4)) -
                                 static_cast<int>(to_u16(cmyk->y))) <= 1);
                REQUIRE(std::abs(static_cast<int>(read_u16le(process16_output, 6)) -
                                 static_cast<int>(to_u16(cmyk->k))) <= 1);
            } else {
                REQUIRE(false);
            }
            std::vector<uint8_t> invalid_output(process16_output.size() - 1);
            REQUIRE(!transform.transform->convert_process16le(
                rgb16_input, invalid_output, true, true));
            std::vector<uint8_t> alpha_dropped(destination_channels * 2);
            REQUIRE(!transform.transform->convert_process16le(
                rgb16_input, alpha_dropped, true, false));
            std::vector<uint8_t> overlapping(16);
            REQUIRE(!transform.transform->convert_process16le(
                std::span<const uint8_t>(overlapping.data(), 8),
                std::span<uint8_t>(overlapping.data() + 2, 8), true, true));

            constexpr size_t kBulkPixels = 8193;
            std::vector<uint8_t> bulk_rgb(kBulkPixels * rgb16_input.size());
            for (size_t pixel = 0; pixel < kBulkPixels; ++pixel)
                std::copy(rgb16_input.begin(), rgb16_input.end(),
                          bulk_rgb.begin() + static_cast<std::ptrdiff_t>(pixel * 8));
            std::vector<uint8_t> bulk_output(
                kBulkPixels * (destination_channels + 1) * 2);
            IccTransformExecutionOptions execution;
            size_t cancellation_checks = 0;
            size_t progress_reports = 0;
            execution.cancelled = [&] { return ++cancellation_checks == 2; };
            execution.progress = [&](uint64_t completed, uint64_t total) {
                REQUIRE(completed <= total);
                ++progress_reports;
            };
            REQUIRE(!transform.transform->convert_process16le(
                bulk_rgb, bulk_output, true, true, execution));
            REQUIRE(cancellation_checks == 2 && progress_reports == 1);

            const auto bpc_request = ProfileColorTransform::create(
                srgb_resource.resource, alternate_resource,
                IccTransformOptions{IccRenderingIntent::RelativeColorimetric, true});
            REQUIRE(!bpc_request && bpc_request.diagnostic.find("black point compensation") != std::string::npos);
        } else {
            std::cout << "[NOT_MEASURED] No installed ICC profile pair accepted by the Windows CMM\n";
        }
        const auto resource_preview = IccPreview::create_from_resources(srgb_resource.resource, {}, &error);
        REQUIRE(resource_preview && error.empty());
        REQUIRE(resource_preview->convert(ColorValue::make_rgb(.2, .4, .8)).b > .79);
        PreviewOptions unavailable_proof;
        unavailable_proof.proof_colors = true;
        const auto proof_failures_before = resource_preview->conversion_failure_count();
        (void)resource_preview->convert(ColorValue::make_rgb(.2, .4, .8), unavailable_proof);
        REQUIRE(resource_preview->conversion_failure_count() > proof_failures_before);
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
            const auto swop_resource = ProfileResource::load_file(swop, ColorSpace::Cmyk);
            REQUIRE(swop_resource && swop_resource.resource->identity().checksum_sha256.size() == 64);
            const auto print = IccPreview::create_from_resources(
                srgb_resource.resource, swop_resource.resource, &error);
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
            const auto managed_report = convert_color_to_mode_report(
                rgb_source, DocumentColorMode::Cmyk, print.get());
            REQUIRE(managed_report.changed);
            REQUIRE(managed_report.quality == ColorConversionQuality::IccManaged);
            REQUIRE(!managed_report.black_policy_applied);
            const auto& process = std::get<CMYKColor>(cmyk.payload());
            REQUIRE(process.a == .37 && cmyk.overprint().fill && cmyk.overprint().stroke);
            auto explicitly_bound_rgb = rgb_source;
            REQUIRE(explicitly_bound_rgb.set_profile_resource(srgb_resource.resource));
            const auto bound_cmyk = convert_color_to_mode(
                explicitly_bound_rgb, DocumentColorMode::Cmyk, print.get());
            REQUIRE(bound_cmyk.profile_resource() == swop_resource.resource);
            const auto page_preview_result = resource_preview->convert(bound_cmyk);
            const auto matching_profile_result = print->convert(bound_cmyk);
            REQUIRE(std::abs(page_preview_result.r - matching_profile_result.r) < .002);
            REQUIRE(std::abs(page_preview_result.g - matching_profile_result.g) < .002);
            REQUIRE(std::abs(page_preview_result.b - matching_profile_result.b) < .002);
            REQUIRE(std::abs(page_preview_result.a - matching_profile_result.a) < 1e-6);
            REQUIRE(resource_preview->conversion_failure_count() == 0);
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

            // The legacy menu item warns on a large RGB round-trip difference.
            const auto neon_green = ColorValue::make_rgb(0.0, 1.0, 0.0);
            const auto neutral_gray = ColorValue::make_rgb(0.5, 0.5, 0.5);

            REQUIRE(print->has_large_rgb_roundtrip_difference(neon_green));
            REQUIRE(!print->has_large_rgb_roundtrip_difference(neutral_gray));

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
            constexpr IccRenderingIntent proof_intents[]{
                IccRenderingIntent::Perceptual,
                IccRenderingIntent::RelativeColorimetric,
                IccRenderingIntent::Saturation,
                IccRenderingIntent::AbsoluteColorimetric};
            for (const auto intent : proof_intents) {
                proof_opts.proof_intent = intent;
                const bool supported = print->supports_proofing(intent);
                if (intent == IccRenderingIntent::RelativeColorimetric)
                    REQUIRE(supported);
                const auto failures_before = print->conversion_failure_count();
                const auto proofed = print->convert(neon_green, proof_opts);
                if (supported) {
                    REQUIRE(std::isfinite(proofed.r) && std::isfinite(proofed.g) &&
                            std::isfinite(proofed.b) &&
                            std::abs(proofed.a - direct_rgb.a) < 1e-5f);
                } else {
                    REQUIRE(print->conversion_failure_count() > failures_before);
                }
                const auto proofed_cmyk = print->convert(source, proof_opts);
                if (supported)
                    REQUIRE(std::isfinite(proofed_cmyk.r) &&
                            std::isfinite(proofed_cmyk.g) &&
                            std::isfinite(proofed_cmyk.b) &&
                            std::abs(proofed_cmyk.a - .6f) < 1e-5f);
            }
            print->set_preview_options(proof_opts);
            REQUIRE(print->preview_options().proof_intent ==
                    IccRenderingIntent::AbsoluteColorimetric);

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
