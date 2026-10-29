# mcp-server scope

Standalone Model Context Protocol (MCP) sidecar process providing stdio and modern transport bridges to PikDraw application instances.
All requests validate JSON-RPC schema and permissions before dispatching via same-user named pipe IPC to the desktop host.
Session table has a hard limit of 64 concurrent sessions with automated TTL expiration to prevent resource exhaustion from orphaned connections.
No domain graphics algorithms or document storage logic belong in this transport adapter.

Read root AGENTS.md, C10 agent automation, and this app README.
Run tests via `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_mcp_server`.
