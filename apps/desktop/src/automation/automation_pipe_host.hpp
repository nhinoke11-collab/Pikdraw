#pragma once

#include "pikdraw/automation/json_value.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {

class AutomationPipeHost final {
public:
    using Handler = std::function<automation::JsonValue(const automation::JsonValue&)>;
    AutomationPipeHost();
    ~AutomationPipeHost();
    AutomationPipeHost(const AutomationPipeHost&) = delete;
    AutomationPipeHost& operator=(const AutomationPipeHost&) = delete;

#ifdef _WIN32
    bool start(HWND window, Handler handler, std::string_view granted_scopes, std::string& error);
    bool invoke(const automation::JsonValue& request, automation::JsonValue& response,
                std::string& error, const std::atomic<bool>* cancellation = nullptr);
    void stop() noexcept;
    [[nodiscard]] const std::wstring& pipe_name() const noexcept;
#else
    bool start(Handler handler, std::string& error);
    void stop() noexcept;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::desktop
