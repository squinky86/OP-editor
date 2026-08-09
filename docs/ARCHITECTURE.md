# OpenPsalm Editor - Architecture & Technical Design

Copyright (C) 2026 Jon Hood, OpenPsalm.com  
SPDX-License-Identifier: AGPL-3.0-or-later

---

## 1. Modular Subsystem Architecture

The codebase is strictly structured into modular components:

```
┌────────────────────────────────────────────────────────┐
│                        UI Layer                        │
│ (MainWindow, ScoreView, NotesEditor, LyricsEditor...)  │
└───────────┬────────────────────────────────────────────┘
            │
┌───────────▼────────────────────────────────────────────┐
│                       Core Model                       │
│  (SongDocument, SongData, PartData, Undo Commands)     │
└─────┬───────────────────┬──────────────────────────────┘
      │                   │
┌─────▼─────────────┐ ┌───▼──────────────┐ ┌─────────────▼─────────┐
│     Notation      │ │   Validation     │ │        Format         │
│ (Pitch, Duration, │ │ (Validator,      │ │ (TomlLoader,          │
│  NoteParser...)   │ │  Diagnostics)    │ │  TomlSerializer,      │
│                   │ │                  │ │  OverlayMerger)       │
└───────────────────┘ └──────────────────┘ └───────────────────────┘
```

---

## 2. Core Timing & Tick Representation

- **Internal Ticks**: 48 ticks per quarter note (`Ticks::Quarter = 48`).
  - Whole note: 192 ticks
  - Half note: 96 ticks
  - Quarter note: 48 ticks
  - Eighth note: 24 ticks
  - Sixteenth note: 12 ticks
- **Phrase Ticks**: 16 ticks per quarter note (`scale factor = 3`). Used in `phrase_breaks` specifications (`M:T` notation).
- **Tuplet Calculation**: A tuplet of ratio `N:M` (e.g. `3:2` triplet) scales the base note duration by `M / N`.
  - Triplet quarter: `(48 * 2) / 3 = 32` ticks.

---

## 3. Translation Overlay Semantics

When opening a translation file such as `song_es.toml`:
1. The editor locates the companion base file `song.toml` in the same directory.
2. `OverlayMerger` computes the merged effective `SongData`:
   - Top-level scalar overrides (e.g., `title`, `language`, `verse_count`) replace base values.
   - Array fields (e.g., `phrase_breaks`, `copyrights`, `[[time_sig_changes]]`) perform wholesale replacement if defined.
   - `[parts.X]` merges field-by-field with the corresponding base part.
   - `[lyrics.*]` or `[parts.X.lyrics.*]` perform wholesale replacement of the respective lyric table if present in the overlay.
3. Editing an overlay persists *only* the overridden keys back to `song_es.toml`, leaving base data intact.

---

## 4. Undo/Redo Architecture

All mutations are encapsulated in `QUndoCommand` subclasses (`EditMetadataCommand`, `EditPartNotesCommand`, `EditLyricsSectionCommand`, `AddPartCommand`, `RemovePartCommand`). The `SongDocument` exposes the centralized `QUndoStack` and handles dirty-state tracking automatically.
