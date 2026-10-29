#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <cassert>
#include <memory>

using namespace pikdraw::document;

int main() {
  Document document("raster-graph-command-test");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Graph command target";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0x404040ffu};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0x40, 0x40, 0x40, 0xff};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(std::make_unique<AddObjectCommand>(image)).success);

  RasterEditGraph edited;
  edited.nodes.push_back({RasterEditNodeId{9001}, true, RasterExposureParams{1.0}});
  edited.nodes.push_back({RasterEditNodeId{9002}, true, RasterLevelsParams{}});
  assert(document.execute_command(std::make_unique<SetRasterEditGraphCommand>(
      image.id, edited)).success);
  assert(document.execute_command(std::make_unique<SetRasterEditGraphCommand>(
      image.id, RasterEditGraph{})).success);
  const auto* cleared = document.objects().lookup_ref(image.id);
  assert(cleared && std::get<ImageObject>(*cleared).edit_graph.nodes.empty());

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.size() == 2);
  assert(document.redo());
  const auto* redone = document.objects().lookup_ref(image.id);
  assert(redone && std::get<ImageObject>(*redone).edit_graph.nodes.empty());

  const auto stale_revision = document.revision() - 1;
  assert(!document.execute_command(std::make_unique<SetRasterEditGraphCommand>(
      image.id, edited, document.epoch(), stale_revision)).success);
  const auto* unchanged = document.objects().lookup_ref(image.id);
  assert(unchanged && std::get<ImageObject>(*unchanged).edit_graph.nodes.empty());

  RasterEditGraph duplicate_ids;
  duplicate_ids.nodes.push_back({RasterEditNodeId{9010}, true,
      RasterExposureParams{1.0}});
  duplicate_ids.nodes.push_back({RasterEditNodeId{9010}, true,
      RasterExposureParams{2.0}});
  assert(!document.execute_command(std::make_unique<SetRasterEditGraphCommand>(
      image.id, duplicate_ids)).success);
}
