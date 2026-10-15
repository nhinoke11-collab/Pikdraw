#include "pikdraw/vector/interaction/input_router.hpp"

namespace pikdraw::vector::interaction {
namespace {
InputRoute result(InputRouteTarget target, InputAction action,
                  bool consumed = true) noexcept {
  return {target, action, consumed};
}
} // namespace

InputRoute InputRouter::route(const KeyInput &input, InputFocusScope focus,
                              bool tool_session_active) noexcept {
  if (input.key == InputKey::Space && !input.down && pan_held_) {
    pan_held_ = false;
    return result(InputRouteTarget::ToolSession, InputAction::EndPan);
  }
  if (input.key == InputKey::S && !input.down && snap_bypass_held_) {
    snap_bypass_held_ = false;
    return result(InputRouteTarget::ToolSession,
                  InputAction::EndSnapBypass);
  }
  if (focus != InputFocusScope::Canvas)
    return result(InputRouteTarget::FocusedWidget,
                  InputAction::ForwardToTool);
  if (input.ime_composing || (input.control && input.right_alt))
    return result(InputRouteTarget::FocusedWidget,
                  InputAction::ForwardToTool);
  if (input.repeated)
    return result(InputRouteTarget::Ignored, InputAction::None, false);
  if (!input.down)
    return result(InputRouteTarget::ToolSession, InputAction::None, false);

  switch (input.key) {
  case InputKey::Escape:
    return result(InputRouteTarget::ToolSession,
                  input.shift ? InputAction::CancelAll : InputAction::Escape);
  case InputKey::Enter:
    return result(InputRouteTarget::ToolSession, InputAction::Enter);
  case InputKey::Backspace:
    return tool_session_active
               ? result(InputRouteTarget::ToolSession, InputAction::UndoDraft)
               : result(InputRouteTarget::FocusedWidget,
                        InputAction::ForwardToTool);
  case InputKey::Space:
    if (input.control || input.alt || input.shift)
      return result(InputRouteTarget::ToolSession, InputAction::None, false);
    pan_held_ = true;
    return result(InputRouteTarget::ToolSession, InputAction::BeginPan);
  case InputKey::S:
    if (input.control || input.alt || input.shift)
      return result(InputRouteTarget::ToolSession, InputAction::None, false);
    snap_bypass_held_ = true;
    return result(InputRouteTarget::ToolSession,
                  InputAction::BeginSnapBypass);
  case InputKey::F3:
    if (input.control || input.alt || input.shift)
      return result(InputRouteTarget::ToolSession, InputAction::None, false);
    return result(InputRouteTarget::ToolSession,
                  InputAction::ToggleSnapMaster);
  case InputKey::Z:
    if (input.control && !input.alt && input.shift)
      return tool_session_active
                 ? result(InputRouteTarget::ToolSession, InputAction::RedoDraft)
                 : result(InputRouteTarget::Document, InputAction::RedoDocument);
    if (input.control && !input.alt)
      return tool_session_active
                 ? result(InputRouteTarget::ToolSession, InputAction::UndoDraft)
                 : result(InputRouteTarget::Document, InputAction::UndoDocument);
    break;
  case InputKey::Y:
    if (input.control && !input.alt)
      return tool_session_active
                 ? result(InputRouteTarget::ToolSession, InputAction::RedoDraft)
                 : result(InputRouteTarget::Document, InputAction::RedoDocument);
    break;
  case InputKey::Other:
    break;
  }
  return result(InputRouteTarget::ToolSession, InputAction::ForwardToTool,
                false);
}

ReleasedTransientInput InputRouter::release_transients() noexcept {
  const ReleasedTransientInput released{pan_held_, snap_bypass_held_};
  pan_held_ = false;
  snap_bypass_held_ = false;
  return released;
}

} // namespace pikdraw::vector::interaction
