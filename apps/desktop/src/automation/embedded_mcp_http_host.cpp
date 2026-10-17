#include "embedded_mcp_http_host.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pikdraw::desktop {
namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kMaximumHeaderBytes = 16 * 1024;
constexpr size_t kMaximumBodyBytes = 1024 * 1024;
constexpr size_t kMaximumClients = 24;
constexpr size_t kMaximumSessions = 32;
constexpr auto kSessionIdleLimit = std::chrono::minutes(30);
constexpr std::string_view kLegacyProtocolVersion = "2025-11-25";
constexpr std::string_view kModernProtocolVersion = "2026-07-28";

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string_view trim_ascii(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

std::string hex_bytes(const std::array<uint8_t, 32>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 0x0f]);
    }
    return output;
}

bool secure_random_hex(std::string& output) {
    std::array<uint8_t, 32> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return false;
    output = hex_bytes(bytes);
    SecureZeroMemory(bytes.data(), bytes.size());
    return true;
}

bool constant_time_equal(std::string_view left, std::string_view right) {
    size_t difference = left.size() ^ right.size();
    const size_t count = std::max(left.size(), right.size());
    for (size_t i = 0; i < count; ++i) {
        const auto a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const auto b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<size_t>(a ^ b);
    }
    return difference == 0;
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

bool receive_all(SOCKET socket, std::string& buffer, size_t required) {
    std::array<char, 4096> chunk{};
    while (buffer.size() < required) {
        const auto remaining = required - buffer.size();
        const int request = static_cast<int>(std::min(remaining, chunk.size()));
        const int received = recv(socket, chunk.data(), request, 0);
        if (received <= 0) return false;
        buffer.append(chunk.data(), static_cast<size_t>(received));
    }
    return true;
}

bool read_http_request(SOCKET socket, HttpRequest& request, int& failure_status) {
    std::string bytes;
    size_t header_end = std::string::npos;
    while ((header_end = bytes.find("\r\n\r\n")) == std::string::npos) {
        if (bytes.size() >= kMaximumHeaderBytes) { failure_status = 431; return false; }
        std::array<char, 4096> chunk{};
        const int capacity = static_cast<int>(std::min<size_t>(chunk.size(), kMaximumHeaderBytes - bytes.size()));
        const int received = recv(socket, chunk.data(), capacity, 0);
        if (received <= 0) { failure_status = 400; return false; }
        bytes.append(chunk.data(), static_cast<size_t>(received));
    }
    if (header_end > kMaximumHeaderBytes) { failure_status = 431; return false; }

    const std::string_view header(bytes.data(), header_end + 2);
    const auto first_line_end = header.find("\r\n");
    if (first_line_end == std::string_view::npos) { failure_status = 400; return false; }
    const auto first_line = header.substr(0, first_line_end);
    const auto first_space = first_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
        ? std::string_view::npos : first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        first_line.substr(second_space + 1) != "HTTP/1.1") {
        failure_status = 400; return false;
    }
    request.method.assign(first_line.substr(0, first_space));
    request.target.assign(first_line.substr(first_space + 1, second_space - first_space - 1));

    size_t cursor = first_line_end + 2;
    while (cursor < header.size()) {
        const auto line_end = header.find("\r\n", cursor);
        if (line_end == std::string_view::npos || line_end == cursor) break;
        const auto line = header.substr(cursor, line_end - cursor);
        if (line.front() == ' ' || line.front() == '\t') { failure_status = 400; return false; }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) { failure_status = 400; return false; }
        const auto name = lower_ascii(line.substr(0, colon));
        const auto value = trim_ascii(line.substr(colon + 1));
        if (std::any_of(value.begin(), value.end(), [](unsigned char c) {
                return (c < 0x20 && c != '\t') || c == 0x7f;
            })) {
            failure_status = 400; return false;
        }
        if (!request.headers.emplace(name, std::string(value)).second) {
            failure_status = 400; return false;
        }
        cursor = line_end + 2;
    }

    const auto transfer = request.headers.find("transfer-encoding");
    if (transfer != request.headers.end()) { failure_status = 400; return false; }
    size_t body_size = 0;
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
        const auto text = trim_ascii(length->second);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), body_size);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            body_size > kMaximumBodyBytes) {
            failure_status = body_size > kMaximumBodyBytes ? 413 : 400;
            return false;
        }
    }
    const size_t body_start = header_end + 4;
    if (!receive_all(socket, bytes, body_start + body_size)) { failure_status = 400; return false; }
    request.body.assign(bytes.data() + body_start, body_size);
    return true;
}

bool send_all(SOCKET socket, std::string_view bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const int count = send(socket, bytes.data() + sent,
                               static_cast<int>(std::min<size_t>(bytes.size() - sent, INT_MAX)), 0);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool send_http_response(SOCKET socket, int status, std::string_view reason,
                        std::string_view body = {}, std::string_view extra_headers = {}) {
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + std::string(reason) + "\r\n";
    if (!body.empty()) response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n";
    response.append(extra_headers);
    response += "\r\n";
    response.append(body);
    return send_all(socket, response);
}

std::string http_error_body(std::string_view code, std::string_view message) {
    return automation::JsonValue(automation::JsonValue::Object{{"error", automation::JsonValue::Object{
        {"code", std::string(code)}, {"message", std::string(message)}}}}).serialize();
}

bool has_media_type(std::string_view value, std::string_view expected) {
    size_t cursor = 0;
    while (cursor <= value.size()) {
        const auto comma = value.find(',', cursor);
        auto item = trim_ascii(value.substr(cursor, comma == std::string_view::npos
            ? value.size() - cursor : comma - cursor));
        const auto semicolon = item.find(';');
        item = trim_ascii(item.substr(0, semicolon));
        if (lower_ascii(item) == expected) return true;
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
    return false;
}

bool valid_loopback_authority(std::string_view authority, unsigned short port) {
    const auto expected_port = ":" + std::to_string(port);
    const auto value = lower_ascii(trim_ascii(authority));
    return value == "127.0.0.1" + expected_port || value == "localhost" + expected_port;
}

bool valid_origin(std::string_view origin, unsigned short port) {
    const auto value = lower_ascii(trim_ascii(origin));
    constexpr std::string_view prefix = "http://";
    if (!value.starts_with(prefix)) return false;
    const auto authority = std::string_view(value).substr(prefix.size());
    if (authority.find_first_of("/?#") != std::string_view::npos) return false;
    return valid_loopback_authority(authority, port);
}

bool valid_bearer(std::string_view header, std::string_view expected) {
    const auto value = trim_ascii(header);
    if (value.size() < 7 || lower_ascii(value.substr(0, 7)) != "bearer ") return false;
    return constant_time_equal(trim_ascii(value.substr(7)), expected);
}

} // namespace

struct EmbeddedMcpHttpHost::Impl {
    struct Session {
        std::unique_ptr<mcp::McpServer> server;
        std::mutex mutex;
        std::atomic<int64_t> last_active_ticks{0};
    };
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    mcp::McpServer::InstanceProvider instances;
    mcp::McpServer::AppCaller app_caller;
    std::atomic<bool> stopping{true};
    std::atomic<bool> is_active{false};
    bool winsock_started{false};
    SOCKET listener{INVALID_SOCKET};
    unsigned short port{0};
    std::string url;
    std::string token;
    std::thread accept_thread;
    std::mutex workers_mutex;
    std::vector<Worker> workers;
    std::mutex clients_mutex;
    std::vector<SOCKET> clients;
    std::mutex sessions_mutex;
    std::map<std::string, std::shared_ptr<Session>, std::less<>> sessions;
    std::mutex modern_server_mutex;
    std::unique_ptr<mcp::McpServer> modern_server;

    static int64_t now_ticks() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }

    void reap_workers() {
        std::lock_guard guard(workers_mutex);
        auto it = workers.begin();
        while (it != workers.end()) {
            if (it->finished->load(std::memory_order_acquire)) {
                if (it->thread.joinable()) it->thread.join();
                it = workers.erase(it);
            } else ++it;
        }
    }

    void expire_idle_sessions() {
        const auto now = now_ticks();
        const auto idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(kSessionIdleLimit).count();
        std::vector<std::shared_ptr<Session>> expired;
        {
            std::lock_guard guard(sessions_mutex);
            auto it = sessions.begin();
            while (it != sessions.end()) {
                if (now - it->second->last_active_ticks.load(std::memory_order_acquire) >= idle_ms) {
                    expired.push_back(it->second);
                    it = sessions.erase(it);
                } else ++it;
            }
        }
        for (const auto& session : expired) {
            std::lock_guard session_guard(session->mutex);
            if (session->server) session->server->disconnect_all();
        }
    }

    std::shared_ptr<Session> create_session() {
        std::string id;
        if (!secure_random_hex(id)) return {};
        auto session = std::make_shared<Session>();
        session->server = std::make_unique<mcp::McpServer>(instances, app_caller, &stopping);
        session->last_active_ticks.store(now_ticks(), std::memory_order_release);
        std::lock_guard guard(sessions_mutex);
        if (sessions.size() >= kMaximumSessions) return {};
        if (!sessions.emplace(id, session).second) return {};
        return session;
    }

    std::string find_session_id(const std::shared_ptr<Session>& session) {
        std::lock_guard guard(sessions_mutex);
        for (const auto& [id, candidate] : sessions)
            if (candidate == session) return id;
        return {};
    }

    std::shared_ptr<Session> get_session(std::string_view id) {
        std::lock_guard guard(sessions_mutex);
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second;
    }

    void erase_session(std::string_view id, const std::shared_ptr<Session>& expected) {
        std::lock_guard guard(sessions_mutex);
        const auto it = sessions.find(id);
        if (it != sessions.end() && it->second == expected) sessions.erase(it);
    }

    void handle_post(SOCKET socket, const HttpRequest& request) {
        const auto accept = request.headers.find("accept");
        if (accept == request.headers.end() || !has_media_type(accept->second, "application/json") ||
            !has_media_type(accept->second, "text/event-stream")) {
            (void)send_http_response(socket, 406, "Not Acceptable",
                http_error_body("ACCEPT_REQUIRED", "Accept must include application/json and text/event-stream."));
            return;
        }
        const auto content_type = request.headers.find("content-type");
        if (content_type == request.headers.end() ||
            !has_media_type(content_type->second, "application/json")) {
            (void)send_http_response(socket, 415, "Unsupported Media Type",
                http_error_body("CONTENT_TYPE_REQUIRED", "MCP POST requests must use application/json."));
            return;
        }
        automation::JsonValue message;
        std::string parse_error;
        if (!automation::JsonValue::parse(request.body, message, parse_error) || !message.object()) {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("INVALID_JSON", "The MCP request body must be one JSON-RPC object."));
            return;
        }
        const auto* method_value = message.get("method");
        const auto* method = method_value ? method_value->string() : nullptr;
        if (!method) {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("INVALID_JSON_RPC", "The MCP request must include a method."));
            return;
        }

        const auto session_header = request.headers.find("mcp-session-id");
        const auto protocol_header = request.headers.find("mcp-protocol-version");
        if (protocol_header != request.headers.end() &&
            trim_ascii(protocol_header->second) != kModernProtocolVersion &&
            trim_ascii(protocol_header->second) != kLegacyProtocolVersion) {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("PROTOCOL_VERSION", "This endpoint supports MCP 2026-07-28 and 2025-11-25."));
            return;
        }
        if (session_header == request.headers.end() && protocol_header != request.headers.end() &&
            trim_ascii(protocol_header->second) == kModernProtocolVersion) {
            const auto* params = message.get("params");
            const auto* meta = params ? params->get("_meta") : nullptr;
            const auto* body_protocol = meta ? meta->get("io.modelcontextprotocol/protocolVersion") : nullptr;
            const auto* capabilities = meta ? meta->get("io.modelcontextprotocol/clientCapabilities") : nullptr;
            if (!body_protocol || !body_protocol->string() ||
                *body_protocol->string() != kModernProtocolVersion ||
                !capabilities || !capabilities->object()) {
                const auto* id = message.get("id");
                const auto error = automation::JsonValue(automation::JsonValue::Object{{"jsonrpc", "2.0"},
                    {"id", id ? *id : automation::JsonValue{}},
                    {"error", automation::JsonValue::Object{{"code", -32602},
                        {"message", "Modern MCP requests require matching version and clientCapabilities metadata."}}}}).serialize();
                (void)send_http_response(socket, 400, "Bad Request", error);
                return;
            }
            const auto method_header = request.headers.find("mcp-method");
            if (method_header == request.headers.end() || trim_ascii(method_header->second) != *method) {
                const auto* id = message.get("id");
                const auto error = automation::JsonValue(automation::JsonValue::Object{{"jsonrpc", "2.0"},
                    {"id", id ? *id : automation::JsonValue{}},
                    {"error", automation::JsonValue::Object{{"code", -32020},
                        {"message", "Mcp-Method must match the JSON-RPC method."}}}}).serialize();
                (void)send_http_response(socket, 400, "Bad Request", error);
                return;
            }
            if (*method == "tools/call") {
                const auto* params = message.get("params");
                const auto* name_value = params ? params->get("name") : nullptr;
                const auto* name = name_value ? name_value->string() : nullptr;
                const auto name_header = request.headers.find("mcp-name");
                if (!name || name_header == request.headers.end() ||
                    trim_ascii(name_header->second) != *name) {
                    const auto* id = message.get("id");
                    const auto error = automation::JsonValue(automation::JsonValue::Object{{"jsonrpc", "2.0"},
                        {"id", id ? *id : automation::JsonValue{}},
                        {"error", automation::JsonValue::Object{{"code", -32020},
                            {"message", "Mcp-Name must match the requested tool name."}}}}).serialize();
                    (void)send_http_response(socket, 400, "Bad Request", error);
                    return;
                }
            }
            std::lock_guard server_guard(modern_server_mutex);
            if (!modern_server) {
                (void)send_http_response(socket, 503, "Service Unavailable",
                    http_error_body("SERVER_STOPPING", "The in-app MCP server is stopping."));
                return;
            }
            auto response = modern_server->handle(message);
            if (response.is_null()) {
                (void)send_http_response(socket, 202, "Accepted", {},
                    "MCP-Protocol-Version: " + std::string(kModernProtocolVersion) + "\r\n");
                return;
            }
            const auto body = response.serialize();
            if (body.size() > kMaximumBodyBytes) {
                (void)send_http_response(socket, 500, "Internal Server Error",
                    http_error_body("RESPONSE_LIMIT", "The MCP response exceeds the 1 MiB limit."));
                return;
            }
            (void)send_http_response(socket, 200, "OK", body,
                "MCP-Protocol-Version: " + std::string(kModernProtocolVersion) + "\r\n");
            return;
        }

        if (session_header == request.headers.end()) {
            if (*method != "initialize") {
                (void)send_http_response(socket, 400, "Bad Request",
                    http_error_body("SESSION_REQUIRED", "Send initialize before other MCP requests."));
                return;
            }
            auto session = create_session();
            if (!session) {
                (void)send_http_response(socket, 503, "Service Unavailable",
                    http_error_body("RESOURCE_LIMIT", "The app reached its active MCP session limit."));
                return;
            }
            const auto id = find_session_id(session);
            automation::JsonValue response;
            {
                std::lock_guard session_guard(session->mutex);
                response = session->server->handle(message);
                session->last_active_ticks.store(now_ticks(), std::memory_order_release);
            }
            const auto* result = response.get("result");
            const auto* negotiated = result ? result->get("protocolVersion") : nullptr;
            if (response.is_null() || !negotiated || !negotiated->string() ||
                *negotiated->string() != kLegacyProtocolVersion) {
                erase_session(id, session);
                (void)send_http_response(socket, 400, "Bad Request",
                    http_error_body("PROTOCOL_VERSION", "The legacy transport supports MCP 2025-11-25."));
                return;
            }
            const auto body = response.serialize();
            if (body.size() > kMaximumBodyBytes) {
                erase_session(id, session);
                (void)send_http_response(socket, 500, "Internal Server Error",
                    http_error_body("RESPONSE_LIMIT", "The MCP response exceeds the 1 MiB limit."));
                return;
            }
            const auto headers = "MCP-Session-Id: " + id + "\r\nMCP-Protocol-Version: " +
                std::string(kLegacyProtocolVersion) + "\r\n";
            (void)send_http_response(socket, 200, "OK", body, headers);
            return;
        }

        const auto session_id = trim_ascii(session_header->second);
        if (session_id.empty() || session_id.size() > 128 ||
            protocol_header == request.headers.end() ||
            trim_ascii(protocol_header->second) != kLegacyProtocolVersion) {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("PROTOCOL_HEADER", "A valid MCP session and protocol version are required."));
            return;
        }
        auto session = get_session(session_id);
        if (!session) {
            (void)send_http_response(socket, 404, "Not Found",
                http_error_body("SESSION_EXPIRED", "The MCP session expired or was revoked."));
            return;
        }
        if (*method == "initialize") {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("ALREADY_INITIALIZED", "This HTTP session has already initialized."));
            return;
        }
        automation::JsonValue response;
        {
            std::lock_guard session_guard(session->mutex);
            response = session->server->handle(message);
            session->last_active_ticks.store(now_ticks(), std::memory_order_release);
        }
        if (response.is_null()) {
            (void)send_http_response(socket, 202, "Accepted");
            return;
        }
        const auto body = response.serialize();
        if (body.size() > kMaximumBodyBytes) {
            (void)send_http_response(socket, 500, "Internal Server Error",
                http_error_body("RESPONSE_LIMIT", "The MCP response exceeds the 1 MiB limit."));
            return;
        }
        const auto headers = "MCP-Session-Id: " + std::string(session_id) + "\r\nMCP-Protocol-Version: " +
            std::string(kLegacyProtocolVersion) + "\r\n";
        (void)send_http_response(socket, 200, "OK", body, headers);
    }

    void handle_delete(SOCKET socket, const HttpRequest& request) {
        const auto protocol_header = request.headers.find("mcp-protocol-version");
        if (protocol_header != request.headers.end() &&
            trim_ascii(protocol_header->second) == kModernProtocolVersion) {
            (void)send_http_response(socket, 405, "Method Not Allowed", {}, "Allow: POST\r\n");
            return;
        }
        const auto session_header = request.headers.find("mcp-session-id");
        if (session_header == request.headers.end() || protocol_header == request.headers.end() ||
            trim_ascii(protocol_header->second) != kLegacyProtocolVersion) {
            (void)send_http_response(socket, 400, "Bad Request",
                http_error_body("SESSION_REQUIRED", "DELETE requires the active MCP session and protocol version."));
            return;
        }
        const auto session_id = std::string(trim_ascii(session_header->second));
        auto session = get_session(session_id);
        if (!session) {
            (void)send_http_response(socket, 404, "Not Found",
                http_error_body("SESSION_EXPIRED", "The MCP session expired or was revoked."));
            return;
        }
        {
            std::lock_guard session_guard(session->mutex);
            session->server->disconnect_all();
        }
        erase_session(session_id, session);
        (void)send_http_response(socket, 204, "No Content");
    }

    void handle_client(SOCKET socket) {
        DWORD timeout = 30000;
        (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                         static_cast<int>(sizeof(timeout)));
        (void)setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                         static_cast<int>(sizeof(timeout)));
        HttpRequest request;
        int failure_status = 400;
        if (!read_http_request(socket, request, failure_status)) {
            const auto reason = failure_status == 413 ? "Payload Too Large" :
                failure_status == 431 ? "Request Header Fields Too Large" : "Bad Request";
            (void)send_http_response(socket, failure_status, reason,
                http_error_body("INVALID_HTTP", "The request is malformed or exceeds an endpoint limit."));
            return;
        }
        if (request.target != "/mcp") {
            (void)send_http_response(socket, 404, "Not Found",
                http_error_body("ENDPOINT_NOT_FOUND", "Use the configured /mcp endpoint."));
            return;
        }
        const auto host = request.headers.find("host");
        if (host == request.headers.end() || !valid_loopback_authority(host->second, port)) {
            (void)send_http_response(socket, 403, "Forbidden",
                http_error_body("HOST_REJECTED", "Only the configured loopback host is accepted."));
            return;
        }
        const auto origin = request.headers.find("origin");
        if (origin != request.headers.end() && !valid_origin(origin->second, port)) {
            (void)send_http_response(socket, 403, "Forbidden",
                http_error_body("ORIGIN_REJECTED", "Cross-origin requests are not accepted."));
            return;
        }
        const auto authorization = request.headers.find("authorization");
        if (authorization == request.headers.end() || !valid_bearer(authorization->second, token)) {
            (void)send_http_response(socket, 401, "Unauthorized",
                http_error_body("AUTH_REQUIRED", "Use the bearer token shown in AI Connections."),
                "WWW-Authenticate: Bearer\r\n");
            return;
        }
        if (request.method == "GET") {
            (void)send_http_response(socket, 405, "Method Not Allowed", {}, "Allow: POST, DELETE\r\n");
            return;
        }
        if (request.method == "POST") {
            handle_post(socket, request);
            return;
        }
        if (request.method == "DELETE") {
            handle_delete(socket, request);
            return;
        }
        (void)send_http_response(socket, 405, "Method Not Allowed", {}, "Allow: POST, GET, DELETE\r\n");
    }

    void accept_loop() {
        while (!stopping.load(std::memory_order_acquire)) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(listener, &read_set);
            timeval timeout{0, 250000};
            const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
            if (stopping.load(std::memory_order_acquire)) break;
            if (ready == SOCKET_ERROR) break;
            if (ready > 0) {
                const SOCKET client = accept(listener, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    bool accepted = false;
                    {
                        std::lock_guard clients_guard(clients_mutex);
                        if (clients.size() < kMaximumClients && !stopping.load(std::memory_order_acquire)) {
                            clients.push_back(client);
                            accepted = true;
                        }
                    }
                    if (accepted) {
                        auto finished = std::make_shared<std::atomic<bool>>(false);
                        std::thread worker_thread;
                        try {
                            worker_thread = std::thread([this, client, finished] {
                                try { handle_client(client); }
                                catch (...) {
                                    (void)send_http_response(client, 500, "Internal Server Error",
                                        http_error_body("INTERNAL_ERROR", "The embedded MCP request failed."));
                                }
                                (void)shutdown(client, SD_BOTH);
                                {
                                    std::lock_guard clients_guard(clients_mutex);
                                    std::erase(clients, client);
                                }
                                closesocket(client);
                                finished->store(true, std::memory_order_release);
                            });
                            std::lock_guard workers_guard(workers_mutex);
                            workers.push_back(Worker{std::move(worker_thread), finished});
                        } catch (...) {
                            if (worker_thread.joinable()) {
                                worker_thread.join();
                            } else {
                                {
                                    std::lock_guard clients_guard(clients_mutex);
                                    std::erase(clients, client);
                                }
                                (void)send_http_response(client, 503, "Service Unavailable",
                                    http_error_body("RESOURCE_LIMIT", "The embedded MCP worker could not start."));
                                closesocket(client);
                            }
                        }
                    } else {
                        (void)send_http_response(client, 503, "Service Unavailable",
                            http_error_body("RESOURCE_LIMIT", "The embedded MCP endpoint is busy."));
                        closesocket(client);
                    }
                }
            }
            expire_idle_sessions();
            reap_workers();
        }
        is_active.store(false, std::memory_order_release);
    }

    void stop() noexcept {
        stopping.store(true, std::memory_order_release);
        is_active.store(false, std::memory_order_release);
        if (listener != INVALID_SOCKET) {
            (void)shutdown(listener, SD_BOTH);
            closesocket(listener);
            listener = INVALID_SOCKET;
        }
        if (accept_thread.joinable()) accept_thread.join();
        {
            std::lock_guard guard(clients_mutex);
            for (const SOCKET client : clients) (void)shutdown(client, SD_BOTH);
        }
        {
            std::lock_guard guard(workers_mutex);
            for (auto& worker : workers)
                if (worker.thread.joinable()) worker.thread.join();
            workers.clear();
        }
        {
            std::lock_guard guard(sessions_mutex);
            sessions.clear();
        }
        {
            std::lock_guard guard(modern_server_mutex);
            modern_server.reset();
        }
        instances = {};
        app_caller = {};
        port = 0;
        url.clear();
        SecureZeroMemory(token.data(), token.size());
        token.clear();
        if (winsock_started) {
            WSACleanup();
            winsock_started = false;
        }
    }
};

EmbeddedMcpHttpHost::EmbeddedMcpHttpHost() : impl_(std::make_unique<Impl>()) {}
EmbeddedMcpHttpHost::~EmbeddedMcpHttpHost() { stop(); }

bool EmbeddedMcpHttpHost::start(mcp::McpServer::InstanceProvider instances,
                                mcp::McpServer::AppCaller app_caller, std::string& error) {
    stop();
    if (!instances || !app_caller) { error = "The embedded MCP endpoint requires an instance and app dispatcher."; return false; }
    try {
        impl_->clients.reserve(kMaximumClients);
        impl_->workers.reserve(kMaximumClients);
    } catch (...) {
        error = "The embedded MCP worker limits could not be reserved.";
        return false;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "Winsock could not be initialized."; return false; }
    impl_->winsock_started = true;
    if (!secure_random_hex(impl_->token)) { error = "A secure MCP access token could not be generated."; impl_->stop(); return false; }
    impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == INVALID_SOCKET) { error = "The local MCP socket could not be created."; impl_->stop(); return false; }
    BOOL exclusive = TRUE;
    if (setsockopt(impl_->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), static_cast<int>(sizeof(exclusive))) == SOCKET_ERROR) {
        error = "The local MCP socket could not be restricted to this app."; impl_->stop(); return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(impl_->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(impl_->listener, static_cast<int>(kMaximumClients)) == SOCKET_ERROR) {
        error = "The loopback MCP endpoint could not start."; impl_->stop(); return false;
    }
    int address_size = sizeof(address);
    if (getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&address), &address_size) == SOCKET_ERROR) {
        error = "The loopback MCP port could not be read."; impl_->stop(); return false;
    }
    impl_->port = ntohs(address.sin_port);
    impl_->url = "http://127.0.0.1:" + std::to_string(impl_->port) + "/mcp";
    impl_->instances = instances;
    impl_->app_caller = app_caller;
    impl_->stopping.store(false, std::memory_order_release);
    impl_->is_active.store(true, std::memory_order_release);
    try {
        impl_->modern_server = std::make_unique<mcp::McpServer>(
            std::move(instances), std::move(app_caller), &impl_->stopping);
        impl_->accept_thread = std::thread([state = impl_.get()] { state->accept_loop(); });
    } catch (...) {
        error = "The embedded MCP server or listener thread could not start.";
        impl_->stop();
        return false;
    }
    error.clear();
    return true;
}

void EmbeddedMcpHttpHost::stop() noexcept {
    if (impl_) impl_->stop();
}

bool EmbeddedMcpHttpHost::active() const noexcept {
    return impl_ && impl_->is_active.load(std::memory_order_acquire);
}

std::string EmbeddedMcpHttpHost::endpoint() const { return impl_ ? impl_->url : std::string{}; }
std::string EmbeddedMcpHttpHost::access_token() const { return impl_ ? impl_->token : std::string{}; }

} // namespace pikdraw::desktop

#else

namespace pikdraw::desktop {

struct EmbeddedMcpHttpHost::Impl {};

EmbeddedMcpHttpHost::EmbeddedMcpHttpHost() : impl_(std::make_unique<Impl>()) {}
EmbeddedMcpHttpHost::~EmbeddedMcpHttpHost() = default;

bool EmbeddedMcpHttpHost::start(pikdraw::mcp::McpServer::InstanceProvider,
                                pikdraw::mcp::McpServer::AppCaller, std::string& error) {
    error = "The embedded MCP HTTP endpoint is available only on Windows.";
    return false;
}

void EmbeddedMcpHttpHost::stop() noexcept {}
bool EmbeddedMcpHttpHost::active() const noexcept { return false; }
std::string EmbeddedMcpHttpHost::endpoint() const { return {}; }
std::string EmbeddedMcpHttpHost::access_token() const { return {}; }

} // namespace pikdraw::desktop

#endif
