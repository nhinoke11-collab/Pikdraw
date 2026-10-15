#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace pikdraw::interop {

enum class FixtureCategory {
    ValidBasic,
    BoundaryLimit,
    CorruptHeader,
    OversizedPayload,
    ExcessiveDepth,
    NonFiniteCoord,
    StaleEnvelope,
    SecurityCanary
};

struct FixtureManifestEntry {
    std::string fixture_id;
    std::string relative_path;
    FixtureCategory category;
    uint64_t file_size_bytes;
    std::string sha256_checksum;
    std::string rights_declaration; // Must be proprietary PikDraw synthetic or public domain
    std::string expected_result;
};

class FixtureManifest {
public:
    [[nodiscard]] static const std::vector<FixtureManifestEntry>& GetEntries() noexcept;
    [[nodiscard]] static size_t Count() noexcept;
};

} // namespace pikdraw::interop
