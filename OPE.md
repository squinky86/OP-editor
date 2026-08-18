# OpenPsalm Editor design

OpenPsalm Editor is a thin, safety-oriented desktop layer over the OpenPsalm
song TOML format. The authored TOML remains the source of truth; the application
does not maintain a second database or export format.

## Data-safety contract

- Opening and saving an untouched file is byte-identical. Parsed values retain
  their original byte spans, and normal edits splice only changed fields.
- Unknown keys and TOML that OPE does not model are retained untouched.
- Every save uses `QSaveFile`, so replacement is atomic on the destination
  filesystem.
- The bytes on disk are compared with the bytes originally opened immediately
  before saving. An external edit, deletion, or newly occupied path requires an
  explicit overwrite decision.
- Base songs and every translation have independent dirty state and undo
  history. Save Current writes one file; Save All writes every dirty file.
- New songs and translations stay in memory until Save. Cancelling or
  discarding them creates no file or directory.
- Translation lyric maps follow the seeder's wholesale-overlay rule. Before one
  inherited lyric is edited, the complete effective map is materialized into
  the overlay so sibling verses are not lost.

The release gate is the real corpus: parse, unchanged round trip, notation
re-emission, and validation must all complete with zero failures.

## Layers

- `src/core/Toml.*` parses TOML and tracks source spans.
- `src/core/Song.*` maps TOML into authored song documents, merges translation
  overlays, and performs byte-minimal serialization.
- `src/core/Notation.*`, `Lyrics.*`, `Validator.*`, and `Playback.*` implement
  notation, alignment, diagnostics, and deterministic playback plans.
- `src/app/Session.*` owns the open document family, per-language undo/dirty
  state, exact editable source, disk-conflict baselines, selections, save
  conflict checks, and the derived effective document.
- `src/ui/` contains Qt Widgets views. Views mutate authored data only through
  `Session`; delayed lyric drafts carry their original language identity.
- `ope-check` runs the same parser, serializer, notation, and validation code
  without a GUI.

## Adding a TOML field

Add the field to `SongDocument` or `Part`, bind it in `io::load`, include it in
dirty/clean tracking, serialize it in both `serialize` and `serializeFresh`, add
validation where necessary, and expose an editing control with its TOML name in
the tooltip or field reference. Add a fixture proving unchanged bytes remain
identical and that editing only the new field reparses correctly.

If OpenPsalm introduces a field before OPE models it, edit it directly in the
Source pane. Valid TOML becomes the session document and updates the structured
views immediately. Invalid source remains available to repair, but it cannot be
saved and structured editing is paused so two competing drafts cannot overwrite
one another.
