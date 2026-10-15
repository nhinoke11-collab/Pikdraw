#include "pikdraw/vector/interaction/input_router.hpp"
#include <iostream>
#include <stdexcept>

using namespace pikdraw::vector::interaction;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void test_focus_ime_and_altgr_are_not_canvas_shortcuts() {
  InputRouter router;
  const auto text = router.route({InputKey::S, true}, InputFocusScope::TextEntry,
                                 true);
  check(text.target == InputRouteTarget::FocusedWidget &&
            !router.snap_bypass_held(),
        "text focus receives S without toggling canvas snap");
  const auto numeric = router.route({InputKey::Enter, true},
                                    InputFocusScope::NumericEntry, true);
  check(numeric.target == InputRouteTarget::FocusedWidget,
        "numeric field retains Enter");
  KeyInput altgr{InputKey::S};
  altgr.control = true;
  altgr.right_alt = true;
  const auto altgr_route =
      router.route(altgr, InputFocusScope::Canvas, true);
  check(altgr_route.target == InputRouteTarget::FocusedWidget &&
            !router.snap_bypass_held(),
        "AltGr chord is never interpreted as a snap modifier");
  KeyInput ime{InputKey::F3};
  ime.ime_composing = true;
  check(router.route(ime, InputFocusScope::Canvas, true).target ==
            InputRouteTarget::FocusedWidget,
        "IME composition receives function keys");
}

void test_draft_shortcuts_and_transient_release() {
  InputRouter router;
  KeyInput undo{InputKey::Z};
  undo.control = true;
  check(router.route(undo, InputFocusScope::Canvas, true).action ==
            InputAction::UndoDraft,
        "Ctrl+Z stays in active draft history");
  check(router.route(undo, InputFocusScope::Canvas, false).action ==
            InputAction::UndoDocument,
        "Ctrl+Z reaches document only outside a draft");
  KeyInput redo{InputKey::Z};
  redo.control = true;
  redo.shift = true;
  check(router.route(redo, InputFocusScope::Canvas, true).action ==
            InputAction::RedoDraft,
        "Ctrl+Shift+Z redoes local draft history");

  check(router.route({InputKey::Space, true}, InputFocusScope::Canvas, true)
                .action == InputAction::BeginPan &&
            router.pan_held(),
        "Space begins temporary pan");
  check(router.route({InputKey::S, true}, InputFocusScope::Canvas, true)
                .action == InputAction::BeginSnapBypass &&
            router.snap_bypass_held(),
        "S temporarily bypasses automatic snap");
  const auto released = router.release_transients();
  check(released.end_pan && released.end_snap_bypass && !router.pan_held() &&
            !router.snap_bypass_held(),
        "focus loss releases pan and snap bypass together");
}

void test_explicit_editor_actions_ignore_key_repeat() {
  InputRouter router;
  KeyInput cancel{InputKey::Escape};
  cancel.shift = true;
  check(router.route(cancel, InputFocusScope::Canvas, true).action ==
            InputAction::CancelAll,
        "Shift+Escape cancels the whole draft");
  check(router.route({InputKey::F3, true}, InputFocusScope::Canvas, false)
                .action == InputAction::ToggleSnapMaster,
        "F3 dispatches the named snap-master action");
  KeyInput repeated{InputKey::F3};
  repeated.repeated = true;
  check(!router.route(repeated, InputFocusScope::Canvas, false).consumed,
        "key repeat does not toggle snap repeatedly");
  check(router.route({InputKey::Space, false}, InputFocusScope::Canvas, true)
                .action == InputAction::None,
        "unmatched Space release is harmless");
}
} // namespace

int main() {
  try {
    test_focus_ime_and_altgr_are_not_canvas_shortcuts();
    test_draft_shortcuts_and_transient_release();
    test_explicit_editor_actions_ignore_key_repeat();
    std::cout << "InputRouter tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << "InputRouter test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
