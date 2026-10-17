#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

static void test_add_and_undo_dimension_ac_d02() {
    Document doc("test-dim-commands");

    // Add a source box: (0, 0) to (100, 60)
    PathObject box;
    box.id = generate_object_id();
    box.name = "Source Box";
    box.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 60.0);
    doc.execute_command(std::make_unique<AddObjectCommand>(box, 0));
    assert(doc.objects().contains(box.id));

    // Create a dimension measuring top edge: (0, 60) to (100, 60)
    DimensionObject dim;
    dim.id = generate_object_id();
    dim.kind = DimensionKind::Linear;
    dim.linear_orientation = geometry::LinearOrientation::Horizontal;
    dim.placement_point = {50.0, 80.0};

    SourceReference ref1;
    ref1.source_object_id = box.id;
    ref1.ref_kind = SourceRefKind::ExplicitPoint;
    ref1.explicit_point = {0.0, 60.0};

    SourceReference ref2;
    ref2.source_object_id = box.id;
    ref2.ref_kind = SourceRefKind::ExplicitPoint;
    ref2.explicit_point = {100.0, 60.0};

    dim.references = {ref1, ref2};

    // Execute AddDimensionCommand
    const auto cmd_res = doc.execute_command(std::make_unique<AddDimensionCommand>(dim, 0));
    assert(cmd_res.success);
    assert(doc.objects().contains(dim.id));
    assert(DimensionRegistry::instance().contains(dim.id));

    // Verify PathObject properties in document
    const auto obj = doc.objects().get(dim.id);
    assert(obj.has_value());
    const auto* path = std::get_if<PathObject>(&(*obj));
    assert(path != nullptr);
    assert(path->has_stroke);

    // Test Undo
    bool undo_ok = doc.undo();
    assert(undo_ok);
    assert(!doc.objects().contains(dim.id));
    assert(!get_dimension(doc, dim.id).has_value());

    // Test Redo
    bool redo_ok = doc.redo();
    assert(redo_ok);
    assert(doc.objects().contains(dim.id));
    assert(get_dimension(doc, dim.id).has_value());
}

static void test_update_style_placement_relink_ac_d09() {
    Document doc("test-dim-updates");

    DimensionObject dim;
    dim.id = generate_object_id();
    dim.kind = DimensionKind::Aligned;
    dim.placement_point = {25.0, 50.0};

    SourceReference ref1;
    ref1.ref_kind = SourceRefKind::ExplicitPoint;
    ref1.explicit_point = {0.0, 0.0};
    SourceReference ref2;
    ref2.ref_kind = SourceRefKind::ExplicitPoint;
    ref2.explicit_point = {50.0, 0.0};
    dim.references = {ref1, ref2};

    doc.execute_command(std::make_unique<AddDimensionCommand>(dim, 0));

    // Update style
    DimensionStyle new_style = dim.style;
    new_style.line_width_mm = 0.5;
    new_style.arrow_type = geometry::ArrowType::ArchitecturalTick;
    const auto style_res = doc.execute_command(std::make_unique<UpdateDimensionStyleCommand>(dim.id, new_style));
    assert(style_res.success);

    auto reg_dim = get_dimension(doc, dim.id);
    assert(reg_dim.has_value());
    assert(reg_dim->style.line_width_mm == 0.5);

    // Undo style update
    doc.undo();
    reg_dim = get_dimension(doc, dim.id);
    assert(reg_dim.has_value());
    assert(reg_dim->style.line_width_mm == 0.25);
    doc.redo();

    // Update placement
    const auto place_res = doc.execute_command(std::make_unique<UpdateDimensionPlacementCommand>(dim.id, geometry::Point2D{25.0, 80.0}));
    assert(place_res.success);
    reg_dim = DimensionRegistry::instance().get(dim.id);
    assert(reg_dim->placement_point.y == 80.0);

    // Relink references
    SourceReference new_ref2;
    new_ref2.ref_kind = SourceRefKind::ExplicitPoint;
    new_ref2.explicit_point = {80.0, 0.0};
    const auto relink_res = doc.execute_command(std::make_unique<RelinkDimensionCommand>(dim.id, std::vector<SourceReference>{ref1, new_ref2}));
    assert(relink_res.success);
    reg_dim = DimensionRegistry::instance().get(dim.id);
    assert(reg_dim->references.size() == 2);
    assert(reg_dim->references[1].explicit_point.x == 80.0);

    // Remove dimension
    const auto rem_res = doc.execute_command(std::make_unique<RemoveDimensionCommand>(dim.id));
    assert(rem_res.success);
    assert(!doc.objects().contains(dim.id));
    assert(!get_dimension(doc, dim.id).has_value());

    // Undo removal
    doc.undo();
    assert(doc.objects().contains(dim.id));
    assert(get_dimension(doc, dim.id).has_value());
}

int main() {
    std::cout << "=== Running PikDraw Dimension Commands Tests ===" << std::endl;

    test_add_and_undo_dimension_ac_d02();
    std::cout << "[PASS] AC-D02: AddDimensionCommand & atomic Undo/Redo lifecycle" << std::endl;

    test_update_style_placement_relink_ac_d09();
    std::cout << "[PASS] AC-D09: Update style, placement, relink & RemoveDimensionCommand" << std::endl;

    std::cout << "All Dimension Commands Tests passed successfully!" << std::endl;
    return 0;
}
