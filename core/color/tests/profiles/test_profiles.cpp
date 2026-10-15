#include "test_harness.hpp"
#include "pikdraw/color/profiles/profile_registry.hpp"

using namespace pikdraw::color;

PIKDRAW_COLOR_TEST(default_profiles_and_registration) {
    auto& reg = ProfileRegistry::instance();
    PIKDRAW_ASSERT(reg.has_profile("sRGB"));
    PIKDRAW_ASSERT(reg.has_profile("ISO Coated v2 300% (ECI)"));
    PIKDRAW_ASSERT(reg.has_profile("Display P3"));

    const auto old_gen = reg.generation();
    reg.register_profile(ProfileIdentifier{"Custom Profile", ColorSpace::Rgb, "abc123hash"});
    PIKDRAW_ASSERT(reg.has_profile("Custom Profile"));
    PIKDRAW_ASSERT(reg.generation() > old_gen);

    const auto found = reg.find_profile("Custom Profile");
    PIKDRAW_ASSERT(found.has_value());
    PIKDRAW_ASSERT(found->checksum_sha256 == "abc123hash");
}
