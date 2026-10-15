#pragma once

#include "pikdraw/interop/ipc_dto.hpp"
#include "pikdraw/workers/win32_common.hpp"

#include <string_view>

namespace pikdraw::workers {

[[nodiscard]] bool HandleV2WorkerFrame(
    HANDLE input,
    HANDLE output,
    HANDLE source_handle,
    const pikdraw::interop::IpcHeader& header,
    bool& negotiated,
    uint32_t& accepted_features);

[[nodiscard]] int RunV2StreamFixture(std::string_view mode, HANDLE source_handle);

} // namespace pikdraw::workers
