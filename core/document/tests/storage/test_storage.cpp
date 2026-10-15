#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
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
    test_atomic_save_and_dirty_tracking();
    std::cout << "[test_storage] All tests passed!\n";
    return 0;
}
