# CDR import runtime notices

PikDraw loads a separately built `pikdraw_cdr_bridge.dll` in the isolated import worker. The bridge calls libcdr and librevenge, both supplied as pinned MSYS2 UCRT64 packages. Archive names, versions, SHA-256 values and dependency scope are listed in [`../../libcdr.lock.json`](../../libcdr.lock.json).

- `libcdr` 0.1.9: Mozilla Public License 2.0. See `libcdr-MPL-2.0.txt`.
- `librevenge` 0.0.6: MPL 2.0 or LGPL 2.1 or later. See both copied license texts.
- ICU 78.3: ICU license. See `ICU-LICENSE.txt`.
- Little CMS 2.19.1: MIT. See `lcms2-LICENSE.txt` and the bundled fast-float notice.
- zlib-ng compatibility 2.3.3: zlib license. See `zlib-ng-LICENSE.md`.
- GCC 16.2 runtime: GPL and GCC Runtime Library Exception. See the copied GCC texts.
- winpthreads 14.0.0: license text is in `libwinpthread-LICENSE.txt`.
- `libunwind.dll` is copied from the configured LLVM-MinGW toolchain. LLVM uses Apache 2.0 with LLVM exceptions; the license text is in `LLVM-LICENSE.txt`.

The app imports CDR; it does not include a CDR writer or claim source-format round-trip support. Generated SVG is normalized into PikDraw's editable subset, and fidelity limits remain visible in the import review dialog.
