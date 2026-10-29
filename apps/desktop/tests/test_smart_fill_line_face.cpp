#include "../src/workflows/smart_fill_line_face.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace pikdraw;
using namespace pikdraw::desktop;
static void check(bool value, const char* label) {
    if (!value) { std::cerr << "[FAIL] " << label << '\n'; std::exit(1); }
}
static geometry::Path2D line(double x1,double y1,double x2,double y2) {
    geometry::Path2D path; path.move_to(x1,y1); path.line_to(x2,y2); return path;
}
int main() {
    std::vector<geometry::Path2D> paths{
        line(0,0,100,0),line(100,0,100,100),line(100,100,0,100),line(0,100,0,0)};
    auto face=detail::resolve_line_face(paths,{25,50},{});
    check(face.status==CutGeometryStatus::Success &&
          std::abs(face.path.bounds().width-100)<1e-8 &&
          std::abs(face.path.bounds().height-100)<1e-8,"four open lines enclose a face");
    check(detail::resolve_line_face(paths,{200,50},{}).status==CutGeometryStatus::NoChange,
          "outside stays unbounded");
    check(detail::resolve_line_face(paths,{0,50},{}).status==CutGeometryStatus::Ambiguous,
          "boundary click is ambiguous");
    paths.push_back(line(50,-10,50,110));
    face=detail::resolve_line_face(paths,{25,50},{});
    check(face.status==CutGeometryStatus::Success && std::abs(face.path.bounds().width-50)<1e-8,
          "crossing lines divide the face");
    paths.push_back(line(10,10,20,10)); paths.push_back(line(20,10,20,20));
    paths.push_back(line(20,20,10,20)); paths.push_back(line(10,20,10,10));
    check(detail::resolve_line_face(paths,{25,50},{}).status==CutGeometryStatus::Ambiguous,
          "nested interior boundaries do not silently lose holes");
    paths.resize(3);
    check(detail::resolve_line_face(paths,{25,50},{}).status==CutGeometryStatus::NoChange,
          "gap does not close implicitly");
    paths.push_back(line(0,0,50,0));
    check(detail::resolve_line_face(paths,{25,50},{}).status==CutGeometryStatus::Ambiguous,
          "overlapping segments are rejected");
}
