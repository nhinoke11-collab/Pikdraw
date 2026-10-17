#pragma once

#include "pikdraw/mcp_server.hpp"

#include <memory>
#include <string>

namespace pikdraw::desktop {

class EmbeddedMcpHttpHost final {
public:
    EmbeddedMcpHttpHost();
    ~EmbeddedMcpHttpHost();
    EmbeddedMcpHttpHost(const EmbeddedMcpHttpHost&) = delete;
    EmbeddedMcpHttpHost& operator=(const EmbeddedMcpHttpHost&) = delete;

    [[nodiscard]] bool start(pikdraw::mcp::McpServer::InstanceProvider instances,
                             pikdraw::mcp::McpServer::AppCaller app_caller,
                             std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::string endpoint() const;
    [[nodiscard]] std::string access_token() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::desktop
