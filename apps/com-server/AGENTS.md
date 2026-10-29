# com-server scope

Windows COM IDispatch and automation runtime bridge for external Windows scripting clients.
Translates legacy automation requests to typed operations handled by the unified PikDraw AutomationService.
Must respect threading apartment boundaries (STA/MTA) and same-user security tokens.
Write operations adhere to the same interaction busy guards as MCP and named pipe clients.

Read root AGENTS.md, C10 agent automation, and this app README.
Run tests via `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_com_dispatch`.
