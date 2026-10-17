#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/contour/contour_tool.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace pikdraw::desktop::tools {

class ContourToolController {
public:
    explicit ContourToolController(document::Document& document) noexcept
        : document_(document), tool_(std::make_unique<vector::tools::contour::ContourTool>(document)) {}

    ContourToolController(const ContourToolController&) = delete;
    ContourToolController& operator=(const ContourToolController&) = delete;

    bool activate(document::ObjectId target_id = document::kInvalidObjectId);
    void deactivate();
    [[nodiscard]] bool active() const noexcept { return is_active_; }
    [[nodiscard]] bool is_active() const noexcept { return is_active_; }

    void set_direction(geometry::ContourDirection dir);
    [[nodiscard]] geometry::ContourDirection direction() const noexcept;

    void set_spacing_mm(double spacing);
    [[nodiscard]] double spacing_mm() const noexcept;

    void set_steps(int steps);
    [[nodiscard]] int steps() const noexcept;

    void set_join_type(geometry::JoinType join);
    [[nodiscard]] geometry::JoinType join_type() const noexcept;

    void set_miter_limit(double limit);
    [[nodiscard]] double miter_limit() const noexcept;

    bool apply();
    bool break_apart();
    bool remove();
    void cancel();

    [[nodiscard]] bool has_preview() const noexcept;
    [[nodiscard]] const geometry::ContourResult& preview_result() const noexcept;
    [[nodiscard]] document::ObjectId target_object() const noexcept;

    [[nodiscard]] vector::tools::contour::ContourTool& tool() noexcept { return *tool_; }
    [[nodiscard]] const vector::tools::contour::ContourTool& tool() const noexcept { return *tool_; }

private:
    document::Document& document_;
    std::unique_ptr<vector::tools::contour::ContourTool> tool_;
    bool is_active_{false};
};

} // namespace pikdraw::desktop::tools
