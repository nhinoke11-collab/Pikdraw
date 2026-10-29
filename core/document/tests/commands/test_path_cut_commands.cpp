#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/document/commands/node_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include <iostream>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

void test_replace_object_with_pieces_and_undo() {
    Document doc("doc-cut-test");
    // Add initial path
    PathObject original;
    original.id = generate_object_id();
    original.name = "Original";
    original.path.move_to(0.0, 0.0);
    original.path.line_to(100.0, 0.0);
    original.path.line_to(100.0, 50.0);
    original.path.line_to(0.0, 50.0);
    original.path.close();
    original.has_fill = true;
    const auto orig_id = original.id;

    auto add_cmd = std::make_unique<AddObjectCommand>(original, 0);
    require(doc.execute_command(std::move(add_cmd)).success, "add original path");
    require(doc.current_snapshot()->object_count() == 1, "one original object");

    // Create 2 pieces
    PathObject piece1;
    piece1.id = orig_id;
    piece1.name = "Piece 1";
    piece1.path.move_to(0.0, 0.0);
    piece1.path.line_to(40.0, 0.0);
    piece1.path.line_to(40.0, 50.0);
    piece1.path.line_to(0.0, 50.0);
    piece1.path.close();
    const auto p1_id = piece1.id;

    PathObject piece2;
    piece2.id = generate_object_id();
    piece2.name = "Piece 2";
    piece2.path.move_to(40.0, 0.0);
    piece2.path.line_to(100.0, 0.0);
    piece2.path.line_to(100.0, 50.0);
    piece2.path.line_to(40.0, 50.0);
    piece2.path.close();
    const auto p2_id = piece2.id;

    std::vector<ObjectVariant> pieces;
    pieces.push_back(std::move(piece1));
    pieces.push_back(std::move(piece2));

    auto replace_cmd = std::make_unique<ReplaceObjectWithPiecesCommand>(orig_id, std::move(pieces));
    auto res = doc.execute_command(std::move(replace_cmd));
    require(res.success, "replace target with pieces");

    // Verify snapshot has 2 objects, original is gone, pieces are present
    require(doc.current_snapshot()->object_count() == 2, "two resulting objects");
    require(doc.current_snapshot()->objects().get(orig_id).has_value(), "first piece keeps target ID");
    require(doc.current_snapshot()->objects().get(p1_id).has_value(), "first piece exists");
    require(doc.current_snapshot()->objects().get(p2_id).has_value(), "second piece exists");

    // Verify z-order in layer 0
    const auto& layer_ids = doc.current_snapshot()->page().layers[0].object_ids;
    require(layer_ids.size() == 2, "two objects remain in owner stack");
    require(layer_ids[0] == p1_id, "first piece keeps target stack position");
    require(layer_ids[1] == p2_id, "second piece follows target in stack");

    // Test Undo restores original
    require(doc.undo(), "undo replacement");
    require(doc.current_snapshot()->object_count() == 1, "undo restores object count");
    require(doc.current_snapshot()->objects().get(orig_id).has_value(), "undo restores target ID");
    require(!doc.current_snapshot()->objects().get(p2_id).has_value(), "undo removes second piece");

    // Test Redo
    require(doc.redo(), "redo replacement");
    require(doc.current_snapshot()->object_count() == 2, "redo restores two pieces");
    require(doc.current_snapshot()->objects().get(orig_id).has_value(), "redo restores target ID on a piece");
    require(doc.current_snapshot()->objects().get(p2_id).has_value(), "redo restores second piece");

    std::cout << "[PASS] test_replace_object_with_pieces_and_undo\n";
}

void test_replacement_rejects_collision_with_unrelated_object() {
    Document doc("doc-cut-id-collision");
    PathObject target;
    target.id = generate_object_id();
    target.path = geometry::Path2D::make_rect(0.0, 0.0, 10.0, 10.0);
    const auto target_id = target.id;
    PathObject unrelated;
    unrelated.id = generate_object_id();
    unrelated.path = geometry::Path2D::make_rect(20.0, 0.0, 10.0, 10.0);
    const auto unrelated_id = unrelated.id;
    require(doc.execute_command(std::make_unique<AddObjectCommand>(target, 0)).success,
            "add collision target");
    require(doc.execute_command(std::make_unique<AddObjectCommand>(unrelated, 0)).success,
            "add unrelated object");

    PathObject replacement_target = target;
    replacement_target.path = geometry::Path2D::make_rect(0.0, 0.0, 5.0, 10.0);
    PathObject collision = unrelated;
    collision.path = geometry::Path2D::make_rect(5.0, 0.0, 5.0, 10.0);
    std::vector<ObjectVariant> pieces;
    pieces.emplace_back(std::move(replacement_target));
    pieces.emplace_back(std::move(collision));
    const auto result = doc.execute_command(std::make_unique<ReplaceObjectWithPiecesCommand>(
        target_id, std::move(pieces), "Invalid ID Collision"));
    require(!result.success, "reject an ID owned by another object");
    require(doc.current_snapshot()->object_count() == 2, "failed command leaves document unchanged");
    require(doc.current_snapshot()->objects().get(target_id).has_value(), "target remains after rejected command");
    require(doc.current_snapshot()->objects().get(unrelated_id).has_value(), "unrelated object is not overwritten");
}

void test_smart_trim_removes_intersection_interval_atomically() {
    Document doc("doc-smart-trim-command");
    PathObject outline;
    outline.id = generate_object_id();
    outline.name = "Outline";
    outline.path.move_to(0.0, 0.0);
    outline.path.line_to(100.0, 0.0);
    outline.path.line_to(100.0, 100.0);
    outline.path.line_to(0.0, 100.0);
    outline.path.close();
    outline.has_fill = false;
    outline.has_stroke = true;
    const auto editable = editable_path_from_path2d(outline.path, nullptr);
    require(editable.has_value(), "build editable smart trim fixture");
    outline.set_authoring_source(*editable);
    const auto id = outline.id;
    const auto subpath_id = editable->subpaths.front().id;
    const auto segment_id = editable->subpaths.front().segments.front().id;
    require(doc.execute_command(std::make_unique<AddObjectCommand>(outline, 0)).success,
            "add smart trim fixture");

    const auto before = doc.current_snapshot();
    const auto result = doc.execute_command(std::make_unique<SmartTrimPathCommand>(
        id, subpath_id, SmartTrimBoundary{segment_id, 0.25},
        SmartTrimBoundary{segment_id, 0.75}, false));
    require(result.success, "smart trim one interval in a closed outline");
    const auto after = doc.current_snapshot();
    const auto trimmed = after->objects().get(id);
    require(trimmed && std::holds_alternative<PathObject>(*trimmed),
            "smart trim keeps the original object identity");
    const auto& trimmed_path = std::get<PathObject>(*trimmed);
    require(trimmed_path.authoring_source &&
                !trimmed_path.authoring_source->subpaths.front().closed &&
                trimmed_path.authoring_source->subpaths.front().segments.size() == 5 &&
                trimmed_path.authoring_source->subpaths.front().nodes.size() == 6,
            "smart trim opens the loop and preserves the untrimmed curve segments");
    require(after->revision() == before->revision() + 1,
            "smart trim commits one document revision");
    require(doc.undo(), "undo smart trim");
    const auto undone = std::get<PathObject>(*doc.current_snapshot()->objects().get(id));
    require(undone.authoring_source->subpaths.front().closed &&
                undone.authoring_source->subpaths.front().segments.size() == 4,
            "Undo restores the original closed outline");
    require(doc.redo(), "redo smart trim");
    const auto redone = std::get<PathObject>(*doc.current_snapshot()->objects().get(id));
    require(!redone.authoring_source->subpaths.front().closed &&
                redone.authoring_source->subpaths.front().segments.size() == 5,
            "Redo restores the trimmed open outline");
}

int main() {
    try {
        test_replace_object_with_pieces_and_undo();
        test_replacement_rejects_collision_with_unrelated_object();
        test_smart_trim_removes_intersection_interval_atomically();
        std::cout << "All path cut command tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Path cut command test failed: " << error.what() << '\n';
        return 1;
    }
}
