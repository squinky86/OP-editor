# OpenPsalmEditor Plan

## Purpose

OpenPsalmEditor is a desktop C++/Qt application for editing OpenPsalm song data files from `/home/jon/projects/OpenPsalm/songs/{id}/song.toml` and translation overlays such as `song_es.toml`. The target is a simplified MuseScore-like editor: a user should be able to open a song, view the SATB score, edit notes/lyrics/metadata with immediate validation, and save TOML that remains strictly compatible with the OpenPsalm format documented in:

- `/home/jon/projects/OpenPsalm/songs/docs/song-toml-format.md`
- `/home/jon/projects/OpenPsalm/songs/docs/song-style-guide.md`

The first product version should be an editor for existing OpenPsalm TOML, not a full general-purpose notation system. The strict requirement is lossless, spec-aligned editing of the OpenPsalm song corpus.

## Non-Negotiable Format Contract

The editor must treat OpenPsalm TOML as the source of truth. It should not invent a parallel project format, silently normalize unsupported constructs, or write TOML that the OpenPsalm seeder cannot import.

### File Layout

- Base songs live as `songs/{integer_id}/song.toml`.
- Translation overlays live beside the base as `song_{lang}.toml`, where `{lang}` is a BCP-47 language code.
- Base `song.toml` must be complete.
- Translation overlays are partial files: omitted fields inherit from `song.toml`.
- `active = false` is per file.

### Top-Level Fields

The editor must support all documented top-level fields:

- Required for complete base song data: `title`, `verse_count`, `key_signature`, `time_sig_numerator`, `time_sig_denominator`, `tempo_bpm`, and at least one `[parts.*]` with `notes`.
- Optional: `active`, `subtitle`, `language`, `time_sig_changes`, `phrase_breaks`, `optional_phrase_breaks`, `non_breaking_phrase_breaks`, `copyrights`, `commentary`, `converge_verses`.
- `time_sig_changes` is an array of tables with `measure`, `numerator`, `denominator`, and `duration`.
- `copyrights` is a full string array and should preserve ordering; the OpenPsalm arrangement/license line normally stays last for public-domain arrangements.

### Parts

Every `[parts.Name]` must support:

- `choral_type`: commonly `soprano`, `alto`, `tenor`, `bass`, but the format allows custom values.
- `clef`: `treble`, `bass`, `treble_8`.
- `staff_number`: positive integer; parts sharing a staff render together.
- `notes`: OpenPsalm note stream.
- `suppress_verses`: integer array.
- `suppress_verses_when`: string array, case-insensitive choral-type comparison.
- `[parts.Name.lyrics.KEY]` per-part lyrics, merged over global lyrics key by key in base files.

The current OpenPsalm code and song 13 also use `splice_lyrics_into`, but it is not documented in `song-toml-format.md`. The editor should initially parse and preserve it, display it as an advanced existing-field property, and mark it as "implemented in OpenPsalm, pending spec documentation" until the format spec is updated.

### Notes

The editor must parse, preserve, validate, render, and write the full note-token grammar used by OpenPsalm:

- Pitches: lowercase steps `a` through `g`; accidentals `is`, `es`, plus importer-supported double forms `isis`, `eses`; octave marks `'` and `,`; base octave is 3.
- Durations: `1`, `2`, `4`, `8`, `16`, `32`, `64`; dots after duration.
- Rests and spacers: `r4`, `s4`, etc.
- Chords/divisi: `<c' e'>4`, where one duration applies to all chord pitches and only the first pitch consumes lyrics.
- Measure separators: explicit ` | ` in serialized output.
- Tuplets: `{N ... }` and `{N:M ... }`; default `M` is largest power of 2 less than `N`; tuplets may not span a barline.
- Regular slurs: `(` and `)`.
- Dashed slurs: `-(` and `-)`.
- Beams: `[` and `]`; in OpenPsalm these are lyric-slot-affecting melisma markers, not merely beat grouping.
- Combined slur/beam shorthand already accepted by the parser: `[(` and `])`.
- Ties: `~`.
- Fermatas: `!`.
- Staccatos: `-.`.
- Chorus and coda section markers: `@c`, `@e`; they may appear on rests, and the marker applies to the next lyric slot if the marked event is not a lyric slot.
- Dynamics: `%ppp`, `%pp`, `%p`, `%mp`, `%mf`, `%f`, `%ff`, `%fff`, `%fp`, `%sfz`.
- Hairpins: `\<`, `\>`, `\!`.
- Tempo/expression spanners: `\rit`, `\ritard`, `\rall`, `\accel`, `\string`, `\atempo`, `\spanend`; these are song-level markings and should be authored on soprano only.
- Deduplication tick offsets: `/N`, `/+N`, `/-N`, immediately after duration and dot, for lyric-dedup fingerprinting only.

The serializer should produce canonical readable note streams while preserving every semantic flag. It may normalize whitespace and measure line wrapping, but must not drop unknown fields or comments until a deliberate TOML-preservation strategy exists.

### Lyrics

The editor must support:

- Global numbered verses: `[lyrics.1]`, `[lyrics.2]`, etc.
- Global chorus: `[lyrics.chorus]`.
- Global coda: `[lyrics.coda]`.
- Shared lyric sections: `[lyrics.sN]`, referenced by standalone `@sN` tokens.
- Per-part lyric maps: `[parts.Name.lyrics.KEY]`.
- Lyric syllable parsing by whitespace, with ` -- ` as a connector inside multi-syllable words.
- `_` placeholder syllables for verse-dependent melismas.
- U+203F undertie (`‿`) as a single lyric-slot elision marker for translations.

Shared lyric sections must be validated like OpenPsalm: `[lyrics.sN]` entries are templates, not verse rows; shared sections may not reference other shared sections; malformed or missing `@sN` references are hard errors.

### Phrase Breaks

The editor must support all three phrase-break arrays:

- `phrase_breaks`: poetic line breaks, sheet/slides breaks, and lyric-dedup boundaries.
- `optional_phrase_breaks`: optional visual/poetry break opportunities and lyric-dedup boundaries.
- `non_breaking_phrase_breaks`: lyric-dedup boundaries only.

Each value is `"M:T"` with 1-based measure number and 64th-note ticks inside the measure. A 4/4 measure is 64 phrase ticks; a quarter note is 16 phrase ticks.

Important implementation split: OpenPsalm importer validation uses internal ticks scaled by 3 so tuplets stay integral (`quarter = 48`, `eighth = 24`, `64th = 3`). Phrase-break strings use 64th-note ticks (`quarter = 16`). The editor must keep both representations explicit and convert deliberately.

### Translation Overlay Rules

For `song_{lang}.toml`, the effective song is `base song.toml + overlay`:

- Present top-level scalar/array values replace inherited values; absent values inherit.
- `copyrights` replaces the whole list.
- `[[time_sig_changes]]` replaces the whole array.
- `[parts.X]` merges field-wise with base part `X`.
- A new `[parts.X]` not in the base is added and must be complete after merge.
- Defining any global `[lyrics.*]` entry replaces the entire global lyric map.
- Defining any `[parts.X.lyrics.*]` entry replaces that part's entire lyric map.
- The merged result must validate as a complete song.

The UI should make this visible: an overlay editor should show inherited values as inherited, changed values as overridden, and allow reverting an override by deleting it from the overlay file.

## Product Scope

### Version 0.1 Goal

Build a stable OpenPsalm TOML editor with:

- Open existing `song.toml` and `song_{lang}.toml` files.
- Browse songs under `/home/jon/projects/OpenPsalm/songs`.
- Edit metadata, copyright lines, phrase breaks, time signatures, parts, note streams, and lyrics.
- Show a simplified score view for note placement and lyric alignment.
- Validate against the documented OpenPsalm format and style guide.
- Save TOML in a deterministic, OpenPsalm-compatible form.
- Run OpenPsalm validation/export commands from the UI or from tests.

### Explicit Non-Goals For 0.1

- Full MuseScore feature parity.
- Arbitrary MusicXML import/export inside the Qt app.
- WYSIWYG engraving equivalent to LilyPond.
- Audio synthesis beyond simple preview/MIDI handoff.
- Editing OpenPsalm database rows directly.
- Replacing OpenPsalm's Rust seeder/exporter.

## Technology

### Core Stack

- Language: C++20 or C++23.
- UI framework: Qt 6 Widgets for a desktop-native editor.
- Build: CMake.
- TOML parser: `toml++` or another actively maintained C++ TOML library that preserves enough source information for diagnostics. If comment-preserving writeback is required, introduce a concrete syntax layer instead of relying only on DOM serialization.
- Tests: Qt Test for Qt-facing units plus plain CTest for parser/model units.
- Optional rendering helpers: `QGraphicsView`/`QGraphicsScene` for score editing; `QPainter` for custom staff and note drawing.

### Repository Layout

```text
OpenPsalmEditor/
  CMakeLists.txt
  OSE.md
  src/
    app/
    core/
    format/
    notation/
    validation/
    ui/
  tests/
    fixtures/
    golden/
  third_party/
  docs/
```

Recommended module split:

- `format`: TOML loading, overlay merging, serialization, source spans, diagnostics.
- `notation`: note-token lexer/parser, AST, duration math, lyric-slot computation.
- `core`: song domain model and document commands.
- `validation`: spec and style checks.
- `ui`: Qt widgets, score view, inspectors, dialogs.
- `app`: application startup, settings, recent files, OpenPsalm path discovery.

## Architecture

### Document Model

Use a single in-memory `SongDocument` that owns both raw file representation and semantic model:

- `SongFile`: path, file kind (`base` or `translation_overlay`), language code, parse diagnostics, raw TOML table.
- `SongData`: semantic equivalent of OpenPsalm's Rust `SongData`.
- `EffectiveSongData`: merged base + overlay when editing a translation.
- `Part`: name, choral type, clef, staff number, notes, lyric overrides, suppression fields, advanced preserved fields.
- `NoteStream`: ordered measures; each measure has ordered events; each event may be a single note/rest/spacer or chord.
- `NoteToken`: pitch/rest/spacer/chord plus duration and all flags.
- `LyricMap`: global and per-part lyric sections.
- `PhraseBreak`: field kind, measure number, phrase tick.
- `Diagnostic`: severity, code, message, file path, source span when available, related note/part/measure.

Maintain enough source mapping to show diagnostics on the exact TOML line or note token.

### Editing Model

Use command objects for all changes:

- Metadata edits.
- Add/remove/reorder copyright line.
- Add/remove/edit part.
- Insert/delete note/rest/spacer/chord event.
- Change pitch, duration, dots, accidentals, octave.
- Toggle tie, slur, dashed slur, beam, fermata, staccato.
- Add/change/remove dynamic, hairpin, tempo spanner, dedup offset.
- Add/edit lyrics and shared sections.
- Add/edit phrase breaks.
- Overlay-specific commands: override inherited field, revert override.

Commands should be undoable with `QUndoStack`. Every command should mark affected validation scopes dirty so validation can run incrementally.

### Score View

Use `QGraphicsView` for a simplified notation surface:

- Vertical grouping by staff number.
- Voices rendered in deterministic OpenPsalm part order: Soprano, Alto, Tenor, Bass, numeric suffixes after base names, then custom names.
- Measures laid out horizontally with proportional widths based on effective time signatures.
- Notes drawn as simplified heads/stems/rest glyphs sufficient for editing.
- Chords displayed as stacked noteheads.
- Spacers shown as faint placeholders in edit mode.
- Slurs, dashed slurs, ties, beams, fermatas, staccatos, dynamics, hairpins, and section markers displayed as editable annotations.
- Phrase breaks shown as vertical markers at `"M:T"` positions, with visual distinction among required, optional, and non-breaking.
- Lyric rows displayed under the relevant staff/part with alignment to computed lyric slots.

Do not try to match LilyPond engraving in 0.1. The view's job is editing confidence and validation, with LilyPond/PDF remaining the final engraver.

### Text View

Include a synchronized TOML/note text editor for precision:

- Show raw `notes` blocks and lyric text.
- Syntax highlight OpenPsalm note tokens.
- Highlight parse and validation errors inline.
- Selecting a token in text selects the score event, and selecting a score event selects the token in text.

This is critical because OpenPsalm's notation contains semantic flags that are faster to author directly than through dialogs.

## Validation

Validation must be split into hard format errors and style-guide warnings.

### Hard Errors

Hard errors block saving by default, with an explicit unsafe override only if needed for preserving an already-invalid file:

- TOML parse failure.
- Missing required complete-song fields.
- Invalid field type.
- Invalid key signature.
- Invalid time signature denominator.
- Empty parts in a complete song.
- Part without `notes` after overlay merge.
- Unknown or malformed note token.
- Unclosed chord or tuplet.
- Tuplet crossing a barline.
- Measure duration mismatch using effective time signature.
- Parts with unequal measure counts or incompatible measure totals.
- Invalid phrase-break syntax.
- Phrase break points that do not land on a real event boundary.
- Lyric syllable count mismatch per part and verse/chorus/coda.
- `[lyrics.coda]` without `@e` in a part that uses those lyrics.
- Missing, malformed, or nested shared lyric references.
- Translation overlay whose merged result is incomplete or invalid.

### Style Warnings

Style warnings should be shown prominently but not always block saving:

- Missing `phrase_breaks` at poetic line ends.
- Pickup measure not padded with spacers.
- Beat-beamed notes that appear to carry different syllables.
- Melisma not marked with beam/slur.
- Dashed slur used when all verses use `_`.
- Same-pitch slur that should be a tie.
- Tie into a rest or non-identical pitch.
- Dynamics/hairpins not duplicated on every sounding part at the same moment.
- Unclosed hairpin.
- Tempo spanner on non-soprano part.
- Fermata/staccato not duplicated on all sounding voices.
- Chorus text present without an `@c` marker, especially when chorus starts with rests.
- Coda text present without `@e`.
- Shared repeated lines pasted into every verse instead of `[lyrics.sN]`.
- `suppress_verses` present without `suppress_verses_when`, or vice versa.
- Translation lyrics inheriting accidentally from the base because of partial lyric-map override.
- Copyright line style issues such as trailing periods or missing OpenPsalm arrangement line.

### Validation Parity

The C++ validator should intentionally mirror the OpenPsalm Rust parser/importer behavior. For confidence:

- Build a golden fixture set from several real songs: simple SATB, pickup, chorus with rests, coda, tuplets, shared lyrics, translation overlay, `splice_lyrics_into`, time signature changes.
- For every fixture, compare C++ validation results with `rm openpsalm.db && cargo run` or a smaller OpenPsalm-side validation command if one is added.
- Keep parser unit tests for tricky token combinations such as `c''4%p!`, `e''4\!%f`, `c''4@c-.`, `a4/+24(`, `<g g,>4!@c`, `{5:4 c'16 d'16 e'16 f'16 g'16 }`.

## Parser and Serialization Strategy

### Note Parser

Implement a lexer/parser rather than editing note strings ad hoc:

- Lex measures by barlines while preserving source offsets.
- Parse tuplets as explicit group nodes.
- Parse chords by reading `<...>` pitch lists and one trailing duration/flag suffix.
- Strip suffix flags in a loop so order-insensitive combinations match OpenPsalm behavior.
- Store unknown/unparsed token text in diagnostics and never guess a correction silently.
- Convert durations to internal ticks with the OpenPsalm scale: whole `192`, half `96`, quarter `48`, eighth `24`, 16th `12`, 32nd `6`, 64th `3`.
- Convert phrase-break ticks separately using phrase scale: whole `64`, half `32`, quarter `16`, eighth `8`, 16th `4`, 32nd `2`, 64th `1`.

### TOML Serialization

Initial serializer should be deterministic:

- Preserve field order close to the documented examples.
- Write scalar metadata first.
- Write phrase-break arrays compactly.
- Write `copyrights` as multiline array.
- Write each `[parts.Name]` followed immediately by nested `[parts.Name.lyrics.KEY]` sections.
- Write global `[lyrics.KEY]` sections last.
- Use triple-quoted multiline strings for `notes`.
- Keep ` | ` as measure separator and wrap long note streams by measure.

Longer-term improvement: add a concrete TOML source preservation layer so comments and original formatting survive round-trips. Until that exists, the app should warn before first saving a file that comments/formatting may be normalized.

## UI Plan

### Main Window

- Left sidebar: song browser rooted at the configured OpenPsalm songs directory.
- Center: tabbed editor with Score, Notes Text, Lyrics, Metadata, and Validation tabs.
- Right inspector: context-sensitive properties for selected note, measure, phrase break, part, lyric section, or metadata field.
- Bottom panel: diagnostics list with severity filters and click-to-focus.
- Toolbar: open, save, undo, redo, add note/rest/spacer, duration selector, slur/tie/beam toggles, dynamic/hairpin menus, phrase-break controls.

### Song Browser

- Default root: `/home/jon/projects/OpenPsalm/songs`, configurable in settings.
- Show song ID, title, language variants, active status, and validation status.
- Open base or specific translation overlay.
- Detect changed files on disk and prompt before reload.

### Metadata Editor

- Dedicated fields for title, subtitle, active, language, verse count, key, time signature, tempo, commentary.
- Table editors for time signature changes and copyright lines.
- Phrase-break list editor with validation and score-overlay sync.

### Part Editor

- Part list with name, choral type, clef, staff number, and measure count.
- Add/remove/rename parts.
- Edit suppression fields and advanced fields.
- Show inherited/overridden state for translation overlays.

### Lyrics Editor

- Global lyric table by key (`1`, `2`, `chorus`, `coda`, `s1`, etc.).
- Per-part lyric override table.
- Live syllable count per lyric section against computed slot count.
- Shared-section reference highlighting for `@sN`.
- Elision support for `‿`, with a menu action to insert it.

### Notation Editing

For 0.1, prioritize reliable structured edits:

- Select an event and edit properties in the inspector.
- Insert/delete events in the selected part/measure.
- Change duration and pitch using keyboard shortcuts plus inspector controls.
- Toggle flags from toolbar/inspector.
- Chord editor for adding/removing pitches in a divisi event.
- Tuplet command to wrap selected events in `{N:M ... }`.
- Phrase-break command to add a break at the selected event boundary.

Direct mouse-driven dragging can wait until the semantic editor is stable.

## Integration With OpenPsalm

The editor should remain loosely coupled to OpenPsalm but use it as the oracle:

- Configurable OpenPsalm repository path, default `/home/jon/projects/OpenPsalm`.
- Optional command integration:
  - run OpenPsalm seed validation;
  - export LilyPond/PDF for the active song;
  - export MIDI for playback checks.
- Show external command output in a Qt output panel with file/line links when possible.

Prefer adding a small validation/export CLI to OpenPsalm later over scraping full `cargo run` startup output. Until that exists, tests can call existing commands manually or through scripts.

## Testing Plan

### Unit Tests

- TOML load/merge/serialize.
- Translation overlay inheritance and wholesale lyric replacement.
- Note lexer/parser for all token flags.
- Duration and time-signature tick math.
- Lyric parser and syllable-type classification.
- Lyric slot computation with slurs, beams, dashed slurs, ties, chords, rests, spacers, chorus-first songs, coda markers.
- Phrase-break validation and tick conversion.

### Golden Tests

Use real fixtures copied or referenced from OpenPsalm:

- Song 1: standard SATB hymn.
- Song 13: chorus/rest markers, suppression, `splice_lyrics_into`.
- Song 101 or current shared-section reference song: `[lyrics.sN]` and `@sN`.
- Song 103: extra voice/part behavior.
- Song 162: translation overlay (`song_es.toml`) and elisions.
- Any current song using `time_sig_changes`.
- Any current song using tuplets, dynamics, hairpins, or tempo spanners.

Golden checks:

- Parse without diagnostics beyond known style warnings.
- Serialize and reparse to equivalent semantic AST.
- Measure totals match OpenPsalm.
- Lyric slot counts match OpenPsalm.
- Overlay merge output matches OpenPsalm behavior.

### Manual Acceptance

Before considering 0.1 complete:

- Open, edit, save, and re-open at least five representative corpus songs.
- Run the OpenPsalm seeder after saved edits.
- Export PDF for edited songs and inspect lyric alignment.
- Confirm translation overlay save writes only intended overrides.
- Confirm an invalid edit produces a precise diagnostic before save.

## Implementation Phases

### Phase 1: Project Skeleton

- Create CMake/Qt application skeleton.
- Add dependency strategy for TOML parsing.
- Add `SongDocument`, `SongData`, `PartData`, `LyricData`, and diagnostics types.
- Add basic file open/save and recent-files support.

### Phase 2: Format Core

- Implement TOML parse into semantic model.
- Implement translation overlay merge.
- Implement deterministic TOML serialization.
- Add fixture-based tests against real OpenPsalm songs.

### Phase 3: Note and Lyric Engine

- Implement note parser/serializer.
- Implement duration math and effective time signatures.
- Implement lyric parser and lyric-slot computation.
- Implement hard validation diagnostics.

### Phase 4: Basic Qt Editor

- Add song browser.
- Add metadata, parts, lyrics, and note text tabs.
- Add diagnostics panel.
- Add undoable edit commands for metadata, lyrics, parts, and raw note text.

### Phase 5: Score View

- Add simplified staff rendering.
- Add note/rest/spacer/chord rendering.
- Add selection sync between score and text.
- Add inspector-based structured note editing.
- Add phrase-break overlays.

### Phase 6: Style Guide Assistant

- Add style-guide warnings.
- Add quick fixes where safe: duplicate dynamics, insert `@c`, add missing paired suppression field, convert same-pitch slur to tie.
- Keep all quick fixes previewable and undoable.

### Phase 7: OpenPsalm Integration

- Add configurable OpenPsalm command runner.
- Add seed validation integration.
- Add PDF/MIDI export integration.
- Add output panel and artifact links.

## Risks and Decisions

- Comment-preserving TOML editing may require a custom concrete syntax layer. The initial deterministic serializer is simpler but should warn before normalizing existing files.
- Qt does not provide music engraving primitives. Use a simplified editing renderer and rely on OpenPsalm/LilyPond for final output.
- The public spec and implementation currently diverge on `splice_lyrics_into`. Preserve and surface the field, but do not expand the public contract until the spec is updated.
- Validation parity matters more than UI convenience. Any UI control that cannot represent the full OpenPsalm semantics should be treated as a helper around the underlying token model, not as a replacement for it.

## Definition of Done For Initial Project Plan

- The codebase can be initialized from this document without additional architectural decisions.
- The data model lists every documented OpenPsalm construct.
- The parser/validator plan matches the OpenPsalm Rust importer behavior where inspected.
- Translation overlays are treated as overlays, not independent full files.
- The UI plan supports both structured notation editing and raw text precision editing.
