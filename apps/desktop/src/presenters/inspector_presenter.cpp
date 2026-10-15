#include "pikdraw/desktop/presenters/inspector_presenter.hpp"

namespace pikdraw::desktop {

InspectorPresenter::InspectorPresenter() {
    layers_ = {
        {1, "Lớp 1", true, false, 0}
    };
}

} // namespace pikdraw::desktop
