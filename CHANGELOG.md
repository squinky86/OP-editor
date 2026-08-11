# Changelog

## Unreleased — 0.1 beta work

- Added a managed OP-songs HEAD download that stages the archive, rejects unsafe
  paths and sizes, runs the complete corpus checker, retains the old directory
  as a timestamped backup, and switches the library only after success.
- Added deterministic offline, TLS/proxy, cancellation, hostile/truncated ZIP,
  validation-failure, replacement, and rollback coverage. Fixed snapshot
  provenance metadata being omitted when the validated tree was installed.
- Exposed whole-corpus checking as structured data shared by the GUI and CLI.
- Added a prefilled GitHub song-problem action for the current song.
- Added exact-byte contribution preflight and ZIP bundles containing proposed
  TOML, a unified patch, report, optional copyright evidence, and hashes. The
  session-opened baseline survives saves, and GitHub handoff stores no token.
- Made new-song submissions identity-free: attach the generated `song.toml`
  directly or paste its copied TOML code block, and let corpus maintainers
  assign the upstream ID.
- Added the 0.1 contribution, documentation, integrity, and release plan.

## 0.0.1 — first alpha

- Added byte-minimal editing and atomic saves for OpenPsalm song TOML.
- Added score, lyrics/alignment, metadata, part inspector, problems, source
  preview, song browser, translation, and new-song workflows.
- Added deterministic built-in playback with pause/resume and per-part mute.
- Added per-language undo and dirty state, Save Current, and Save All.
- Added pre-save detection of external edits, deletions, and newly occupied
  paths so corpus files are never silently overwritten.
- Kept new songs and translations in memory until an explicit save, with safe
  language-code and duplicate validation.
- Protected delayed lyric and header edits across tab, song, undo, reload, and
  window transitions.
- Added time-signature, tempo, verse, notation, lyric, and overlay validation.
- Made Aiken seven-shape noteheads the default score view, including la-based
  minor-key assignments and duration-appropriate filled and hollow heads.
- Preserved the alignment grid's horizontal position when phrase-break edits
  rebuild the Lyrics view.
- Added `ope-check`, full-corpus compatibility checks, deterministic audio/CLI
  tests, and focused widget workflow tests.
- Added reproducible Debian amd64 and self-contained static Windows x64 package
  builds with runtime and architecture verification in GitHub Actions.
