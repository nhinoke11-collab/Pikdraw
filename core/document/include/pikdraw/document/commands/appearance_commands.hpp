#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/effects/effect_stack.hpp"
#include <vector>

namespace pikdraw::document {

struct AppearanceReplacement {
  ObjectId object_id{kInvalidObjectId};
  Appearance appearance;
};

/// Atomically replace appearance values on a Path/Text selection and preserve each
/// object's previous stack for one-step undo. Callers construct one replacement per
/// target so effect instance IDs remain unique across the document.
class SetAppearanceCommand final : public DocumentCommand {
public:
  explicit SetAppearanceCommand(std::vector<AppearanceReplacement> replacements);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Set Appearance";
  }
  CommandResult execute(Document& doc) override;
  CommandResult undo(Document& doc) override;

private:
  std::vector<AppearanceReplacement> replacements_;
  std::vector<Appearance> previous_;
  bool applied_{false};
};

} // namespace pikdraw::document
