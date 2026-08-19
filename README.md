# OpenPsalm Editor

A desktop editor for OpenPsalm `song.toml` files — fixing hymns, writing singing
translations, and creating new ones, without hand-editing TOML and waiting for
the seeder to tell you what broke.

Copyright © 2026 Jon Hood, OpenPsalm.com. Licensed under the
[GNU Affero General Public License v3.0 or later](LICENSE).

The design and the reasoning behind it are in [OPE.md](OPE.md).
For task-oriented instructions, start with
[Getting started](docs/getting-started.md) and
[Reporting and contributing songs](docs/contributing.md).

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
- **Refuses silent overwrites.** If a file changed on disk after it was opened,
  OPE stops before saving and requires an explicit decision. Each translation
  has its own dirty marker and undo history, with separate Save Current and Save
  All actions.

It does not export anything. MusicXML, LilyPond, PDF, MIDI files, and slides
stay with the website; this program edits the source of truth.

Every TOML field used by the current corpus is available through the Song and
Inspector tabs, the Score or Lyrics panes, and the translation workflow. Help ▸
TOML Field Reference maps exact TOML names to those controls. Score, Lyrics, and
Source remain visible as independently resizable and collapsible vertical panes.
Source edits the exact bytes directly with TOML highlighting and word wrapping;
valid edits immediately update every structured pane, while invalid TOML pauses
Save and structured editing until it is fixed or reverted.

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

Tagged and `main` builds also produce two installable workflow artifacts:

- `openpsalm-editor_VERSION-1_amd64.deb`, built and tested on Debian 13.
- `OpenPsalmEditor-VERSION-Windows-x64.zip`, containing self-contained 64-bit
  Windows executables. Qt and the MSVC runtime are rebuilt and linked statically;
  no Qt DLL installation is required. The supported baseline is Windows 10
  version 1809 or newer (including Windows 11); normal Windows system DLLs such
  as the operating system's ICU library are still used.

The exact open-source Qt build inputs are pinned in `vcpkg.json` and
`packaging/vcpkg-triplets/`. See `THIRD_PARTY_NOTICES.md` for licensing and
corresponding-source details.

## Running

```sh
build/src/ope                      # opens with the song list
build/src/ope path/to/song.toml    # opens one song
```

On first run, use one of these two corpus modes:

- Choose *File ▸ Download Latest OP-songs…* to download the head of OP-songs'
  public `main` branch into OPE's managed application-data directory. OPE first
  resolves the exact commit SHA, then downloads that immutable archive. It shows
  the exact destination, extracts into temporary storage, rejects unsafe archive
  entries, and runs the complete `ope-check` suite before changing anything. If
  a managed corpus already exists, it is moved to a timestamped backup rather
  than deleted.
- Press *Folder…* under the song list (or *File ▸ Preferences*) to use an
  existing OP-songs checkout — the folder holding the numbered song
  directories. OPE does not run `git pull`; you retain complete control of that
  checkout.

The editor never silently updates or merges either corpus. A managed download
replaces only OPE's managed directory after a warning; an external checkout is
never overwritten by the download command. Editing itself needs no Rust
toolchain, OpenPsalm checkout, LilyPond, or soundfont. Network access is needed
only while checking or downloading a managed snapshot, or opening GitHub in a
browser.

For a managed corpus, the Songs dock displays its commit and “current as of”
time. OPE compares it with OP-songs HEAD at startup and every six hours. A newer
HEAD turns the textual **Update OP-songs…** button yellow; installation still
requires an explicit click and confirmation. **Backups…** lists retained
snapshots and can validate/restore one or permanently delete a selected backup.

The song list stays on screen: click a hymn to open it, and press *Refresh*
(or `F5`) after a `git pull` or an edit made outside the editor.

### Reporting a corpus problem

Open the affected song and choose *Help ▸ Report a Song Problem…*. OPE opens the
OP-songs repository's maintained GitHub issue form and prefills the song number,
title, language, filename, and editor version. You will need a GitHub account to
submit the issue. Describe both what is wrong and what it should be; a lawful
photo or citation for the source is especially helpful.

To submit an edited correction or new song, choose *File ▸ Prepare
Contribution…* before closing the editing session. OPE preserves the bytes
originally opened even across saves, checks the exact proposed TOML in an
isolated directory, and blocks syntax, round-trip, notation, or validation
errors. For a correction or translation, a successful preflight creates a ZIP
containing the proposed file, a unified patch, a Markdown report, any sibling
`copyright.txt`, and SHA-256 hashes. For a brand-new song, OPE instead puts an
identity-free `song.toml` at the top of the review folder: attach that file
directly to the issue, or paste the TOML code block OPE copies. The corpus
maintainer assigns the upstream song ID. No GitHub credential is stored.

Prefer a freshly downloaded or pulled corpus, and review every warning and the
generated submission artifacts. Automated upstream comparison and
stale-baseline rejection are intentionally outside the 0.1 scope. See [the 0.1
release plan](ROADMAP_0.1.md) for the remaining corpus-integrity checks.

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

Phrase breaks can also be placed with the mouse, and OPE works out the `"M:T"`
value either way:

- **Score ▸ the three ruler lanes** under each system — click a lane to toggle a
  break there. The click snaps to the nearest note boundary, and a dotted ghost
  shows where it will land before you commit to it.
- **Lyrics ▸ Alignment ▸ the `break` row** — click to break the line after that
  syllable or rest, right-click for the optional and non-breaking lanes. This is
  the view that answers "which word does this break follow?", and it hatches a
  break that falls between the displayed voice's notes instead of between two
  of its syllables.

Anywhere:

| Key | Action |
|---|---|
| `Space` | play / pause |
| `F5` | re-read the songs folder |
| `Ctrl+O` / `Ctrl+L` | jump to the song list |
| `Ctrl+1` `Ctrl+2` `Ctrl+3` | expand and focus score / lyrics / source |

## Tests

```sh
cd build && ctest --output-on-failure
```

For the release sanitizer gate:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DOPE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
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
