// Negative test fixture: Violates architecture invariant by including UI headers in a core module.
#include <core/document/document.hpp>
#include <apps/desktop/main_window.hpp>
#include <QtWidgets/QWidget>

namespace pikdraw::document {
    void bad_ui_leak() {
        // Illegal usage of UI widget inside core module
    }
}
