#include "test_harness.hpp"
#include "pikdraw/color/preview/preview_cache.hpp"
#include "pikdraw/color/profiles/profile_registry.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace pikdraw::color;

namespace {

void write_u32_be(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> synthetic_rgb_icc() {
    std::vector<std::uint8_t> bytes(164, 0);
    write_u32_be(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
    bytes[8] = 2;
    const auto copy_signature = [&bytes](std::size_t offset, const char* text) {
        std::copy_n(text, 4, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    };
    copy_signature(12, "mntr");
    copy_signature(16, "RGB ");
    copy_signature(20, "XYZ ");
    copy_signature(36, "acsp");
    write_u32_be(bytes, 128, 1);
    copy_signature(132, "wtpt");
    write_u32_be(bytes, 136, 144);
    write_u32_be(bytes, 140, 20);
    copy_signature(144, "XYZ ");
    return bytes;
}

} // namespace

PIKDRAW_COLOR_TEST(default_profiles_and_registration) {
    auto& reg = ProfileRegistry::instance();
    PIKDRAW_ASSERT(reg.has_profile("sRGB"));
    PIKDRAW_ASSERT(reg.has_profile("ISO Coated v2 300% (ECI)"));
    PIKDRAW_ASSERT(reg.has_profile("Display P3"));
    PIKDRAW_ASSERT(reg.default_rgb_profile().is_standard());

    const auto old_gen = reg.generation();
    reg.register_profile(ProfileIdentifier{"Custom Profile", ColorSpace::Rgb, "abc123hash"});
    PIKDRAW_ASSERT(reg.has_profile("Custom Profile"));
    PIKDRAW_ASSERT(reg.generation() > old_gen);

    const auto found = reg.find_profile("Custom Profile");
    PIKDRAW_ASSERT(found.has_value());
    PIKDRAW_ASSERT(found->checksum_sha256 == "abc123hash");

    const ProfileIdentifier same_name_other_bytes{"Custom Profile", ColorSpace::Rgb, "otherhash"};
    PIKDRAW_ASSERT(*found != same_name_other_bytes);
    reg.register_profile(same_name_other_bytes);
    PIKDRAW_ASSERT(!reg.find_profile("Custom Profile").has_value());
    PIKDRAW_ASSERT(reg.find_profile(*found).has_value());
    PIKDRAW_ASSERT(reg.find_profile(same_name_other_bytes).has_value());
}

PIKDRAW_COLOR_TEST(profile_resources_validate_icc_bytes_and_bind_by_digest) {
    auto bytes = synthetic_rgb_icc();
    const auto loaded = ProfileResource::create("Synthetic sRGB profile", ColorSpace::Rgb, bytes);
    PIKDRAW_ASSERT(loaded);
    PIKDRAW_ASSERT(loaded.resource->bytes() == bytes);
    PIKDRAW_ASSERT(loaded.resource->identity().checksum_sha256 ==
        "3968b29c2c08c89b844d5eac094b2793d7410a46c5d51727c3c97d2801d04201");

    auto& registry = ProfileRegistry::instance();
    const auto identity = loaded.resource->identity();
    PIKDRAW_ASSERT(registry.register_resource(loaded.resource));
    PIKDRAW_ASSERT(registry.find_profile(identity).has_value());
    PIKDRAW_ASSERT(registry.find_resource(identity) == loaded.resource);

    const auto mislabeled_builtin = ProfileResource::create("sRGB", ColorSpace::Rgb, bytes);
    PIKDRAW_ASSERT(mislabeled_builtin);
    PIKDRAW_ASSERT(!mislabeled_builtin.resource->identity().is_standard());

    auto same_name_different_bytes = bytes;
    same_name_different_bytes.back() = 1;
    const auto other = ProfileResource::create(
        "Synthetic sRGB profile", ColorSpace::Rgb, std::move(same_name_different_bytes));
    PIKDRAW_ASSERT(other);
    PIKDRAW_ASSERT(identity != other.resource->identity());
    PIKDRAW_ASSERT(!registry.find_resource(other.resource->identity()));
}

PIKDRAW_COLOR_TEST(paint_profile_binding_is_semantic_and_preview_cache_keyed) {
    const auto first_bytes = synthetic_rgb_icc();
    const auto first = ProfileResource::create(
        "Paint profile", ColorSpace::Rgb, first_bytes);
    PIKDRAW_ASSERT(first);
    auto other_bytes = first_bytes;
    other_bytes.back() = 1;
    const auto other = ProfileResource::create(
        "Paint profile", ColorSpace::Rgb, std::move(other_bytes));
    PIKDRAW_ASSERT(other);

    auto unbound = ColorValue::make_rgb(.2, .4, .6);
    auto bound = unbound;
    auto same_identity = unbound;
    auto other_binding = unbound;
    PIKDRAW_ASSERT(bound.set_profile_resource(first.resource));
    PIKDRAW_ASSERT(same_identity.set_profile_resource(first.resource));
    PIKDRAW_ASSERT(other_binding.set_profile_resource(other.resource));
    PIKDRAW_ASSERT(unbound.has_same_profile_binding(unbound));
    PIKDRAW_ASSERT(bound.has_same_profile_binding(same_identity));
    PIKDRAW_ASSERT(!unbound.has_same_profile_binding(bound));
    PIKDRAW_ASSERT(!bound.has_same_profile_binding(other_binding));

    ColorPreviewCache cache;
    (void)cache.get_or_compute(unbound, 1);
    (void)cache.get_or_compute(bound, 1);
    (void)cache.get_or_compute(other_binding, 1);
    PIKDRAW_ASSERT(cache.size() == 3);
}

PIKDRAW_COLOR_TEST(profile_resources_reject_invalid_size_signature_space_and_tags) {
    auto invalid_size = synthetic_rgb_icc();
    invalid_size[3]--;
    PIKDRAW_ASSERT(ProfileResource::create("Bad size", ColorSpace::Rgb, invalid_size).error ==
        ProfileResourceError::InvalidHeader);

    auto invalid_signature = synthetic_rgb_icc();
    invalid_signature[36] = 'x';
    PIKDRAW_ASSERT(ProfileResource::create("Bad signature", ColorSpace::Rgb,
        std::move(invalid_signature)).error == ProfileResourceError::InvalidHeader);

    PIKDRAW_ASSERT(ProfileResource::create("Wrong model", ColorSpace::Cmyk,
        synthetic_rgb_icc()).error == ProfileResourceError::ColorSpaceMismatch);

    auto invalid_tag = synthetic_rgb_icc();
    write_u32_be(invalid_tag, 136, 0xfffffffcu);
    PIKDRAW_ASSERT(ProfileResource::create("Bad tag", ColorSpace::Rgb,
        std::move(invalid_tag)).error == ProfileResourceError::InvalidTagTable);
}
