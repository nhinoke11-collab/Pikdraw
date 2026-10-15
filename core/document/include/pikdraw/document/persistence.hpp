#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include "pikdraw/document/snapshot.hpp"
#include "pikdraw/document/document.hpp"

namespace pikdraw::document {

enum class FaultInjectionPoint {
    None = 0,
    BeforeWrite,
    DuringWrite,
    BeforeFlush,
    BeforeReplace,
    AfterReplace
};

struct SaveResult {
    bool success{false};
    std::string error_message;
    Epoch epoch{0};
    Revision revision{0};
};

struct LoadResult {
    bool success{false};
    std::string error_message;
    std::unique_ptr<Document> document;
};

class PersistenceManager {
public:
    // Serialization and fault-tolerant atomic replacement
    static SaveResult save_snapshot(
        const std::shared_ptr<const DocumentSnapshot>& snapshot,
        const std::filesystem::path& target_path,
        FaultInjectionPoint fault = FaultInjectionPoint::None
    );

    static LoadResult load_document(
        const std::filesystem::path& source_path
    );

    // Low-level buffer serialization for round-trip testing and checksum validation
    static std::vector<uint8_t> serialize_to_buffer(const DocumentSnapshot& snapshot);
    static std::shared_ptr<const DocumentSnapshot> deserialize_from_buffer(const std::vector<uint8_t>& buffer);
};

} // namespace pikdraw::document
