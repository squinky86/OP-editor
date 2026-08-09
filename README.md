# OpenPsalm Editor

**OpenPsalm Editor** is a fast, native desktop application for authoring, editing, and validating SATB choral hymn scores and song metadata in the OpenPsalm TOML format.

Copyright (C) 2026 Jon Hood, OpenPsalm.com  
Licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later).

---

## Features

- **Multi-Staff Score Rendering**: Live graphical rendering of SATB parts (Treble and Bass staves), noteheads, stems, rests, spacers, and measure barlines using Qt Graphics Framework.
- **Bi-directional Editing**: Edit directly via the visual score or the tabbed per-part plain text note stream.
- **Spec-Compliant TOML Serialization**: Full support for standard OpenPsalm base songs (`song.toml`) and translation overlays (`song_<lang>.toml`) with deterministic field ordering and formatting.
- **Rigorous Semantic Validation**:
  - Exact measure-by-measure duration checking against time signature (48 ticks per quarter note).
  - Cross-part measure count parity verification.
  - Tuplet integrity and barline crossing detection.
  - Phrase break syntax and tick bound validation (`M:T`).
  - Syllable vs note slot alignment checks with melisma/slur support.
  - Style guide enforcement (e.g. non-soprano tempo spanners, unpaired verse suppression).
- **Translation Overlays**: Seamless handling of inherited base song fields and language-specific overrides.
- **Undo / Redo**: Full transaction-based `QUndoStack` integration across all editing operations.
- **Song Corpus Browser**: Instant navigation and search across all songs and overlays in `/home/jon/projects/OpenPsalm/songs`.

---

## Building and Running

### Prerequisites
- Linux / GCC 11+ (C++20 compliant compiler)
- CMake 3.20 or newer
- Qt 6 (Core, Gui, Widgets, Test)

### Build Instructions

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### Running Tests

```bash
ctest --output-on-failure
# Or run the test executable directly:
./tests/OpenPsalmTests
```

### Running OpenPsalm Editor

```bash
./src/OpenPsalmEditor [/path/to/song.toml]
```

---

## Architecture Overview

```
OpenPsalmEditor/
├── src/
│   ├── app/           # Application entry point, CLI arguments, persistent settings
│   ├── core/          # Song model (SongData, PartData, LyricMap, SongDocument, Commands)
│   ├── notation/      # Music theory AST, Pitch, Duration, NoteToken, NoteParser, LyricAligner
│   ├── format/        # TOML parsing (toml++), serialization, translation overlay merger
│   ├── validation/    # Format and style-guide validation engine
│   └── ui/            # Qt Widgets: MainWindow, ScoreView, NotesEditor, LyricsEditor, etc.
├── tests/             # Comprehensive QTest test suite
├── third_party/       # Vendored header-only dependencies (toml++)
└── docs/              # Specifications and architectural documentation
```

For more details on the architecture and file format, refer to [ARCHITECTURE.md](docs/ARCHITECTURE.md) and [FORMAT_SPEC.md](docs/FORMAT_SPEC.md).
