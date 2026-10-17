#pragma once

#ifdef _WIN32

#include <oaidl.h>

#include <functional>

namespace pikdraw::com_server {

/// Creates the late-bound automation surface. The object owns one MCP adapter,
/// so session handles stay attached to this COM client object.
HRESULT create_automation_dispatch(IDispatch** result,
                                  std::function<void()> on_destroy = {}) noexcept;

} // namespace pikdraw::com_server

#endif
