#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/typography/layout/text_flow.hpp"
#include "pikdraw/typography/layout/text_on_path.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>

namespace pikdraw::vector {

/// Links source frame to target frame for text threading/overflow flow.
class LinkTextFramesCommand final : public document::DocumentCommand {
public:
    LinkTextFramesCommand(document::ObjectId source_id, document::ObjectId target_id);
    std::string_view name() const noexcept override { return "Link Text Frames"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;

private:
    document::ObjectId source_id_;
    document::ObjectId target_id_;
    std::optional<document::TextObject> before_source_;
    std::optional<document::TextObject> before_target_;
};

/// Unlinks a frame from its linked text frame chain.
class UnlinkTextFramesCommand final : public document::DocumentCommand {
public:
    explicit UnlinkTextFramesCommand(document::ObjectId frame_id);
    std::string_view name() const noexcept override { return "Unlink Text Frame"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;

private:
    document::ObjectId frame_id_;
    std::vector<std::pair<document::ObjectId, document::TextObject>> before_chain_;
};

/// Combines multiple text objects into a single multiline text object.
class CombineTextObjectsCommand final : public document::DocumentCommand {
public:
    CombineTextObjectsCommand(std::vector<document::ObjectId> text_ids, size_t target_layer_idx,
                              document::ObjectId* out_created_id = nullptr);
    std::string_view name() const noexcept override { return "Combine Text Objects"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;
    [[nodiscard]] document::ObjectId created_id() const noexcept { return created_id_; }

private:
    std::vector<document::ObjectId> source_ids_;
    size_t target_layer_idx_{0};
    document::ObjectId* out_created_id_{nullptr};
    document::ObjectId created_id_{document::kInvalidObjectId};
    std::vector<std::pair<document::ObjectId, document::TextObject>> removed_objects_;
};

/// Splits a multiline text object into separate single-line text objects.
class SplitTextLinesCommand final : public document::DocumentCommand {
public:
    SplitTextLinesCommand(document::ObjectId text_id, size_t target_layer_idx,
                          std::vector<document::ObjectId>* out_created_ids = nullptr);
    std::string_view name() const noexcept override { return "Split Text by Lines"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;
    [[nodiscard]] const std::vector<document::ObjectId>& created_ids() const noexcept { return created_ids_; }

private:
    document::ObjectId source_id_;
    size_t target_layer_idx_{0};
    std::vector<document::ObjectId>* out_created_ids_{nullptr};
    std::optional<document::TextObject> removed_source_;
    std::vector<document::ObjectId> created_ids_;
};

/// Attaches a text object to a path curve (Text on Path).
class AttachTextToPathCommand final : public document::DocumentCommand {
public:
    AttachTextToPathCommand(document::ObjectId text_id, document::ObjectId path_id);
    std::string_view name() const noexcept override { return "Fit Text to Path"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;

private:
    document::ObjectId text_id_;
    document::ObjectId path_id_;
    std::optional<document::TextObject> before_text_;
};

/// Detaches text from its path curve back to regular flat text.
class DetachTextFromPathCommand final : public document::DocumentCommand {
public:
    explicit DetachTextFromPathCommand(document::ObjectId text_id);
    std::string_view name() const noexcept override { return "Straighten Text"; }
    document::CommandResult execute(document::Document& doc) override;
    document::CommandResult undo(document::Document& doc) override;

private:
    document::ObjectId text_id_;
    std::optional<document::TextObject> before_text_;
};

} // namespace pikdraw::vector
