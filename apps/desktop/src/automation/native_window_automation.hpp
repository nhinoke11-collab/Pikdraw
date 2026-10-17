#pragma once

#include "automation_pipe_host.hpp"
#include "embedded_mcp_http_host.hpp"
#include "pikdraw/automation/automation_rpc.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"

#include <memory>
#include <set>
#include <unordered_map>

namespace pikdraw::desktop {

struct NativeWindowAutomationRuntime {
    struct Gateway {
        automation::AutomationService service;
        automation::AutomationRpc rpc;

        Gateway(document::Document& document, std::set<std::string> scopes)
            : service(document), rpc(service, std::move(scopes)) {}
    };

    std::set<std::string> granted_scopes;
    std::unordered_map<document::Document*, std::unique_ptr<Gateway>> documents;
    AutomationPipeHost pipe;
    EmbeddedMcpHttpHost http;

    [[nodiscard]] bool start(NativeWindow& window, bool edit_access, std::string& error);
    [[nodiscard]] automation::JsonValue handle(NativeWindow& window,
                                               const automation::JsonValue& request);
};

} // namespace pikdraw::desktop
