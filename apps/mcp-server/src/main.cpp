#include "pikdraw/mcp_server.hpp"

#include <iostream>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    pikdraw::mcp::McpServer server;
    constexpr size_t max_line_bytes = 1024 * 1024;
    std::string line;
    bool oversized = false;
    for (int ch; (ch = std::cin.get()) != EOF;) {
        if (ch != '\n') {
            if (!oversized && line.size() < max_line_bytes) line.push_back(static_cast<char>(ch));
            else oversized = true;
            continue;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!oversized) {
            pikdraw::automation::JsonValue request;
            std::string error;
            if (pikdraw::automation::JsonValue::parse(line, request, error)) {
                const auto response = server.handle(request);
                if (!response.is_null()) std::cout << response.serialize() << '\n' << std::flush;
            } else {
                pikdraw::automation::JsonValue response(pikdraw::automation::JsonValue::Object{
                    {"jsonrpc", "2.0"}, {"id", nullptr},
                    {"error", pikdraw::automation::JsonValue::Object{{"code", -32700}, {"message", "Parse error."}}}});
                std::cout << response.serialize() << '\n' << std::flush;
            }
        } else {
            pikdraw::automation::JsonValue response(pikdraw::automation::JsonValue::Object{
                {"jsonrpc", "2.0"}, {"id", nullptr},
                {"error", pikdraw::automation::JsonValue::Object{{"code", -32700}, {"message", "MCP line exceeds 1 MiB."}}}});
            std::cout << response.serialize() << '\n' << std::flush;
        }
        line.clear(); oversized = false;
    }
    return 0;
}
