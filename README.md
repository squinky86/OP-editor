# OpenPsalm Editor

A desktop editor for OpenPsalm `song.toml` files — fixing hymns, writing singing
translations, and creating new ones, without hand-editing TOML and waiting for
the seeder to tell you what broke.

Copyright © 2026 Jon Hood, OpenPsalm.com. Licensed under the
[GNU Affero General Public License v3.0 or later](LICENSE).

The design and the reasoning behind it are in [OPE.md](OPE.md).

---

## What it does

- **Shows the song as music.** Staves, lyrics under the notes, phrase-break
  lanes, and a red overlay on any measure whose ticks do not add up.
- **Catches what the seeder does not.** A verse with one syllable too many is
  silently truncated on import; a typo'd duration silently becomes a quarter
  note. Both are errors here, before the file is written.
- **Aligns lyrics visibly.** The alignment grid puts one column per lyric slot
  with the note above it, so a translator can see which notes take a syllable
  and which are melisma continuations that do not.
- **Says who sings what.** Lyrics are edited one section at a time: the song's
  own text on top, and under it a box per voice that overrides it, each labelled
  with the voice and the TOML table it writes, each counted against *that*
  voice's slots, and each revertible in one click.
- **Plays the hymn.** All voices, any verse, with per-part mute and a tempo
  override — using the same tempo, velocity, and tie rules as the site's MIDI
  export, so what you hear is what the site renders.
- **Keeps diffs honest.** Saving a song rewrites only the bytes you changed.
  Opening and saving any of the 200-plus songs in OP-songs without editing
  produces a byte-identical file; this is enforced by a test.

It does not export anything. MusicXML, LilyPond, PDF, MIDI files, and slides
stay with the website; this program edits the source of truth.

## Building

Needs Qt 6.5+, CMake 3.25+, and a C++23 compiler (GCC 13+, Clang 17+, or
MSVC 19.35+).

```sh
cmake -S . -B build
cmake --build build -j
```

Two binaries land in `build/src`:

| Binary | Purpose |
|---|---|
| `ope` | the editor |
| `ope-check` | the same format checks with no GUI, for a terminal or CI |

## Running

```sh
build/src/ope                      # opens with the song list
build/src/ope path/to/song.toml    # opens one song
```

On first run, press *Folder…* under the song list (or *File ▸ Preferences*) and
point it at your OP-songs checkout — the folder holding the numbered song
directories. That is the only thing the editor needs on disk: no Rust toolchain,
no OpenPsalm checkout, no LilyPond, no soundfont, no network.

The song list stays on screen: click a hymn to open it, and press *Refresh*
(or `F5`) after a `git pull` or an edit made outside the editor.

### Checking a corpus from the terminal

```sh
build/src/ope-check --check /path/to/songs
build/src/ope-check --check /path/to/songs --errors-only
build/src/ope-check --check /path/to/songs/42/song.toml
```

It reports three things per file: whether it parses, whether saving it unchanged
would alter a single byte, whether every note token survives being regenerated —
and then runs the rule engine. The exit code is non-zero if anything failed.

## Keyboard

In the score view:

| Key | Action |
|---|---|
| `←` `→` | previous / next note |
| `↑` `↓` | pitch up / down one step |
| `Shift+↑↓` / `Ctrl+↑↓` | by a semitone / by an octave |
| `Alt+↑↓` | move to the part above / below |
| `1 2 4 8 6 3` | whole, half, quarter, eighth, 16th, 32nd |
| `.` | add a dot |
| `R` / `P` / `N` | rest / spacer / note |
| `T` | tie |
| `S` / `Shift+S` | slur start / end |
| `B` / `Shift+B` | beam start / end |
| `D` / `Shift+D` | dashed slur start / end |
| `F` / `K` | fermata / staccato |
| `C` / `E` | `@c` chorus marker / `@e` coda marker |
| `Ins` / `Del` | insert / delete a note |
| `Ctrl+B` | phrase break at the cursor (OPE computes the `"M:T"` value) |
| `Ctrl+Shift+B` / `Alt+B` | optional / non-breaking phrase break |
| `Ctrl+Enter` | copy this marking to every voice sounding on the beat |
| `Ctrl+wheel` | zoom |

Anywhere:

| Key | Action |
|---|---|
| `Space` | play / pause |
| `F5` | re-read the songs folder |
| `Ctrl+O` / `Ctrl+L` | jump to the song list |
| `Ctrl+1` `Ctrl+2` `Ctrl+3` | score / lyrics / source |

## Tests

```sh
cd build && ctest --output-on-failure
```

`FormatTests` and `DocumentTests` are ports of the unit tests in OpenPsalm's
`src/seed/` — where they disagree with the Rust seeder, the seeder is right and
this program has a bug. `CorpusTests` runs the whole round-trip and validation
suite against a real songs directory; set `OPE_SONGS_DIR` to point it somewhere,
or let it find `../OpenPsalm/songs` beside this repo. Without one it skips.

## Layout

```
src/core/     the format: TOML spans, note tokens, lyric slots, rules, playback
src/cli/      the headless checker
src/app/      the open document, its undo stack, and its findings
src/audio/    the built-in synth and the transport
src/ui/       score, lyrics, panels, dialogs
tests/        unit tests plus the corpus gate
```

`src/core` depends only on QtCore, so the whole format layer runs headless.
