# Appearance and Effects Properties evidence

The English and Vietnamese images are 1440 x 900 captures from `NativeWindow::capture_screenshot_bmp` in the desktop shell acceptance test. The capture selects one editable Path and resets the Style dock to its first page, so Object Opacity, Fill Opacity, and Stroke Opacity are visible together with the selected object and its appearance context. The palette section now starts below the one-line opacity help, with visible spacing and no overlapping labels.

The same acceptance test exercises Blend mode, Gaussian Blur, Drop Shadow, parameter edits, effect ordering/enabling, undo, and selection compatibility. The two images focus on the opacity controls; they do not display the complete effect stack at once because the Style dock is vertically scrollable.

Verification: `.\tooling\native\build.ps1 -Test -TestRegex '^test_pikdraw_desktop_shell$'` — Debug build/relink succeeded and focused CTest passed 1/1 (2026-10-17). These files were captured and visually inspected after that run.

These files capture the application Win32/GDI paint path, not the full OS desktop. They do not establish manual interaction, DPI/accessibility, full Release behavior, performance, or Illustrator/CorelDRAW/AI/CDR fidelity. Current interop remains feature-scoped; SVG subset behavior does not imply AI/CDR support.
