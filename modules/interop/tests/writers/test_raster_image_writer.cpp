#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/interop/writers/raster_image_writer.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <objbase.h>
#include <wincodec.h>
#endif

using namespace pikdraw::interop;

namespace {

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class OwnedTestOutput final {
public:
    explicit OwnedTestOutput(std::filesystem::path path) : path_(std::move(path)) {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    ~OwnedTestOutput() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

#ifdef _WIN32
std::vector<uint8_t> read_cmyk16_wic(const std::filesystem::path& path,
                                    uint32_t width, uint32_t height) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = apartment == S_OK || apartment == S_FALSE;
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return {};
    struct ApartmentGuard {
        bool active;
        ~ApartmentGuard() { if (active) CoUninitialize(); }
    } guard{uninitialize};

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<uint8_t> pixels;
    const auto release = [&] {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
    };
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) break;
        if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder) break;
        if (FAILED(decoder->GetFrame(0, &frame)) || !frame) break;
        WICPixelFormatGUID source_format{};
        if (FAILED(frame->GetPixelFormat(&source_format)) ||
            IsEqualGUID(source_format, GUID_WICPixelFormat64bppCMYK) == FALSE) {
            if (FAILED(factory->CreateFormatConverter(&converter)) || !converter) break;
            BOOL can_convert = FALSE;
            if (FAILED(converter->CanConvert(source_format,
                    GUID_WICPixelFormat64bppCMYK, &can_convert)) || !can_convert ||
                FAILED(converter->Initialize(frame, GUID_WICPixelFormat64bppCMYK,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;
        }
        const uint64_t stride = static_cast<uint64_t>(width) * 8u;
        const uint64_t byte_count = stride * height;
        if (stride > UINT32_MAX || byte_count > UINT32_MAX || byte_count > SIZE_MAX) break;
        pixels.resize(static_cast<size_t>(byte_count));
        IWICBitmapSource* source = converter
            ? static_cast<IWICBitmapSource*>(converter)
            : static_cast<IWICBitmapSource*>(frame);
        if (FAILED(source->CopyPixels(nullptr, static_cast<UINT>(stride),
                static_cast<UINT>(byte_count), pixels.data())))
            pixels.clear();
    } while (false);
    release();
    return pixels;
}
#endif

void test_validation_rejects_mismatched_input_and_extension() {
    const std::vector<uint8_t> pixels(16, 0);
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    const auto wrong_size = WriteRasterImageFile(L"image.tif", 2, 2,
        std::span<const uint8_t>(pixels).first(15), options);
    assert(!wrong_size.success);

    const auto wrong_extension = WriteRasterImageFile(L"image.jpg", 2, 2, pixels, options);
    assert(!wrong_extension.success);

    const std::vector<uint8_t> oversized_profile(1024u * 1024u + 1u, 0);
    options.embedded_icc_profile = oversized_profile;
    const auto oversized_result = WriteRasterImageFile(L"image.tif", 2, 2, pixels, options);
    assert(!oversized_result.success);

    std::vector<uint8_t> cmyk_profile(128, 0);
    cmyk_profile[16] = 'C';
    cmyk_profile[17] = 'M';
    cmyk_profile[18] = 'Y';
    cmyk_profile[19] = 'K';
    options.embedded_icc_profile = cmyk_profile;
    const auto non_rgb_profile = WriteRasterImageFile(L"image.tif", 2, 2, pixels, options);
    assert(!non_rgb_profile.success);

    options.embedded_icc_profile = {};
    options.convert_pixels_to_destination_profile = true;
    const auto missing_destination = WriteRasterImageFile(L"image.tif", 2, 2, pixels, options);
    assert(!missing_destination.success);

    std::vector<uint8_t> destination_profile(128, 0);
    destination_profile[16] = 'R';
    destination_profile[17] = 'G';
    destination_profile[18] = 'B';
    destination_profile[19] = ' ';
    auto mismatched_embedded_profile = destination_profile;
    mismatched_embedded_profile[20] = 1;
    options.destination_icc_profile = destination_profile;
    options.embedded_icc_profile = mismatched_embedded_profile;
    const auto mismatched_profiles = WriteRasterImageFile(
        L"image.tif", 2, 2, pixels, options);
    assert(!mismatched_profiles.success);
    assert(mismatched_profiles.diagnostic.find("exactly match") != std::string::npos);
}

#ifdef _WIN32
void test_tiff_is_lossless_alpha_aware_and_stores_dpi() {
    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-test.tiff";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const std::vector<uint8_t> pixels{
        255, 0, 0, 128,  0, 255, 0, 0,
        0, 0, 255, 255,  20, 40, 60, 200};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    options.dpi_x = 600.0;
    options.dpi_y = 300.0;
    const auto written = WriteRasterImageFile(output.wstring(), 2, 2, pixels, options);
    assert(written.success);
    const auto bytes = read_file(output);
    assert(!bytes.empty());
    const auto decoded = RasterImageDecoder::decode(bytes);
    assert(decoded.success);
    assert(decoded.image.width_px == 2 && decoded.image.height_px == 2);
    assert(decoded.image.pixels_rgba == pixels);
    assert(std::abs(decoded.image.dpi_x - 600.0) < 0.1);
    assert(std::abs(decoded.image.dpi_y - 300.0) < 0.1);
    std::filesystem::remove(output, ignored);
}

void test_tiff16_preserves_sub_8_bit_channel_values() {
    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-16bit-test.tiff";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const std::vector<uint8_t> rgba16le{
        0x34, 0x12, 0x35, 0x12, 0xff, 0x00, 0x00, 0x80,
        0x01, 0xff, 0x02, 0xff, 0x03, 0xff, 0xff, 0xff};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    options.dpi_x = 600.0;
    options.dpi_y = 300.0;
    const auto written = WriteRasterImageFile16(
        output.wstring(), 2, 1, rgba16le, options);
    if (!written.success) std::cerr << "RGBA16LE TIFF write failed: "
                                    << written.diagnostic << '\n';
    assert(written.success);
    const auto bytes = read_file(output);
    assert(!bytes.empty());
    RasterImageDecodeOptions decode_options;
    decode_options.preserve_16bit = true;
    const auto decoded = RasterImageDecoder::decode(bytes, decode_options);
    if (!decoded.success) std::cerr << "RGBA16LE TIFF decode failed: "
                                    << decoded.diagnostic_code << " - "
                                    << decoded.message << '\n';
    assert(decoded.success && decoded.image.decoded_bits_per_channel == 16);
    assert(decoded.image.pixels_rgba16le == rgba16le);
    assert(decoded.image.pixels_rgba[0] == 0x12 &&
           decoded.image.pixels_rgba[4] == 0xff);
    assert(std::abs(decoded.image.dpi_x - 600.0) < 0.1);
    assert(std::abs(decoded.image.dpi_y - 300.0) < 0.1);
    std::filesystem::remove(output, ignored);
}

void test_cmyk16_tiff_preserves_samples_profile_and_dpi() {
#ifdef _WIN32
    const auto srgb_path = pikdraw::color::IccPreview::system_srgb_profile();
    if (srgb_path.empty()) return;
    const auto profile_path = srgb_path.parent_path() / L"RSWOP.icm";
    if (!std::filesystem::is_regular_file(profile_path)) return;
    const auto profile = pikdraw::color::ProfileResource::load_file(
        profile_path, pikdraw::color::ColorSpace::Cmyk);
    assert(profile);

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    OwnedTestOutput output(std::filesystem::temp_directory_path() /
        (L"pikdraw-cmyk16-writer-" + std::to_wstring(stamp) + L".tiff"));
    const std::vector<uint8_t> samples{
        0x01,0x02, 0x03,0x04, 0x05,0x06, 0x07,0x08,
        0x11,0x12, 0x13,0x14, 0x15,0x16, 0x17,0x18};
    RasterCmykImageWriteOptions options;
    options.dpi_x = 600.0;
    options.dpi_y = 300.0;
    options.embedded_icc_profile = profile.resource->bytes();
    const auto written = WriteRasterImageCmykFile16(
        output.path().wstring(), 2, 1, samples, options);
    if (!written.success)
        std::cerr << "CMYK16 TIFF write failed: " << written.diagnostic << '\n';
    if (!written.success) throw std::runtime_error("CMYK16 TIFF writer rejected native samples");
    if (read_cmyk16_wic(output.path(), 2, 1) != samples)
        throw std::runtime_error("CMYK16 TIFF writer changed native channel samples");

    const auto decoded = RasterImageDecoder::decode(read_file(output.path()));
    if (!decoded.success || decoded.image.width_px != 2 || decoded.image.height_px != 1)
        throw std::runtime_error("CMYK16 TIFF did not preserve its dimensions");
    if (decoded.image.icc_profile_status != RasterIccProfileStatus::Preserved ||
        decoded.image.embedded_icc_profile != profile.resource->bytes())
        throw std::runtime_error("CMYK16 TIFF did not preserve the exact ICC profile");
    if (std::abs(decoded.image.dpi_x - 600.0) >= 0.1 ||
        std::abs(decoded.image.dpi_y - 300.0) >= 0.1)
        throw std::runtime_error("CMYK16 TIFF did not preserve source DPI");
#endif
}

void test_tiff_embeds_exact_declared_icc_profile() {
    const auto profile_path = pikdraw::color::IccPreview::system_srgb_profile();
    assert(!profile_path.empty());
    std::ifstream profile_input(profile_path, std::ios::binary);
    const std::vector<uint8_t> profile(
        (std::istreambuf_iterator<char>(profile_input)),
        std::istreambuf_iterator<char>{});
    assert(!profile.empty() && profile.size() <= 1024u * 1024u);

    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-profile-test.tiff";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const std::vector<uint8_t> pixels{128, 64, 32, 255};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    options.embedded_icc_profile = profile;
    const auto written = WriteRasterImageFile(output.wstring(), 1, 1, pixels, options);
    assert(written.success);
    const auto bytes = read_file(output);
    const auto decoded = RasterImageDecoder::decode(bytes);
    assert(decoded.success);
    assert(decoded.image.icc_profile_status == RasterIccProfileStatus::Preserved);
    assert(decoded.image.embedded_icc_profile == profile);
    assert(decoded.image.pixels_rgba.size() == pixels.size());
    for (size_t index = 0; index < pixels.size(); ++index) {
        if (index % 4 == 3) assert(decoded.image.pixels_rgba[index] == pixels[index]);
        else assert(std::abs(static_cast<int>(decoded.image.pixels_rgba[index]) -
            static_cast<int>(pixels[index])) <= 1);
    }
    std::filesystem::remove(output, ignored);

}

void test_tiff_converts_pixels_to_declared_rgb_profile() {
    const auto profile_path = pikdraw::color::IccPreview::system_srgb_profile();
    assert(!profile_path.empty());
    std::ifstream profile_input(profile_path, std::ios::binary);
    const std::vector<uint8_t> profile(
        (std::istreambuf_iterator<char>(profile_input)),
        std::istreambuf_iterator<char>{});
    assert(profile.size() >= 128 && profile[16] == 'R' && profile[17] == 'G' &&
        profile[18] == 'B' && profile[19] == ' ');

    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-convert-test.tiff";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const std::vector<uint8_t> pixels{
        128, 64, 32, 255, 20, 180, 240, 127};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    options.destination_icc_profile = profile;
    options.embedded_icc_profile = profile;
    options.convert_pixels_to_destination_profile = true;
    const auto written = WriteRasterImageFile(output.wstring(), 2, 1, pixels, options);
    assert(written.success);
    const auto decoded = RasterImageDecoder::decode(read_file(output));
    assert(decoded.success);
    assert(decoded.image.icc_profile_status == RasterIccProfileStatus::Preserved);
    assert(decoded.image.embedded_icc_profile == profile);
    assert(decoded.image.pixels_rgba.size() == pixels.size());
    for (size_t index = 0; index < pixels.size(); ++index) {
        if (index % 4 == 3) {
            assert(decoded.image.pixels_rgba[index] == pixels[index]);
        } else {
            assert(std::abs(static_cast<int>(decoded.image.pixels_rgba[index]) -
                static_cast<int>(pixels[index])) <= 1);
        }
    }
    std::filesystem::remove(output, ignored);

    const auto untagged_output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-convert-untagged.tiff";
    options.embedded_icc_profile = {};
    const auto untagged_written = WriteRasterImageFile(
        untagged_output.wstring(), 2, 1, pixels, options);
    assert(untagged_written.success);
    const auto untagged = RasterImageDecoder::decode(read_file(untagged_output));
    assert(untagged.success);
    assert(untagged.image.icc_profile_status == RasterIccProfileStatus::NotPresent);
    assert(untagged.image.pixels_rgba.size() == pixels.size());
    for (size_t index = 0; index < pixels.size(); ++index) {
        if (index % 4 == 3) assert(untagged.image.pixels_rgba[index] == pixels[index]);
        else assert(std::abs(static_cast<int>(untagged.image.pixels_rgba[index]) -
            static_cast<int>(pixels[index])) <= 1);
    }
    std::filesystem::remove(untagged_output, ignored);
}

void test_tiff_conversion_matches_non_srgb_profile_oracle() {
    using namespace pikdraw::color;
    const auto srgb_path = IccPreview::system_srgb_profile();
    assert(!srgb_path.empty());
    const auto source = ProfileResource::load_file(srgb_path, ColorSpace::Rgb);
    assert(source);

    const auto read_be32 = [](const std::vector<uint8_t>& bytes, size_t offset) {
        assert(offset + 4 <= bytes.size());
        return (static_cast<uint32_t>(bytes[offset]) << 24) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
            static_cast<uint32_t>(bytes[offset + 3]);
    };
    const auto write_be32 = [](std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
        assert(offset + 4 <= bytes.size());
        bytes[offset] = static_cast<uint8_t>(value >> 24);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
        bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 3] = static_cast<uint8_t>(value);
    };
    auto alternate_bytes = source.resource->bytes();
    assert(alternate_bytes.size() >= 100);
    std::fill(alternate_bytes.begin() + 84, alternate_bytes.begin() + 100, 0);
    const auto tag_count = read_be32(alternate_bytes, 128);
    std::optional<size_t> red_xyz;
    std::optional<size_t> green_xyz;
    for (uint32_t index = 0; index < tag_count; ++index) {
        const size_t record = 132u + static_cast<size_t>(index) * 12u;
        if (record + 12 > alternate_bytes.size()) break;
        const std::string signature(alternate_bytes.begin() + static_cast<ptrdiff_t>(record),
            alternate_bytes.begin() + static_cast<ptrdiff_t>(record + 4));
        const auto offset = read_be32(alternate_bytes, record + 4);
        const auto size = read_be32(alternate_bytes, record + 8);
        if (offset > alternate_bytes.size() || size > alternate_bytes.size() - offset ||
            size < 20 || std::string(alternate_bytes.begin() + offset,
                alternate_bytes.begin() + offset + 4) != "XYZ ") continue;
        if (signature == "rXYZ") red_xyz = offset;
        else if (signature == "gXYZ") green_xyz = offset;
    }
    assert(red_xyz && green_xyz);
    const auto perturb_pair = [&](size_t component_offset, int32_t delta) {
        const auto red_value = static_cast<int32_t>(read_be32(alternate_bytes,
            *red_xyz + component_offset));
        const auto green_value = static_cast<int32_t>(read_be32(alternate_bytes,
            *green_xyz + component_offset));
        write_be32(alternate_bytes, *red_xyz + component_offset,
            static_cast<uint32_t>(red_value + delta));
        write_be32(alternate_bytes, *green_xyz + component_offset,
            static_cast<uint32_t>(green_value - delta));
    };
    perturb_pair(8, 6554);  // Shift X primaries by 0.100 while retaining the white sum.
    perturb_pair(16, 3277); // Shift Z primaries by 0.050 while retaining the white sum.
    const auto destination_result = ProfileResource::create(
        "PikDraw alternate primaries test", ColorSpace::Rgb, std::move(alternate_bytes));
    assert(destination_result);
    const auto destination = destination_result.resource;
    const auto oracle_result = ProfileColorTransform::create(source.resource, destination);
    assert(oracle_result);
    const auto oracle = oracle_result.transform;

    const auto tagged_output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-non-srgb-tagged.tiff";
    const auto raw_output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-non-srgb-raw.tiff";
    std::error_code ignored;
    std::filesystem::remove(tagged_output, ignored);
    std::filesystem::remove(raw_output, ignored);

    const std::vector<uint8_t> pixels{
        128, 64, 32, 255,  20, 180, 240, 127,
        224, 32, 144, 64,  90, 130, 210, 200};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    options.source_icc_profile = source.resource->bytes();
    options.destination_icc_profile = destination->bytes();
    options.embedded_icc_profile = destination->bytes();
    options.convert_pixels_to_destination_profile = true;
    const auto tagged_write = WriteRasterImageFile(tagged_output.wstring(), 4, 1,
        pixels, options);
    assert(tagged_write.success);
    const auto tagged_bytes = read_file(tagged_output);
    const auto tagged_decode = RasterImageDecoder::decode(tagged_bytes);
    assert(tagged_decode.success);
    assert(tagged_decode.image.embedded_icc_profile == destination->bytes());
    assert(tagged_decode.image.pixel_color_basis == RasterPixelColorBasis::ConvertedToSRgb);
    assert(tagged_decode.image.pixels_rgba.size() == pixels.size());
    int max_tagged_srgb_roundtrip_error = 0;
    for (size_t index = 0; index < pixels.size(); ++index) {
        if (index % 4 == 3) {
            assert(tagged_decode.image.pixels_rgba[index] == pixels[index]);
        } else {
            max_tagged_srgb_roundtrip_error = (std::max)(max_tagged_srgb_roundtrip_error,
                std::abs(static_cast<int>(tagged_decode.image.pixels_rgba[index]) -
                    static_cast<int>(pixels[index])));
        }
    }
    assert(max_tagged_srgb_roundtrip_error <= 8);

    options.embedded_icc_profile = {};
    const auto raw_write = WriteRasterImageFile(raw_output.wstring(), 4, 1, pixels, options);
    assert(raw_write.success);
    const auto raw_decode = RasterImageDecoder::decode(read_file(raw_output));
    assert(raw_decode.success);
    assert(raw_decode.image.icc_profile_status == RasterIccProfileStatus::NotPresent);
    assert(raw_decode.image.pixel_color_basis == RasterPixelColorBasis::AssumedSRgb);
    bool observed_conversion = false;
    int maximum_oracle_error = 0;
    for (size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
        ColorValue oracle_input{RGBColor{
            pixels[pixel * 4] / 255.0, pixels[pixel * 4 + 1] / 255.0,
            pixels[pixel * 4 + 2] / 255.0, pixels[pixel * 4 + 3] / 255.0}};
        assert(oracle_input.set_profile_resource(source.resource));
        ColorValue expected;
        assert(oracle->convert(oracle_input, expected));
        const auto* expected_rgb = std::get_if<RGBColor>(&expected.payload());
        assert(expected_rgb);
        const double expected_channels[] = {
            expected_rgb->r, expected_rgb->g, expected_rgb->b};
        for (size_t channel = 0; channel < 3; ++channel) {
            const auto expected_byte = static_cast<int>(std::lround(
                std::clamp(expected_channels[channel], 0.0, 1.0) * 255.0));
            const auto actual_byte = static_cast<int>(
                raw_decode.image.pixels_rgba[pixel * 4 + channel]);
            maximum_oracle_error = (std::max)(maximum_oracle_error,
                std::abs(actual_byte - expected_byte));
            observed_conversion = observed_conversion ||
                std::abs(actual_byte - static_cast<int>(pixels[pixel * 4 + channel])) >= 5;
        }
        assert(raw_decode.image.pixels_rgba[pixel * 4 + 3] == pixels[pixel * 4 + 3]);
    }
    assert(observed_conversion);
    assert(maximum_oracle_error <= 1);

    std::filesystem::remove(tagged_output, ignored);
    std::filesystem::remove(raw_output, ignored);
}

void test_jpeg_flattens_alpha_and_preserves_dpi() {
    const auto profile_path = pikdraw::color::IccPreview::system_srgb_profile();
    assert(!profile_path.empty());
    std::ifstream profile_input(profile_path, std::ios::binary);
    const std::vector<uint8_t> profile(
        (std::istreambuf_iterator<char>(profile_input)),
        std::istreambuf_iterator<char>{});
    assert(profile.size() >= 128);
    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-test.jpg";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const std::vector<uint8_t> pixels{
        255, 0, 0, 0,  0, 255, 0, 255,
        0, 0, 255, 255,  255, 255, 255, 128};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Jpeg;
    options.dpi_x = 240.0;
    options.dpi_y = 240.0;
    options.jpeg_quality = 1.0f;
    options.destination_icc_profile = profile;
    options.embedded_icc_profile = profile;
    options.convert_pixels_to_destination_profile = true;
    const auto written = WriteRasterImageFile(output.wstring(), 2, 2, pixels, options);
    assert(written.success);
    const auto bytes = read_file(output);
    assert(!bytes.empty());
    const auto decoded = RasterImageDecoder::decode(bytes);
    assert(decoded.success);
    assert(decoded.image.icc_profile_status == RasterIccProfileStatus::Preserved);
    assert(decoded.image.embedded_icc_profile == profile);
    assert(decoded.image.width_px == 2 && decoded.image.height_px == 2);
    for (size_t pixel = 0; pixel < 4; ++pixel)
        assert(decoded.image.pixels_rgba[pixel * 4 + 3] == 255);
    // Transparent red is flattened to the selected white background.
    for (size_t channel = 0; channel < 3; ++channel)
        assert(decoded.image.pixels_rgba[channel] > 245);
    assert(std::abs(decoded.image.dpi_x - 240.0) < 0.1);
    assert(std::abs(decoded.image.dpi_y - 240.0) < 0.1);
    std::filesystem::remove(output, ignored);
}

void test_existing_output_requires_explicit_replace() {
    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-existing.tiff";
    {
        std::ofstream marker(output, std::ios::binary | std::ios::trunc);
        marker << "keep";
    }
    const std::vector<uint8_t> pixels{0, 0, 0, 255};
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    const auto rejected = WriteRasterImageFile(output.wstring(), 1, 1, pixels, options);
    assert(!rejected.success);
    const auto marker = read_file(output);
    assert(std::string(marker.begin(), marker.end()) == "keep");
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
}

void test_raster_writer_uses_shared_memory_waiter_and_surfaces_timeout() {
    const std::vector<uint8_t> pixels(65u * 4u, 0xFF);
    const auto output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-memory-wait.tiff";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    RasterImageWriteOptions options;
    options.format = RasterImageOutputFormat::Tiff;
    size_t ready_calls = 0;
    uint64_t required_bytes = 0;
    options.wait_for_memory_headroom = [&](uint64_t required) {
        ++ready_calls;
        required_bytes = required;
        return true;
    };
    const auto written = WriteRasterImageFile(output.wstring(), 1, 65, pixels, options);
    assert(written.success && ready_calls == 2 && required_bytes > 0);
    std::filesystem::remove(output, ignored);

    const auto timed_out_output = std::filesystem::temp_directory_path() /
        L"pikdraw-raster-writer-memory-timeout.tiff";
    size_t timeout_calls = 0;
    options.wait_for_memory_headroom = [&](uint64_t required) {
        ++timeout_calls;
        assert(required > 0);
        return false;
    };
    const auto timed_out = WriteRasterImageFile(
        timed_out_output.wstring(), 1, 65, pixels, options);
    assert(!timed_out.success && timeout_calls == 1);
    assert(timed_out.diagnostic.find("timed out") != std::string::npos);
    std::filesystem::remove(timed_out_output, ignored);
}
#endif

} // namespace

int main() {
    test_validation_rejects_mismatched_input_and_extension();
#ifdef _WIN32
    test_tiff_is_lossless_alpha_aware_and_stores_dpi();
    test_tiff16_preserves_sub_8_bit_channel_values();
        test_cmyk16_tiff_preserves_samples_profile_and_dpi();
    test_tiff_embeds_exact_declared_icc_profile();
    test_tiff_converts_pixels_to_declared_rgb_profile();
    test_tiff_conversion_matches_non_srgb_profile_oracle();
    test_jpeg_flattens_alpha_and_preserves_dpi();
    test_existing_output_requires_explicit_replace();
    test_raster_writer_uses_shared_memory_waiter_and_surfaces_timeout();
#endif
}
