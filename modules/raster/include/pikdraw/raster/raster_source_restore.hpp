#pragma once

#include "pikdraw/raster/asset_store.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"

#include <memory>
#include <string>

namespace pikdraw::raster {

struct RasterSourceRestoreResult {
    std::shared_ptr<document::DocumentSnapshot> snapshot;
    std::string diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return snapshot != nullptr && diagnostic.empty();
    }
};

/// Reattaches process-local providers to persisted Embedded raster sources.
/// Source payloads in the .pr snapshot remain authoritative and are not mutated.
[[nodiscard]] RasterSourceRestoreResult restore_embedded_source_providers(
    const std::shared_ptr<document::DocumentSnapshot>& snapshot,
    const RasterAssetStore::CancelCheck& cancelled = {},
    const RasterAssetStore::ProgressCallback& progress = {});

}  // namespace pikdraw::raster
