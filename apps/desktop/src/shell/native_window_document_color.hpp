#pragma once
#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <functional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::desktop::document_color_ui {
// Separate range so adding these menu routes does not renumber legacy menu actions.
inline constexpr int kModeMenu = 10001, kDpiMenu = 10002;
inline constexpr int kCmyk = 10003, kRgb = 10004, kDpi300 = 10005, kDpi150 = 10006, kDpi72 = 10007;
const char* action_id(int action) noexcept;
void register_actions(actions::ActionDispatcher& dispatcher,
    std::function<void(color::DocumentColorMode)> convert, std::function<void(int32_t)> set_dpi);
#ifdef _WIN32
int choose_setting(HWND owner, const document::DocumentSnapshot& snapshot, bool vi, bool dpi, int menu_left);
bool confirm_conversion(HWND owner, bool vi);
#endif
} // namespace pikdraw::desktop::document_color_ui
