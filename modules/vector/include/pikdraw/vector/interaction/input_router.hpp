#pragma once

#include <cstdint>

namespace pikdraw::vector::interaction {

enum class InputFocusScope : uint8_t {
  Canvas,
  TextEntry,
  NumericEntry,
  ImeComposition,
  Menu,
  Modal
};

enum class InputKey : uint8_t {
  Escape,
  Enter,
  Backspace,
  Space,
  S,
  F3,
  Z,
  Y,
  Other
};

enum class InputAction : uint8_t {
  None,
  ForwardToTool,
  Escape,
  CancelAll,
  Enter,
  UndoDraft,
  RedoDraft,
  UndoDocument,
  RedoDocument,
  BeginPan,
  EndPan,
  BeginSnapBypass,
  EndSnapBypass,
  ToggleSnapMaster
};

enum class InputRouteTarget : uint8_t {
  Ignored,
  ToolSession,
  Document,
  FocusedWidget
};

struct KeyInput {
  InputKey key{InputKey::Other};
  bool down{true};
  bool shift{false};
  bool control{false};
  bool alt{false};
  bool right_alt{false};
  bool repeated{false};
  bool ime_composing{false};
};

struct InputRoute {
  InputRouteTarget target{InputRouteTarget::Ignored};
  InputAction action{InputAction::None};
  bool consumed{false};
};

struct ReleasedTransientInput {
  bool end_pan{false};
  bool end_snap_bypass{false};
};

class InputRouter {
public:
  [[nodiscard]] InputRoute route(const KeyInput &input,
                                 InputFocusScope focus,
                                 bool tool_session_active) noexcept;
  [[nodiscard]] ReleasedTransientInput release_transients() noexcept;

  [[nodiscard]] bool pan_held() const noexcept { return pan_held_; }
  [[nodiscard]] bool snap_bypass_held() const noexcept {
    return snap_bypass_held_;
  }

private:
  bool pan_held_{false};
  bool snap_bypass_held_{false};
};

} // namespace pikdraw::vector::interaction
