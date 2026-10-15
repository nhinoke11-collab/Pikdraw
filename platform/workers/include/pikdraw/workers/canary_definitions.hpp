#pragma once

#include <cstdint>
#include <string_view>

namespace pikdraw::workers {

enum CanaryType : uint32_t {
    CanaryFileRead = 1,
    CanaryFileWrite = 2,
    CanaryNetworkOutbound = 3,
    CanaryChildProcess = 4,
    CanaryUngrantedHandle = 5
};

[[nodiscard]] constexpr std::string_view CanaryTypeToString(CanaryType type) noexcept {
    switch (type) {
        case CanaryFileRead: return "CanaryFileRead";
        case CanaryFileWrite: return "CanaryFileWrite";
        case CanaryNetworkOutbound: return "CanaryNetworkOutbound";
        case CanaryChildProcess: return "CanaryChildProcess";
        case CanaryUngrantedHandle: return "CanaryUngrantedHandle";
        default: return "UnknownCanary";
    }
}

} // namespace pikdraw::workers
