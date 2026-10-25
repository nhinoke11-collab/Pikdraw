#include "pikdraw/raster/raster_source_restore.hpp"

#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace pikdraw::raster {
namespace {
using AssetKey = std::tuple<uint64_t, uint64_t, uint64_t>;

AssetKey key_for(const RasterSourceDescriptor& descriptor) {
    return {descriptor.asset_id.high, descriptor.asset_id.low,
            descriptor.revision};
}

bool same_asset(const RasterSourceDescriptor& left,
                const RasterSourceDescriptor& right) {
    return left.asset_id == right.asset_id &&
        left.revision == right.revision && left.mode == right.mode &&
        left.encoded_byte_length == right.encoded_byte_length &&
        left.media_type == right.media_type &&
        left.fingerprint_sha256 == right.fingerprint_sha256;
}

RasterSourceRestoreResult failure(std::string message) {
    return {nullptr, std::move(message)};
}
}  // namespace

RasterSourceRestoreResult restore_embedded_source_providers(
    const std::shared_ptr<document::DocumentSnapshot>& snapshot,
    const RasterAssetStore::CancelCheck& cancelled,
    const RasterAssetStore::ProgressCallback& progress) {
    if (!snapshot)
        return failure("Raster source restore requires a document snapshot.");
    try {
        auto pages = snapshot->pages();
        std::shared_ptr<TemporaryRasterAssetSession> session;
        std::map<AssetKey, std::pair<RasterSourceDescriptor,
            std::shared_ptr<const document::RasterSourceProvider>>> providers;
        bool changed = false;

        for (auto& page : pages) {
            const auto objects = page.objects.all();
            for (const auto& [id, object] : objects) {
                const auto* image = std::get_if<document::ImageObject>(&object);
                if (!image || !image->asset_source ||
                    image->asset_source->mode != document::RasterStorageMode::Embedded ||
                    (image->source.asset_provider &&
                     image->source.asset_provider->size_bytes() ==
                         image->asset_source->encoded_byte_length))
                    continue;
                if (cancelled && cancelled())
                    return failure("Raster source restore was cancelled.");

                // Copying legacy inline vectors here could temporarily double the
                // decoded/source RAM. Current .pr asset tables expose these buffers
                // by shared immutable pointers; older layouts retain their original
                // load path until a streaming storage migration is available.
                if ((!image->source.original_bytes.empty() &&
                     !image->source.shared_original_bytes) ||
                    (!image->pixels_rgba.empty() && !image->shared_pixels_rgba))
                    continue;

                const auto source_bytes = image->source.original_data();
                if (source_bytes.empty())
                    return failure("Embedded raster descriptor has no authoritative source bytes.");
                if (source_bytes.size() != image->asset_source->encoded_byte_length)
                    return failure("Embedded raster source length differs from its descriptor.");

                const auto key = key_for(*image->asset_source);
                auto found = providers.find(key);
                if (found != providers.end() &&
                    !same_asset(found->second.first, *image->asset_source))
                    return failure("Raster asset identity was reused for different source metadata.");
                if (found == providers.end()) {
                    if (!session) {
                        std::string session_error;
                        session = TemporaryRasterAssetSession::create(&session_error);
                        if (!session)
                            return failure("Could not create the reopened raster source session: " +
                                           session_error);
                    }
                    std::string provider_error;
                    auto provider = session->restore_embedded(
                        std::span<const uint8_t>(source_bytes.data(),
                                                 source_bytes.size()),
                        *image->asset_source, cancelled, progress, &provider_error);
                    if (!provider)
                        return failure("Could not restore embedded raster provider: " +
                                       provider_error);
                    found = providers.emplace(key,
                        std::pair{*image->asset_source,
                            std::static_pointer_cast<const document::RasterSourceProvider>(
                                std::move(provider))}).first;
                }

                document::ImageObject restored = *image;
                restored.source.asset_provider = found->second.second;
                // The provider now owns the runtime path to the immutable
                // source. Do not keep the deserialized source table resident
                // in every document snapshot after the verified disk restore.
                std::vector<uint8_t>().swap(restored.source.original_bytes);
                restored.source.shared_original_bytes.reset();
                restored.source.source_memory_lease.reset();
                page.objects = page.objects.set(id, std::move(restored));
                changed = true;
            }
        }
        if (!changed) return {snapshot, {}};
        auto restored = std::make_shared<document::DocumentSnapshot>(
            snapshot->epoch(), snapshot->revision(), std::move(pages),
            snapshot->active_page_id(), snapshot->document_id(),
            snapshot->content_mode());
        return {std::move(restored), {}};
    } catch (const std::exception& error) {
        return failure(std::string("Could not reattach raster source providers: ") +
                       error.what());
    } catch (...) {
        return failure("Could not reattach raster source providers.");
    }
}

}  // namespace pikdraw::raster
