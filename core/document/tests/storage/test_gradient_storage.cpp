#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

PathObject make_authoring_path(ObjectId id, const std::string& name, const color::ColorValue& fill_color) {
    PathObject po;
    po.id = id;
    po.name = name;
    po.fill = fill_color;
    po.has_fill = true;
    po.fill_gradient = nullptr;

    EditablePath source;
    EditableSubpath sp;
    sp.id = SubpathId{id * 10000 + 1};
    sp.closed = true;
    sp.nodes = {
        PathNode{NodeId{id * 10000 + 2}, {0.0, 0.0}},
        PathNode{NodeId{id * 10000 + 3}, {100.0, 0.0}},
        PathNode{NodeId{id * 10000 + 4}, {100.0, 50.0}}
    };
    sp.segments = {
        EditableSegment{SegmentId{id * 10000 + 5}, PathSegmentKind::Line},
        EditableSegment{SegmentId{id * 10000 + 6}, PathSegmentKind::Line},
        EditableSegment{SegmentId{id * 10000 + 7}, PathSegmentKind::Line}
    };
    source.subpaths.push_back(sp);
    po.set_authoring_source(std::move(source));
    return po;
}

} // namespace

void test_v5_roundtrip_gradient() {
    reset_gradient_id_allocators_for_tests();

    Page page = Page::make_default();
    const auto id1 = generate_object_id();
    const auto id2 = generate_object_id();
    const auto id3 = generate_object_id();

    page.layers[0].object_ids = {id1, id2, id3};

    auto p1 = make_authoring_path(id1, "Linear Path", color::RGBColor{1, 0, 0, 1});
    auto p2 = make_authoring_path(id2, "Radial Path", color::RGBColor{0, 1, 0, 1});
    auto p3 = make_authoring_path(id3, "Solid Path", color::RGBColor{0, 0, 1, 1});

    // Linear gradient with 3 stops and custom midpoint
    GradientPaint linear;
    linear.id = 5001;
    linear.kind = GradientKind::Linear;
    linear.spread = GradientSpread::Reflect;
    linear.gradient_to_object = geometry::Transform2D::rotation_degrees(30.0);
    linear.stops = {
        GradientStop{6001, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.35},
        GradientStop{6002, 0.5, color::RGBColor{1, 1, 0, 1}, 0.8, 0.70},
        GradientStop{6003, 1.0, color::RGBColor{0, 0, 1, 1}, 0.5, 0.50}
    };
    assert(linear.is_valid());
    p1.fill_gradient = std::make_shared<const GradientPaint>(linear);

    // Radial gradient with hard edge (tie at 0.5)
    GradientPaint radial;
    radial.id = 5002;
    radial.kind = GradientKind::Radial;
    radial.spread = GradientSpread::Repeat;
    radial.gradient_to_object = geometry::Transform2D::scale(50.0, 30.0);
    radial.stops = {
        GradientStop{7001, 0.0, color::RGBColor{0, 0, 0, 1}, 1.0, 0.5},
        GradientStop{7002, 0.5, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{7003, 0.5, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5},
        GradientStop{7004, 1.0, color::RGBColor{1, 1, 1, 1}, 1.0, 0.5}
    };
    assert(radial.is_valid());
    p2.fill_gradient = std::make_shared<const GradientPaint>(radial);

    PersistentObjectMap objects;
    objects = objects.set(id1, p1);
    objects = objects.set(id2, p2);
    objects = objects.set(id3, p3);

    DocumentSnapshot snapshot(1, 10, page, objects, "doc-v5-test");

    // Serialize to v5
    auto bytes = NativeFormat::serialize(snapshot);
    assert(bytes.size() > 36);

    // Header check
    const auto* header = reinterpret_cast<const PikDrawHeader*>(bytes.data());
    assert(header->version == kArtboardsFormatVersion);

    // Deserialize
    std::string error;
    auto loaded = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    if (!loaded) {
        std::cerr << "Deserialization failed: " << error << "\n";
    }
    assert(loaded != nullptr);
    assert(loaded->epoch() == 1);
    assert(loaded->revision() == 10);
    assert(loaded->object_count() == 3);

    // Verify p1
    auto obj1 = loaded->get_object(id1);
    assert(obj1.has_value() && std::holds_alternative<PathObject>(*obj1));
    const auto& lp1 = std::get<PathObject>(*obj1);
    assert(lp1.name == "Linear Path");
    assert(lp1.fill_gradient != nullptr);
    assert(lp1.fill_gradient->same_payload(linear));

    // Verify p2
    auto obj2 = loaded->get_object(id2);
    assert(obj2.has_value() && std::holds_alternative<PathObject>(*obj2));
    const auto& lp2 = std::get<PathObject>(*obj2);
    assert(lp2.name == "Radial Path");
    assert(lp2.fill_gradient != nullptr);
    assert(lp2.fill_gradient->same_payload(radial));

    // Verify p3 (solid, no gradient)
    auto obj3 = loaded->get_object(id3);
    assert(obj3.has_value() && std::holds_alternative<PathObject>(*obj3));
    const auto& lp3 = std::get<PathObject>(*obj3);
    assert(lp3.fill_gradient == nullptr);
    assert(lp3.has_fill);

    std::cout << "[PASS] test_v5_roundtrip_gradient\n";
}

void test_malformed_records_rejection() {
    Page page = Page::make_default();
    const auto id1 = generate_object_id();
    page.layers[0].object_ids = {id1};
    auto p1 = make_authoring_path(id1, "Path", color::RGBColor{1, 0, 0, 1});

    GradientPaint linear = GradientPaint::make_default_linear(100, geometry::Rect2D{0, 0, 100, 50});
    p1.fill_gradient = std::make_shared<const GradientPaint>(linear);

    PersistentObjectMap objects;
    objects = objects.set(id1, p1);
    DocumentSnapshot snapshot(1, 1, page, objects, "doc-malformed-test");

    auto valid_bytes = NativeFormat::serialize(snapshot);

    // 1. Truncated file (< 36 bytes)
    std::string err;
    assert(NativeFormat::deserialize(valid_bytes.data(), 20, &err) == nullptr);

    // 2. Corrupt CRC32
    auto corrupt_crc = valid_bytes;
    corrupt_crc[valid_bytes.size() - 5] ^= 0xFF;
    assert(NativeFormat::deserialize(corrupt_crc.data(), corrupt_crc.size(), &err) == nullptr);

    // 3. Unknown container version (e.g. 99)
    auto bad_version = valid_bytes;
    bad_version[4] = 99;
    // Recompute CRC for bad_version container
    for (unsigned i = 0; i < 4; ++i) bad_version[32 + i] = 0;
    uint32_t new_crc = NativeFormat::calculate_crc32(bad_version.data(), bad_version.size());
    for (unsigned i = 0; i < 4; ++i) bad_version[32 + i] = static_cast<uint8_t>(new_crc >> (i * 8));
    assert(NativeFormat::deserialize(bad_version.data(), bad_version.size(), &err) == nullptr);

    // 4. Truncate payload by 10 bytes
    assert(NativeFormat::deserialize(valid_bytes.data(), valid_bytes.size() - 10, &err) == nullptr);

    std::cout << "[PASS] test_malformed_records_rejection\n";
}

int main() {
    try {
        test_v5_roundtrip_gradient();
        test_malformed_records_rejection();
        std::cout << "All test_gradient_storage checks PASSED.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception in test_gradient_storage: " << e.what() << "\n";
        return 1;
    }
}
