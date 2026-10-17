#pragma once

#ifdef _WIN32
#include <guiddef.h>

namespace pikdraw::com_server {

inline constexpr CLSID kPikDrawAutomationClsid{
    0x9e3f8d47, 0xf078, 0x43b4, {0x9b, 0x61, 0x5c, 0x18, 0x8b, 0x62, 0x43, 0xa1}};
inline constexpr wchar_t kPikDrawAutomationProgId[] = L"PikDraw.Automation.1";
inline constexpr wchar_t kPikDrawAutomationVersionIndependentProgId[] = L"PikDraw.Automation";
inline constexpr wchar_t kPikDrawAutomationVersion[] = L"1.0";

} // namespace pikdraw::com_server
#endif
