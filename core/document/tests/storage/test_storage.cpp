#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <filesystem>

using namespace pikdraw;
using namespace pikdraw::document;

void test_native_format_roundtrip() {
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
    assert(bytes[4] == kCurrentFormatVersion);
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

void test_native_v5_migrates_to_one_page() {
    Document doc("doc-v5-compat");
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(2.0, 3.0, 20.0, 10.0);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);
    const auto current = NativeFormat::serialize(*doc.current_snapshot());

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
    const size_t page_record = prefix_end + 4 + 8;
    const auto page_name_length = read_u32(page_record + 8);
    const size_t legacy_page_body = page_record + 8 + 4 + page_name_length;
    std::vector<uint8_t> legacy(current.begin(), current.begin() + 36);
    legacy.insert(legacy.end(), current.begin() + 36, current.begin() + prefix_end);
    legacy.insert(legacy.end(), current.begin() + legacy_page_body, current.end());
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
    assert(reopened->object_count() == 1);
    assert(reopened->contains_object(path.id));
}

void test_atomic_save_and_dirty_tracking() {
    Document doc("doc-dirty-test");
    std::string test_dir = "build/test_storage_tmp";
    std::filesystem::create_directories(test_dir);
    std::string target_file = test_dir + "/test_atomic.pkdr";

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
    std::string recovery_dir = test_dir + "/recovery";
    bool wrote_chk = SaveRecoveryService::write_recovery_checkpoint(recovery_dir, doc.id(), *reloaded);
    assert(wrote_chk);

    auto latest_chk = SaveRecoveryService::find_latest_checkpoint(recovery_dir, doc.id());
    assert(latest_chk.has_value());
    assert(latest_chk->revision == 2);

    SaveRecoveryService::cleanup_checkpoints(recovery_dir, doc.id());
    auto cleaned_chk = SaveRecoveryService::find_latest_checkpoint(recovery_dir, doc.id());
    assert(!cleaned_chk.has_value());

    std::filesystem::remove_all(test_dir);
}

int main() {
    std::cout << "[test_storage] Running storage tests...\n";
    test_native_format_roundtrip();
    test_native_multipage_roundtrip();
    test_native_v5_migrates_to_one_page();
    test_atomic_save_and_dirty_tracking();
    std::cout << "[test_storage] All tests passed!\n";
    return 0;
}
