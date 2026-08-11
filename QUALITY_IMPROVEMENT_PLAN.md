# OpenPsalm Editor Quality Improvement Plan

Audit date: 2026-08-10
Audited revision: `4404a4f` (`main`)
Current application version: `0.0.1`

## Purpose

This plan turns the current prototype into a dependable maintainer-grade editor.
The order is deliberate: protect authored song data first, establish repeatable
quality gates second, then improve format coverage, workflow, and distribution.

The editor already has a strong foundation. Its format layer is separated from
the UI, writes are atomic, unchanged files round-trip byte for byte, notation
tokens can be regenerated and reparsed, validation is extensive, edits use undo,
and the real OpenPsalm corpus is exercised locally. The work below should extend
those strengths rather than replace the architecture.

Export is intentionally out of scope. OpenPsalm Editor should continue to edit
the source TOML while the website owns PDF, MIDI, LilyPond, MusicXML, and slide
generation.

## Implementation Status

### Alpha release-safety batch completed 2026-08-10

- [x] Delayed lyric drafts carry their original language identity and every
  destructive/navigation action flushes pending lyric and header edits first.
- [x] Base and translation documents have independent undo/dirty state, visible
  tab markers, Save Current, and Save All.
- [x] Saving compares exact disk bytes with the opened baseline and requires an
  explicit overwrite decision after an external edit, deletion, or path race.
- [x] New songs and translations remain in memory until Save. New-song folders
  are created only during save, occupied directories are refused, and language
  codes are normalized, filename-safe, and duplicate-checked.
- [x] Direct overlay paths open the sibling base once and select the requested
  language. Merged views are refused consistently in Debug and Release.
- [x] Added widget lifecycle tests, Debug and sanitizer gates, offscreen startup
  screenshots, install/package smoke coverage, release documentation, and a
  `0.0.1` CPack archive.

### Functionality batch completed 2026-08-10

- [x] Play now resumes from the paused position and Stop rewinds to the start.
  Notes that began before the resume point but are still sounding are restored.
- [x] Per-part mute choices survive edits, validation refreshes, and language
  changes instead of resetting every time the document changes.
- [x] Base-song, new-song, and metre-change denominator controls now offer only
  supported values: 1, 2, 4, 8, 16, 32, and 64.
- [x] Tick arithmetic now handles `/32` and `/64` metres.
- [x] Validation reports invalid numerators/denominators, non-positive or
  out-of-song metre-change ranges, and overlapping metre changes.
- [x] `ope-check --quiet` now emits only its summary, and `--limit` rejects
  missing, non-numeric, zero, and negative values with exit code 2.
- [x] Added deterministic `AudioTests` and `CliTests`, increasing the suite from
  four to six test executables.

Verification for this batch:

- Warning-as-error application build: passed.
- RelWithDebInfo CTest: 6/6 passed, including the full corpus.
- Current corpus check: 209 files, 0 parse failures, 0 byte-round-trip changes,
  0 notation re-emission mismatches, and 0 errors.
- Warning-as-error Debug build: passed. All tests except the already-documented
  assertion-dependent `DocumentTests` case pass; the new metre tests also pass
  individually in Debug.

## Audit Baseline

### Verified healthy

- The existing `RelWithDebInfo` build completes without compiler warnings.
- All four tests pass in the existing build: `FormatTests`, `DocumentTests`,
  `CorpusTests`, and `SessionTests`.
- The available OpenPsalm corpus covers 208 TOML files with:
  - 0 parse failures
  - 0 byte-round-trip changes
  - 0 notation re-emission mismatches
  - 0 validation errors
  - 172 warnings, which are content guidance rather than gate failures
- A separate Debug build with `OPE_WARNINGS_AS_ERRORS=ON` compiles successfully.
- `QSaveFile` is used for atomic replacement of a single file.
- Unknown TOML constructs are retained, and merged translation views are guarded
  against accidental serialization.

### Verified gaps

1. Pending lyric text is owned by `LyricsPanel`, keyed only by part and section,
   and committed after a 600 ms timer. Switching language or song before that
   timer fires can commit the old text into the newly selected document. Pending
   text also is not reflected by `Session::isDirty()`, so discard checks can miss
   it entirely.
2. A session may contain dirty base and translation documents, but Save writes
   only the current document. The close/open prompt names only the current file
   and does not provide explicit Save All behavior.
3. There is no check that a file changed on disk after it was opened. A later
   atomic save can still overwrite another editor's or a `git pull`'s changes.
4. The Add Translation dialog warns about an existing language but does not
   disable acceptance. Its caller writes immediately, so accepting a duplicate
   can overwrite an existing translation. Free-form language codes also are not
   validated as safe filename components.
5. New Song creates the target directory before the first save, ignores the
   `mkpath` result, and can leave an empty numbered directory after cancellation
   or discard.
6. A Debug test run aborts in `aMergedViewIsNeverWritten()` because the test
   invokes a `Q_ASSERT` path. It passes in release only because assertions are
   compiled out.
7. The documented `--screenshot` smoke path timed out in two clean headless runs
   and produced no image while multimedia initialization attempted unavailable
   PipeWire/PulseAudio backends.
8. `--quiet` is parsed but never consulted. `--limit` accepts missing or invalid
   values without a useful error. **Resolved in the 2026-08-10 functionality
   batch.**
9. Time-signature denominator controls accept every integer from 1 through 32,
   while tick calculation handles only 1, 2, 4, 8, and 16 and silently treats
   everything else as quarter-note units. The validator does not report an
   unsupported denominator. **Resolved in the 2026-08-10 functionality batch.**
10. Play after Pause starts at 0 rather than resuming. Per-part mute choices are
    rebuilt and reset after every document edit. **Resolved in the 2026-08-10
    functionality batch.**
11. Song list scanning is synchronous on the GUI thread and calls the full song
    loader even though the source comment promises a header-only scan.
12. There is no committed CI configuration, no widget-level workflow suite, no
    application icon/desktop integration, and no release packaging.
13. `README.md` links to `OPE.md`, but that file is absent.

## Priority 0 — Data Integrity and Safe Document Lifecycles

These changes should land before expanding the editor's feature set.

### 0.1 Make pending edits document-owned and impossible to misroute

Changes:

- Move draft identity to a structure that includes the exact document/language,
  part, and lyric section, or commit drafts synchronously before any context
  change.
- Add one `flushPendingEdits()` orchestration point and invoke it before Save,
  Save All, language changes, opening/reloading/new-song actions, undo/redo,
  translation creation, and window close.
- Mark the session visibly dirty as soon as editable text diverges, not only
  after a debounce timer fires.
- Cancel or reject stale timer callbacks by document generation/token so a
  callback created for one song cannot mutate another.
- Apply the same contract to header and inspector controls; widget focus should
  not be the persistence boundary.

Acceptance:

- Type lyrics and immediately switch language, switch song, reload, undo, close,
  or press Save; the text either lands in its original document or the action is
  cancelled, never in a different document.
- Unsaved prompts appear for uncommitted text.
- Automated tests cover all of those transitions with the debounce timer still
  pending.

### 0.2 Introduce explicit per-document dirty state and Save All

Changes:

- Add APIs such as `dirtyLanguages()`, `isDirty(language)`, `save(language)`, and
  `saveAll()` to `Session`.
- Show a dirty marker on each language tab and name every dirty file in discard
  prompts.
- Keep Ctrl+S as Save Current and add Save All with the platform-standard
  shortcut where available.
- Do not allow navigation/close after a partial or cancelled save; report which
  files were saved and which remain dirty.
- Define undo ownership per document. Prefer one undo stack per language so an
  undo in Spanish cannot silently switch the UI to English.

Acceptance:

- Editing base plus one or more translations cannot lose changes through open,
  reload, close, or quit.
- Saving one language does not mark another language clean.
- Undo/redo history and action labels always refer to the selected language.

### 0.3 Detect external modification before overwrite

Changes:

- Record a content hash (and inexpensive size/mtime hints) at load and successful
  save.
- Immediately before saving, compare the current disk bytes to that fingerprint.
- On conflict, offer Reload, Save a Copy, Compare, or explicit Overwrite. Never
  silently choose one.
- Optionally use `QFileSystemWatcher` for early notification, but retain the
  pre-save byte check because filesystem notifications are advisory.
- Preserve the current byte-minimal serializer when there is no conflict.

Acceptance:

- An external edit made after opening cannot be overwritten without a clear,
  explicit choice.
- Conflict behavior is covered for current files, translations, deleted files,
  and files replaced by `git pull`/rename.

### 0.4 Make creation paths non-destructive

Changes:

- Disable Translation dialog acceptance for an empty, duplicate, malformed, or
  unsafe code. Normalize and validate a conservative BCP-47 filename form.
- Recheck target existence immediately before writing, not only when the dialog
  is constructed.
- Keep new translations in the session as unsaved documents so creation follows
  the same undo/save/discard rules as every other edit.
- Defer new-song directory creation until save. Check and report directory
  creation failures, and never leave an empty directory after discarding a new
  document.
- Refuse to reuse any non-empty numbered directory without an explicit recovery
  workflow.

Acceptance:

- Existing translation files cannot be replaced through Add Translation.
- Language input cannot escape or create unexpected paths.
- Discarding an unsaved new song or translation leaves the filesystem unchanged.

### 0.5 Harden save completion and direct-file opening

Changes:

- Treat failure to reload a just-written file as a save failure that blocks
  further editing until the in-memory/disk state is reconciled; do not merely
  mark stale spans clean.
- Make serialization of merged views return an explicit error in all build modes
  instead of relying on an assertion plus a release fallback.
- When a command-line or file-manager path points to `song_LANG.toml`, normalize
  it to the sibling `song.toml`, open the family once, and select `LANG`.
- If one overlay is malformed, offer to open the valid base and quarantine that
  translation as an error tab rather than making the whole song inaccessible.

Acceptance:

- Debug and release builds have identical save-safety behavior.
- Directly opening a translation produces one base document plus one correctly
  selected overlay, without duplicate language tabs.

## Priority 1 — Reproducible Quality Gates

### 1.1 Add workflow-level Qt tests

Create a `UiWorkflowTests` target that instantiates the real `MainWindow` with a
temporary songs root and a controllable/fake audio service. Cover:

- open, edit, validate, save, reload, and byte-minimal diff behavior;
- pending edits during language/song/window transitions;
- multi-language dirty state, Save Current, Save All, undo, and discard;
- duplicate translation refusal and new-song cleanup;
- external modification conflicts and failed writes;
- shortcuts, problem navigation, enabled/disabled states, and selection;
- compact and large window layouts using deterministic screenshots or geometry
  assertions.

Keep core tests headless and fast. Widget tests should use dependency injection
for dialogs and audio rather than relying on a live desktop/audio server.

### 1.2 Make every supported build mode pass

Changes:

- Fix the assertion-dependent merged-view test so Debug CTest passes.
- Run Debug with assertions, Release/RelWithDebInfo, and warnings-as-errors.
- Add AddressSanitizer plus UndefinedBehaviorSanitizer on Linux.
- Add targeted property/fuzz tests for the TOML parser, notation tokenizer, span
  editor, and parse-edit-serialize-reparse invariants.
- Set finite CTest timeouts and make corpus skips visible in CI rather than a
  silent green substitute for corpus coverage.

### 1.3 Add CI as the merge gate

Add GitHub Actions jobs for:

- Linux GCC and Clang build/test on the documented minimum Qt 6.5/CMake 3.25;
- a current-Qt warnings-as-errors build;
- sanitizer tests;
- Windows and macOS compile/package smoke checks;
- a pinned or checked-out OP-songs corpus compatibility job;
- `ope-check` CLI behavior and install-tree smoke tests;
- a deterministic offscreen/Xvfb UI startup screenshot test.

Cache only dependencies/build products, not test results. Upload failed test logs
and UI screenshots as artifacts.

### 1.4 Make automation independent of audio hardware

Changes:

- Initialize multimedia lazily on first Play, or inject an audio backend.
- Add an explicit no-audio mode for tests and screenshots.
- Make `--screenshot` validate its output write, print errors, and exit nonzero on
  failure or timeout.
- Test startup with no audio device, a rejected format, and a runtime sink error.

Acceptance:

- The screenshot command exits on its own and creates a valid PNG in a clean
  headless environment.
- The editor opens normally even when PipeWire/PulseAudio is absent or broken.

## Priority 2 — Format and Editing Correctness

### 2.1 Validate metadata before it reaches tick arithmetic

Changes:

- [x] Represent denominators with a constrained selector, not a free integer spin
  box. Support every denominator the OpenPsalm seeder supports and reject the
  rest consistently in base fields and metre-change rows.
- [x] Add validation for numerator, denominator, measure/duration ranges,
  overlapping metre changes, and changes that extend past the song.
- [ ] Add corresponding validation for tempo and verse-count ranges.
- [ ] Port authoritative edge cases from the Rust seeder and document any deliberate
  permissiveness.
- [ ] Replace the hard-coded new-song copyright year with a tested current-year or
  release-year policy.

Acceptance:

- The UI cannot create a metre that the tick engine interprets differently.
- Hand-authored invalid values produce actionable findings rather than silent
  fallback behavior.

### 2.2 Complete safe notation editing surfaces

The parser understands more than the score UI can comfortably edit. Add a
compact inspector/toolbar for dynamics, hairpins, tempo spanners, chord pitches,
tuplets, and dedup offsets. Keep raw-token preservation and expose a clear
advanced source fallback for constructs not yet editable.

Every new operation must:

- be one undo step and be a no-op when it changes nothing;
- retain unrelated raw token spelling and file layout;
- reparse to an equivalent event;
- show validation changes immediately;
- work correctly in overlays without writing inherited notation accidentally.

### 2.3 Repair and test CLI behavior

Changes:

- [x] Honor `--quiet` for findings while retaining the summary.
- [x] Reject missing, non-numeric, zero/negative (unless deliberately supported), or
  otherwise invalid `--limit` values with exit code 2.
- [ ] Use a single command-line parser shared by `ope` and `ope-check`; reject unknown
  GUI options rather than accidentally skipping their following argument.
- [ ] Define single-overlay checking: locate/merge its sibling base or report that a
  complete validation was not possible.
- [ ] Add tests for every remaining option, path type, output mode, and exit-code
  class. Positive and invalid `--limit` parsing now has focused coverage.

## Priority 3 — Workflow, Performance, and Accessibility

### 3.1 Fix and finish playback behavior

Changes:

- [x] Resume from the paused position; Stop alone should rewind.
- [x] Preserve mute selections by part name across document refreshes and language
  changes.
- [ ] Report sink state/errors after startup instead of showing an apparently active
  transport with no sound.
- [ ] Add a seek slider and optional play-from-selection now that pause/resume is
  stable.
- [ ] Test tempo overrides, mute persistence, end-of-song state, and device loss.
  A note already sounding at the resume point now has deterministic coverage.

### 3.2 Keep the song browser responsive and honest

Changes:

- Implement the promised header-only scan or move full scanning to a worker with
  generation/cancellation guards.
- Show scanning, empty, invalid-root, and partial-error states.
- Debounce search, retain selection, and display translation-specific parse
  problems without blocking base-song discovery.
- Benchmark cold scan and search against a corpus several times larger than the
  current one; set a responsiveness budget before optimizing.

### 3.3 Persist useful workspace state

Persist window geometry, dock state, active central tab, score zoom/layout,
problem filters, and sensible transport preferences. Version the settings schema
and provide a Reset Layout command so stale settings are recoverable.

### 3.4 Improve discoverability and accessibility

Changes:

- Add score editing actions to menus/toolbars/context menus so keyboard shortcuts
  are accelerators rather than the only discoverable interface.
- Give icon-only and custom-painted controls accessible names/descriptions and
  expose score selection/status to assistive technology.
- Verify complete keyboard traversal, visible focus, mnemonic conflicts, screen
  reader labels, high-DPI scaling, 200% text scaling, and right-to-left layout.
- Replace fixed low-contrast text colors with palette-aware semantic colors and
  test light, dark, and high-contrast themes.
- Persist neither accessibility-hostile fixed heights nor clipped labels at a
  compact window size.

## Priority 4 — Maintainer and Release Readiness

### 4.1 Repair and expand documentation

- Add the promised `OPE.md` architecture/design document or remove the dead link.
- Add `CONTRIBUTING.md` with build presets, test/corpus setup, formatting, format
  parity rules, and release checks.
- Maintain `CHANGELOG.md` and define when project version `0.0.1` advances.
- Document settings location, backup/conflict behavior, supported platforms,
  audio limitations, and recovery from a malformed translation.
- Document the synchronization process for the bundled language registry and
  Rust seeder rule/format changes.

### 4.2 Add install and release artifacts

- Add application icons, Linux `.desktop`/AppStream metadata, and platform bundle
  metadata.
- Verify `cmake --install` into a clean prefix and run both installed binaries.
- Add reproducible Linux, Windows, and macOS packages with license and version
  metadata.
- Publish checksums and a short signed release checklist; defer automatic update
  behavior until packages and data-safety work are mature.

### 4.3 Establish maintenance signals

- Track crash-free startup, corpus compatibility, startup/scan time, and test
  duration as release gates without collecting song content or user telemetry.
- Add issue templates for corrupted output, format mismatch, audio, and UI
  accessibility reports, each requesting the diagnostics needed to reproduce.

## Suggested Pull Request Sequence

1. Pending-edit lifecycle tests and fix.
2. Per-document dirty state, per-language undo, and Save All.
3. External-change conflicts plus safe song/translation creation.
4. Explicit serialization/save errors and direct-overlay opening.
5. Debug-test repair, UI workflow harness, and lazy/no-audio startup.
6. CI matrix, sanitizers, corpus gate, and screenshot smoke test.
7. Time-signature/metadata validation and CLI correctness.
8. Playback state, browser responsiveness, and workspace persistence.
9. Accessible editing actions and remaining notation-editing surfaces.
10. Documentation, icons, install verification, and release packaging.

Each pull request should keep the corpus guarantees at zero parse failures,
zero unchanged-file diffs, and zero re-emission mismatches. Changes to parsing,
serialization, overlay merging, or editing must add focused fixtures before they
land. Any intentional change in validator warnings should be reviewed against
the full corpus and recorded in the changelog.

## Definition of Maintainer-Ready

The application is ready for a broader release when:

- no UI transition can lose or misroute pending edits;
- concurrent disk edits are detected before overwrite;
- base and translation files have explicit, tested save/undo ownership;
- Debug, Release, sanitizer, installed-binary, CLI, corpus, and UI smoke gates
  pass in CI;
- startup succeeds without audio hardware and playback state is predictable;
- every value the UI can author is either format-valid or immediately diagnosed;
- compact, high-DPI, dark, keyboard-only, and screen-reader workflows have been
  checked;
- the repository contains accurate architecture, contribution, install, and
  release documentation.
