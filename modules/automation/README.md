# PikDraw automation service

This module exposes typed, UI-independent application operations to automation adapters.
It is not an MCP server, COM server, transport, authorization system, or proof of installed
client compatibility. External callers must enter through a host adapter that authenticates
the principal and dispatches calls onto the owning document writer thread.

Document mutation uses PikDraw document commands, strict epoch/revision checks and undo.
The first verified operation is rectangle creation in document millimeters. Layer targeting
uses the current layer index because the document model does not yet assign stable layer IDs.
Capability descriptors report only handlers in this service.
