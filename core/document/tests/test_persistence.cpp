#include "test_harness.hpp"
#include "pikdraw/document/persistence.hpp"
#include "pikdraw/document/document.hpp"
#include <filesystem>
#include <chrono>

using namespace pikdraw::document;
using namespace pikdraw::geometry;

PIK_TEST_CASE(vector_save_dirty_newer) {
    // Contract vector: save-dirty-newer
    // save snapshot r=10 -> edit -> 11 -> save10 completes -> current11 remains dirty
    Document doc("doc-dirty-test");
    auto obj = std::make_shared<PathObject>();
    obj->id = doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(obj));
    for (int i = 0; i < 9; ++i) {
        doc.execute_command(std::make_unique<TransformObjectCommand>(obj->id, Transform2D::translation(1, 0)));
    }
    PIK_CHECK_EQUAL(doc.revision(), 10);
    const auto snap10 = doc.create_snapshot();

    // Edit to 11
    doc.execute_command(std::make_unique<TransformObjectCommand>(obj->id, Transform2D::translation(5, 0)));
    PIK_CHECK_EQUAL(doc.revision(), 11);
    PIK_CHECK(doc.is_dirty());

    // Save10 completes
    doc.notify_save_completed(snap10->epoch(), snap10->revision());

    // Document must remain dirty because revision 11 was not saved
    PIK_CHECK(doc.is_dirty());
}

PIK_TEST_CASE(persistence_atomic_roundtrip_and_fault_injection) {
    const auto test_dir = std::filesystem::current_path() /
        ("test_output_persistence-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    PIK_CHECK(std::filesystem::create_directory(test_dir));
    const auto file_path = test_dir / "sample.pikdraw";

    Document doc("doc-persist-test");
    auto text_obj = std::make_shared<TextObject>();
    text_obj->id = doc.allocate_id();
    text_obj->utf8_text = "PikDraw Vector Editor - Tiếng Việt Có Dấu";
    text_obj->fill_color = ColorData::make_cmyk(0.1f, 0.8f, 0.9f, 0.05f, "Fogra39");
    text_obj->stroke_color = ColorData::make_spot("Pantone 185 C", 1.0f);
    text_obj->stroke_color.overprint = true;
    doc.execute_command(std::make_unique<InsertObjectCommand>(text_obj));

    const auto snap = doc.create_snapshot();

    // Successful save
    auto res = PersistenceManager::save_snapshot(snap, file_path);
    PIK_CHECK(res.success);
    PIK_CHECK(std::filesystem::exists(file_path));

    // Load and verify metadata fidelity
    auto load_res = PersistenceManager::load_document(file_path);
    PIK_CHECK(load_res.success);
    PIK_CHECK(load_res.document != nullptr);
    auto loaded_snap = load_res.document->create_snapshot();
    auto loaded_obj = loaded_snap->get_object(text_obj->id);
    PIK_CHECK(loaded_obj != nullptr);
    PIK_CHECK(loaded_obj->type() == ObjectType::Text);
    auto* loaded_text = static_cast<const TextObject*>(loaded_obj.get());
    PIK_CHECK_EQUAL(loaded_text->utf8_text, text_obj->utf8_text);
    PIK_CHECK_EQUAL(loaded_text->fill_color.icc_profile_name, "Fogra39");
    PIK_CHECK_EQUAL(loaded_text->stroke_color.spot_name, "Pantone 185 C");
    PIK_CHECK(loaded_text->stroke_color.overprint);

    // Fault injection before replace: original file must remain intact
    auto fault_res = PersistenceManager::save_snapshot(snap, file_path, FaultInjectionPoint::BeforeReplace);
    PIK_CHECK(!fault_res.success);
    PIK_CHECK(std::filesystem::exists(file_path));

    // Re-verify loaded data after failed replace
    auto reload = PersistenceManager::load_document(file_path);
    PIK_CHECK(reload.success);

    std::error_code ec;
    std::filesystem::remove(file_path, ec);
    std::filesystem::remove(test_dir, ec);
}
