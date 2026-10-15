#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"

namespace pikdraw::desktop {

CompositionRoot::CompositionRoot() = default;

CompositionRoot::~CompositionRoot() {
    shutdown();
}

void CompositionRoot::initialize(double window_width, double window_height, double dpi_scale) {
    if (is_initialized_) {
        return;
    }

    document_ = std::make_unique<document::Document>();
    vector_controller_ = std::make_unique<vector::VectorController>(vector::ToolMode::Select);
    renderer_ = std::make_unique<rendering::ViewportRenderer>();
    save_service_ = std::make_unique<document::SaveRecoveryService>();
    window_ = std::make_unique<DesktopWindow>(window_width, window_height, dpi_scale);

    wire_actions_to_services();
    is_initialized_ = true;
}

void CompositionRoot::wire_actions_to_services() {
    using namespace actions;
    auto& dispatcher = window_->action_dispatcher();

    // Wire Undo
    dispatcher.register_action({
        std::string(ActionId::kEditUndo), "Hoàn tác", "Undo", "Ctrl+Z",
        [this]() {
            if (document_) {
                document_->undo();
                const auto& snap = document_->current_snapshot();
                window_->status_bar().set_document_state(snap->revision(), document_->is_dirty());
            }
        },
        [this]() -> bool { return document_ && document_->can_undo(); }
    });

    // Wire Redo
    dispatcher.register_action({
        std::string(ActionId::kEditRedo), "Làm lại", "Redo", "Ctrl+Y",
        [this]() {
            if (document_) {
                document_->redo();
                const auto& snap = document_->current_snapshot();
                window_->status_bar().set_document_state(snap->revision(), document_->is_dirty());
            }
        },
        [this]() -> bool { return document_ && document_->can_redo(); }
    });

    // Wire Tool Modes
    dispatcher.register_action({
        std::string(ActionId::kToolSelect), "Công cụ chọn", "Select Tool", "V",
        [this]() {
            window_->tool_strip().set_active_tool(ToolMode::Select);
            if (vector_controller_) {
                vector_controller_->set_mode(vector::ToolMode::Select);
            }
        },
        nullptr
    });

    dispatcher.register_action({
        std::string(ActionId::kToolRectangle), "Hình chữ nhật", "Rectangle Tool", "R",
        [this]() {
            window_->tool_strip().set_active_tool(ToolMode::Rectangle);
            if (vector_controller_) {
                vector_controller_->set_mode(vector::ToolMode::Rectangle);
            }
        },
        nullptr
    });

    dispatcher.register_action({
        std::string(ActionId::kToolEllipse), "Hình elip", "Ellipse Tool", "O",
        [this]() {
            window_->tool_strip().set_active_tool(ToolMode::Ellipse);
            if (vector_controller_) {
                vector_controller_->set_mode(vector::ToolMode::Ellipse);
            }
        },
        nullptr
    });

    dispatcher.register_action({
        std::string(ActionId::kToolText), "Văn bản", "Text Tool", "T",
        [this]() {
            window_->tool_strip().set_active_tool(ToolMode::Text);
            if (vector_controller_) {
                vector_controller_->set_mode(vector::ToolMode::Text);
            }
        },
        nullptr
    });
}

void CompositionRoot::shutdown() {
    if (!is_initialized_) {
        return;
    }

    // Teardown in reverse dependency order
    window_.reset();
    save_service_.reset();
    renderer_.reset();
    vector_controller_.reset();
    document_.reset();

    is_initialized_ = false;
}

} // namespace pikdraw::desktop
