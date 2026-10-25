#include <chrono>
#include "pikdraw/document/commands/clip_commands.hpp"
#include "../test_support.hpp"
#include "pikdraw/vector/vector_controller.hpp"
using namespace pikdraw;
using namespace vector;
using namespace document;
DocumentSnapshot make_snapshot(Page page, const std::vector<ObjectVariant> &objects) {
    PersistentObjectMap map;
    for (const auto &object : objects) {
        const auto id = std::visit([](const auto &o) { return o.id; }, object);
        map = map.set(id, object);
    }
    return {1, 0, std::move(page), std::move(map), "selection"};
}
void geometry_and_painter_order() {
    auto bottom = ShapeFactory::create_rectangle({0, 0, 100, 100});
    auto top = ShapeFactory::create_rectangle({0, 0, 100, 100});
    bottom.id = 900;
    top.id = 100; // painter order must not be ID order
    auto page = Page::make_default();
    page.layers[0].object_ids = {900, 100};
    auto snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 100);
    top.locked = true;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    const auto visible_locked_hit =
        SelectionModel::hit_test_visible_detail({10, 10}, snapshot, 0);
    CHECK(visible_locked_hit && visible_locked_hit->id == 100);
    top.locked = false;
    top.visible = false;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    const auto hidden_hit =
        SelectionModel::hit_test_visible_detail({10, 10}, snapshot, 0);
    CHECK(hidden_hit && hidden_hit->id == 900);
    top.visible = true;
    page.layers[0].object_ids = {900};
    page.layers.push_back(Layer{"top", true, true, {100}});
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    const auto locked_layer_hit =
        SelectionModel::hit_test_visible_detail({10, 10}, snapshot, 0);
    CHECK(locked_layer_hit && locked_layer_hit->id == 100);
    page.layers[1].locked = false;
    page.layers[1].visible = false;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    PathObject triangle;
    triangle.id = 1;
    triangle.path.move_to(0, 0);
    triangle.path.line_to(100, 0);
    triangle.path.line_to(0, 100);
    triangle.path.close();
    triangle.has_stroke = false;
    page = Page::make_default();
    page.layers[0].object_ids = {1};
    snapshot = make_snapshot(page, {triangle});
    CHECK(!SelectionModel::hit_test({80, 80}, snapshot, 0)); // in bounds but outside source fill
    CHECK(SelectionModel::hit_test({10, 10}, snapshot, 0) == 1);
    triangle.has_fill = false;
    triangle.has_stroke = true;
    triangle.stroke_width_mm = 2;
    snapshot = make_snapshot(page, {triangle});
    CHECK(!SelectionModel::hit_test({20, 20}, snapshot, 0));
    CHECK(SelectionModel::hit_test({50, .5}, snapshot, 0) == 1);
    triangle.transform =
        geometry::Transform2D::translation(1000, 2000) * geometry::Transform2D::rotation_degrees(30);
    snapshot = make_snapshot(page, {triangle});
    CHECK(SelectionModel::hit_test(triangle.transform.map_point({50, .5}), snapshot, 0) == 1);
}
void winding_curves_and_quality() {
    PathObject p;
    p.id = 1;
    p.has_stroke = false;
    p.path = geometry::Path2D::make_rect(0, 0, 100, 100);
    const auto inner = geometry::Path2D::make_rect(25, 25, 50, 50);
    for (const auto &e : inner.elements()) {
        if (e.verb == geometry::PathVerb::MoveTo)
            p.path.move_to(e.pt);
        else if (e.verb == geometry::PathVerb::LineTo)
            p.path.line_to(e.pt);
        else if (e.verb == geometry::PathVerb::Close)
            p.path.close();
    }
    auto page = Page::make_default();
    page.layers[0].object_ids = {1};
    p.path.set_winding_rule(geometry::WindingRule::EvenOdd);
    auto snapshot = make_snapshot(page, {p});
    CHECK(!SelectionModel::hit_test({50, 50}, snapshot, 0));
    CHECK(SelectionModel::hit_test({10, 10}, snapshot, 0) == 1);
    p.path.set_winding_rule(geometry::WindingRule::NonZero);
    snapshot = make_snapshot(page, {p});
    CHECK(SelectionModel::hit_test({50, 50}, snapshot, 0) == 1);
    p = ShapeFactory::create_ellipse({50, 50}, 50, 50);
    p.id = 1;
    p.has_stroke = false;
    snapshot = make_snapshot(page, {p});
    CHECK(!SelectionModel::hit_test({1, 1}, snapshot, .01));
    const auto hit = SelectionModel::hit_test_detail({50, 50}, snapshot, 0);
    CHECK(hit && hit->quality == HitQuality::CurveApproximation);
    TextObject text;
    text.id = 2;
    text.text = "A";
    page.layers[0].object_ids = {2};
    snapshot = make_snapshot(page, {text});
    const auto text_hit = SelectionModel::hit_test_detail({.1, .1}, snapshot, 0);
    CHECK(text_hit && text_hit->quality == HitQuality::TextApproximation);
    CHECK(!SelectionModel::hit_test_detail({.1, .1}, snapshot, 0, false));
    SelectionModel selection;
    selection.select(1);
    selection.select(2, SelectionMode::Add);
    selection.select(1, SelectionMode::Toggle);
    CHECK(selection.count() == 1 && selection.is_selected(2));
}
void large_source_and_diagnostics() {
    PathObject path;
    path.id = 1;
    path.has_stroke = false;
    path.path.move_to(0, 0);
    for (int i = 1; i <= 100000; ++i)
        path.path.line_to(i * .01, (i % 2) * .1);
    path.path.line_to(1000, 100);
    path.path.line_to(0, 100);
    path.path.close();
    auto page = Page::make_default();
    page.layers[0].object_ids = {1};
    auto snapshot = make_snapshot(page, {path});
    CHECK(SelectionModel::hit_test({500, 50}, snapshot, 0) == 1);
    path.path.clear();
    path.path.move_to(0, 0);
    path.path.arc_to(10, 10, 0, false, true, 20, 0);
    snapshot = make_snapshot(page, {path});
    HitDiagnostics diagnostics;
    CHECK(!SelectionModel::hit_test_detail({10, 0}, snapshot, 0, true, &diagnostics));
    CHECK(diagnostics.unsupported_objects == 1);
    path = ShapeFactory::create_ellipse({50, 50}, 50, 50);
    path.id = 1;
    snapshot = make_snapshot(page, {path});
    CHECK(!SelectionModel::hit_test_detail({50, 50}, snapshot, 1e-300, true, &diagnostics));
    CHECK(diagnostics.approximation_budget_exceeded == 1);
    path.path.clear();
    path.authoring_source.reset();
    path.path.move_to(0, 0);
    path.path.cubic_to(100, 0, 100, 0, 1, 0); // collinear overshoot must not collapse to [0,1]
    path.has_fill = false;
    path.has_stroke = true;
    path.stroke_width_mm = 1;
    snapshot = make_snapshot(page, {path});
    CHECK(SelectionModel::hit_test({70, 0}, snapshot, 0) == 1);
}
void hierarchy_scope_pick_and_drag() {
    auto content=ShapeFactory::create_rectangle({0,0,20,20}); content.id=1001;
    auto frame=ShapeFactory::create_rectangle({0,0,8,8}); frame.id=1002;
    GroupObject group;group.id=1003;group.children_ids={1002,1001};group.clip=ClipProperties{};
    group.clip->mask_object_id=1002;group.transform=geometry::Transform2D::translation(30,20);
    auto page=Page::make_default();page.layers[0].object_ids={1003};
    auto snapshot=make_snapshot(page,{content,frame,group});
    CHECK(SelectionModel::hit_test({33,23},snapshot,0)==1003);
    CHECK(!SelectionModel::hit_test({45,35},snapshot,0));
    CHECK(SelectionModel::hit_test_in_scope({45,35},snapshot,1003,0)==1001);
    CHECK(SelectionModel::scope_roots(snapshot,1003)==std::vector<ObjectId>{1001});
    SelectionModel selection;selection.select(1003);
    CHECK(near(selection.bounds(snapshot)->x,30));
    CHECK(snapshot.hierarchy().roots(std::vector<ObjectId>{1003,1001})==std::vector<ObjectId>{1003});
    Document doc("scope-drag");
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(content)).success);
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(frame)).success);
    auto place=std::make_unique<ClipCommand>(ClipOperation::Place,frame.id,std::vector<ObjectId>{content.id});
    const auto clip=place->result_id();CHECK(doc.execute_command(std::move(place)).success);
    CHECK(doc.execute_command(std::make_unique<TransformObjectCommand>(clip,
        geometry::Transform2D::translation(30,20)*geometry::Transform2D::rotation_degrees(30))).success);
    const auto start=doc.current_snapshot()->hierarchy().find(content.id)->world.map_point({5,5});
    VectorController controller;controller.set_selection({content.id},*doc.current_snapshot());controller.set_selection_scope(clip);
    controller.on_resolved_pointer_down(start,*doc.current_snapshot(),content.id);
    CHECK(controller.gesture().is_active());
    controller.on_pointer_move(start+geometry::Point2D{10,7});
    CHECK(controller.on_pointer_up(start+geometry::Point2D{10,7},doc).success);
    const auto end=doc.current_snapshot()->hierarchy().find(content.id)->world.map_point({5,5});
    CHECK(near(end.x-start.x,10)&&near(end.y-start.y,7));
    CHECK(doc.undo());CHECK(doc.current_snapshot()->hierarchy().find(content.id)->world.map_point({5,5}).distance_to(start)<1e-8);
}
void scoped_index_matches_exhaustive_and_reports_latency() {
    auto frame=ShapeFactory::create_rectangle({0,0,100,100});frame.id=20001;frame.authoring_source.reset();
    GroupObject group;group.id=20002;group.children_ids={frame.id};group.clip=ClipProperties{};group.clip->mask_object_id=frame.id;
    std::vector<ObjectVariant> objects{frame};
    for(int i=0;i<10000;++i) {
        auto path=ShapeFactory::create_rectangle({double(i%100)*5,double(i/100)*5,3,3});path.id=30000+i;path.authoring_source.reset();
        group.children_ids.push_back(path.id);objects.emplace_back(path);
    }
    objects.emplace_back(group);auto page=Page::make_default();page.layers[0].object_ids={group.id};
    auto snapshot=make_snapshot(page,objects);VectorController controller;
    controller.set_selection({},snapshot);controller.set_selection_scope(group.id);
    (void)controller.pick({1,1},snapshot,0);
    std::vector<double> times;
    for(int i=0;i<200;++i) {
        const geometry::Point2D point{double((i*31)%100)*5+1,double((i*17)%100)*5+1};
        const auto start=std::chrono::steady_clock::now();const auto hit=controller.pick(point,snapshot,0);
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        const auto oracle=SelectionModel::hit_test_in_scope(point,snapshot,group.id,0);
        CHECK(hit&&oracle&&hit->id==*oracle);
    }
    std::sort(times.begin(),times.end());
    std::cout << "PowerClip 10k indexed pick median_ms=" << times[times.size()/2]
              << " p95_ms=" << times[times.size()*95/100] << " (Debug; exhaustive parity 200/200)\n";
}

int main() {
    return run([] {
        scoped_index_matches_exhaustive_and_reports_latency();
        hierarchy_scope_pick_and_drag();
        geometry_and_painter_order();
        winding_curves_and_quality();
        large_source_and_diagnostics();
    });
}
