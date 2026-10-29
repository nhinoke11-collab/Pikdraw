#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/raster_edit_graph.hpp"

#include <optional>

namespace pikdraw::document {

/// Adds or updates one stable raster edit node as a single Undo step.
/// Reusing a node ID edits its parameters instead of appending a duplicate.
class SetRasterEditNodeCommand final : public DocumentCommand {
 public:
  SetRasterEditNodeCommand(ObjectId image_id, RasterEditNode node);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit Raster Image";
  }
  CommandResult execute(Document& doc) override;
  CommandResult undo(Document& doc) override;

 private:
  ObjectId image_id_{kInvalidObjectId};
  RasterEditNode node_;
  std::optional<RasterEditGraph> previous_graph_;
  bool executed_{false};
};

/// Atomically replaces an image's ordered raster edit graph as one Undo step.
class SetRasterEditGraphCommand final : public DocumentCommand {
 public:
  SetRasterEditGraphCommand(ObjectId image_id, RasterEditGraph graph,
      std::optional<uint64_t> expected_epoch = {},
      std::optional<uint64_t> expected_revision = {});

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reset Raster Adjustments";
  }
  CommandResult execute(Document& doc) override;
  CommandResult undo(Document& doc) override;

 private:
  ObjectId image_id_{kInvalidObjectId};
  RasterEditGraph graph_;
  std::optional<uint64_t> expected_epoch_;
  std::optional<uint64_t> expected_revision_;
  std::optional<RasterEditGraph> previous_graph_;
  bool executed_{false};
};

}  // namespace pikdraw::document
