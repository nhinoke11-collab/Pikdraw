#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include "pikdraw/document/commands/artboard_commands.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/native_file.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../../src/storage/native_chunk_decode.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::document;

void test_native_format_roundtrip() {
    assert(kNativeDocumentExtension == ".pr");
    assert(is_native_document_path("design.pr"));
    assert(is_native_document_path("design.PR"));
    assert(!is_native_document_path("design.other"));
    assert(!is_native_document_path("design.pr.svg"));
    assert(!is_native_document_path("design"));
    Document doc("doc-storage-test");

    // Add Path Object
    PathObject po;
    po.id = generate_object_id();
    po.name = "Vector Star";
    po.path.move_to({100.0, 100.0});
    po.path.line_to({200.0, 150.0});
    po.path.quad_to(250.0, 180.0, 300.0, 100.0);
    po.path.cubic_to(280.0, 80.0, 220.0, 50.0, 100.0, 100.0);
    po.path.close();
    doc.execute_command(std::make_unique<AddObjectCommand>(po));

    // Add Vietnamese Text Object (NFC and NFD)
    TextObject to;
    to.id = generate_object_id();
    to.name = "Vietnamese Sign";
    to.text = "Cộng hòa Xã hội Chủ nghĩa Việt Nam";
    to.font.family_name = "PikDrawSans";
    to.font.size_pt = 48.0;
    to.position = {50.0, 200.0};
    doc.execute_command(std::make_unique<AddObjectCommand>(to));

    auto snap = doc.current_snapshot();
    auto bytes = NativeFormat::serialize(*snap);
    assert(!bytes.empty());
    std::vector<uint8_t> streamed_bytes;
    const NativeFormat::StreamWrite stream_write =
        [&streamed_bytes](std::span<const uint8_t> chunk) {
            streamed_bytes.insert(streamed_bytes.end(), chunk.begin(), chunk.end());
        };
    const NativeFormat::StreamPatch stream_patch =
        [&streamed_bytes](uint64_t offset, std::span<const uint8_t> replacement) {
            assert(offset <= streamed_bytes.size());
            assert(replacement.size() <= streamed_bytes.size() - offset);
            std::copy(replacement.begin(), replacement.end(),
                streamed_bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        };
    NativeFormat::serialize_to(*snap, stream_write, stream_patch);
    assert(streamed_bytes == bytes);

    std::string err;
    auto loaded = NativeFormat::deserialize(bytes.data(), bytes.size(), &err);
    assert(loaded != nullptr);
    assert(loaded->epoch() == snap->epoch());
    assert(loaded->revision() == snap->revision());
    assert(loaded->object_count() == 2);

    auto loaded_to = loaded->get_object(to.id);
    assert(loaded_to.has_value());
    assert(std::get<TextObject>(*loaded_to).text == "Cộng hòa Xã hội Chủ nghĩa Việt Nam");

    // Test CRC32 corruption detection
    bytes[sizeof(PikDrawHeader) + 5] ^= 0xFF; // Corrupt a byte in payload
    auto corrupt_loaded = NativeFormat::deserialize(bytes.data(), bytes.size(), &err);
    assert(corrupt_loaded == nullptr);
    assert(!err.empty());
}

void test_native_chunked_compression_roundtrip() {
    Document doc("doc-storage-compression");
    ImageObject image;
    image.id = generate_object_id();
    image.name = "Repeated RGBA asset";
    image.width_px = 2048;
    image.height_px = 1024;
    image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
        static_cast<size_t>(image.width_px) * image.height_px, 0xff345678u);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);

    const auto snapshot = doc.current_snapshot();
    const auto bytes = NativeFormat::serialize(*snapshot);
    std::vector<uint8_t> streamed_bytes;
    NativeFormat::serialize_to(*snapshot,
        [&streamed_bytes](std::span<const uint8_t> part) {
            streamed_bytes.insert(streamed_bytes.end(), part.begin(), part.end());
        },
        [&streamed_bytes](uint64_t offset, std::span<const uint8_t> replacement) {
            assert(offset <= streamed_bytes.size());
            assert(replacement.size() <= streamed_bytes.size() - offset);
            std::copy(replacement.begin(), replacement.end(),
                streamed_bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        });
    assert(streamed_bytes == bytes);
    assert(bytes[4] == kLegacyChunkedContainerFormatVersion);
    assert(bytes[8] == kNativeCompressionFlagChunked);
    assert(bytes.size() < image.shared_pixels_rgba->size() * sizeof(uint32_t));
    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    const auto restored = reopened->get_object(image.id);
    assert(restored && std::get<ImageObject>(*restored).rgba_pixels().size() ==
        image.shared_pixels_rgba->size());
    assert(std::get<ImageObject>(*restored).rgba_pixels().front() == 0xff345678u);

    auto corrupted = bytes;
    corrupted[kChunkedContainerHeaderBytes + kChunkedContainerChunkBytes + 2] ^= 0x80;
    std::fill(corrupted.begin() + 32, corrupted.begin() + 36, 0);
    const auto checksum = NativeFormat::calculate_crc32(
        corrupted.data(), corrupted.size());
    for (unsigned byte = 0; byte < 4; ++byte)
        corrupted[32 + byte] = static_cast<uint8_t>(checksum >> (byte * 8));
    assert(!NativeFormat::deserialize(corrupted.data(), corrupted.size(), &error));
    assert(!error.empty());
}

void test_native_vector_payload_streams_as_legacy_chunked_container() {
    Document doc("doc-storage-large-vector-stream");
    TextObject text;
    text.id = generate_object_id();
    text.name = "Large vector-only payload";
    text.text.assign(2u * 1024u * 1024u, 'x');
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(text)).success);

    const auto snapshot = doc.current_snapshot();
    const auto bytes = NativeFormat::serialize(*snapshot);
    assert(bytes.size() > kNativeChunkBytes);
    assert(bytes[4] == kLegacyChunkedContainerFormatVersion);
    assert(bytes[8] == kNativeCompressionFlagChunked);
    std::vector<uint8_t> streamed_bytes;
    NativeFormat::serialize_to(*snapshot,
        [&streamed_bytes](std::span<const uint8_t> part) {
            streamed_bytes.insert(streamed_bytes.end(), part.begin(), part.end());
        },
        [&streamed_bytes](uint64_t offset, std::span<const uint8_t> replacement) {
            assert(offset <= streamed_bytes.size());
            assert(replacement.size() <= streamed_bytes.size() - offset);
            std::copy(replacement.begin(), replacement.end(),
                streamed_bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        });
    assert(streamed_bytes == bytes);

    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    const auto restored = reopened->get_object(text.id);
    assert(restored && std::get<TextObject>(*restored).text == text.text);
}

void test_native_chunk_decode_cancellation() {
    using pikdraw::document::storage_detail::NativeDecodeStatus;
    using pikdraw::document::storage_detail::calculate_chunk_crc32;
    using pikdraw::document::storage_detail::inflate_native_chunk;

    Document doc("doc-storage-cancel-decode");
    ImageObject image;
    image.id = generate_object_id();
    image.width_px = 2048;
    image.height_px = 1024;
    image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
        static_cast<size_t>(image.width_px) * image.height_px, 0xff345678u);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);
    const auto bytes = NativeFormat::serialize(*doc.current_snapshot());

    const auto read_u32 = [&bytes](size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    size_t chunk_offset = kChunkedContainerHeaderBytes;
    bool checked_deflated_chunk = false;
    int cancel_checks = 0;
    const auto cancel_on_second_window = [&cancel_checks] {
        return ++cancel_checks == 2;
    };
    while (chunk_offset + kChunkedContainerChunkBytes <= bytes.size()) {
        const auto raw_size = read_u32(chunk_offset);
        const auto stored_size = read_u32(chunk_offset + 4);
        const auto codec = bytes[chunk_offset + 8];
        assert(stored_size > 0);
        assert(chunk_offset + kChunkedContainerChunkBytes + stored_size <=
            bytes.size());
        if (codec == 1 && raw_size == 1024u * 1024u) {
            const auto stored = std::span<const uint8_t>(
                bytes.data() + chunk_offset + kChunkedContainerChunkBytes,
                stored_size);
            std::vector<uint8_t> decoded(raw_size);
            assert(inflate_native_chunk(
                stored, decoded, cancel_on_second_window) ==
                NativeDecodeStatus::Cancelled);
            assert(cancel_checks == 2);
            assert(inflate_native_chunk(stored, decoded, [] { return false; }) ==
                NativeDecodeStatus::Success);

            uint32_t crc = 0;
            assert(calculate_chunk_crc32(decoded, [] { return false; }, crc) ==
                NativeDecodeStatus::Success);
            assert(crc == read_u32(chunk_offset + 12));
            cancel_checks = 0;
            assert(calculate_chunk_crc32(decoded, cancel_on_second_window, crc) ==
                NativeDecodeStatus::Cancelled);
            assert(cancel_checks == 2);
            checked_deflated_chunk = true;
            break;
        }
        chunk_offset += kChunkedContainerChunkBytes + stored_size;
    }
    assert(checked_deflated_chunk);
}

void test_native_container_above_legacy_size_limit() {
    if (std::getenv("PIKDRAW_TEST_LARGE_NATIVE_CONTAINER") == nullptr)
        return;
    Document doc("doc-storage-large-container");
    ImageObject image;
    image.id = generate_object_id();
    image.name = "Incompressible RGBA payload";
    image.width_px = 8193;
    image.height_px = 8192;
    auto pixels = std::make_shared<std::vector<uint32_t>>(
        static_cast<size_t>(image.width_px) * image.height_px);
    uint32_t state = 0x6d2b79f5u;
    for (auto& pixel : *pixels) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        pixel = state;
    }
    image.shared_pixels_rgba = pixels;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);

    std::filesystem::path test_dir;
    if (const auto* output_dir = std::getenv("PIKDRAW_TEST_OUTPUT_DIR");
        output_dir && *output_dir) {
        test_dir = std::filesystem::path(output_dir) / "large-native-container";
    } else {
        test_dir = std::filesystem::temp_directory_path() /
            ("pikdraw-large-native-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    std::filesystem::create_directories(test_dir);
    const auto test_file = (test_dir / "large-over-256mib.pr").string();
    const auto saved = SaveRecoveryService::atomic_save(
        test_file, *doc.current_snapshot());
    const auto file_size = saved.success
        ? std::filesystem::file_size(test_file) : 0;
    std::string error;
    const auto reopened = saved.success
        ? SaveRecoveryService::load(test_file, &error) : nullptr;
    std::error_code cleanup_error;
    std::filesystem::remove_all(test_dir, cleanup_error);
    assert(!cleanup_error);
    assert(saved.success && file_size > 256ull * 1024ull * 1024ull);
    assert(reopened && error.empty());
    const auto restored = reopened->get_object(image.id);
    assert(restored && std::get<ImageObject>(*restored).rgba_pixels().size() ==
        pixels->size());
}

void test_native_multipage_roundtrip() {
    Document doc("doc-storage-pages");
    const auto first_page_id = doc.active_page_id();
    auto second_page = make_document_page("CNC Stock", Page::make_default());
    const auto second_page_id = second_page.id;
    assert(doc.execute_command(std::make_unique<AddPageCommand>(second_page)).success);

    PathObject path;
    path.id = generate_object_id();
    path.name = "Cut profile";
    path.path.move_to({1.0, 2.0});
    path.path.line_to({11.0, 2.0});
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);

    const auto snapshot = doc.current_snapshot();
    const auto bytes = NativeFormat::serialize(*snapshot);
    assert(bytes.size() > sizeof(PikDrawHeader));
    assert(bytes[4] == kArtboardsFormatVersion);
    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    assert(reopened->page_count() == 2);
    assert(reopened->active_page_id() == second_page_id);
    assert(reopened->find_page(first_page_id) != nullptr);
    assert(reopened->find_page(second_page_id)->name == "CNC Stock");
    assert(reopened->find_page(second_page_id)->objects.contains(path.id));

    Document restored(*reopened);
    assert(restored.pages().size() == 2);
    assert(restored.active_page_id() == second_page_id);
    assert(restored.objects().contains(path.id));
}

void test_native_artboards_roundtrip_and_legacy_page_migration() {
    Document doc("doc-storage-artboards");
    const auto page_id = doc.active_page_id();
    const auto base = doc.current_snapshot()->active_page().artboards.front();
    assert(doc.execute_command(std::make_unique<AddArtboardCommand>(page_id, "Wide output",
        geometry::Rect2D{base.width_mm + 20.0, 0.0, 500.0, 300.0})).success);
    const auto snapshot = doc.current_snapshot();
    const auto bytes = NativeFormat::serialize(*snapshot);
    assert(bytes[4] == kArtboardsFormatVersion);
    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    const auto* page = reopened->find_page(page_id);
    assert(page && page->artboards.size() == 2);
    assert(page->artboards[1].name == "Wide output");
    assert(page->artboards[1].owner_page_id == page_id);
    assert(page->artboards[1].x_mm == base.width_mm + 20.0 &&
           page->artboards[1].width_mm == 500.0 &&
           !page->artboards[1].linked_to_page_bounds);
    Document restored(*reopened);
    assert(restored.current_snapshot()->active_page().artboards.size() == 2);
}

void strip_v22_layer_fields(std::vector<uint8_t>& bytes, size_t layer_count_offset) {
    uint32_t name_length = 0;
    for (unsigned i = 0; i < 4; ++i)
        name_length |= static_cast<uint32_t>(bytes[layer_count_offset + 4 + i]) << (i * 8);
    const size_t extension = layer_count_offset + 10 + name_length;
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(extension),
                bytes.begin() + static_cast<std::ptrdiff_t>(extension + 5));
}

void test_raster_asset_reference_and_edit_graph_roundtrip() {
    Document doc("raster-asset-reference-roundtrip");
    ImageObject image;
    image.id = generate_object_id();
    image.name = "Linked large source";
    image.width_px = 2;
    image.height_px = 2;
    image.pixels_rgba.assign(4, 0xff336699u);
    image.shared_pixels_rgba16le = std::make_shared<const std::vector<uint8_t>>(
        std::vector<uint8_t>{
        0x01, 0x80, 0x02, 0x80, 0x03, 0x80, 0xff, 0xff,
        0x04, 0x80, 0x05, 0x80, 0x06, 0x80, 0xfe, 0xff,
        0x07, 0x80, 0x08, 0x80, 0x09, 0x80, 0xfd, 0xff,
        0x0a, 0x80, 0x0b, 0x80, 0x0c, 0x80, 0xfc, 0xff});
    image.asset_source = RasterSourceDescriptor{
        RasterAssetId{0x1122334455667788ull, 0x99aabbccddeeff00ull},
        7,
        RasterStorageMode::Linked,
        8ull * 1024ull * 1024ull * 1024ull,
        "image/tiff",
        "assets/large.tif",
        std::string(64, 'a'),
        -123456789};
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{41}, true, RasterCropParams{0.1, 0.2, 0.7, 0.6}});
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{42}, true, RasterExposureParams{1.25}});
    image.edit_graph.nodes.back().mask = RasterAdjustmentMask{
        9001, 2, 2, 67, true, {0, 64, 128, 255}};
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{43}, true, RasterLevelsParams{
            RasterToneChannel::Green, 0.08, 0.92, 1.35, 0.02, 0.98}});
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{44}, true, RasterHslParams{23.0, -35.0, 12.0}});
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{45}, true, RasterCurvesParams{
            RasterToneChannel::Blue, {0.0, 0.1, 0.55, 0.9, 1.0}}});
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{46}, true, RasterVibranceParams{65.0}});
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{47}, true, RasterSelectiveHslParams{
            RasterHueRange::Cyans, 20.0, -40.0, 15.0, 75.0}});
    RasterAdjustmentMask linear_mask;
    linear_mask.id = 9002;
    linear_mask.width_px = 2;
    linear_mask.height_px = 2;
    linear_mask.kind = RasterAdjustmentMaskKind::LinearGradient;
    linear_mask.gradient_angle_degrees = 37.5;
    linear_mask.left = 0.2;
    linear_mask.top = 0.1;
    linear_mask.width = 0.7;
    linear_mask.height = 0.8;
    linear_mask.feather_px = 1.5;
    image.edit_graph.nodes.back().mask = linear_mask;
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{48}, true, RasterLightParams{
            0.25, 20.0, 10.0, -15.0, 5.0, -3.0}});
    RasterChannelMixerParams mixer;
    mixer.coefficients = {0.9, 0.1, 0.0, 0.05, 0.9, 0.05, 0.0, 0.15, 0.85};
    image.edit_graph.nodes.push_back({RasterEditNodeId{49}, true, mixer});
    RasterAdjustmentMask radial_mask;
    radial_mask.id = 9003;
    radial_mask.width_px = 2;
    radial_mask.height_px = 2;
    radial_mask.kind = RasterAdjustmentMaskKind::RadialGradient;
    radial_mask.left = 0.25;
    radial_mask.top = 0.25;
    radial_mask.width = 0.5;
    radial_mask.height = 0.5;
    radial_mask.feather_px = 0.75;
    image.edit_graph.nodes.back().mask = radial_mask;
    const RasterCloneStampParams clone{0.2, 0.3, 0.7, 0.8, 4.0, 55.0, 80.0};
    image.edit_graph.nodes.push_back({RasterEditNodeId{50}, true, clone});
    const RasterColorBalanceParams balance{
        RasterTonalRange::Highlights, 12.5, -20.0, 37.0, false};
    image.edit_graph.nodes.push_back({RasterEditNodeId{51}, true, balance});
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);

    const auto snapshot = doc.current_snapshot();
    const auto bytes = NativeFormat::serialize(*snapshot);
    assert(bytes.size() > sizeof(PikDrawHeader));
    assert(bytes[4] == kArtboardsFormatVersion);
    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    const auto value = reopened->get_object(image.id);
    assert(value && std::holds_alternative<ImageObject>(*value));
    const auto& restored = std::get<ImageObject>(*value);
    assert(restored.asset_source);
    assert(restored.asset_source->asset_id == image.asset_source->asset_id);
    assert(restored.asset_source->revision == 7);
    assert(restored.asset_source->mode == RasterStorageMode::Linked);
    assert(restored.asset_source->encoded_byte_length == 8ull * 1024ull * 1024ull * 1024ull);
    assert(restored.asset_source->source_modified_ticks == -123456789);
    assert(restored.asset_source->locator == "assets/large.tif");
    if (restored.edit_graph.nodes.size() != 11 ||
        restored.edit_graph.schema_version != RasterEditGraph::kSchemaVersion)
        throw std::runtime_error("Raster graph v17 Color Balance save/reopen failed");
    assert(restored.edit_graph.nodes.size() == 11);
    assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
    assert(std::get<RasterChannelMixerParams>(restored.edit_graph.nodes[8].parameters).coefficients ==
        mixer.coefficients);
    const auto& restored_clone = std::get<RasterCloneStampParams>(
        restored.edit_graph.nodes[9].parameters);
    assert(restored_clone.source_x == clone.source_x &&
        restored_clone.source_y == clone.source_y &&
        restored_clone.target_x == clone.target_x &&
        restored_clone.target_y == clone.target_y &&
        restored_clone.radius_percent == clone.radius_percent &&
        restored_clone.hardness_percent == clone.hardness_percent &&
        restored_clone.opacity_percent == clone.opacity_percent);
    const auto& restored_balance = std::get<RasterColorBalanceParams>(
        restored.edit_graph.nodes[10].parameters);
    assert(restored_balance.range == balance.range &&
        restored_balance.red_cyan_percent == balance.red_cyan_percent &&
        restored_balance.green_magenta_percent == balance.green_magenta_percent &&
        restored_balance.blue_yellow_percent == balance.blue_yellow_percent &&
        restored_balance.preserve_luminosity == balance.preserve_luminosity);
    assert(restored.edit_graph.nodes[0].id.value == 41);
    assert(restored.edit_graph.nodes[0].algorithm_version == 1);
    assert(std::get<RasterCropParams>(restored.edit_graph.nodes[0].parameters).width == 0.7);
    assert(restored.edit_graph.nodes[1].id.value == 42);
    assert(restored.edit_graph.nodes[1].algorithm_version == 1);
    assert(std::get<RasterExposureParams>(restored.edit_graph.nodes[1].parameters).exposure_ev == 1.25);
    assert(restored.edit_graph.nodes[1].mask == image.edit_graph.nodes[1].mask);
    assert(restored.edit_graph.nodes[6].mask == image.edit_graph.nodes[6].mask);
    assert(restored.edit_graph.nodes[6].mask->gradient_angle_degrees == 37.5);
    assert(restored.edit_graph.nodes[8].mask == image.edit_graph.nodes[8].mask);
    assert(restored.edit_graph.nodes[2].id.value == 43);
    const auto& levels = std::get<RasterLevelsParams>(
        restored.edit_graph.nodes[2].parameters);
    assert(levels.channel == RasterToneChannel::Green &&
        levels.black_in == 0.08 && levels.white_in == 0.92 &&
        levels.gamma == 1.35 && levels.black_out == 0.02 &&
        levels.white_out == 0.98);
    const auto* restored_hsl = std::get_if<RasterHslParams>(
        &restored.edit_graph.nodes[3].parameters);
    if (!restored_hsl || restored.edit_graph.nodes[3].id.value != 44 ||
        restored_hsl->hue_degrees != 23.0 ||
        restored_hsl->saturation_percent != -35.0 ||
        restored_hsl->lightness_percent != 12.0)
        throw std::runtime_error("HSL parameters changed during .pr round-trip");
    const auto* restored_curves = std::get_if<RasterCurvesParams>(
        &restored.edit_graph.nodes[4].parameters);
    if (!restored_curves || restored.edit_graph.nodes[4].id.value != 45 ||
        restored_curves->channel != RasterToneChannel::Blue ||
        restored_curves->output_points !=
            std::array<double, 5>{0.0, 0.1, 0.55, 0.9, 1.0})
        throw std::runtime_error("Curves parameters changed during .pr round-trip");
    const auto* restored_vibrance = std::get_if<RasterVibranceParams>(
        &restored.edit_graph.nodes[5].parameters);
    if (!restored_vibrance || restored.edit_graph.nodes[5].id.value != 46 ||
        restored_vibrance->amount_percent != 65.0)
        throw std::runtime_error("Vibrance parameters changed during .pr round-trip");
    const auto* restored_selective_hsl = std::get_if<RasterSelectiveHslParams>(
        &restored.edit_graph.nodes[6].parameters);
    if (!restored_selective_hsl || restored.edit_graph.nodes[6].id.value != 47 ||
        restored_selective_hsl->range != RasterHueRange::Cyans ||
        restored_selective_hsl->hue_degrees != 20.0 ||
        restored_selective_hsl->saturation_percent != -40.0 ||
        restored_selective_hsl->lightness_percent != 15.0 ||
        restored_selective_hsl->feather_percent != 75.0)
        throw std::runtime_error("Selective HSL parameters changed during .pr round-trip");
    const auto* restored_light = std::get_if<RasterLightParams>(
        &restored.edit_graph.nodes[7].parameters);
    if (!restored_light || restored.edit_graph.nodes[7].id.value != 48 ||
        restored_light->exposure_ev != 0.25 ||
        restored_light->contrast_percent != 20.0 ||
        restored_light->highlights_percent != 10.0 ||
        restored_light->shadows_percent != -15.0 ||
        restored_light->whites_percent != 5.0 ||
        restored_light->blacks_percent != -3.0)
        throw std::runtime_error("Light parameters changed during .pr round-trip");
    assert(restored.rgba16le_pixels().size() ==
           image.shared_pixels_rgba16le->size());
    assert(std::equal(restored.rgba16le_pixels().begin(),
                      restored.rgba16le_pixels().end(),
                      image.shared_pixels_rgba16le->begin()));
}

void test_raster_edit_graph_v2_migrates_without_precision_payload() {
    Document doc("raster-graph-v2-migration");
    ImageObject image;
    image.id = generate_object_id();
    image.name = "legacy raster schema marker";
    image.width_px = 1;
    image.height_px = 1;
    image.pixels_rgba = {0xff336699u};
    image.edit_graph.nodes.push_back({
        RasterEditNodeId{73}, true, RasterExposureParams{0.5}});
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);
    auto bytes = NativeFormat::serialize(*doc.current_snapshot());
    assert(bytes[4] == kArtboardsFormatVersion);

    const auto marker = std::search(bytes.begin(), bytes.end(),
        image.name.begin(), image.name.end());
    assert(marker != bytes.end());
    const size_t name_end = static_cast<size_t>(marker - bytes.begin()) +
                            image.name.size();
    const size_t graph_schema_offset = name_end + 48 + 2 + 32 + 8 + 4 + 4 + 1 +
        4 + 16 + 1 + 16 + 1 + 4 + 4 + 1 + 1;
    const auto read_u32 = [&](size_t offset) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(bytes[offset + i]) << (i * 8);
        return value;
    };
    assert(read_u32(graph_schema_offset) == RasterEditGraph::kSchemaVersion);
    assert(read_u32(graph_schema_offset + 4) == 1);
    const size_t precision_flag_offset = graph_schema_offset + 4 + 4 + 25;
    assert(bytes[precision_flag_offset] == 0);
    bytes[graph_schema_offset] = 2;
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(precision_flag_offset));
    const auto write_u32 = [](std::vector<uint8_t>& target, size_t offset,
                              uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            target[offset + i] = static_cast<uint8_t>(value >> (i * 8));
    };
    write_u32(bytes, 28, static_cast<uint32_t>(bytes.size() - 36));
    std::fill(bytes.begin() + 32, bytes.begin() + 36, 0);
    write_u32(bytes, 32, NativeFormat::calculate_crc32(bytes.data(), bytes.size()));

    std::string error;
    const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened && error.empty());
    const auto restored_value = reopened->get_object(image.id);
    assert(restored_value && std::holds_alternative<ImageObject>(*restored_value));
    const auto& restored = std::get<ImageObject>(*restored_value);
    assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
    assert(restored.edit_graph.nodes.size() == 1);
    assert(std::get<RasterExposureParams>(
        restored.edit_graph.nodes.front().parameters).exposure_ev == 0.5);
    assert(restored.rgba16le_pixels().empty());
}

void test_native_v12_units_migrate_to_implicit_millimeters() {
    using geometry::units::LengthUnit;
    Page page = Page::make_default();
    page.width_mm = 431.8;
    page.display_length_unit = LengthUnit::Inch;
    page.display_length_unit_explicit = true;
    Document doc("native-v12-unit-migration", page);
    auto current = NativeFormat::serialize(*doc.current_snapshot());
    const auto read_u32 = [&](size_t offset) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(current[offset + i]) << (i * 8);
        return value;
    };
    const auto write_u32 = [](std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
    };
    const auto document_id_length = read_u32(36);
    const size_t schema_prefix_end = 36 + 4 + document_id_length + 7 * 4;
    const size_t color_settings_bytes = 1 + 4 + read_u32(schema_prefix_end + 1) + 4 + 1;
    const size_t page_count_offset = schema_prefix_end + color_settings_bytes + 1 + 4;
    assert(read_u32(page_count_offset) == 1);
    const size_t page_record = page_count_offset + 4 + 8;
    const auto page_name_length = read_u32(page_record + 8);
    const size_t page_body = page_record + 8 + 4 + page_name_length;
    const size_t display_unit_offset = page_body + 16;
    const auto profile_length = read_u32(display_unit_offset + 2 + 1);
    const size_t page_color_settings_bytes = 1 + 4 + profile_length + 4 + 1 + 4;
    const size_t page_profile_reference_offset =
        display_unit_offset + 2 + page_color_settings_bytes - 4;
    const size_t import_report_count_offset = page_profile_reference_offset + 4 + 2;

    const size_t default_artboard_start = current.size() - 8 - 67; // one page-bound Artboard 1 record
    auto v14 = current;
    v14.erase(v14.begin() + static_cast<std::ptrdiff_t>(default_artboard_start),
              v14.begin() + static_cast<std::ptrdiff_t>(default_artboard_start + 67));
    v14.erase(v14.begin() + static_cast<std::ptrdiff_t>(import_report_count_offset),
              v14.begin() + static_cast<std::ptrdiff_t>(import_report_count_offset + 4));
    v14.erase(v14.begin() + static_cast<std::ptrdiff_t>(page_profile_reference_offset),
              v14.begin() + static_cast<std::ptrdiff_t>(page_profile_reference_offset + 4));
    strip_v22_layer_fields(v14, import_report_count_offset - 4);
    v14.resize(v14.size() - 8); // native ICC dictionary and path-feature counts
    v14[4] = static_cast<uint8_t>(kPageColorSettingsFormatVersion);
    write_u32(v14, 36 + 4 + document_id_length, kPageColorSettingsFormatVersion);
    write_u32(v14, 28, static_cast<uint32_t>(v14.size() - 36));
    std::fill(v14.begin() + 32, v14.begin() + 36, 0);
    write_u32(v14, 32, NativeFormat::calculate_crc32(v14.data(), v14.size()));
    std::string v14_error;
    const auto migrated_v14 = NativeFormat::deserialize(v14.data(), v14.size(), &v14_error);
    if (!migrated_v14)
        std::cerr << "native v14 migration failed: " << v14_error << '\n';
    assert(migrated_v14 && v14_error.empty() &&
        migrated_v14->page().display_length_unit == LengthUnit::Inch &&
        migrated_v14->page().color_profile_name == "sRGB" &&
        !migrated_v14->page().color_profile_resource &&
        migrated_v14->active_page().artboards.size() == 1 &&
        migrated_v14->active_page().artboards.front().linked_to_page_bounds);

    current.erase(current.begin() + static_cast<std::ptrdiff_t>(default_artboard_start),
                  current.begin() + static_cast<std::ptrdiff_t>(default_artboard_start + 67));
    current.erase(current.begin() + static_cast<std::ptrdiff_t>(import_report_count_offset),
                  current.begin() + static_cast<std::ptrdiff_t>(import_report_count_offset + 4));
    strip_v22_layer_fields(current, import_report_count_offset);
    current.erase(current.begin() + static_cast<std::ptrdiff_t>(display_unit_offset),
                  current.begin() + static_cast<std::ptrdiff_t>(
                      display_unit_offset + 2 + page_color_settings_bytes));
    current.resize(current.size() - 8); // native ICC dictionary and path-feature counts
    current[4] = static_cast<uint8_t>(kPdfSourcePreviewFormatVersion);
    write_u32(current, 36 + 4 + document_id_length, kPdfSourcePreviewFormatVersion);
    write_u32(current, 28, static_cast<uint32_t>(current.size() - 36));
    std::fill(current.begin() + 32, current.begin() + 36, 0);
    write_u32(current, 32, NativeFormat::calculate_crc32(current.data(), current.size()));

    std::string error;
    const auto migrated = NativeFormat::deserialize(current.data(), current.size(), &error);
    assert(migrated && error.empty());
    const auto& restored_page = migrated->pages().front().layout;
    assert(restored_page.width_mm == 431.8);
    assert(restored_page.display_length_unit == LengthUnit::Millimeter);
    assert(!restored_page.display_length_unit_explicit);
    assert(migrated->active_page().artboards.size() == 1 &&
           migrated->active_page().artboards.front().linked_to_page_bounds);
}

void test_native_v5_migrates_to_one_page() {
    Document doc("doc-v5-compat");
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(2.0, 3.0, 20.0, 10.0);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);
    auto current = NativeFormat::serialize(*doc.current_snapshot());
    current.erase(current.end() - 8 - 67, current.end() - 8); // v5 had no Artboard section
    // v5 did not store the v20 import-report count or v22 layer properties.
    const size_t schema_prefix_end = 36 + 4 + std::string("doc-v5-compat").size() + 7 * 4;
    const size_t color_settings_bytes = 1 + 4 + std::string("sRGB").size() + 4 + 1;
    const size_t page_count_offset = schema_prefix_end + color_settings_bytes + 1 + 4;
    const size_t compatibility_page_record = page_count_offset + 4 + 8;
    const auto get_u32 = [&](size_t offset) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(current[offset + i]) << (i * 8);
        return value;
    };
    const auto compatibility_page_name_length = get_u32(compatibility_page_record + 8);
    const size_t compatibility_page_body = compatibility_page_record + 8 + 4 + compatibility_page_name_length;
    const auto compatibility_profile_length = get_u32(compatibility_page_body + 16 + 2 + 1);
    const size_t report_count_offset = compatibility_page_body + 16 + 2 + 1 + 4 + compatibility_profile_length + 4 + 1 + 4 + 2;
    current.erase(current.begin() + static_cast<std::ptrdiff_t>(report_count_offset),
                  current.begin() + static_cast<std::ptrdiff_t>(report_count_offset + 4));
    strip_v22_layer_fields(current, report_count_offset);
    current.resize(current.size() - 8); // native ICC dictionary and path-feature counts

    // Convert the single v6 page record back to the exact v5 layout: v5 had
    // the same schema prefix and page body, without page IDs or active-page data.
    const auto read_u32 = [&](size_t offset) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(current[offset + i]) << (i * 8);
        return value;
    };
    const auto doc_id_length = read_u32(36);
    const size_t prefix_end = 36 + 4 + doc_id_length + 7 * 4;
    const size_t color_bytes = 1 + 4 + read_u32(prefix_end + 1) + 4 + 1;
    const size_t page_record = prefix_end + color_bytes + 1 + 4 + 4 + 8;
    const auto page_name_length = read_u32(page_record + 8);
    const size_t legacy_page_body = page_record + 8 + 4 + page_name_length;
    const auto legacy_profile_length = read_u32(legacy_page_body + 16 + 2 + 1);
    const size_t legacy_page_tail = legacy_page_body + 16 + 2 + 1 + 4 + legacy_profile_length + 4 + 1 + 4 + 2;
    const auto layer_name_length = read_u32(legacy_page_tail + 4);
    const auto layer_refs_offset = legacy_page_tail + 8 + layer_name_length + 2;
    const auto layer_ref_count = read_u32(layer_refs_offset);
    const auto global_object_count_offset = layer_refs_offset + 4 + 8 * layer_ref_count;
    assert(read_u32(global_object_count_offset) == 1);
    // v17 added paint-profile indexes to each source color. Strip the two RGB
    // indexes to reconstruct the old v5 object record before changing version.
    const size_t object_start = global_object_count_offset + 4;
    const auto object_name_length = read_u32(object_start + 1 + 8);
    const size_t common_end = object_start + 1 + 8 + 4 + object_name_length + 48 + 2;
    const size_t rgb_color_without_binding = 1 + 2 + 4 * 8;
    const size_t fill_binding = common_end + rgb_color_without_binding;
    const size_t stroke_binding = fill_binding + 4 + 1 + rgb_color_without_binding;
    assert(current[common_end] == 0 && current[common_end + 39] == 0);
    current.erase(current.begin() + static_cast<std::ptrdiff_t>(stroke_binding),
                  current.begin() + static_cast<std::ptrdiff_t>(stroke_binding + 4));
    current.erase(current.begin() + static_cast<std::ptrdiff_t>(fill_binding),
                  current.begin() + static_cast<std::ptrdiff_t>(fill_binding + 4));
    std::vector<uint8_t> legacy(current.begin(), current.begin() + 36);
    legacy.insert(legacy.end(), current.begin() + 36, current.begin() + prefix_end);
    legacy.insert(legacy.end(), current.begin() + legacy_page_body,
                  current.begin() + legacy_page_body + 16);
    legacy.insert(legacy.end(), current.begin() + legacy_page_tail, current.end());
    legacy[4] = static_cast<uint8_t>(kGradientFormatVersion);
    const auto compatible_offset = 36 + 4 + doc_id_length;
    for (unsigned i = 0; i < 4; ++i)
        legacy[compatible_offset + i] = static_cast<uint8_t>(kGradientFormatVersion >> (i * 8));
    const auto payload_size = static_cast<uint32_t>(legacy.size() - 36);
    for (unsigned i = 0; i < 4; ++i)
        legacy[28 + i] = static_cast<uint8_t>(payload_size >> (i * 8));
    std::fill(legacy.begin() + 32, legacy.begin() + 36, 0);
    const auto checksum = NativeFormat::calculate_crc32(legacy.data(), legacy.size());
    for (unsigned i = 0; i < 4; ++i)
        legacy[32 + i] = static_cast<uint8_t>(checksum >> (i * 8));

    std::string error;
    const auto reopened = NativeFormat::deserialize(legacy.data(), legacy.size(), &error);
    if (!reopened) std::cerr << "native v5 compatibility failed: " << error << '\n';
    assert(reopened && error.empty());
    assert(reopened->page_count() == 1);
    assert(reopened->active_page().artboards.size() == 1 &&
           reopened->active_page().artboards.front().linked_to_page_bounds);
    assert(reopened->object_count() == 1);
    assert(reopened->contains_object(path.id));
}

void test_atomic_save_and_dirty_tracking() {
    Document doc("doc-dirty-test");
    std::filesystem::path test_dir;
    if (const auto* output_dir = std::getenv("PIKDRAW_TEST_OUTPUT_DIR");
        output_dir && *output_dir) {
        test_dir = std::filesystem::path(output_dir) / "document-storage";
    } else {
        test_dir = std::filesystem::temp_directory_path() /
            ("pikdraw-storage-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    std::filesystem::create_directories(test_dir);
    const std::string target_file = (test_dir / "test_atomic.pr").string();

    PathObject po;
    po.id = generate_object_id();
    po.path.move_to({0, 0});
    doc.execute_command(std::make_unique<AddObjectCommand>(po));

    // Save snapshot at rev 1
    auto snap1 = doc.current_snapshot();
    assert(doc.revision() == 1);
    assert(doc.is_dirty());

    auto save_res = SaveRecoveryService::atomic_save(target_file, *snap1);
    assert(save_res.success);

    // If user edited before async save finishes (revision bumped to 2)
    doc.execute_command(std::make_unique<TransformObjectCommand>(
        po.id, geometry::Transform2D::translation(10, 10)));
    assert(doc.revision() == 2);

    // Mark saved with rev 1 result: Document must still be DIRTY because rev 2 is unsaved!
    doc.mark_saved(save_res.saved_epoch, save_res.saved_revision);
    assert(doc.is_dirty()); // Invariant: r+1 edits keep document dirty!

    // Now save rev 2
    auto snap2 = doc.current_snapshot();
    auto save_res2 = SaveRecoveryService::atomic_save(target_file, *snap2);
    assert(save_res2.success);
    doc.mark_saved(save_res2.saved_epoch, save_res2.saved_revision);
    assert(!doc.is_dirty()); // Clean now!

    // Verify loading saved file
    std::string load_err;
    auto reloaded = SaveRecoveryService::load(target_file, &load_err);
    assert(reloaded != nullptr);
    assert(reloaded->revision() == 2);

    // Test recovery checkpoint
    std::string recovery_dir = (test_dir / "recovery").string();
    bool wrote_chk = SaveRecoveryService::write_recovery_checkpoint(recovery_dir, doc.id(), *reloaded);
    assert(wrote_chk);

    auto latest_chk = SaveRecoveryService::find_latest_checkpoint(recovery_dir, doc.id());
    assert(latest_chk.has_value());
    assert(latest_chk->revision == 2);

    SaveRecoveryService::cleanup_checkpoints(recovery_dir, doc.id());
    auto cleaned_chk = SaveRecoveryService::find_latest_checkpoint(recovery_dir, doc.id());
    assert(!cleaned_chk.has_value());

    ImageObject chunked_image;
    chunked_image.id = generate_object_id();
    chunked_image.name = "Chunked streaming save";
    chunked_image.width_px = 1024;
    chunked_image.height_px = 1024;
    chunked_image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
        static_cast<size_t>(chunked_image.width_px) * chunked_image.height_px,
        0xff456789u);
    assert(doc.execute_command(
        std::make_unique<AddObjectCommand>(chunked_image)).success);
    const auto chunked_path = (test_dir / "test_chunked_stream_save.pr").string();
    const auto chunked_save = SaveRecoveryService::atomic_save(
        chunked_path, *doc.current_snapshot());
    assert(chunked_save.success);
    const auto chunked_reopen = SaveRecoveryService::load(chunked_path, &load_err);
    assert(chunked_reopen && load_err.empty());
    const auto reopened_image = chunked_reopen->get_object(chunked_image.id);
    assert(reopened_image &&
        std::get<ImageObject>(*reopened_image).rgba_pixels().size() ==
            chunked_image.shared_pixels_rgba->size());

    std::filesystem::remove_all(test_dir);
}

int main() {
    std::cout << "[test_storage] Running storage tests...\n";
    test_native_format_roundtrip();
    test_native_chunked_compression_roundtrip();
    test_native_vector_payload_streams_as_legacy_chunked_container();
    test_native_chunk_decode_cancellation();
    test_native_container_above_legacy_size_limit();
    test_native_multipage_roundtrip();
    test_native_artboards_roundtrip_and_legacy_page_migration();
    test_raster_asset_reference_and_edit_graph_roundtrip();
    test_raster_edit_graph_v2_migrates_without_precision_payload();
    test_native_v12_units_migrate_to_implicit_millimeters();
    test_native_v5_migrates_to_one_page();
    test_atomic_save_and_dirty_tracking();
    std::cout << "[test_storage] All tests passed!\n";
    return 0;
}
