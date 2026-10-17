#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include <vector>

namespace pikdraw::document {

struct FillPaintReplacement {
    ObjectId object_id{kInvalidObjectId};
    bool has_fill{true};
    color::ColorValue fill{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    std::shared_ptr<const GradientPaint> fill_gradient{nullptr};
};

struct PreviousFillPaint {
    bool has_fill{true};
    color::ColorValue fill{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    std::shared_ptr<const GradientPaint> fill_gradient{nullptr};
};

/// Atomically replace fill paint on a PathObject selection (batch support).
/// Preserves previous fill state for one-step undo.
class SetFillPaintCommand final : public DocumentCommand {
public:
    explicit SetFillPaintCommand(std::vector<FillPaintReplacement> replacements);

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Set Fill Paint";
    }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] const std::vector<FillPaintReplacement>& replacements() const noexcept {
        return replacements_;
    }

private:
    std::vector<FillPaintReplacement> replacements_;
    std::vector<PreviousFillPaint> previous_;
    bool applied_{false};
};

} // namespace pikdraw::document
