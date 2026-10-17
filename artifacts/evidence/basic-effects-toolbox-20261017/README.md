# Effects toolbox acceptance evidence

These 1440×900 English and Vietnamese BMPs were captured by `test_effects_toolbox_flyout` from the PikDraw `NativeWindow` Win32/GDI paint path. The capture shows the selected Path, the existing Style Properties section, and the new Appearance flyout with its three actions.

- [English](effects-toolbox-english.bmp)
- [Tiếng Việt](effects-toolbox-vietnamese.bmp)

Focused acceptance used `.\tooling\native\build.ps1 -Test -TestRegex '^test_pikdraw_desktop_shell$'`. The first run compiled the changed source and test but could not relink the app executable because Windows reported it was in use. The focused shell target passed CTest 1/1, then the standard runner completed the app relink and passed CTest 1/1 after the process exited. See the task handoff for exact final results.

These are application paint-path captures, not screenshots of the Windows desktop. They do not certify manual accessibility, DPI scaling, narrow-window behavior, all themes, performance, or import/export fidelity with Illustrator/CorelDRAW formats.
