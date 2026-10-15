#include "pikdraw/desktop/presenters/ribbon_presenter.hpp"

namespace pikdraw::desktop {

void RibbonPresenter::update_selection(bool has_selection, std::string target_label) noexcept {
    has_selection_ = has_selection;
    target_label_ = std::move(target_label);
}

void RibbonPresenter::update_geometry(RibbonGeometryState geom) noexcept {
    geom_ = geom;
    if (on_geometry_changed_) {
        on_geometry_changed_(geom_);
    }
}

void RibbonPresenter::update_style(RibbonStyleState style) noexcept {
    style_ = std::move(style);
}

void RibbonPresenter::update_text(RibbonTextState text) noexcept {
    text_ = std::move(text);
}

} // namespace pikdraw::desktop
