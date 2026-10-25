#pragma once

#include "pikdraw/color/profiles/profile_id.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::color {

enum class ProfileResourceError {
    None,
    EmptyName,
    UnsupportedColorSpace,
    TooLarge,
    ReadFailed,
    Truncated,
    InvalidHeader,
    ColorSpaceMismatch,
    InvalidTagTable,
    HashUnavailable
};

class ProfileResource;

struct ProfileResourceResult {
    std::shared_ptr<const ProfileResource> resource;
    ProfileResourceError error{ProfileResourceError::None};

    [[nodiscard]] explicit operator bool() const noexcept {
        return resource != nullptr && error == ProfileResourceError::None;
    }
};

/// Immutable, byte-backed ICC resource. Document data can refer to its digest;
/// it never supplies a filesystem path for the application to open.
class ProfileResource final {
public:
    static constexpr std::size_t kMaxProfileBytes = 16 * 1024 * 1024;
    static constexpr std::size_t kMaxTagCount = 4096;

    [[nodiscard]] static ProfileResourceResult create(
        std::string name, ColorSpace expected_space, std::vector<std::uint8_t> bytes);
    [[nodiscard]] static ProfileResourceResult load_file(
        const std::filesystem::path& path, ColorSpace expected_space);

    [[nodiscard]] const ProfileIdentifier& identity() const noexcept { return identity_; }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    ProfileResource(ProfileIdentifier identity, std::vector<std::uint8_t> bytes)
        : identity_(std::move(identity)), bytes_(std::move(bytes)) {}

    ProfileIdentifier identity_;
    std::vector<std::uint8_t> bytes_;
};

} // namespace pikdraw::color
