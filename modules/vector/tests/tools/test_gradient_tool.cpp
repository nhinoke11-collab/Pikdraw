#include "pikdraw/vector/tools/gradient_tool.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::vector::tools;

namespace {

PathObject make_test_path(ObjectId id, double x, double y, double w, double h) {
  PathObject po;
  po.id = id;
  po.path = geometry::Path2D::make_rect(x, y, w, h);
  po.has_fill = true;
  po.has_stroke = false;
  po.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  return po;
}

std::unique_ptr<Document> create_test_doc(ObjectId id) {
  Page page = Page::make_default();
  page.layers[0].object_ids = {id};

  PersistentObjectMap map;
  map = map.set(id, make_test_path(id, 10, 10, 80, 40));
  DocumentSnapshot snap(1, 1, page, map, "test-doc");
  return std::make_unique<Document>(snap);
}

} // namespace

void test_drag_creation_and_threshold() {
  const auto id = generate_object_id();
  auto doc = create_test_doc(id);
  const auto base_rev = doc->current_snapshot()->revision();

  GradientTool tool(*doc);
  tool.set_target_objects({id});
  assert(!tool.is_editing());

  // 1. Click below threshold (0.2 mm drag)
  tool.on_pointer_down({20.0, 20.0}, false, false);
  assert(tool.is_editing());
  tool.on_pointer_move({20.1, 20.1}, false, false);
  bool committed = tool.on_pointer_up({20.1, 20.1});
  assert(!committed); // Under threshold: reject implicit commit (AC-G08)
  assert(!tool.is_editing());
  assert(doc->current_snapshot()->revision() == base_rev); // Document not dirtied

  // 2. Drag above threshold (50 mm drag)
  tool.on_pointer_down({10.0, 10.0}, false, false);
  tool.on_pointer_move({60.0, 10.0}, false, false);
  committed = tool.on_pointer_up({60.0, 10.0});
  assert(committed);
  assert(!tool.is_editing());
  assert(doc->current_snapshot()->revision() == base_rev + 1); // Exactly one commit

  // Verify object now has gradient fill
  const auto snap = doc->current_snapshot();
  const auto obj_variant = snap->get_object(id);
  assert(obj_variant.has_value());
  const auto &po = std::get<PathObject>(*obj_variant);
  assert(po.fill_gradient != nullptr);

  std::cout << "[PASS] test_drag_creation_and_threshold\n";
}

void test_shift_15_deg_snap() {
  const auto id = generate_object_id();
  auto doc = create_test_doc(id);

  GradientTool tool(*doc);
  tool.set_target_objects({id});

  // Drag with shift at roughly ~17 degrees
  // 17 deg is closer to 15 deg than 30 deg
  tool.on_pointer_down({0.0, 0.0}, true, false);
  const double rad17 = 17.0 * (3.14159265358979323846 / 180.0);
  tool.on_pointer_move({100.0 * std::cos(rad17), 100.0 * std::sin(rad17)}, true, false);

  const auto *paint = tool.draft_paint();
  assert(paint != nullptr);
  // Column X direction: (a, b)
  const double angle = std::atan2(paint->gradient_to_object.b, paint->gradient_to_object.a);
  const double angle_deg = angle * (180.0 / 3.14159265358979323846);
  assert(std::abs(angle_deg - 15.0) < 1e-4);

  tool.on_cancel();
  assert(!tool.is_editing());

  std::cout << "[PASS] test_shift_15_deg_snap\n";
}

void test_cancel_and_stale_discard() {
  const auto id = generate_object_id();
  auto doc = create_test_doc(id);
  const auto base_rev = doc->current_snapshot()->revision();

  GradientTool tool(*doc);
  tool.set_target_objects({id});
  const uint64_t gen0 = tool.draft_generation();

  tool.on_pointer_down({10.0, 10.0}, false, false);
  assert(tool.draft_generation() > gen0);
  tool.on_pointer_move({80.0, 80.0}, false, false);

  // Esc / Cancel / Stale event
  tool.on_cancel();
  assert(!tool.is_editing());
  assert(doc->current_snapshot()->revision() == base_rev);

  std::cout << "[PASS] test_cancel_and_stale_discard\n";
}

void test_handle_manipulation() {
  const auto id = generate_object_id();
  auto doc = create_test_doc(id);

  GradientTool tool(*doc);
  tool.set_target_objects({id});

  // Create initial linear gradient
  tool.on_pointer_down({10.0, 20.0}, false, false);
  tool.on_pointer_move({90.0, 20.0}, false, false);
  tool.on_pointer_up({90.0, 20.0});

  // Get handles
  const auto handles = tool.get_handles();
  assert(handles.size() >= 4); // Start, End, 2 stops

  // Hit test near end handle (90, 20)
  const auto hit = tool.hit_test_handles({90.5, 20.2}, 2.0);
  assert(hit.kind == GradientHandleKind::LinearEnd);

  // Drag end handle to (100, 20)
  tool.on_pointer_down({90.0, 20.0}, false, false);
  assert(tool.state() == GradientToolState::DraggingHandle);
  tool.on_pointer_move({100.0, 20.0}, false, false);
  bool ok = tool.on_pointer_up({100.0, 20.0});
  assert(ok);

  std::cout << "[PASS] test_handle_manipulation\n";
}

void test_batch_apply() {
  const auto id1 = generate_object_id();
  const auto id2 = generate_object_id();

  Page page = Page::make_default();
  page.layers[0].object_ids = {id1, id2};

  PersistentObjectMap map;
  map = map.set(id1, make_test_path(id1, 10, 10, 40, 40));
  map = map.set(id2, make_test_path(id2, 60, 10, 40, 40));
  DocumentSnapshot snap(1, 1, page, map, "batch-doc");
  Document doc(snap);

  const auto base_rev = doc.current_snapshot()->revision();

  GradientTool tool(doc);
  tool.set_target_objects({id1, id2});

  tool.on_pointer_down({10.0, 10.0}, false, false);
  tool.on_pointer_move({80.0, 50.0}, false, false);
  bool ok = tool.on_pointer_up({80.0, 50.0});
  assert(ok);
  assert(doc.current_snapshot()->revision() == base_rev + 1); // Single atomic undo

  // Both objects should have gradient
  const auto snap2 = doc.current_snapshot();
  const auto po1 = std::get<PathObject>(*snap2->get_object(id1));
  const auto po2 = std::get<PathObject>(*snap2->get_object(id2));
  assert(po1.fill_gradient != nullptr);
  assert(po2.fill_gradient != nullptr);

  std::cout << "[PASS] test_batch_apply\n";
}

int main() {
  try {
    test_drag_creation_and_threshold();
    test_shift_15_deg_snap();
    test_cancel_and_stale_discard();
    test_handle_manipulation();
    test_batch_apply();
    std::cout << "All test_gradient_tool checks PASSED.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Exception in test_gradient_tool: " << e.what() << "\n";
    return 1;
  }
}
