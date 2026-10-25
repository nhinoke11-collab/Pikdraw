#include "pikdraw/color/profiles/profile_resource.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace pikdraw::color {
namespace {

std::uint32_t read_u32_be(const std::vector<std::uint8_t>& bytes, std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

bool has_signature(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                   const std::array<char, 4>& signature) noexcept {
    return offset <= bytes.size() && bytes.size() - offset >= signature.size() &&
        bytes[offset] == static_cast<std::uint8_t>(signature[0]) &&
        bytes[offset + 1] == static_cast<std::uint8_t>(signature[1]) &&
        bytes[offset + 2] == static_cast<std::uint8_t>(signature[2]) &&
        bytes[offset + 3] == static_cast<std::uint8_t>(signature[3]);
}

bool valid_name(const std::string& name) noexcept {
    if (name.empty() || name.size() > 256) return false;
    for (const unsigned char ch : name)
        if (ch == 0 || ch < 0x20) return false;
    return true;
}

bool profile_space_matches(const std::vector<std::uint8_t>& bytes,
                           ColorSpace expected_space) noexcept {
    switch (expected_space) {
    case ColorSpace::Rgb: return has_signature(bytes, 16, {'R', 'G', 'B', ' '});
    case ColorSpace::Cmyk: return has_signature(bytes, 16, {'C', 'M', 'Y', 'K'});
    case ColorSpace::Grayscale: return has_signature(bytes, 16, {'G', 'R', 'A', 'Y'});
    case ColorSpace::Spot: return false;
    }
    return false;
}

bool valid_tag_table(const std::vector<std::uint8_t>& bytes) noexcept {
    if (bytes.size() < 132) return false;
    const auto tag_count = static_cast<std::size_t>(read_u32_be(bytes, 128));
    if (tag_count == 0 || tag_count > ProfileResource::kMaxTagCount) return false;
    constexpr std::size_t kTagRecordBytes = 12;
    if (tag_count > (bytes.size() - 132) / kTagRecordBytes) return false;
    const std::size_t data_floor = 132 + tag_count * kTagRecordBytes;
    for (std::size_t i = 0; i < tag_count; ++i) {
        const std::size_t record = 132 + i * kTagRecordBytes;
        const auto offset = static_cast<std::size_t>(read_u32_be(bytes, record + 4));
        const auto size = static_cast<std::size_t>(read_u32_be(bytes, record + 8));
        if (size < 4 || offset < data_floor || offset > bytes.size() ||
            size > bytes.size() - offset || (offset & 3u) != 0)
            return false;
    }
    return true;
}

bool calculate_sha256(const std::vector<std::uint8_t>& bytes, std::string& hex) {
#ifdef _WIN32
    struct AlgorithmOwner {
        BCRYPT_ALG_HANDLE value{};
        ~AlgorithmOwner() {
            if (value) BCryptCloseAlgorithmProvider(value, 0);
        }
    } algorithm;
    struct HashOwner {
        BCRYPT_HASH_HANDLE value{};
        ~HashOwner() {
            if (value) BCryptDestroyHash(value);
        }
    } hash;
    ULONG object_bytes = 0;
    ULONG result_bytes = 0;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return false;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &result_bytes, 0)) ||
        object_bytes == 0)
        return false;
    std::vector<UCHAR> object(object_bytes);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm.value, &hash.value, object.data(), object_bytes, nullptr, 0, 0)))
        return false;
    if (!BCRYPT_SUCCESS(BCryptHashData(
            hash.value, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0)))
        return false;
    std::array<UCHAR, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.value, digest.data(),
            static_cast<ULONG>(digest.size()), 0)))
        return false;
    static constexpr char kHex[] = "0123456789abcdef";
    hex.clear();
    hex.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0f]);
    }
    return true;
#else
    (void)bytes;
    (void)hex;
    return false;
#endif
}

} // namespace

ProfileResourceResult ProfileResource::create(
    std::string name, ColorSpace expected_space, std::vector<std::uint8_t> bytes) {
    if (!valid_name(name)) return {{}, ProfileResourceError::EmptyName};
    if (expected_space != ColorSpace::Rgb && expected_space != ColorSpace::Cmyk &&
        expected_space != ColorSpace::Grayscale)
        return {{}, ProfileResourceError::UnsupportedColorSpace};
    if (bytes.size() > kMaxProfileBytes) return {{}, ProfileResourceError::TooLarge};
    if (bytes.size() < 132) return {{}, ProfileResourceError::Truncated};
    if (read_u32_be(bytes, 0) != bytes.size() ||
        !has_signature(bytes, 36, {'a', 'c', 's', 'p'}))
        return {{}, ProfileResourceError::InvalidHeader};
    if (!profile_space_matches(bytes, expected_space))
        return {{}, ProfileResourceError::ColorSpaceMismatch};
    if (!valid_tag_table(bytes)) return {{}, ProfileResourceError::InvalidTagTable};

    std::string checksum;
    if (!calculate_sha256(bytes, checksum))
        return {{}, ProfileResourceError::HashUnavailable};
    ProfileIdentifier identity{std::move(name), expected_space, std::move(checksum)};
    auto resource = std::shared_ptr<const ProfileResource>(
        new ProfileResource(std::move(identity), std::move(bytes)));
    return {std::move(resource), ProfileResourceError::None};
}

ProfileResourceResult ProfileResource::load_file(
    const std::filesystem::path& path, ColorSpace expected_space) {
    if (path.empty()) return {{}, ProfileResourceError::ReadFailed};
    std::error_code file_error;
    const auto byte_count = std::filesystem::file_size(path, file_error);
    if (file_error) return {{}, ProfileResourceError::ReadFailed};
    if (byte_count > kMaxProfileBytes) return {{}, ProfileResourceError::TooLarge};
    if (byte_count < 132) return {{}, ProfileResourceError::Truncated};

    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, ProfileResourceError::ReadFailed};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_count));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof())
        return {{}, ProfileResourceError::ReadFailed};

    const auto utf8_name = path.stem().u8string();
    std::string name;
    name.reserve(utf8_name.size());
    for (const auto ch : utf8_name) name.push_back(static_cast<char>(ch));
    if (name.empty()) name = "ICC profile";
    return create(std::move(name), expected_space, std::move(bytes));
}

} // namespace pikdraw::color
