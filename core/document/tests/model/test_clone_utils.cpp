#include "pikdraw/document/model/clone_utils.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/validation.hpp"
#include <cassert>
#include <iostream>
#include <unordered_set>

using namespace pikdraw;
using namespace pikdraw::document;

void test_clone_editable_path() {
    EditablePath path;
    path.winding_rule = geometry::WindingRule::EvenOdd;

    EditableSubpath sp;
    sp.id = generate_path_element_id<SubpathId>();
    sp.closed = true;

    PathNode n1;
    n1.id = generate_path_element_id<NodeId>();
    n1.anchor = {0.0, 0.0};
    n1.mode = PathNodeMode::Corner;

    PathNode n2;
    n2.id = generate_path_element_id<NodeId>();
    n2.anchor = {100.0, 0.0};
    n2.mode = PathNodeMode::Corner;

    PathNode n3;
    n3.id = generate_path_element_id<NodeId>();
    n3.anchor = {100.0, 50.0};
    n3.mode = PathNodeMode::Corner;

    sp.nodes = {n1, n2, n3};

    EditableSegment s1{generate_path_element_id<SegmentId>(), PathSegmentKind::Line, {}, {}};
    EditableSegment s2{generate_path_element_id<SegmentId>(), PathSegmentKind::Line, {}, {}};
    EditableSegment s3{generate_path_element_id<SegmentId>(), PathSegmentKind::Line, {}, {}};
    sp.segments = {s1, s2, s3};

    path.subpaths.push_back(sp);

    assert(!path.validation_error().has_value());

    const auto cloned = clone_editable_path(path);
    assert(!cloned.validation_error().has_value());
    assert(cloned.winding_rule == geometry::WindingRule::EvenOdd);
    assert(cloned.subpaths.size() == 1);

    const auto& c_sp = cloned.subpaths[0];
    assert(c_sp.id.valid());
    assert(c_sp.id != sp.id);
    assert(c_sp.nodes.size() == 3);
    assert(c_sp.segments.size() == 3);

    for (size_t i = 0; i < 3; ++i) {
        assert(c_sp.nodes[i].id.valid());
        assert(c_sp.nodes[i].id != sp.nodes[i].id);
        assert(c_sp.nodes[i].anchor == sp.nodes[i].anchor);

        assert(c_sp.segments[i].id.valid());
        assert(c_sp.segments[i].id != sp.segments[i].id);
    }

    std::cout << "test_clone_editable_path: PASS\n";
}

void test_asset_backed_raster_model_and_clone() {
    ImageObject image = ImageObject::make_procedural_photo(generate_object_id(), 1, 2, 30, 20);
    image.source.frame_count = 300;
    image.source.frame_index = 299;
    image.asset_source = RasterSourceDescriptor{
        RasterAssetId{0x1234, 0x5678}, 7, RasterStorageMode::Linked, UINT64_C(0x100000001),
        "image/tiff", "file:///assets/large.tif", std::string(64, 'a')};
    image.edit_graph.nodes = {
        RasterEditNode{RasterEditNodeId{101}, true, RasterCropParams{0.1, 0.2, 0.7, 0.6}},
        RasterEditNode{RasterEditNodeId{102}, true, RasterFlipParams{true, false}},
        RasterEditNode{RasterEditNodeId{103}, true, RasterRotateParams{90.0}},
        RasterEditNode{RasterEditNodeId{104}, true, RasterExposureParams{1.25}}};
    RasterAdjustmentMask local_mask;
    local_mask.id = 505;
    local_mask.width_px = 12;
    local_mask.height_px = 21;
    local_mask.kind = RasterAdjustmentMaskKind::Rectangle;
    local_mask.left = 0.1;
    local_mask.top = 0.2;
    local_mask.width = 0.5;
    local_mask.height = 0.5;
    local_mask.feather_px = 2.0;
    image.edit_graph.nodes.back().mask = local_mask;
    image.shared_pixels_rgba =
        std::make_shared<const std::vector<uint32_t>>(std::move(image.pixels_rgba));
    validate_object_source(image.id, ObjectVariant{image});

    const auto clone_variant = clone_object(ObjectVariant{image});
    const auto& clone = std::get<ImageObject>(clone_variant);
    assert(clone.id != image.id);
    assert(clone.asset_source->asset_id == image.asset_source->asset_id);
    assert(clone.asset_source->revision == image.asset_source->revision);
    assert(clone.asset_source->encoded_byte_length == UINT64_C(0x100000001));
    assert(clone.source.frame_count == 300 && clone.source.frame_index == 299);
    assert(clone.edit_graph.nodes.size() == image.edit_graph.nodes.size());
    for (size_t i = 0; i < image.edit_graph.nodes.size(); ++i)
        assert(clone.edit_graph.nodes[i].id == image.edit_graph.nodes[i].id);
    assert(clone.edit_graph.nodes.back().mask == local_mask);
    assert(clone.rgba_pixels().data() == image.rgba_pixels().data());

    auto expect_invalid = [](ImageObject invalid) {
        bool rejected = false;
        try { validate_object_source(invalid.id, ObjectVariant{std::move(invalid)}); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    };
    auto invalid = image;
    invalid.asset_source->asset_id = {};
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.asset_source->revision = 0;
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.asset_source->mode = RasterStorageMode::Linked;
    invalid.asset_source->locator.clear();
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.asset_source->fingerprint_sha256 = "not-a-hash";
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.edit_graph.nodes[1].id = invalid.edit_graph.nodes[0].id;
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.edit_graph.nodes[0].parameters = RasterCropParams{0.8, 0.0, 0.3, 1.0};
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.edit_graph.nodes[2].parameters = RasterRotateParams{std::numeric_limits<double>::infinity()};
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.edit_graph.nodes[3].parameters = RasterExposureParams{33.0};
    expect_invalid(std::move(invalid));
    invalid = image;
    invalid.edit_graph.schema_version++;
    expect_invalid(std::move(invalid));
}

class SetupGroupTestCommand final : public DocumentCommand {
public:
    SetupGroupTestCommand(ObjectId p1_id, PathObject p1, ObjectId p2_id, PathObject p2, ObjectId grp_id, GroupObject grp)
        : p1_id_(p1_id), p1_(std::move(p1)), p2_id_(p2_id), p2_(std::move(p2)), grp_id_(grp_id), grp_(std::move(grp)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Setup Group Test"; }
    CommandResult execute(Document& doc) override {
        Page page = doc.page();
        page.layers[0].object_ids = {grp_id_};
        PersistentObjectMap map = doc.objects().set(p1_id_, p1_).set(p2_id_, p2_).set(grp_id_, grp_);
        doc.apply_mutation(std::move(page), std::move(map));
        return CommandResult::ok();
    }
    CommandResult undo(Document&) override { return CommandResult::ok(); }
private:
    ObjectId p1_id_;
    PathObject p1_;
    ObjectId p2_id_;
    PathObject p2_;
    ObjectId grp_id_;
    GroupObject grp_;
};

void test_clone_hierarchy_and_groups() {
    Document doc;
    const auto p1_id = generate_object_id();
    const auto p2_id = generate_object_id();

    PathObject p1;
    p1.id = p1_id;
    p1.path.move_to(0.0, 0.0);
    p1.path.line_to(50.0, 0.0);
    p1.path.line_to(50.0, 50.0);
    p1.path.close();

    PathObject p2;
    p2.id = p2_id;
    p2.path.move_to(60.0, 0.0);
    p2.path.line_to(110.0, 0.0);
    p2.path.line_to(110.0, 50.0);
    p2.path.close();

    const auto grp_id = generate_object_id();
    GroupObject grp;
    grp.id = grp_id;
    grp.children_ids = {p1_id, p2_id};

    const auto res = doc.execute_command(std::make_unique<SetupGroupTestCommand>(
        p1_id, std::move(p1), p2_id, std::move(p2), grp_id, std::move(grp)));
    assert(res.success);

    const auto snapshot = doc.current_snapshot();
    const auto shift = geometry::Transform2D::translation(20.0, 30.0);

    const auto result = clone_hierarchy({grp_id}, *snapshot, shift);
    assert(result.top_level_cloned_ids.size() == 1);
    const auto new_grp_id = result.top_level_cloned_ids[0];
    assert(new_grp_id != grp_id);
    assert(result.cloned_objects.size() == 3); // 2 children + 1 group

    std::unordered_set<ObjectId> all_new_ids;
    for (const auto& obj : result.cloned_objects) {
        const auto id = std::visit([](const auto& o) { return o.id; }, obj);
        assert(id != kInvalidObjectId);
        assert(id != p1_id);
        assert(id != p2_id);
        assert(id != grp_id);
        assert(all_new_ids.insert(id).second); // Must be unique
    }

    // Check that the cloned group references the newly cloned children IDs
    for (const auto& obj : result.cloned_objects) {
        if (const auto* g = std::get_if<GroupObject>(&obj)) {
            assert(g->id == new_grp_id);
            assert(g->children_ids.size() == 2);
            assert(g->children_ids[0] == result.id_map.at(p1_id));
            assert(g->children_ids[1] == result.id_map.at(p2_id));
        }
    }

    std::cout << "test_clone_hierarchy_and_groups: PASS\n";
}

int main() {
    test_clone_editable_path();
    test_asset_backed_raster_model_and_clone();
    test_clone_hierarchy_and_groups();
    std::cout << "All clone_utils tests PASSED.\n";
    return 0;
}
