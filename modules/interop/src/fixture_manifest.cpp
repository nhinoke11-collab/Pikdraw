#include "pikdraw/interop/fixture_manifest.hpp"
namespace pikdraw::interop {
const std::vector<FixtureManifestEntry>& FixtureManifest::GetEntries() noexcept {
    // No on-disk corpus has been provisioned or hashed. Unit vectors are not format evidence.
    static const std::vector<FixtureManifestEntry> entries;
    return entries;
}
size_t FixtureManifest::Count() noexcept { return GetEntries().size(); }
}
