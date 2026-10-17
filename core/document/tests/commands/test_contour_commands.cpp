#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/document/commands/command.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

int main() {
    std::cout << "=== Running PikDraw Document Contour Tests ===" << std::endl;

    // Test 1: ApplyContourCommand execution and undo/redo
    {
        Document doc("test-doc");
        clear_all_attached_contours();

        PathObject obj;
        obj.id = generate_object_id();
        obj.name = "Source Rect";
        obj.path = geometry::Path2D::make_rect(0.0, 0.0, 50.0, 50.0);

        auto add_res = doc.execute_command(std::make_unique<AddObjectCommand>(obj, 0));
        assert(add_res.success);

        ContourFeature feature;
        feature.direction = geometry::ContourDirection::Outside;
        feature.spacing_mm = 5.0;
        feature.steps = 2;
        feature.join_type = geometry::JoinType::Miter;

        auto apply_res = doc.execute_command(std::make_unique<ApplyContourCommand>(obj.id, feature));
        assert(apply_res.success);

        auto attached = get_object_contour(doc, obj.id);
        assert(attached.has_value());
        assert(attached->derived_loops.size() == 2);
        assert(attached->steps == 2);

        // Undo
        bool undo_ok = doc.undo();
        assert(undo_ok);
        assert(!get_object_contour(doc, obj.id).has_value());

        // Redo
        bool redo_ok = doc.redo();
        assert(redo_ok);
        assert(get_object_contour(doc, obj.id).has_value());
        assert(get_object_contour(doc, obj.id)->derived_loops.size() == 2);

        std::cout << "[PASS] apply_contour_command_lifecycle" << std::endl;
    }

    // Test 2: BreakContourApartCommand creates independent PathObjects with stable IDs and undo/redo
    {
        Document doc("test-break");
        clear_all_attached_contours();

        PathObject obj;
        obj.id = generate_object_id();
        obj.name = "Break Source";
        obj.path = geometry::Path2D::make_rect(10.0, 10.0, 40.0, 40.0);

        doc.execute_command(std::make_unique<AddObjectCommand>(obj, 0));

        ContourFeature feature;
        feature.direction = geometry::ContourDirection::Outside;
        feature.spacing_mm = 4.0;
        feature.steps = 2;
        feature.join_type = geometry::JoinType::Miter;

        doc.execute_command(std::make_unique<ApplyContourCommand>(obj.id, feature));

        auto break_cmd = std::make_unique<BreakContourApartCommand>(obj.id);
        auto break_res = doc.execute_command(std::move(break_cmd));
        assert(break_res.success);

        // Source contour link removed
        assert(!get_object_contour(doc, obj.id).has_value());

        // Document should now have 1 (source) + 2 (contours) = 3 objects in layer 0
        assert(doc.page().layers[0].object_ids.size() == 3);

        ObjectId contour1_id = doc.page().layers[0].object_ids[1];
        ObjectId contour2_id = doc.page().layers[0].object_ids[2];
        assert(contour1_id != obj.id);
        assert(contour2_id != obj.id);
        assert(contour1_id != contour2_id);

        auto c1_opt = doc.objects().get(contour1_id);
        assert(c1_opt.has_value());
        const auto* c1 = std::get_if<PathObject>(&(*c1_opt));
        assert(c1 != nullptr);
        assert(!c1->path.empty());
        // Step 1: 40 + 2*4 = 48 mm width
        assert(std::abs(c1->path.bounds().width - 48.0) < 1e-4);

        auto c2_opt = doc.objects().get(contour2_id);
        assert(c2_opt.has_value());
        const auto* c2 = std::get_if<PathObject>(&(*c2_opt));
        assert(c2 != nullptr);
        // Step 2: 40 + 2*8 = 56 mm width
        assert(std::abs(c2->path.bounds().width - 56.0) < 1e-4);

        // Undo Break Apart: created objects removed, source contour restored
        bool undo_ok = doc.undo();
        assert(undo_ok);
        assert(doc.page().layers[0].object_ids.size() == 1);
        assert(!doc.objects().get(contour1_id).has_value());
        assert(!doc.objects().get(contour2_id).has_value());
        assert(get_object_contour(doc, obj.id).has_value());

        // Redo Break Apart: same objects recreated with identical IDs
        bool redo_ok = doc.redo();
        assert(redo_ok);
        assert(doc.page().layers[0].object_ids.size() == 3);
        assert(doc.page().layers[0].object_ids[1] == contour1_id);
        assert(doc.page().layers[0].object_ids[2] == contour2_id);
        assert(doc.objects().get(contour1_id).has_value());
        assert(doc.objects().get(contour2_id).has_value());
        assert(!get_object_contour(doc, obj.id).has_value());

        std::cout << "[PASS] break_contour_apart_stable_ids_and_undo" << std::endl;
    }

    // Test 3: RemoveContourCommand
    {
        Document doc("test-remove");
        clear_all_attached_contours();

        PathObject obj;
        obj.id = generate_object_id();
        obj.path = geometry::Path2D::make_rect(0.0, 0.0, 20.0, 20.0);
        doc.execute_command(std::make_unique<AddObjectCommand>(obj, 0));

        ContourFeature feature;
        feature.direction = geometry::ContourDirection::Outside;
        feature.spacing_mm = 2.0;
        doc.execute_command(std::make_unique<ApplyContourCommand>(obj.id, feature));
        assert(get_object_contour(doc, obj.id).has_value());

        doc.execute_command(std::make_unique<RemoveContourCommand>(obj.id));
        assert(!get_object_contour(doc, obj.id).has_value());

        doc.undo();
        assert(get_object_contour(doc, obj.id).has_value());

        std::cout << "[PASS] remove_contour_command" << std::endl;
    }

    std::cout << "All PikDraw Document Contour Tests passed!" << std::endl;
    return 0;
}
