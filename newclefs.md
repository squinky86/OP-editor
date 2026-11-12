# New clefs, TTBB roles, and OpenPsalm TOML compatibility

Status: **historical implementation plan; implemented for 0.2.0.**
Prepared 2026-09-23 for a new agent working in OpenPsalmEditor. See the
[implementation validation record](docs/clef-support-validation.md) for results
and differences from this planning snapshot.

Implement support for the updated OpenPsalm source format throughout the editor:
loading, validation, score display, Inspector editing, lyrics, playback, new-song
creation, translations, and contribution checks. Song 369 is the real acceptance
example. Opening or editing its metadata must never apply another octave shift.

## Context and authoritative references

OpenPsalm's TTBB implementation is present in the sibling working tree. At this
audit, OpenPsalmEditor HEAD was `e52a893`, OpenPsalm HEAD was `974f706`, and the
OpenPsalm feature included local changes beyond HEAD. Read the working files;
checking out the recorded commits alone will not reproduce that state.

Read these before implementing:

- [OpenPsalm TOML format](../OpenPsalm/songs/docs/song-toml-format.md), especially
  Part Definitions, TTBB arrangements, suppression/splicing, and translations.
- [Song style guide](../OpenPsalm/songs/docs/song-style-guide.md).
- [Implemented TTBB plan](../OpenPsalm/refactors/TTBB.md) and
  [implementation/source validation](../OpenPsalm/refactors/TTBB-validation.md).
- [Canonical clefs](../OpenPsalm/src/music/clef.rs),
  [voice policies](../OpenPsalm/src/music/voicing.rs), and
  [import validation/overlay merge](../OpenPsalm/src/seed/data.rs).
- [MIDI playback](../OpenPsalm/src/export/midi.rs) and
  [export preparation](../OpenPsalm/src/routes/songs/exports.rs) for tempo ownership.
- This repository's [README](README.md), [design](OPE.md), and existing tests.

The editor remains a standalone Qt/C++ application. Port the small musical
policies into its QtCore layer; these references are not runtime dependencies.
The website's HTTP filters, export renderers, and external solfège handoff need
no desktop counterparts for this task. OPE still edits TOML and monitors audio.

## Audit findings

The editor working tree was clean before this document was created. No applicable
`AGENTS.md` was found in this repository or its parent directories during the audit;
check again when starting implementation.

The existing `src/ope-check` binary was run read-only against
`../OpenPsalm/songs/369/song.toml`. It reported zero parse failures, zero unchanged
round-trip differences, zero note-regeneration mismatches, zero errors, and five
warnings: `W-CLEF` for both tenor clefs and `W-CT` for the three new role strings.
This was an existing September 8 build, not a freshly rebuilt baseline. Rebuild
before recording implementation comparisons.

| Area | Current behavior and required work |
| --- | --- |
| `src/core/Song.h`, `Song.cpp` | All affected fields are already strings/optional fields. Parsing, span-preserving serialization, and field-wise overlays already support the data shape. `Part::sortRank()` infers SATB order from **part names**; Baritone consequently follows Bass. Replace that policy with role-aware ordering without reordering stored document indices or untouched source bytes. |
| `src/core/Validator.cpp` | `validClefs()` permits only treble/bass/treble_8; `validChoralTypes()` permits only SATB. Unsupported clefs are only warnings. There is no matching positive-staff/shared-clef validation. Rule R5.3 requires soprano tempo markers. |
| `src/ui/ScoreView.cpp` | `bottomLineDiatonic()` recognizes only bass and treats everything else as ordinary treble. `paintStaff()` draws bass or treble only. Stems follow the part's array position on the staff, not its role. Staff clef comes from the first part encountered. |
| `src/ui/Glyphs.h`, `Glyphs.cpp` | Vector G and F clefs exist; a C-clef path is needed. Use the existing vector system. |
| `src/ui/Panels.cpp` | Inspector role/clef/splice choices use the old validator lists. The clef combo is noneditable: an unknown value can leave a previous selection visible. `commitPart()` reads all controls, so another metadata edit can then overwrite the unsupported clef. An unknown splice target similarly falls back to `(none)`. |
| `TransportBar` in `src/ui/Panels.cpp` | Playback labels use `part->name.left(3)`: Tenor1 and Tenor2 both display as `Ten`. Mute state is correctly keyed by exact part name; preserve that. |
| `src/core/Playback.cpp` | `collectSpans()` chooses the first part carrying any tempo span in the old display order. It should use the authored arrangement lead. `TempoMap` is built before muting; retain that useful property. |
| `src/core/Lyrics.cpp`, `src/ui/LyricsPanel.cpp` | Most behavior is already generic. Splice matching lowercases strings but does not trim them and takes the first raw document match. Ordered panels should follow the new shared policy while retaining exact part identities. |
| `src/ui/Dialogs.h`, `Dialogs.cpp` | New Song offers a SATB checkbox, otherwise one soprano. Add a TTBB preset without adding an `arrangement` TOML field. |
| `src/cli/`, contribution/download validation | Already use the core parser/validator. Test the updated behavior through these entry points instead of adding separate TTBB validators. |

## Format and musical contract

### Existing keys, expanded values

There are **no new TOML keys or schema version** required by the TTBB change.
Retain the existing fields and serialization rules:

| Field | Contract |
| --- | --- |
| `[parts.<name>]` | The name remains a stable author-chosen identifier. Role, clef, and pitch do not rename it. |
| `choral_type` | Documented values: `soprano`, `alto`, `tenor`, `bass`, **`tenor1`, `tenor2`, `baritone`**. Match exact values after trimming and lowercasing. |
| `clef` | Canonical values: `treble`, `bass`, `treble_8`, **`alto`, `tenor`**. Parse case/whitespace consistently with OpenPsalm. |
| `staff_number` | A supplied number must be positive. Parts explicitly sharing a staff must have the same effective clef. Omission leaves a part on its own staff. |
| `suppress_verses_when` | Array of exact normalized role strings; allow the new roles. `tenor` does not mean Tenor I or II. |
| `splice_lyrics_into` | Exact normalized target role; allow all new roles, retain fallback reporting when the target is absent. |
| `notes`, lyrics, phrase breaks | Their grammar, timing units, and inheritance have not changed. |

Unknown/custom choral types remain preservable with the existing warning/fallback
behavior; the Rust importer does not reject every custom role. Unsupported
explicit clefs, including bare `C`, `baritone`, or an empty string, are import
errors. Omitted clef retains the historical `treble` default. For example, an
omitted clef and explicit bass on the same numbered staff conflict; do not infer
bass for the omitted field. Validate an overlay's effective merged document.

Normalize for interpretation. Loading, displaying, switching parts, or saving
without edits must not normalize the original bytes or materialize omitted fields.
Explicit edits and newly created data should write canonical values.

### Clefs and geometry

Notes always encode **absolute sounding pitch**: `c` = C3/MIDI 48,
`c'` = C4/MIDI 60. Clef changes affect placement only. Standard alto and tenor
C clefs do not transpose. `baritone` is a vocal role on an ordinary bass clef.

The following bottom-line indices use the editor's existing
`Pitch::diatonic() = octave * 7 + stepIndex`, with C = 0:

| TOML clef | Sign / line from bottom | Octave indication | Bottom-line sounding pitch | Diatonic index |
| --- | --- | --- | --- | --- |
| `treble` | G / 2 | none | E4 | 30 |
| `bass` | F / 4 | none | G2 | 18 |
| `treble_8` | G / 2 | 8 below; sounds one octave below the written G-clef position | E3 | 23 |
| `alto` | C / 3 | none | F3 | 24 |
| `tenor` | C / 4 | none | D3 | 22 |

Draw the C glyph with its center/reference at C4: middle line for alto, fourth
line for tenor. Keep ledger lines, noteheads, accidentals, ties, hit boxes and
selection overlays tied to the same geometry. `treble_8` currently uses E4 as its
bottom-line anchor in OPE, despite absolute sounding pitches; correct that display
offset while leaving stored notes and audio unchanged. An octave indication is
not permission to shift playback.

For an unsupported/conflicting clef, retain editable source and show a clear
finding/invalid display state. Do not silently present it as a valid treble score.

### Roles, staves, and lead

| Conventional part ID | Role | Friendly label | TTBB staff | Clef | Shared-staff stem |
| --- | --- | --- | --- | --- | --- |
| `Tenor1` | `tenor1` | Tenor I | 1 | tenor | up |
| `Tenor2` | `tenor2` | Tenor II | 1 | tenor | down |
| `Baritone` | `baritone` | Baritone | 2 | bass | up |
| `Bass` | `bass` | Bass | 2 | bass | down |

Preserve SATB order S, A, T, B and stems S/T up, A/B down. TTBB order is
T1, T2, Baritone, Bass. OpenPsalm's general ordinary role rank is
`soprano, tenor1, alto, tenor2, tenor, baritone, bass`; use it for mixed roles too.
Within a role, use trailing numeric suffixes numerically, then deterministic name
tiebreakers (e.g. Bass, Bass2, Bass10). Unknown roles sort deterministically after
known roles. Neither pitch crossings nor part names determine voice identity.

The lead is the first part under that ordinary order: normally Soprano for SATB,
Tenor I for TTBB, with deterministic fallback when absent. Tempo spanners belong
to that part. A muted lead still supplies the global tempo map. A lone displayed
voice gets free stems; repeated roles and extra staves need deterministic handling.
Keep independent events editable at unisons and crossings.

OPE does not currently perform the website's compact voice merging. Support the
roles with independent streams; adding a compact engraver is outside this plan.

## Implementation sequence

### 1. Share metadata policies in the core layer

Add focused QtCore-only helpers, for example `src/core/Clefs.{h,cpp}` and
`src/core/Voicing.{h,cpp}`, and register them in `src/CMakeLists.txt`.

- Provide canonical parsing, known-value lists and labels; distinguish an absent
  clef from an unsupported explicit value. Share the clef reference/geometry data
  with the score view instead of duplicating string switches.
- Provide role ordering, preferred stems, and lead selection. Retain original
  `Part::name` and document indices as identities.
- Replace `Part::sortRank()`/`partsInDisplayOrder()` policy. Sort views of parts or
  index lists; `Selection`, `PlaybackNote::partIndex`, overlay edits and undo must
  continue to identify the same underlying part.
- Keep `validClefs()`/`validChoralTypes()` as delegating APIs if useful to existing
  callers. Avoid a second hardcoded list in dialogs or panels.

### 2. Bring validation and round trips into agreement

Update `src/core/Validator.cpp`:

- Accept the five canonical clefs and seven documented roles, including normalized
  case/whitespace. Preserve warnings for unrecognized roles.
- Add error findings for unsupported explicit clefs, nonpositive supplied staff
  numbers, and mismatched effective clefs on a shared explicit staff. Name the
  song/parts/staff involved. Omitted staff identities must not accidentally collide
  with numbered staves through the current `i + 1` display fallback.
- Change R5.3 to identify the arrangement lead by part identity, including both
  start markers and `\spanend`; a second part with the same role is not a second
  independent global tempo owner.
- Check suppression/splice role interpretation and report unavailable targets
  without erasing lyrics or rewriting the authored target.

Retain `Field<T>` spans, unknown keys/comments, `io::serialize()` splice behavior,
translation inheritance and per-language dirty/undo state. The format additions
do not warrant a parser or serializer rewrite. Check clef-only, role-only,
notes-only and lyrics-only overlays, plus unsupported values in a source draft.

### 3. Render and select the new staves correctly

Update `src/ui/Glyphs.*` and `src/ui/ScoreView.*`:

- Add one reusable vector C-clef path with a documented reference origin. Place
  it on line 3 or 4 via the shared descriptor; retain G/F paths and the octave 8.
- Replace both the treble fallback in `bottomLineDiatonic()` and the binary
  bass/treble branch in `paintStaff()`.
- Build staff membership and within-staff display order with shared policies,
  retaining original part indices. Honor role stems even when TOML tables are
  shuffled or parts cross. Preserve a usable single-voice stem policy.
- Make Alt+Up/Down follow visible part order through an index mapping;
  `moveSelectionToPart()` currently increments raw document indices.
- Check note selection at ledger lines, shared unisons and crossings. Selection,
  keyboard pitch edits and undo must affect the intended independent voice.
- Reserve enough header space for the C clef and distinguishable part labels at
  small/large zoom. Check all displayed lyric verses and phrase-break rulers.

The editor currently draws editing notation, not a LilyPond-equivalent page; for
example it uses the key for Aiken shapes without a separately engraved key-signature
block. Preserve that scope while making clef-dependent note positions correct.
Do not copy the source scan's historical clef glyph into this renderer.

### 4. Make metadata and voice controls safe to use

Update `src/ui/Panels.*`, relevant `LyricsPanel` labels/order, and field help in
`src/ui/MainWindow.cpp`:

- Clef choices should identify Treble, Bass, Treble 8vb, Alto (C on line 3), and
  Tenor (C on line 4). Store canonical values as combo item data. Use friendly
  role labels with the corresponding canonical `choral_type` values.
- Retain arbitrary part IDs in labels/tooltips where needed to distinguish
  duplicated roles; lyrics editors must still show the exact TOML table they edit.
- Preserve unsupported/current values visibly instead of selecting a previous
  value or `(none)`. This applies to clef, role, splice target and out-of-range
  staff controls. An unrelated Inspector edit must never coerce another field.
- Compare effective values appropriately so merely opening a mixed-case value or
  inherited field does not create a source edit. Commit only changed metadata
  through the existing `Session::mutate()` undo boundary.
- Keep clef edits part-specific and report resulting shared-staff conflicts.
  Do not silently change a partner's metadata or a translation's inherited notes.
- Give transport voices distinct labels (`T1`, `T2`, `Bar`, `Bass`, or full names)
  and accessible/tool-tip names. Continue muting by exact part ID, including
  numbered siblings. Verify each solo, staff pair, and all-muted silence.
- Refresh lyrics panel order/labels on role edits without losing text waiting to
  be committed. Use normalized exact role matching and deterministic target order
  in `analyseSplice()`.

### 5. Preserve sounding pitch and tempo during playback

Update lead selection in `src/core/Playback.cpp` and its documentation. Build the
tempo map from the authored lead before applying `mutedParts`; do not choose the
first remaining audible part or the first arbitrary part with markers. If the
lead has no markers, a non-lead's invalid markers must not silently take over.

Keep pitch production in `Pitch::midiNote()` and existing per-part timing,
ties, tuplets and dynamics. Neither the synth nor `AudioEngine` needs a
clef-dependent transposition. Test equal stored pitches under all five clefs.

Compare synthetic T1 tempo spans against the **current** OpenPsalm MIDI output,
including when only T2 is audible and T2 has a different rhythm. Existing OPE
`collectSpans()`/`TempoMap` approximations use event-end span boundaries and their
own restoration rules; the Rust implementation uses the terminating note's onset
for the ramp and restores after that note. Do not assume their comments prove
parity. Correct discrepancies exercised by these tests and document any remaining
pre-existing playback differences rather than claiming exact parity without evidence.

### 6. Create TTBB songs and explain the format

Replace New Song's SATB-only checkbox (`NewSongDialog` in `src/ui/Dialogs.*`)
with an arrangement preset selector preserving the existing default:

1. SATB: existing Soprano/Alto/Tenor/Bass setup.
2. TTBB: the four IDs, roles, staves and clefs in the contract table above.
3. Single voice: preserve the existing soprano/treble starting document.

The choice only creates ordinary part fields; never serialize an `arrangement`
flag. Keep meter-correct rest generation and existing draft/contribution identity.
This preset creates new documents; it is not a conversion or octave-repair tool
for already-authored songs.

Update README, `docs/getting-started.md`, relevant help text and the changelog with
the supported clefs/roles, sounding-pitch rule, arrangement lead, TTBB preset and
song 369 example. Retain the documented standalone build and editing workflow.

## Tests and verification

Add deterministic coverage in the existing test families, with small authored
fixtures in `tests/Fixtures.cpp` or dedicated fixture files. Avoid multiline TOML
raw literals in moc-processed test classes: `tests/Fixtures.h` explains the existing
moc limitation. Register any new test targets in `tests/CMakeLists.txt`.

| Tests | Required evidence |
| --- | --- |
| `FormatTests` / `DocumentTests` | Parse all roles/clefs; untouched bytes preserved; edit only one field; numeric suffix/renamed/shuffled part order; omission and normalization semantics; invalid clef/staff/conflict findings; role-aware R5.3; suppression/splicing; overlay inheritance and minimal writes. |
| Core metadata tests | Independently asserted geometry anchors, canonical mappings, role stems/order/lead and unknown-value behavior. C4 is on line 3 in alto and line 4 in tenor; octave-treble uses E3 as its sounding bottom line. |
| `GlyphTests` / score widget coverage | Nonempty usable C glyph at both reference lines; actual notehead/ledger/stem positions; octave 8; crossing/unison voice selection; shuffled table order; no unsupported-clef treble masquerade. Test behavior/geometry, not only that a path exists. |
| `SessionTests` / `UiWorkflowTests` | Inspector displays and safely edits every choice; unknown values survive unrelated edits; undo/redo; base versus overlay; source-to-structured refresh; visible-order keyboard navigation; TTBB creation; distinct transport labels/mutes. |
| Playback tests (`DocumentTests` or a dedicated core target) | Opening/final pitch anchors, clef-independent MIDI pitches, ties and tuplets, per-part mute, T2-only tempo with muted T1 and mismatched rhythms, lead fallback, no second octave shift. Keep synth-only tests in `AudioTests` where appropriate. |
| `CliTests` / `ContributionTests` | Valid TTBB has no stale clef/role warnings; invalid shared staff/clef yields a failing check/preflight; a valid proposed TTBB file survives isolated contribution validation. |
| `CorpusTests` / download coverage | Existing unchanged-save/token-regeneration gates and translations still pass; a synthetic TTBB corpus snapshot is accepted through the existing checker path without network access. |

Use independent expected constants rather than computing expected positions or
pitches with the helper under test. Include arbitrary part names, Bass2/Bass10,
duplicate roles on extra staves, solo C-clef parts and an SATB regression fixture.
Default unit/widget tests must not require the sibling catalog or scan. The
[OpenPsalm TTBB fixture](../OpenPsalm/tests/fixtures/ttbb/README.md) is a useful
reference: its chorus/coda and tempo mechanisms are synthetic, not song 369 itself.
Copy/adapt small reviewed fixtures into this repository for standalone tests.

### Build and automated commands

From OpenPsalmEditor, use a separate build directory; do not repurpose or clean
the existing in-tree build products. Prerequisites are those in README/CMake
(Qt 6.5+, Qt Test, CMake 3.25+, C++23 compiler, libzip).

```sh
cmake -S . -B build-newclefs -DCMAKE_BUILD_TYPE=Debug
cmake --build build-newclefs -j
ctest --test-dir build-newclefs --output-on-failure

build-newclefs/src/ope-check --check ../OpenPsalm/songs/369/song.toml
OPE_SONGS_DIR="$(realpath ../OpenPsalm/songs)" \
  ctest --test-dir build-newclefs -R '^CorpusTests$' --output-on-failure
build-newclefs/src/ope-check --check ../OpenPsalm/songs --errors-only
```

CTest already configures offscreen Qt for widget tests. Capture the full-corpus
baseline before code changes and distinguish unrelated catalog findings from
regressions; do not fix unrelated songs as part of this editor task. Full-corpus
tests may skip without a catalog, so explicitly verify that the local acceptance
run actually found song 369. Reuse the README sanitizer gate for final core/UI
changes where the toolchain supports it.

### Song 369 local acceptance

The repaired song is already TTBB. At this audit its SHA-256 was
`f34e46277fbd5009f9cbf23fa3fc92fea9baf38b48d8fb039c3ee46d532d12e6`.
It was an **untracked** directory inside OpenPsalm's songs submodule; do not
assume a new clone contains it or overwrite later maintainer changes.

Use a disposable copy of the song directory and isolated editor preferences/data
for interactive edits. The catalog and normal editor corpus are not test outputs.

Verify all four roles, two tenor-clef upper voices, and two bass-clef lower voices.
Check score/lyrics/source/Inspector synchronization, small and large windows,
zoom, all three stanzas, phrase rulers, keyboard editing, undo, save/reopen,
ensemble playback, each solo and both staff pairs. Compare sounding notes with
the website output, allowing for the editor's simpler notation display.

Independent pitch checkpoints (MIDI numbers):

| Checkpoint | Tenor I | Tenor II | Baritone | Bass |
| --- | --- | --- | --- | --- |
| First note | 58 | 53 | 50 | 46 |
| Measure 1 final beat | 60 | 57 | 53 | 51 |
| Final dotted half | 62 | 58 | 53 | 46 |
| Sounding range | 58–67 | 53–62 | 50–60 | 41–58 |

Tenor I measure 30 reaches G4/MIDI 67. Preserve 32 measures of 3/4, B-flat major,
tempo 96, three stanzas, and phrase breaks at `4:48`, `8:48`, …, `28:48`.
The role/octave repair and later source-confirmed tie/natural corrections are
already in the file; see the validation report for their provenance.

**Keep the printed lower rhythm in measure 28.** The maintainer explicitly chose
that for local review. The later source reading also places stanza 3's “pa” on
measure 27's final beat, leaving “vil-ioned” for measure 28. Do not split the lower
half note or re-transcribe the hymn during this editor work.

Run the same editing/round-trip checks on representative SATB and translation
data: songs 8, 13, 101, 103, 273 and 162/es when available. Keep source scans and
temporary rendered comparisons outside either public repository.

## Completion criteria and handoff

The editor is ready for local review when it can load, validate, display, edit,
play and save TTBB and all five clefs while preserving source bytes on an
unchanged save and preserving sounding pitches on a clef-only edit. TTBB creation,
translation inheritance, distinct voice controls and contribution preflight must
work, and the SATB/corpus regression gates must retain their prior behavior.

Report changed components, tests actually run, any pre-existing corpus/playback
limitations, and the command/path for the new editor binary. This task does not
include catalog repairs, catalog commits, deployment, new website exports, or
TTBB external sight-singing support. Only this plan was written during the
planning session; the implementation and validation were completed afterward.
