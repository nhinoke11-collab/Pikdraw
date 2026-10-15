# Third-Party Software and Toolchain Notices

This document details third-party components, dependencies, toolchains, and licenses used in the PikDraw project.

---

## 1. Toolchains and Compilers

### LLVM / Clang
- **Version**: 18+ / LLVM-MinGW UCRT x86_64
- **License**: Apache License 2.0 with LLVM Exception
- **Website**: https://llvm.org/
- **Copyright**: Copyright (c) 2003-2026 LLVM Team.

### Microsoft Visual C++ Build Tools & Windows SDK
- **Version**: MSVC 19.44+ (Visual Studio 2022) / Windows SDK 10.0.22621+
- **License**: Microsoft Software License Terms
- **Copyright**: Copyright (c) Microsoft Corporation. All rights reserved.

### CMake
- **Version**: 3.25+
- **License**: BSD 3-Clause License
- **Website**: https://cmake.org/
- **Copyright**: Copyright 2000-2026 Kitware, Inc. and Contributors.

### Ninja Build System
- **Version**: 1.11+
- **License**: Apache License 2.0
- **Website**: https://ninja-build.org/
- **Copyright**: Copyright 2011 Google Inc.

### Python Runtime
- **Version**: 3.10+
- **License**: Python Software Foundation License (PSFL)
- **Website**: https://www.python.org/
- **Copyright**: Copyright (c) 2001-2026 Python Software Foundation.

---

## 2. Frameworks and Core Libraries (Spike Baseline / Roadmap)

### Qt Framework (Qt Widgets, Core, GUI)
- **Version**: 6.8+
- **License**: GNU Lesser General Public License version 3 (LGPLv3) / Commercial
- **Website**: https://www.qt.io/
- **Usage**: Native desktop shell, dock layouts, native window hosting, external QSS/form styling.
- **Copyright**: Copyright (C) 2026 The Qt Company Ltd.

### Skia Graphics Library
- **Version**: Chrome m128+ baseline
- **License**: BSD 3-Clause License
- **Website**: https://skia.org/
- **Usage**: High-performance 2D vector drawing, text shaping, surface rendering.
- **Copyright**: Copyright 2011 Google Inc. All rights reserved.

### Clipper2 Polygon Clipping and Offsetting
- **Version**: 1.4+
- **License**: Boost Software License 1.0 (BSL-1.0)
- **Website**: https://www.angusj.com/clipper2/
- **Usage**: Robust 2D polygon Boolean operations, offset curves, topology validation.
- **Copyright**: Copyright (c) 2010-2026 Angus Johnson.

---

## 3. Platform Security and Isolation

### Windows AppContainer and Job Objects
- **Usage**: Process isolation, network denial, resource quotas, and IPC containment for third-party file converters and worker processes.
- **Platform**: Microsoft Windows Native Platform APIs.
