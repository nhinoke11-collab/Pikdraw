#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include <iostream>
#include <cassert>

using namespace pikdraw::desktop::actions;

void test_action_id_validation() {
    assert(ActionId::is_valid_action_id(ActionId::kFileNew));
    assert(ActionId::is_valid_action_id(ActionId::kFileNewFromTemplate));
    assert(ActionId::is_valid_action_id(ActionId::kFileOpenRecent));
    assert(ActionId::is_valid_action_id(ActionId::kFilePublishPdf));
    assert(ActionId::is_valid_action_id(ActionId::kFileProperties));
    assert(ActionId::is_valid_action_id(ActionId::kViewHome));
    assert(ActionId::is_valid_action_id(ActionId::kEditUndo));
    assert(ActionId::is_valid_action_id(ActionId::kViewReloadUi));
    assert(ActionId::is_valid_action_id(ActionId::kObjectAlignLeft));
    assert(ActionId::is_valid_action_id(ActionId::kObjectAlignCenterVertical));
    assert(ActionId::is_valid_action_id(ActionId::kObjectCenterPage));
    assert(ActionId::is_valid_action_id(ActionId::kObjectDistributeSpacingHorizontal));
    assert(ActionId::is_valid_action_id(ActionId::kObjectFlipHorizontal));
    assert(ActionId::is_valid_action_id(ActionId::kObjectFlipVertical));
    assert(ActionId::is_valid_action_id(ActionId::kObjectRotate90CW));
    assert(ActionId::is_valid_action_id(ActionId::kObjectRotate90CCW));
    assert(ActionId::is_valid_action_id(ActionId::kObjectRotate180));
    assert(!ActionId::is_valid_action_id("invalid.id"));
    assert(!ActionId::is_valid_action_id("action."));
    std::cout << "[PASS] test_action_id_validation\n";
}

void test_action_dispatcher_routing_and_shortcuts() {
    ActionDispatcher dispatcher;

    bool new_called = false;
    bool undo_called = false;
    bool redo_called = false;

    dispatcher.register_action({
        std::string(ActionId::kFileNew), "Tạo mới", "New", "Ctrl+N",
        [&new_called]() { new_called = true; },
        nullptr
    });

    dispatcher.register_action({
        std::string(ActionId::kEditUndo), "Hoàn tác", "Undo", "Ctrl+Z",
        [&undo_called]() { undo_called = true; },
        nullptr
    });

    bool can_redo = false;
    dispatcher.register_action({
        std::string(ActionId::kEditRedo), "Làm lại", "Redo", "Ctrl+Y",
        [&redo_called]() { redo_called = true; },
        [&can_redo]() { return can_redo; }
    });

    // Dispatch by ID
    assert(dispatcher.dispatch(ActionId::kFileNew));
    assert(new_called);
    assert(dispatcher.last_dispatched_id() == ActionId::kFileNew);

    // Dispatch by Shortcut
    assert(dispatcher.dispatch_shortcut("Ctrl+Z"));
    assert(undo_called);
    assert(dispatcher.last_dispatched_id() == ActionId::kEditUndo);

    // Disabled action cannot be dispatched
    assert(!dispatcher.is_action_enabled(ActionId::kEditRedo));
    assert(!dispatcher.dispatch_shortcut("Ctrl+Y"));
    assert(!redo_called);

    // Enable and retry
    can_redo = true;
    assert(dispatcher.is_action_enabled(ActionId::kEditRedo));
    assert(dispatcher.dispatch_shortcut("Ctrl+Y"));
    assert(redo_called);

    // Check history
    const auto& hist = dispatcher.dispatch_history();
    assert(hist.size() == 3);
    assert(hist[0] == ActionId::kFileNew);
    assert(hist[1] == ActionId::kEditUndo);
    assert(hist[2] == ActionId::kEditRedo);

    std::cout << "[PASS] test_action_dispatcher_routing_and_shortcuts\n";
}

int main() {
    std::cout << "=== Running Desktop Actions Tests ===\n";
    test_action_id_validation();
    test_action_dispatcher_routing_and_shortcuts();
    std::cout << "All Desktop Actions Tests PASSED.\n";
    return 0;
}
