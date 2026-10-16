#include "pikdraw/interop/adapters/dxf_importer.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <variant>

using namespace pikdraw::interop;

namespace {

const std::string kDxf = R"DXF(0
SECTION
2
HEADER
9
$INSUNITS
70
4
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
2
0
LAYER
2
CUT
70
4
62
2
6
CONTINUOUS
0
LAYER
2
HIDDEN
70
1
62
3
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
LINE
5
A1
8
CUT
10
0
20
0
11
100
21
50
0
LWPOLYLINE
5
A2
8
CUT
90
3
70
1
10
0
20
0
42
1
10
10
20
0
10
10
20
10
0
ARC
5
A3
8
HIDDEN
10
5
20
5
40
2
50
0
51
90
0
CIRCLE
5
A4
8
CUT
10
20
20
20
40
5
0
ENDSEC
0
EOF
)DXF";

std::string ReplaceOnce(std::string source, const std::string &from,
                        const std::string &to) {
  const size_t position = source.find(from);
  assert(position != std::string::npos);
  source.replace(position, from.size(), to);
  return source;
}

void AssertRejected(const std::string &source) {
  const auto result = DxfImporter::import_from_string(source);
  assert(!result.success);
  assert(!result.report.is_clean());
  assert(result.report.has_unsupported());
  assert(result.objects.empty());
  assert(result.source_ids.empty());
}

void TestLayersUnitsAndSourceCurves() {
  const auto result = DxfImporter::import_from_string(kDxf);
  if (!result.success)
    std::cerr << "DXF fixture rejected: " << result.error_message << '\n';
  assert(result.success);
  assert(result.page.layers.size() == 2);
  assert(result.layer_source_ids.size() == 2);
  assert(result.page.layers[0].name == "CUT");
  assert(result.page.layers[0].locked);
  assert(!result.page.layers[1].visible);
  assert(result.objects.size() == 4);
  assert(result.source_ids.size() == 4);
  assert(result.page.layers[0].object_ids.size() == 3);
  assert(result.page.layers[1].object_ids.size() == 1);

  const auto &line = std::get<pikdraw::document::PathObject>(result.objects[0]);
  assert(line.path.size() == 2);
  assert(line.has_stroke && !line.has_fill);
  assert(line.locked && line.visible);
  assert(std::abs(line.path.elements()[1].pt.x - 100.0) < 1.0e-9);
  assert(std::abs(line.path.elements()[1].pt.y - 50.0) < 1.0e-9);

  const auto &polyline =
      std::get<pikdraw::document::PathObject>(result.objects[1]);
  assert(polyline.path.elements().size() == 5);
  assert(polyline.path.elements()[1].verb ==
         pikdraw::geometry::PathVerb::ArcTo);
  assert(polyline.path.elements()[1].sweep);
  assert(polyline.path.elements().back().verb ==
         pikdraw::geometry::PathVerb::Close);

  const auto &arc = std::get<pikdraw::document::PathObject>(result.objects[2]);
  assert(arc.path.elements().size() == 2);
  assert(!arc.visible);
  assert(arc.path.elements()[1].verb == pikdraw::geometry::PathVerb::ArcTo);
  assert(arc.path.elements()[1].sweep);

  const auto &circle =
      std::get<pikdraw::document::PathObject>(result.objects[3]);
  assert(circle.path.elements().size() == 4);
  assert(circle.path.elements()[1].verb == pikdraw::geometry::PathVerb::ArcTo);
  assert(circle.path.elements()[2].verb == pikdraw::geometry::PathVerb::ArcTo);
  assert(circle.path.elements().back().verb ==
         pikdraw::geometry::PathVerb::Close);
}

void TestUnitConversionAndAciWarning() {
  const std::string inch_dxf =
      ReplaceOnce(kDxf, "70\n4\n0\nENDSEC", "70\n1\n0\nENDSEC");
  const auto inches = DxfImporter::import_from_string(inch_dxf);
  assert(inches.success);
  const auto &line = std::get<pikdraw::document::PathObject>(inches.objects[0]);
  assert(std::abs(line.path.elements()[1].pt.x - 2540.0) < 1.0e-9);
  assert(std::abs(line.path.elements()[1].pt.y - 1270.0) < 1.0e-9);

  const std::string aci7 =
      ReplaceOnce(kDxf, "62\n2\n6\nCONTINUOUS", "62\n7\n6\nCONTINUOUS");
  const auto warned = DxfImporter::import_from_string(aci7);
  assert(warned.success);
  assert(warned.report.issue_count() > 0);
  assert(!warned.report.has_unsupported());

  std::string unicode_layer = kDxf;
  size_t position = 0;
  while ((position = unicode_layer.find("CUT", position)) !=
         std::string::npos) {
    unicode_layer.replace(position, 3, "CẮT");
    position += std::string("CẮT").size();
  }
  const auto unicode = DxfImporter::import_from_string(unicode_layer);
  assert(unicode.success);
  assert(unicode.page.layers[0].name == "CẮT");
}

void TestUnsupportedAndMalformedSourcesRejectWholeDrawing() {
  AssertRejected(ReplaceOnce(kDxf, "70\n4\n0\nENDSEC", "70\n0\n0\nENDSEC"));
  AssertRejected(ReplaceOnce(kDxf, "70\n4\n0\nENDSEC", "70\n7\n0\nENDSEC"));
  AssertRejected(ReplaceOnce(kDxf, "0\nCIRCLE\n5\nA4", "0\nELLIPSE\n5\nA4"));
  AssertRejected(ReplaceOnce(kDxf, "21\n50\n0\nLWPOLYLINE",
                             "21\n50\n31\n1\n0\nLWPOLYLINE"));
  AssertRejected(
      ReplaceOnce(kDxf, "8\nCUT\n10\n0", "8\nCUT\n420\n16711680\n10\n0"));
  AssertRejected(ReplaceOnce(kDxf, "6\nCONTINUOUS\n0\nLAYER\n2\nHIDDEN",
                             "6\nDASHED\n0\nLAYER\n2\nHIDDEN"));
  AssertRejected(ReplaceOnce(kDxf, "21\n50\n0\nLWPOLYLINE",
                             "21\n50\n39\n1\n0\nLWPOLYLINE"));
  AssertRejected(
      std::string("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nEOF\n"));
}

} // namespace

int main() {
  TestLayersUnitsAndSourceCurves();
  TestUnitConversionAndAciWarning();
  TestUnsupportedAndMalformedSourcesRejectWholeDrawing();
  std::cout << "DXF adapter tests passed\n";
  return 0;
}
