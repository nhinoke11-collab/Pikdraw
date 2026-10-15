#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/vector/vector_controller.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include <memory>
#include <string>

namespace pikdraw::desktop {

/// Central composition root owning services, executors, and the desktop shell facade
class CompositionRoot {
public:
    CompositionRoot();
    ~CompositionRoot();

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    /// Initialize the entire runtime pipeline
    void initialize(double window_width = 1920.0, double window_height = 1080.0, double dpi_scale = 1.0);

    /// Cleanly teardown services and release callbacks
    void shutdown();

    [[nodiscard]] bool is_initialized() const noexcept { return is_initialized_; }

    [[nodiscard]] document::Document& document() noexcept { return *document_; }
    [[nodiscard]] const document::Document& document() const noexcept { return *document_; }

    [[nodiscard]] vector::VectorController& vector_controller() noexcept { return *vector_controller_; }
    [[nodiscard]] const vector::VectorController& vector_controller() const noexcept { return *vector_controller_; }

    [[nodiscard]] rendering::ViewportRenderer& renderer() noexcept { return *renderer_; }
    [[nodiscard]] const rendering::ViewportRenderer& renderer() const noexcept { return *renderer_; }

    [[nodiscard]] document::SaveRecoveryService& save_service() noexcept { return *save_service_; }
    [[nodiscard]] const document::SaveRecoveryService& save_service() const noexcept { return *save_service_; }

    [[nodiscard]] DesktopWindow& window() noexcept { return *window_; }
    [[nodiscard]] const DesktopWindow& window() const noexcept { return *window_; }

    /// Wire action dispatcher commands to actual runtime services
    void wire_actions_to_services();

private:
    bool is_initialized_{false};
    std::unique_ptr<document::Document> document_;
    std::unique_ptr<vector::VectorController> vector_controller_;
    std::unique_ptr<rendering::ViewportRenderer> renderer_;
    std::unique_ptr<document::SaveRecoveryService> save_service_;
    std::unique_ptr<DesktopWindow> window_;
};

} // namespace pikdraw::desktop
