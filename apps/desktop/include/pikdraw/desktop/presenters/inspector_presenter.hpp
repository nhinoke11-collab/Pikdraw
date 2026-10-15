#pragma once

#include <string>
#include <vector>

namespace pikdraw::desktop {

enum class InspectorTab {
    Layers,
    Transform,
    Appearance,
    Export
};

struct LayerItem {
    uint64_t id;
    std::string name;
    bool is_visible{true};
    bool is_locked{false};
    size_t object_count{0};
};

class InspectorPresenter {
public:
    InspectorPresenter();

    [[nodiscard]] InspectorTab active_tab() const noexcept { return active_tab_; }
    void set_active_tab(InspectorTab tab) noexcept { active_tab_ = tab; }

    [[nodiscard]] const std::vector<LayerItem>& layers() const noexcept { return layers_; }
    void set_layers(std::vector<LayerItem> layers) { layers_ = std::move(layers); }

    [[nodiscard]] bool is_visible() const noexcept { return is_visible_; }
    void set_visible(bool visible) noexcept { is_visible_ = visible; }

    [[nodiscard]] double width() const noexcept { return width_; }
    void set_width(double w) noexcept { width_ = w; }

private:
    InspectorTab active_tab_{InspectorTab::Layers};
    std::vector<LayerItem> layers_;
    bool is_visible_{true};
    double width_{300.0};
};

} // namespace pikdraw::desktop
