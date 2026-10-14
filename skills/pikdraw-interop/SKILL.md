---
name: pikdraw-interop
description: Build or review PikDraw PDF/CDR/AI/EPS/DXF/SKP adapters using separate fidelity, editability and export capability evidence.
---

# PikDraw interchange

Read modules/interop/AGENTS.md. For the targeted format/version, specify import, editable object coverage, metadata retention and source-format export independently.

Use synthetic or redistributable fixtures. Include missing fonts, clipping, transparency, gradients, color profiles, units and malformed data relevant to the format. Compare rendered output plus editable structure; a raster-perfect preview does not prove editability.

Do not infer full AI support from a PDF-compatible representation, CDR write support from a reader, or SKP licensing from SDK availability. Verify primary documentation and record the exact dependency/version/license before integration.

Run conversion outside the editor process with cancellation and resource bounds. Report each substitution, outline/flatten operation and unsupported feature. Read [quality gates](../../docs/quality/gates.md) when defining release evidence.
