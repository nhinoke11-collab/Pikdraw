#include "pikdraw/interop/adapters/dxf_worker_adapter.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace pikdraw::interop;

namespace {

std::vector<uint8_t> Source(std::string value) {
  return {value.begin(), value.end()};
}

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
1
0
LAYER
2
CUT
70
0
62
1
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
E1
8
CUT
10
1
20
2
11
30
21
40
0
ENDSEC
0
EOF
)DXF";

} // namespace

int main() {
  auto bytes = Source(kDxf);
  std::vector<CanonicalImportRecord> records;
  std::string diagnostic;
  if (!BuildDxfImportRecords(bytes, records, diagnostic))
    std::cerr << "DXF worker adapter fixture rejected: " << diagnostic << '\n';
  assert(BuildDxfImportRecords(bytes, records, diagnostic));
  assert(diagnostic.empty());
  assert(records.size() == 4);
  assert(records[0].type == ImportRecordType::Page);
  assert(records[1].type == ImportRecordType::Layer);
  assert(records[2].type == ImportRecordType::Path);
  assert(records[3].type == ImportRecordType::FidelityReport);
  const auto layer =
      DeserializeImportRecord(records[1].type, records[1].payload);
  assert(layer && std::get<ImportLayerRecord>(*layer).object_local_ids ==
                      std::vector<uint32_t>{1});
  const auto path =
      DeserializeImportRecord(records[2].type, records[2].payload);
  assert(path &&
         std::get<ImportPathRecord>(*path).source_id == "dxf-object-E1");
  const auto report =
      DeserializeImportRecord(records.back().type, records.back().payload);
  assert(report &&
         std::get<ImportFidelityReportRecord>(*report).coverage_profile ==
             "dxf-entities-v1");

  std::string unsupported = kDxf;
  const auto line = unsupported.find("LINE");
  assert(line != std::string::npos);
  unsupported.replace(line, 4, "SPLINE");
  auto unsupported_bytes = Source(std::move(unsupported));
  assert(!BuildDxfImportRecords(unsupported_bytes, records, diagnostic));
  assert(records.empty());
  assert(!diagnostic.empty());

  std::cout << "DXF worker adapter tests passed\n";
  return 0;
}
