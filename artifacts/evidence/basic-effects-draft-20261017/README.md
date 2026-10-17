# Effect draft acceptance evidence · 2026-10-17

Win32/GDI `NativeWindow` captures at 1440×900 from `test_pikdraw_effect_draft` show the Appearance Properties dock with a Gaussian Blur draft and Preview enabled in English and Vietnamese.

- `effect-draft-preview-english.bmp`
- `effect-draft-preview-vietnamese.bmp`

Verification: `.\tooling\native\build.ps1 -TestsOnly -TestRegex '^test_pikdraw_effect_draft$'` passed 1/1. A subsequent full `-Test` build compiled the draft test source, then stopped while compiling `test_desktop_shell.cpp` in the active selection-resize task (`geometry`/`vector` names are unresolved near lines 2123–2176). The shared shell suite therefore could not be rebuilt or run to the Effects cases. An earlier build attempt also encountered `Permission denied` linking `pikdraw.exe` while that app instance was open; the app is now closed.

These are paint-path captures, not OS desktop screenshots. They do not verify manual keyboard interaction, DPI/accessibility, performance, Release configuration, or external-format fidelity. Draft rendering is synchronous; async cancellation latency and worker-generation behavior are not claimed.
