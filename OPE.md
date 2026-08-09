# OpenPsalm Editor (OPE) — Design & Implementation Plan

**A C++/Qt6 desktop editor for OpenPsalm `song.toml` files.**

Status: plan, awaiting review. Target repo: `/home/jon/projects/OpenPsalmEditor` (standalone; not a submodule of OpenPsalm).

---

## 1. Purpose

OPE is the default authoring tool for the OpenPsalm song corpus. Three jobs, in order of frequency:

1. **Fix** an existing hymn — a wrong pitch, a swallowed syllable, a phrase break in the wrong place.
2. **Translate** a hymn — author `song_{lang}.toml` overlays with syllable counts that actually line up.
3. **Create** a new hymn — a new `songs/{N}/song.toml` that seeds cleanly on the first try.

Today all three are done by hand-editing TOML and running `cargo run` to find out what broke. The seeder's feedback loop is slow, its error messages are terse, and its worst failure mode is silent: a verse with too many syllables is **truncated without complaint**, and a note token with a typo'd duration **silently becomes a quarter note**. OPE's core value is turning that loop into immediate, in-place feedback.

### Success criteria

- Opening and saving any of the 201 existing songs (and its translations) produces a **byte-identical file**.
- Every error the Rust seeder can raise is caught in the editor first, with the same wording, before the file is written.
- A translator can produce a syllable-correct `song_es.toml` without ever counting syllables by hand.
- A user can hear the hymn — all voices, any verse, with a moving cursor — without leaving the app.
- No external runtime dependencies: no LilyPond, no timidity, no database, no network.

### Non-goals

- **No exports.** No MusicXML, LilyPond, MIDI file, PDF, PPTX, or MP3 output. The website owns rendering; OPE owns the source of truth. (The internal playback engine is not an export; it never writes a file.)
- **No engraving fidelity.** The score view is an *editing* view — legible and accurate as to content, not publication-quality. LilyPond remains the authority on how a page looks.
- **No git integration.** OPE writes files; committing them to OP-songs and bumping the submodule pointer stays a shell task.
- **No database, no server, no import from MusicXML/ABC/LilyPond/NWC.** The `tools/` Python importers keep that job.
- **No multi-user or cloud features.**

---

## 2. Design principles

1. **The format is the contract.** OPE's parser, slot counter, and overlay merge are ports of `src/seed/parser.rs`, `src/seed/importer.rs`, and `src/seed/data.rs` — behaviour-identical, including their quirks (integer tick truncation in tuplets, order-independent suffix stripping, wholesale lyric-map replacement). Where the Rust seeder is silently permissive, OPE is loudly strict, but it never *rejects* what the seeder accepts.
2. **Diffs must be minimal.** These files live in git and are reviewed by humans. Saving a song must touch only the lines the user changed. This drives the whole I/O design (§6).
3. **Never block the user.** A song mid-edit is often temporarily invalid — a measure sums to 60 ticks while you retype it. OPE shows that in red and keeps going. Save is always available (with a confirmation when errors exist).
4. **Minimal surface.** One window, one document, three panes. Every feature must earn its place against "could the user just type this in the TOML?"
5. **Offline and self-contained.** The only thing OPE requires on disk is a songs directory — no Rust toolchain, no OpenPsalm checkout, no LilyPond, no soundfont, no network, at build time as much as at runtime. A single binary plus an embedded font.

---

## 3. Format surface — the complete contract

Everything OPE must round-trip. Derived from `songs/docs/song-toml-format.md`, `song-style-guide.md`, and the seeder source (which is authoritative where they differ).

### 3.1 Top-level fields

| Field | Type | Editor treatment |
|---|---|---|
| `title` | string | Header field. Required (`require_complete`). |
| `subtitle` | string | Header field. |
| `active` | bool | Header checkbox "Published". Absent = true. Read per file. |
| `language` | string | Base file only; declares its language (default `en`). Overlay's language comes from the filename. |
| `key_signature` | string | Combo: `C G D A E B F# F Bb Eb Ab` + `m` suffix for minor. **Display/transposition only — note accidentals are always explicit.** |
| `time_sig_numerator` / `_denominator` | int | Header spinners. |
| `time_sig_changes` | array of tables | Table editor: measure, num, den, duration. Affects per-measure tick validation. |
| `tempo_bpm` | int | Header spinner; drives playback. |
| `verse_count` | int | Header spinner; cross-checked against `[lyrics.N]` keys. |
| `phrase_breaks` | `"M:T"` array | Edited **graphically** in the score's phrase ruler (§9.4). |
| `optional_phrase_breaks` | `"M:T"` array | Same, second lane. |
| `non_breaking_phrase_breaks` | `"M:T"` array | Same, third lane. |
| `copyrights` | string array | List editor with templates and `[text](url)` link support. |
| `commentary` | string | Multi-line plain-text box (contains HTML; no WYSIWYG). |
| `converge_verses` | bool | Tri-state: unset (auto from shared sections) / true / false. |

### 3.2 `[parts.Name]`

| Field | Editor treatment |
|---|---|
| `choral_type` | `soprano alto tenor bass` — combo. Several parts may share one. |
| `clef` | `treble bass treble_8` — combo. |
| `staff_number` | int; parts sharing a number share a staff. |
| `notes` | The note stream (§3.3). Edited in the score view. |
| `suppress_verses` / `suppress_verses_when` | Part inspector, both-or-nothing. |
| `splice_lyrics_into` | Echo lyric splice (format doc § *Echo Lyric Splice*; reference song 13). A **choral type**, not a part name, matched case-insensitively. Part inspector combo of the choral types present in the song. |
| `[parts.X.lyrics.KEY]` | Per-part lyric overrides; merged per key over global. |

### 3.3 Note stream grammar (as the parser actually reads it)

```
notes    := line ("\n" line)*            # newline is treated as "|"
line     := measure (" | " measure)*     # empty segments are skipped
measure  := event*
event    := tupletOpen | tupletClose | chord | note
tupletOpen  := "{" (N | N ":" M)         # M defaults to largest power of 2 < N
tupletClose := "}"
chord    := "<" pitch (WS pitch)* ">" suffixes
note     := (pitch | "r" | "s") suffixes
pitch    := [a-g] ("is"|"es"|"isis"|"eses")? ("'"|",")*     # base octave 3
suffixes := DIGITS "."* flags*
```

Parsing rules that must be mirrored exactly:

- The **pitch ends at the first ASCII digit**; everything from there on is the "duration token" that flag-stripping chews on. Rests (`r…`) are special-cased.
- Flag extraction order in `strip_note_flags`: hairpin → tempo spanner → `%dynamic` → `/±N` dedup offset → then a **loop** stripping `-.`, `@c`, `@e`, `!`, `~`, then one of `[(` / `-(` / `(` / `[`, then one of `])` / `-)` / `)` / `]`, repeating until a pass consumes nothing. **Suffix order is therefore free**: `c''4!)` ≡ `c''4)!`.
- `\` escapes only `<`, `>`, `!` (so hairpins survive tokenisation); any other `\` is pushed literally and the next char is re-examined.
- In a chord, only the **first** pitch keeps `!`, `-.`, `@c`, `@e`, and only the first is a lyric slot.
- Unknown duration digits → **silently "quarter"**. Missing digits → **silently "quarter"**. Unknown `%name` → accepted verbatim. Unknown `\word` → left in the duration string, which then parses as quarter. All four are OPE **errors** (§8, E-DUR-*, E-DYN-*, E-SPAN-*), because the seeder will happily import garbage.
- Tuplet ticks: `base * normal / actual` with **C integer division** (truncating). OPE's validator must reproduce the truncation bit-for-bit so its "sums to N ticks" messages match the seeder's.

Complete token feature list to support: rests `r`, spacers `s`, chords `<…>`, tuplets `{N …}` / `{N:M …}`, dots, ties `~`, slurs `(` `)`, dashed slurs `-(` `-)`, beams `[` `]`, combined `[(` `])`, fermata `!`, staccato `-.`, chorus `@c`, coda `@e`, dynamics `%ppp…%sfz`, hairpins `\<` `\>` `\!`, tempo spanners `\rit \ritard \rall \accel \string \atempo` + `\spanend`, dedup tick offset `/±N`.

### 3.4 Tick systems (three of them — keep them straight)

| System | Quarter | Whole | Used by |
|---|---|---|---|
| **Seeder internal** | 48 | 192 | measure validation, tuplet scaling |
| **Phrase-break ticks (64ths)** | 16 | 64 | `phrase_breaks` `"M:T"` strings |
| **Audio** | rational seconds | | playback engine |

OPE uses the seeder's 48-per-quarter resolution internally (64th = 3, triplet-eighth = 16) and converts to 64ths (`÷3`) only when reading/writing phrase-break strings. Rational arithmetic is used for audio scheduling so exotic tuplets don't drift, while the *validator* uses the truncating integer path.

### 3.5 Lyrics

- `[lyrics.N]` numbered verses, `[lyrics.chorus]`, `[lyrics.coda]`, `[lyrics.sN]` shared sections.
- Syllables split on whitespace; ` -- ` joins syllables within a word; syllable_type ∈ begin/middle/end/single is derived.
- `_` = melisma placeholder. `‿` (U+203F) = elision (one slot, two words).
- `@sN` standalone token splices `[lyrics.sN]`; expansion happens **on the merged (global + per-part) map**, so a part overriding `sN` re-targets references inside inherited verse texts.
- Slot attachment (`attach_lyrics`), which the alignment view must replicate exactly:
  - A verse key whose text yields **0 syllables is not a verse** (excluded from `has_verse_lyrics` and `max_verse_length`).
  - Verse offset = 0, unless the chorus starts at slot 0 (`@c` on the first slot ⇒ *refrain-first*), in which case verses start at `max_chorus_length`.
  - Chorus offset = 0 if refrain-first; else `max_verse_length` if the part has verse lyrics; else the `@c` slot index.
  - Coda offset = the `@e` slot index.
  - Syllables past the end of the note list are **dropped silently** by the seeder. OPE reports E-SLOTS.
- Lyric-slot state machine (`LyricSlotState::is_lyric_slot`), ported verbatim: secondary chord voices never count; rests/spacers never count and reset tie state; a tie-continuation never counts; otherwise a note is a slot iff `(!wasInBeam && (slurStart || !wasInSlur)) || (!wasInSlur && beamStart)`.
- `@c` / `@e` on a rest **defer** to the next lyric slot.

**Echo lyric splice.** A part carrying `splice_lyrics_into = "<choral_type>"` still authors one syllable per slot of its own notes; the splice only changes *where* the print exporters put them. The rule is per verse: the source part's surviving syllables must fall **strictly after** the target's last surviving syllable, or that verse falls back to its own row. OPE models this because the alignment grid must show the user which verses will splice and which won't — a tail that interleaves by one syllable looks fine in the TOML and prints wrong. Rendered as an annotation on the source part's rows ("splices into Soprano" / "verse 2 will not splice — first echo syllable at slot 41 precedes Soprano's last at slot 43"). The splice is print-only: it never changes slot counts, so E-SLOTS is unaffected by it.

### 3.6 Translations (`song_{lang}.toml`)

Overlay merge (`merge_overlay`), ported verbatim:

| Construct | Rule |
|---|---|
| Top-level scalars/arrays | present ⇒ replace; absent ⇒ inherit. Empty `title` ⇒ inherit. |
| `copyrights` | replaces the whole list (and should always be set, in the translation's language). |
| `[parts.X]` | **field-wise** merge; blank/absent `notes` inherits the base notation. |
| `[parts.X]` not in base | added whole; must be complete. |
| `[parts.X.lyrics]` / `[lyrics]` | any entry ⇒ **the entire map is replaced**. |
| `[[time_sig_changes]]` | replaces the whole array. |

Language codes must exist in `src/i18n.rs`'s `LANGUAGES` (append-only ordinals). Currently `en` (1), `es` (2). A `song_{lang}.toml` in the base language is an error, not a translation.

---

## 4. Architecture

```
OpenPsalmEditor/
├── CMakeLists.txt                 # C++23, CMake ≥3.25, qt_standard_project_setup
├── OPE.md  README.md  LICENSE
├── third_party/tomlplusplus/      # header-only, MIT (vendored or FetchContent)
├── src/
│   ├── main.cpp
│   ├── core/            # NO Qt Widgets — headless-testable
│   │   ├── Ticks.h              # 48/quarter, 64th conversions, rationals
│   │   ├── NoteToken.{h,cpp}    # tokenizer + emitter (raw-preserving)
│   │   ├── NoteStream.{h,cpp}   # measures ← → token list, tick math
│   │   ├── SongModel.{h,cpp}    # SongDocument / Part / Measure / Event / LyricSection
│   │   ├── SongIo.{h,cpp}       # toml++ load; span-preserving save
│   │   ├── Overlay.{h,cpp}      # translation merge (mirrors data.rs)
│   │   ├── LyricSlots.{h,cpp}   # slot state machine + attach algorithm + @sN
│   │   ├── Validator.{h,cpp}    # rule engine, §8
│   │   ├── Playback.{h,cpp}     # tempo map + note event scheduling
│   │   └── Library.{h,cpp}      # songs/ scan, next id, i18n registry read
│   ├── app/             # the open document, undo stack, findings
│   ├── audio/
│   │   ├── Synth.{h,cpp}        # polyphonic additive/organ voice + ADSR
│   │   └── AudioEngine.{h,cpp}  # QAudioSink push, transport, position feedback
│   └── ui/
│       ├── MainWindow  SongBrowser  HeaderPanel  PartInspector
│       ├── ScoreView  ScoreLayout  ScoreRenderer  PhraseRuler
│       ├── LyricGrid  LyricTextEditor  TranslationPane
│       ├── ProblemsPanel  TransportBar  SourcePreview
│       └── NewSongDialog  TranslationDialog  PreferencesDialog
└── tests/               # Qt Test; corpus fixtures
```

`core/` depends only on QtCore + toml++ so the entire format layer runs in a headless CI job against the real corpus.

### Dependencies

| Dep | Why | License |
|---|---|---|
| Qt 6.5+ (Core, Gui, Widgets, Multimedia, Test) | UI, audio sink | LGPLv3 |
| toml++ 3.x | TOML parse **with source regions** (needed for span-preserving save) | MIT |
| ~~Bravura~~ | *Not used.* See below. | |

Optional, default OFF: `OPE_WITH_FLUIDSYNTH` — nicer playback timbre when a soundfont is available. The built-in synth is the supported path.

**Implementation note — no music font.** The plan called for embedding Bravura. In the event, the notation is drawn from vector paths built in `src/ui/Glyphs.cpp` (noteheads, clefs, accidentals, rests, flags, fermatas) in staff-space units. This removes the only asset the program would have needed at runtime and makes the dependency list Qt alone, which fits the self-contained rule better than the original plan did. The cost is that the glyphs are approximations — acceptable, because §7.1 already committed to an editing view rather than an engraving.

**Implementation note — undo by snapshot.** The plan implied a `QUndoCommand` subclass per mutation. What shipped is one: `SnapshotCommand` keeps a copy of the document either side of an edit. A song is a few hundred kilobytes, so the copy is cheap, and it is exact — which a hand-written inverse for each of the format's constructs would not reliably be. Every operation gets correct undo, including ones not written yet.

### Language standard

**C++23** (`CMAKE_CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON`), which sets the floor at GCC 13 / Clang 17 / MSVC 19.35. Qt itself only requires C++17, so this constrains OPE's own code and nothing else. Where it earns its keep:

- **`std::expected<T, Error>`** for every fallible core operation — file load, TOML parse, token parse, overlay merge. The format layer returns errors as values with no exceptions and no out-parameters, and the Problems panel consumes the same `Error` type the loader produced.
- **Deducing `this`** to collapse the const/non-const accessor pairs on the document tree.
- **`std::ranges::to`**, `views::chunk`, `views::slide`, `views::enumerate` — the slot-attachment and beam/slur-grouping algorithms are windowed passes over event lists and read far better as ranges pipelines than as index loops.
- **`std::print` / `std::format`** for the headless CLI validator output.
- **`if consteval`, `constexpr` `std::string`** to make the tick tables and duration maps compile-time.

Guard rail: `core/` must stay free of anything a supported compiler lacks; the CI matrix (GCC 13 and Clang 17 minimum) is what enforces that, not discipline.

---

## 5. Data model

```cpp
struct Event {                 // one chord/note/rest/spacer position
    QString raw;               // verbatim source token — emitted unchanged when clean
    bool    dirty = false;     // set by any mutation; forces re-emission
    QVector<Pitch> pitches;    // 1 = note, >1 = divisi chord, 0 = rest/spacer
    bool    isRest, isSpacer;
    Duration dur;              // base + dots
    std::optional<Tuplet> tuplet;  // actual/normal + start/end flags
    Flags   flags;             // tie, slur/dashed/beam open+close, fermata,
                               // staccato, chorusStart, codaStart
    std::optional<QString> dynamic, hairpin, tempoSpanner;
    bool    spannerEnd = false;
    int     dedupOffset = 0;
    int     tickInMeasure = 0; // derived
    int     slotIndex = -1;    // derived; -1 = not a lyric slot
};

struct Measure { QVector<Event> events; int number, expectedTicks, actualTicks; };
struct Part    { QString name, choralType, clef; int staffNumber;
                 QVector<Measure> measures;
                 QVector<int> suppressVerses; QStringList suppressVersesWhen;
                 std::optional<QString> spliceLyricsInto;
                 QMap<QString, LyricSection> lyrics;      // per-part overrides
                 SourceSpans spans; };
struct LyricSection { QString rawText; QVector<Syllable> syllables; };  // syllables derived

struct SongDocument {
    QString path, language; int workId; bool isOverlay;
    Header header;                       // all §3.1 fields, each with a "present" flag
    QVector<Part> parts;                 // SATB-sorted for display, source order for save
    QMap<QString, LyricSection> lyrics;  // global
    QByteArray originalBytes;            // for span splicing
    toml::table tree;                    // toml++ parse result, holds source regions
    SongDocument* base = nullptr;        // overlays only
};
```

An overlay document keeps **both** its own sparse fields and a computed `merged` view. The UI renders the merged view, styling inherited values as ghosted; editing a ghosted value materialises it in the overlay.

---

## 6. TOML I/O — the diff-minimal save

This is the single most important engineering decision in the project.

**Why not a canonical emitter:** a survey of the corpus shows top-level key order varies across at least 15 distinct orderings, `notes` blocks are single-line in 399 cases and multi-line in 383, and multi-line blocks use 1–12 measures per line. Any canonical writer would reflow essentially every file on first save, producing unreviewable diffs.

**The design: three levels of verbatim preservation.**

1. **File level.** `SongIo` keeps `originalBytes`. Save = apply an ordered list of byte-range replacements. Anything not covered by a replacement — comments, blank lines, key order, indentation, array formatting — survives untouched.
2. **Value level.** toml++ gives a `source_region` for every key and value. A changed scalar replaces exactly its value's byte range. A changed array is re-emitted in the *style it was already written in* (single-line vs. one-entry-per-line with trailing commas, detected from the original span). A new key is inserted at a canonical position within its table (documented order, matching the majority convention), with surrounding blank lines matched to the file's existing style.
3. **Token level.** Inside a `notes` string, every `Event` retains its `raw` source text. Only events marked dirty are re-emitted. Line grouping is preserved: if the block had *k* lines with measure counts `[4,4,4,5]`, the rewritten block keeps that shape as long as the measure count is unchanged; when measures are added or removed, the changed line grows/shrinks and neighbours are untouched. A brand-new block defaults to 4 measures per line.

**Canonical token emission** (only for dirty events): `pitch` `duration` `dots` `/±N` `(`|`[`|`[(`|`-(` `~` `!` `-.` `@c`|`@e` `%dyn` `\<`|`\>`|`\!` `\spanner` `\spanend` `)`|`]`|`])`|`-)`. Order is arbitrary as far as the parser is concerned (its stripping loop is order-independent); fixing one keeps OPE's own output stable.

**New files** (new song, new translation) use the canonical emitter end to end, matching the layout `tools/musicxml_to_toml.py` produces and the ordering the majority of the corpus uses:
`title, subtitle, copyrights, key_signature, time_sig_numerator, time_sig_denominator, tempo_bpm, verse_count, phrase_breaks, optional_phrase_breaks, non_breaking_phrase_breaks, commentary, converge_verses` → `[[time_sig_changes]]` → `[parts.*]` (SATB order) → `[parts.*.lyrics.*]` → `[lyrics.1..N]`, `[lyrics.chorus]`, `[lyrics.coda]`, `[lyrics.sN]`.

**Write safety:** write to `song.toml.tmp` in the same directory, `fsync`, `rename` over the target. `QFileSystemWatcher` detects external modification and offers reload/keep. Writes are confined to the configured songs root.

**Gate (CI):** open every `songs/*/song.toml` and `song_*.toml`, save with no edits, assert byte equality. This test is the definition of "correct I/O" and runs on every commit.

---

## 7. Score view

### 7.1 Rendering

A `QAbstractScrollArea` with a hand-rolled layout engine (no QGraphicsScene — the score is a flat list of systems, and direct painting keeps hit-testing and the playback cursor trivial).

- **Font:** Bravura, embedded. **Round noteheads only** — no Aiken shape notes, no toggle. The published scores use shape notes, but the editor's job is to show *what the data says*, and one notehead shape is one less control in the toolbar.
- **Layout:** staves grouped by `staff_number` in ascending order, parts sharing a staff drawn with opposing stems (S/T up, A/B down) exactly as the LilyPond exporter arranges them. Lyrics rows under the bottom staff of the group that owns them; per-part lyrics render under that part's own staff.
- **System breaking:** when "Phrased" is on (default), break lines only at `phrase_breaks` / `optional_phrase_breaks` — so the editor's line layout previews the printed layout, which is the whole reason those fields exist. Off ⇒ fill width.
- **Drawn content:** clef, key signature, time signature (numeric always — §13.1), barlines, noteheads with dots, stems, **explicit beams only** (the exporter runs `\autoBeamOff`, so a beam appears iff `[`…`]` says so — everything else gets flags), ties, solid and dashed slurs, fermata, staccato, dynamics, hairpins, tempo spanners with dashed extenders, chords/divisi, tuplet brackets with ratio numbers, rests, and spacers (drawn as faint grey outlines so pickup padding is visible).
- **Overlays:** selection halo; playback highlight; per-measure error tint with a `+4 ticks` badge when a measure doesn't sum; slot index / syllable shown under the notehead of the active verse; grey "inherited from base" wash on parts a translation didn't override.

### 7.2 Selection and editing model

Selection is a set of events within one part (rubber-band or shift-click), or a single event. Edits go through `QUndoCommand`s so undo/redo is uniform and the dirty flag is exact.

**Keyboard map** (single-key, no modifier, when the score has focus):

| Key | Action |
|---|---|
| `←` `→` | previous / next event in the part |
| `↑` `↓` | pitch up/down one letter step (keeps accidental) |
| `Shift+↑/↓` | up/down one semitone (adds/removes `is`/`es`) |
| `Ctrl+↑/↓` | octave up/down (`'` / `,`) |
| `Alt+↑/↓` | move to the part above/below |
| `1 2 4 8 6 3` | whole, half, quarter, eighth, 16th, 32nd |
| `.` | toggle augmentation dot (cycles 0→1→2) |
| `r` / `p` | convert to rest / spacer |
| `t` | tie to next |
| `s` | slur over selection (`(` first, `)` last) |
| `b` | beam over selection |
| `d` | dashed slur over selection |
| `f` / `.`(alt) / `k` | fermata / staccato |
| `c` / `e` | toggle `@c` / `@e` on this event |
| `v` | insert divisi pitch into the chord at the cursor |
| `y` | wrap selection in a tuplet (prompts for N or N:M) |
| `Enter` | edit the lyric syllable on this slot inline |
| `Ins` / `Del` | insert event after cursor / delete event |
| `Space` | play/pause from cursor |
| `Ctrl+B` | insert/remove a required phrase break at the cursor |
| `Ctrl+Shift+B` / `Ctrl+Alt+B` | optional / non-breaking phrase break |

Mouse: click selects; vertical drag changes pitch; double-click opens the note inspector; click in a phrase-ruler lane toggles a break there.

### 7.3 Measures never silently rebar

Changing a duration does **not** shuffle notes across barlines. The measure shows its tick delta in red and the Problems panel lists it. Composition helpers that keep the sum intact are offered explicitly: *Split note* (halves it), *Merge with next*, *Fill remainder with rest/spacer*, *Pad pickup with spacer* (computes the exact `s` values needed for measure 1).

### 7.4 Phrase-break ruler

Three thin lanes under the bottom staff (required / optional / non-breaking). Each break is a draggable marker; hovering shows the computed `"M:T"`. Clicking between two notes inserts a break at that boundary and OPE computes the 64th-tick value. This removes the single most error-prone hand calculation in the format. Breaks that don't land on a note boundary in every part, or that fall inside a melisma, are flagged in place (R9.4/R9.5).

### 7.5 Vertical (all-voices) operations

The style guide requires several markings on *every sounding voice*: dynamics and hairpins (§5.1), fermatas and staccatos (§6.1). One command — **Apply to all voices at this beat** (`Ctrl+Enter`) — copies the selected marking to every part with a note starting at the same tick, which is exactly the rule the validator checks. Tempo spanners are excluded (soprano only, §5.3).

---

## 8. Validation engine

Runs incrementally on every mutation (debounced ~150 ms), and fully on open and save. Findings render in the Problems panel and as in-place badges; double-click navigates to the offending event, measure, or lyric slot. Each finding carries: severity, rule id, human message, source anchor, and — where safe — a one-click fix.

### 8.1 Errors — the seeder will reject the file

| Id | Condition | Mirrors |
|---|---|---|
| `E-TOML` | file doesn't parse — **the document does not open** (§8.5) | `read_song_file` |
| `E-TITLE` | missing/blank `title` (after merge, for overlays) | `require_complete` |
| `E-NOPARTS` | no `[parts.*]` | `require_complete` |
| `E-NONOTES` | a part with blank `notes` | `require_complete` |
| `E-MEASURE` | measure ticks ≠ effective time signature (honouring `time_sig_changes`) | `insert_measures` |
| `E-CODA` | `[lyrics.coda]` with no `@e` in the part | `attach_lyrics` |
| `E-SHARED-REF` | `@sN` with no `[lyrics.sN]` | `expand_shared_lyrics` |
| `E-SHARED-NEST` | a shared section referencing another | `expand_shared_lyrics` |
| `E-SHARED-MALFORMED` | `@token` that isn't a well-formed `@sN` | `expand_shared_lyrics` |
| `E-LANG-UNKNOWN` | `song_{lang}.toml` code not in `LANGUAGES` | `seed_from_folder` |
| `E-LANG-DUP` | translation code == base language | `seed_from_folder` |

Error messages are copied verbatim from the Rust strings (e.g. `Measure duration mismatch in "X" part "Y" measure N: expected 4/4 (192 ticks), got 180 ticks`) so searching either codebase finds the same text.

### 8.2 Errors — the seeder *accepts* these and produces wrong output

The highest-value class. These are silent corruption today.

| Id | Condition |
|---|---|
| `E-SLOTS` | syllable count ≠ lyric slot count, per part × per lyric key (after `@sN` expansion and per-part merge). Reports the delta and the first misaligned slot. |
| `E-DUR-MISSING` | token with no duration digits (parses as quarter) |
| `E-DUR-UNKNOWN` | duration digits outside `1 2 4 8 16 32 64` |
| `E-DYN-UNKNOWN` | `%name` outside the set `dynamic_to_velocity` recognises (`ppp pppp pp p mp mf f fp pf ff sf sfp rf fz fff ffff sfz sffz rfz sfpp`); anything else falls through to velocity 80 with no warning. Names outside the ten documented in the format doc are downgraded to a warning, not an error. |
| `E-SPAN-UNKNOWN` | `\word` that is neither a hairpin nor a known spanner/`\spanend` |
| `E-MEASURE-COUNT` | parts disagree on measure count (style §4.5; breaks alignment everywhere) |
| `E-TICK-GRID` | a `"M:T"` phrase break whose measure doesn't exist, or whose tick exceeds the measure |
| `E-CHORD-EMPTY` | `<>` with no pitches, or an unterminated `<`/`{` |

### 8.3 Style warnings — port of `tools/audit_style.py`

Same rule ids so the two tools' output is comparable, plus the guide sections they enforce.

| Id | Check |
|---|---|
| `R1.1` | all-short melisma slurred, should be beamed |
| `R1.2` | beam contains a quarter or longer |
| `R1.4` | beam crosses a barline |
| `R2.1` | dashed slur where all verses use `_` (should be a real slur) or no verse does (marking unnecessary) |
| `R2.3` | dashed-slur group also beamed |
| `R3.1` | same-pitch slur that should be a tie; tie between different pitches |
| `R3.4` | tie into a rest or spacer |
| `R3.5` | tie inside a measure where a dotted value would do (advisory) |
| `R-BAL` | unbalanced slur / dashed / beam markers in a part |
| `R5.1` | dynamic present on some parts but not all, at a given measure |
| `R5.2` | hairpin opened and never terminated |
| `R5.3` | tempo spanner on a non-soprano part |
| `R6.1` | fermata/staccato missing on a voice that sounds at that exact tick |
| `R9.1` | no `phrase_breaks` at all |
| `R9.4` | a break falls inside a melisma |
| `R9.5` | a break tick doesn't fall on a note boundary in some part |
| `R10.1` | chorus lyrics but no `@c` marker anywhere |
| `R11.1` | tight `--` instead of ` -- ` |
| `R12.x` | shared-section advisories: line pasted verbatim into every verse (should be `[lyrics.sN]`); shared-section seam that isn't a phrase boundary |

### 8.4 Metadata and translation lints

| Id | Check |
|---|---|
| `W-KEY` / `W-CLEF` / `W-CT` | value outside the documented enumeration |
| `W-VERSECOUNT` | `verse_count` ≠ number of numbered lyric sections |
| `C1` | copyright line ends with `.`; the OpenPsalm arrangement line isn't last; a public-domain line missing `(public domain)`; malformed `[text](url)` |
| `T1` | translation overrides a part's `notes` — base notation fixes will no longer propagate (info, not a defect) |
| `T2` | translation's lyric map has fewer verses than `verse_count` (the map replaces wholesale, so the rest would be missing, not inherited) |
| `T3` | translation's `copyrights` not overridden — page would show credits in the base language |
| `T4` | translation contains a lyric slot with a space where an elision `‿` is likely intended |

### 8.5 Failure policy — what blocks, and what doesn't

| Situation | Behaviour |
|---|---|
| **TOML syntax error on open** | **Refuse to open.** A dialog shows toml++'s message with the file, line, column, and the offending source line quoted with a caret; buttons are *Copy error* and *Close*. No partial model is built and no empty editor is presented — a half-loaded document is how a span-preserving editor destroys a file it never understood. Fixing it is a text-editor job; OPE says exactly where. |
| **Overlay whose merge is incomplete** (`E-TITLE`, `E-NOPARTS`, `E-NONOTES` after merge) | Opens, since the file itself is well-formed, but flagged as an error and confirmed on save. |
| **Semantic errors** (§8.1 measure/lyric/language rules, §8.2 silent-corruption class) | Never block editing — a song mid-edit is routinely invalid. Save shows a confirmation naming the error count and the first two errors, with *Save anyway* / *Cancel*. |
| **Style warnings** (§8.3) and **lints** (§8.4) | Purely advisory. They never block, never prompt, and never gate a save. |

### 8.6 Quick fixes

Conservative, single-step, always undoable: convert slur↔beam; convert slur→tie; add the missing `_` placeholder; pad the pickup with spacers; terminate a hairpin at the next dynamic; duplicate a dynamic/fermata/staccato to all sounding voices; strip a trailing period from a copyright line; move the arrangement line last; replace tight `--` with ` -- `; insert the `@c` marker on the first chorus event.

---

## 9. Lyrics and translation workflow

Two views of the same data, live-bound.

### 9.1 Text view

One text box per lyric key (`1…N`, `chorus`, `coda`, `sN`), showing the raw ` -- ` string — the fastest way to type or paste a stanza. Live gutter: syllable count vs. required slots, green/red. `@sN` chips are rendered inline and expand on hover. A character palette provides `‿`, `’`, and the accented characters translators reach for.

### 9.2 Alignment grid — the translator's tool

A table whose **columns are lyric slots** and whose **rows are lyric keys**:

```
          m1              m2                       m3
        ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┐
 slot   │  0    │  1    │  2    │  3    │  4    │  5    │  6    │
 note   │ ♩bes' │ ♩g'   │ ♩g'   │ ♩f'   │ ♩g'   │ ♩bes' │ 𝅗𝅥bes' │
        ├───────┼───────┼───────┼───────┼───────┼───────┼───────┤
 v1     │ Je -  │ sus   │ loves │ me!   │ This  │ I     │ know, │
 v2     │ Je -  │ sus   │ loves │ me!   │ He    │ who   │ died  │
 es 1   │ En    │ pre - │ sen - │cia‿es-│ tar   │ de    │Cris-to│  ← 8 syl / 7 slots
        └───────┴───────┴───────┴───────┴───────┴───────┴───────┘
                              ┊ phrase break 3:64
```

- Cells are editable in place; Tab advances; a red column marks where the counts diverge.
- Vertical rules mark phrase boundaries, so a translator can see where poetic lines must land.
- Melisma slots (slur/beam continuations) are shown greyed and non-editable — they don't take a syllable — which makes the format's hardest concept visible instead of theoretical.
- `_` placeholders render distinctly; `‿` renders as the tie glyph it is.
- When per-part lyrics exist, a part selector switches the note row; `splice_lyrics_into` and `suppress_verses` are annotated so the user knows why a part looks different.

### 9.3 Translation mode

*File → Add translation…* offers the registered language codes from OPE's **bundled** registry (§9.4) and warns clearly when a code is missing from it, quoting the exact `Language { … }` entry to append to `src/i18n.rs`.

The translation editor is the alignment grid with the base language pinned as a reference row, plus:

- Ghosted display of everything inherited; an **Override** affordance per field, and **Revert to inherited** to remove it again.
- A prominent, permanent reminder that defining *any* lyric entry replaces the whole map — with a one-click "copy all base verses in as a starting point".
- The `copyrights` block pre-populated from the base with the label words swapped to the target language (`Words:`→`Letra:`, `Music:`→`Música:`, `(public domain)`→`(dominio público)`) and a `Traducción:` line inserted after the lyricist. Suggestions only, fully editable.
- Note overrides are possible but deliberately friction-ful: overriding a part's notes shows T1 and marks the part in the score view.

### 9.4 Bundled language registry

OPE is self-contained: it never reads, parses, or requires an OpenPsalm source checkout. The language registry lives in OPE as a `constexpr` table mirroring `LANGUAGES` in `src/i18n.rs` — code, ordinal, English name, endonym — currently `en` (1) and `es` (2), with the same append-only, never-renumber discipline recorded in a comment above it.

Consequences, all deliberate:

- The only thing OPE needs on disk is a songs directory. It works against a bare OP-songs clone with no Rust toolchain and no OpenPsalm checkout anywhere.
- Adding a language is a **two-repo change**: append to `src/i18n.rs` (with the next unused ordinal) and to OPE's table. The *Add translation* dialog states this and shows both snippets, so the second half can't be forgotten silently.
- A `song_{lang}.toml` whose code OPE doesn't know still opens and edits normally — it is flagged `E-LANG-UNKNOWN` with the fix text, exactly as the seeder would fail. OPE never refuses to edit a file over a registry gap it may simply be behind on.
- A drift check lives in CI, not in the app: a test asserts OPE's table matches `src/i18n.rs` when an OpenPsalm checkout is present at a configurable path, and skips when it isn't.

---

## 10. Playback

The user must be able to hear the hymn. This is a monitoring tool, not an export: nothing is written to disk.

- **Event building** mirrors `src/export/midi.rs`: MIDI pitch = `12*(octave+1) + step + alter` (so `c'` = 60); ties merged into one sustained note; slurs and fermatas have no playback effect (matching the exporter — fermatas are engraving-only); tuplets scale by `normal/actual`; velocity from `dynamic_to_velocity` (ppp=20, pp=35, p=50, mp=64, mf=80, f=96, ff=112, fff/sfz=127, unknown=80) applied per part-track and held until the next marking; hairpins ramp velocity within the part; `\rit`/`\accel` interpolate BPM across the span (≈0.6× / ≈1.4×) until `\spanend`, with `\atempo` restoring `tempo_bpm`.
- **Synthesis:** a small polyphonic synth in `audio/Synth` — a few detuned harmonics with an ADSR envelope, one voice per sounding note, mixed at 48 kHz float and pushed to `QAudioSink`. No soundfont, no external binary. (Optional FluidSynth backend behind a CMake flag for those who want a better organ.)
- **Transport:** play / pause / stop, play from cursor, loop the selection or the current phrase, count-in, tempo override slider (does not modify `tempo_bpm` unless the user clicks "Apply"), per-part mute/solo, verse selector (drives which lyrics highlight), metronome toggle.
- **Cursor:** the sink's `processedUSecs()` feeds a 60 Hz UI timer that maps elapsed time → tick → the event to highlight, auto-scrolling the score. Lyrics highlight in step, which makes a mis-assigned syllable audible *and* visible.

---

## 11. Window layout and flows

One main window; no floating tool palettes.

```
┌───────────────────────────────────────────────────────────────────────────┐
│ File  Edit  Song  View  Play  Help                          [Song 162 ▾]  │  ← title + language tabs
├───────────────────────────────────────────────────────────────────────────┤
│ Title [Face to Face with Christ …] Subtitle [   ] Key [Bb▾] 4/4 ♩=96 v4   │  ← header strip (collapsible)
├───────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│                          SCORE  /  LYRICS  /  SOURCE                      │  ← tabbed centre
│                                                                           │
│   (score: staves, lyrics rows, phrase-ruler lanes, error badges)          │
│                                                                           │
├───────────────────────────────────────────────────────────────────────────┤
│ ▸ Problems (2 errors, 5 warnings)                            [Inspector]  │  ← bottom dock + right dock
│   E-SLOTS  Soprano · verse 3: 33 syllables, 32 slots (first gap: slot 18) │
│   R1.1     Alto m12: all-short melisma slurred, should be beamed          │
├───────────────────────────────────────────────────────────────────────────┤
│ ▶ ⏸ ⏹  │ verse [3▾] │ S A T B (mute/solo) │ ♩=96 ──○── │ 🔁 phrase        │  ← transport
└───────────────────────────────────────────────────────────────────────────┘
```

- **Language tabs** appear only when a song has translations; switching tabs switches documents within the same song.
- **Inspector** (right dock) is context-sensitive: note properties when an event is selected, part properties when a staff is selected, song properties otherwise.
- **Source tab** shows the exact bytes that will be written, with a live diff against the file on disk. Read-only, but it's what keeps the tool trustworthy: the user can always see the TOML.
- **Song browser** is a modal-ish start screen / `Ctrl+O` panel: a searchable list of the 201 songs (id, title, subtitle, languages present, validation status from a background scan). Not a permanent sidebar.

### Flows

**Fix a hymn.** Open browser → type title → Enter → score view → click the wrong note → `↑` → Problems clears → `Space` to hear it → `Ctrl+S`. Diff: one token.

**Translate.** Open song → *Add translation* → pick `es` → alignment grid with base pinned → type stanza by stanza, counter green → copyrights template filled and edited → `Ctrl+S` writes `songs/162/song_es.toml`.

**New hymn.** *File → New song* → wizard: next free id (max+1, pre-filled), title, subtitle, key, time signature, tempo, verse count, part set (SATB default; add/remove), copyright lines from template → empty score with one measure per part → enter notes → enter lyrics → set phrase breaks → validate → save.

---

## 12. Testing

| Suite | What it proves |
|---|---|
| **Corpus round-trip** | open + save every `songs/*/song.toml` and `song_*.toml` unchanged ⇒ byte-identical. The I/O correctness gate. |
| **Tokenizer conformance** | OPE's parse of every note token in the corpus matches a JSON dump generated from the Rust parser (a small `--dump-parse` harness added to OpenPsalm). The dump is **committed to `tests/fixtures/`**, so the test suite needs no Rust toolchain and no OpenPsalm checkout; regenerating it is a deliberate act when the format changes. Covers the quirks nobody would think to unit-test. |
| **Slot conformance** | OPE's lyric-slot indices and attachment offsets match a committed Rust dump of `attach_lyrics` results for the whole corpus. |
| **i18n drift** | OPE's bundled language table matches `src/i18n.rs` — runs only when an OpenPsalm checkout is configured, skips otherwise. |
| **Overlay merge** | ports the `data.rs` test cases verbatim; song 162 merges to the same effective document. |
| **Validator agreement** | OPE's R-rule findings over the corpus match `tools/audit_style.py` output exactly (differences must be justified in a changelog). |
| **Measure validation** | OPE flags exactly the measures the seeder would reject; a corpus scan should report zero E-MEASURE (the corpus imports today). |
| **Edit round-trips** | mutate → save → reload ⇒ identical model; and the diff touches only the expected lines. |
| **UI smoke (QTest)** | open a song, select a note, transpose, undo, save, play 2 s, stop — headless with `offscreen` and a null audio sink. |

CI: Linux (primary), plus Windows/macOS build jobs to keep the code portable.

---

## 13. Milestones

Sequenced so each one is independently useful. Estimates assume one developer.

| # | Milestone | Contents | Est. |
|---|---|---|---|
| **M0** | Skeleton | CMake, Qt6, toml++, Bravura resource, empty window, CI | 2–3 d |
| **M1** | Read + prove I/O | full model, tokenizer, toml++ load, span-preserving save, **corpus round-trip test green**, song browser, read-only score view | 2–3 wk |
| **M2** | Validation | slot machine, overlay merge, all E-rules, R-rule port, Problems panel, navigation, conformance fixtures | 2 wk |
| **M3** | Playback | tempo map, event builder, synth, QAudioSink transport, cursor, mute/solo/verse | 1.5 wk |
| **M4** | Note editing | selection, undo stack, all keyboard ops, measure-integrity helpers, phrase ruler, note inspector, header/part editors | 3 wk |
| **M5** | Lyrics | text view, alignment grid, per-part lyrics, shared sections, quick fixes | 2 wk |
| **M6** | Translations | overlay editing UI, inheritance display, language registry, copyright templating, T-lints | 1.5 wk |
| **M7** | New song + polish | wizard, preferences, source/diff tab, external-change watch, docs, packaging (AppImage / Flatpak-ready, Windows zip) | 1.5 wk |

Roughly 3–3.5 months of focused work; M1+M2 alone (≈5 weeks) already replace the hand-edit-and-reseed loop with a fast one and could ship as a validating viewer.

---

## 14. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **Format drift** — OpenPsalm changes the format and OPE silently diverges | Conformance fixtures generated from the Rust source; a `tools/dump_parse` harness in OpenPsalm and a CI job that regenerates and diffs. Document in OpenPsalm's `CLAUDE.md` that format changes require an OPE fixture refresh. |
| **Span-preserving save is fiddly** | It is the first milestone and gated by a whole-corpus byte-equality test, so the risk is retired before any UI depends on it. Fallback if a construct defeats splicing: mark that key "canonical-emit only when edited" and accept a localized reflow. |
| **Score rendering scope creep** | Explicit rule: the score view is an editing view. No spacing optimizer, no collision avoidance, no page layout. If a user wants to see the real page, they render on the site. |
| **Audio portability** | QAudioSink + a self-written synth avoids soundfont and MIDI-device dependencies entirely; a null sink keeps CI headless. |
| **Fields OPE doesn't know about** (a future format addition, or a key added to OpenPsalm before OPE catches up) | OPE preserves *every* key it reads, known or not, via span preservation — an unknown key is never dropped or reordered, only flagged as unrecognised at info level. Editing a song with a newer field is therefore safe by construction. |
| **Registry drift** — a language added to `src/i18n.rs` but not to OPE's bundled table | Unknown codes still open and edit; they raise `E-LANG-UNKNOWN` with the fix. The optional CI drift check catches it when a checkout is available. Accepted cost of being self-contained. |
| **Corpus normalisation temptation** | Out of scope. OPE never reformats a file it wasn't asked to change. If a bulk normalisation is ever wanted, that's a separate scripted commit. |

---

## 15. Decisions taken

Settled in review; recorded here so the reasoning survives.

1. **Language standard: C++23.** Compiler floor GCC 13 / Clang 17 / MSVC 19.35, enforced by the CI matrix. `std::expected` carries every fallible core operation; ranges pipelines carry the windowed slot/beam/slur passes (§4, *Language standard*).
2. **Source tab is read-only** — a preview plus a live diff against disk. No raw-TOML editing surface; the score, lyrics, and inspector are the only ways to mutate a document.
3. **`splice_lyrics_into` is a first-class, documented field.** The format doc now has an *Echo Lyric Splice* section; OPE exposes it in the part inspector and models the per-verse "pure tail" rule so the alignment grid can tell the user which verses will actually splice (§3.5).
4. **A TOML syntax error refuses to open the file** — dialog with file, line, column, and the quoted source line; no partial model, no empty editor. Semantic errors never block editing and only prompt on save; style warnings are purely advisory (§8.5).
5. **Round noteheads only.** No shape-note rendering and no toggle for it, even though shape notes are the published house style — the editor shows what the data says with one glyph set and one less control (§7.1).
6. **The language registry is bundled.** A `constexpr` table mirroring `LANGUAGES` in `src/i18n.rs`; OPE never reads a Rust source tree. Adding a language is a documented two-repo change, and unknown codes still open and edit while raising `E-LANG-UNKNOWN` (§9.4).

The last two both fall out of one rule worth stating on its own: **the only thing OPE requires on disk is a songs directory.** No Rust toolchain, no OpenPsalm checkout, no LilyPond, no soundfont, no network — at build time for the tests as much as at runtime.
