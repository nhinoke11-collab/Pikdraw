#pragma once

#include "pikdraw/automation/automation_service.hpp"

#include <set>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pikdraw::automation {

/// Application-protocol adapter shared by local transports. Construct and call on the
/// document owner thread. Authentication is supplied by the transport; session handles
/// bind each request to the principal's explicitly granted scopes.
class AutomationRpc final {
public:
    AutomationRpc(AutomationService& service, std::set<std::string> granted_scopes);
    [[nodiscard]] JsonValue handle(const JsonValue& request);
    [[nodiscard]] bool owns_session(std::string_view session_handle) const noexcept;
    [[nodiscard]] bool has_scope(std::string_view session_handle,
                                 std::string_view scope) const;
    [[nodiscard]] size_t session_count() const noexcept { return sessions_.size(); }

private:
    struct Receipt {
        std::string canonical_payload;
        JsonValue response;
    };
    struct Session {
        std::set<std::string> scopes;
        std::string document_id;
        uint64_t epoch{0};
        std::map<std::string, Receipt, std::less<>> receipts;
    };

    [[nodiscard]] JsonValue error(std::string code, std::string message) const;
    [[nodiscard]] JsonValue handle_connect(const JsonValue& request);
    [[nodiscard]] bool authorize(const JsonValue& request, std::string_view required_scope,
                                 JsonValue& failure) const;

    AutomationService& service_;
    std::set<std::string> granted_scopes_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace pikdraw::automation
