# OpenPsalmEditor Project Guidelines & Agent Instructions

## Workspace & Environment
- **Project Root:** `/home/jon/projects/OpenPsalmEditor` (also accessible at `/home/jon/project/OpenPsalmEditor`)
- **Project Plan:** `/home/jon/projects/OpenPsalmEditor/OSE.md`
- **Target Platform:** Linux desktop (C++20 / Qt 6 Widgets / CMake)

## Licensing & Copyright Requirements
- **License:** GNU Affero General Public License version 3 (AGPL-3.0-or-later). The root `LICENSE` contains the AGPL v3 text.
- **Copyright Header:** Every new source/header file must begin with:
```cpp
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later
```

## Architecture & Modules
As outlined in `OSE.md`:
1. `src/format`: TOML reading/writing, translation overlay inheritance/merging, source span tracking, and diagnostics.
2. `src/notation`: OpenPsalm note token parser, lexer, AST, duration math, and lyric slot computation.
3. `src/core`: Song domain model (`SongDocument`, `SongData`, `Part`, `LyricMap`), undo/redo command system (`QUndoStack`).
4. `src/validation`: Spec compliance and style guide checks.
5. `src/ui`: Qt 6 Widgets UI, simplified score view (`QGraphicsView`), inspectors, syntax-highlighted note text editor, diagnostics panel, and song browser.
6. `src/app`: Application entry point, configuration, recent files, and OpenPsalm path integration.
7. `docs/`: Comprehensive project documentation, format guide, architecture, and user guides.
8. `tests/`: Unit and golden tests using QtTest and CTest.

## File Operations
- Always reference files within `/home/jon/projects/OpenPsalmEditor/`.
- Treat OpenPsalm TOML specification (`OSE.md`) as the single source of truth.
