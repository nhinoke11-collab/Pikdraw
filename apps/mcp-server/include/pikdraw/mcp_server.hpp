#pragma once

#include "pikdraw/automation/json_value.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace pikdraw::mcp {

class McpServer final {
public:
    using InstanceProvider = std::function<automation::JsonValue()>;
    using AppCaller = std::function<bool(std::string_view, const automation::JsonValue&,
                                         automation::JsonValue&, std::string&,
                                         const std::atomic<bool>*)>;

    McpServer() = default;
    McpServer(InstanceProvider instances, AppCaller app_caller,
              const std::atomic<bool>* cancellation = nullptr);

    /// Processes one JSON-RPC message. A null return means the input was a notification.
    [[nodiscard]] automation::JsonValue handle(const automation::JsonValue& request);
    /// Releases all application sessions owned by this transport session.
    void disconnect_all();
    /// Releases idle sessions older than ttl. Returns number of expired sessions.
    size_t expire_idle_sessions(std::chrono::milliseconds ttl = std::chrono::minutes(30));

private:
    struct Endpoint {
        std::string instance_id;
        std::wstring pipe_name;
        automation::JsonValue granted_scopes;
        std::chrono::steady_clock::time_point last_seen{std::chrono::steady_clock::now()};
    };

    enum class ProtocolMode { Undecided, Modern, Legacy };
    [[nodiscard]] automation::JsonValue dispatch_tool(std::string_view name,
                                                      const automation::JsonValue& arguments);
    [[nodiscard]] automation::JsonValue list_instances() const;
    [[nodiscard]] bool find_instance(std::string_view instance_id, Endpoint& endpoint) const;
    [[nodiscard]] bool call_app(const Endpoint& endpoint, const automation::JsonValue& request,
                                automation::JsonValue& response, std::string& error) const;
    [[nodiscard]] automation::JsonValue tool_result(const automation::JsonValue& result,
                                                     bool modern) const;

    ProtocolMode mode_{ProtocolMode::Undecided};
    std::map<std::string, Endpoint, std::less<>> sessions_;
    InstanceProvider instance_provider_;
    AppCaller app_caller_;
    const std::atomic<bool>* cancellation_{nullptr};
};

} // namespace pikdraw::mcp
