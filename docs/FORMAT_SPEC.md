# OpenPsalm TOML Song Format Specification

Copyright (C) 2026 Jon Hood, OpenPsalm.com  
SPDX-License-Identifier: AGPL-3.0-or-later

---

## 1. Top-Level Metadata Fields

| Field | Type | Required (Base) | Description |
|---|---|---|---|
| `title` | String | Yes | Primary hymn title |
| `subtitle` | String | No | Secondary title or tune name |
| `active` | Boolean | No (default true) | Whether the song is active in hymnal |
| `language` | String | Yes (default "en") | ISO language code |
| `verse_count` | Integer | Yes | Number of standard verses |
| `key_signature` | String | Yes | e.g. "C", "G", "Eb", "F#" |
| `time_sig_numerator` | Integer | Yes | e.g. 4, 3, 6 |
| `time_sig_denominator` | Integer | Yes | e.g. 4, 8, 2 |
| `tempo_bpm` | Integer | Yes | Standard tempo in beats per minute |
| `copyrights` | Array of Strings | No | Copyright lines |
| `commentary` | String | No | Background/historical notes |
| `phrase_breaks` | Array of Strings | No | Mandatory phrase breaks in `"M:T"` format |
| `optional_phrase_breaks` | Array of Strings | No | Optional phrase breaks in `"M:T"` format |
| `non_breaking_phrase_breaks`| Array of Strings | No | Non-breaking markers |

---

## 2. Note Token Stream Syntax

Notes for each part are written as space-separated tokens in triple-quoted multiline strings separated by measure barlines ` | `.

### 2.1 Notes, Rests, Spacers & Chords
- **Single Note**: `[prefix]PITCH[DUR][DOTS][POSTFIX]`
  - Examples: `c'4`, `f#'8.`, `bb2`, `g,1`
- **Rest**: `r[DUR][DOTS]` (e.g. `r4`, `r2.`, `r8`)
- **Spacer**: `s[DUR][DOTS]` (e.g. `s4`, `s16`)
- **Chord**: `<pitch1 pitch2 ...>[DUR][DOTS]` (e.g. `<c' e' g'>4`)

### 2.2 Octave Marks
- `'` (apostrophe): Octave 4 (one `'` = 4, `''` = 5, etc.)
- `,` (comma): Lower octaves (no comma = octave 3, `,` = octave 2, `,,` = octave 1)

### 2.3 Articulations, Slurs, and Markers
- `( ... )`: Slur start and end
- `[ ... ]`: Beam start and end
- `~`: Tie
- `!`: Fermata
- `.`: Staccato (when attached to note name)
- `@c`: Chorus section marker
- `@e`: Coda section marker
- `@sN`: Shared lyric section marker `N` (e.g. `@s1`)
- `\rit`, `\accel`, `\a_tempo`: Tempo spanner markings (Soprano only)

### 2.4 Tuplets
- `{3:2 note1 note2 note3}`: 3 notes in the time of 2
- `{3 note1 note2 note3}`: Shorthand for `{3:2 ...}`
- Tuplets cannot cross a barline ` | `.

---

## 3. Lyrics Syntax

Lyrics sections are defined under `[lyrics.KEY]` or `[parts.NAME.lyrics.KEY]`.

- Words are space-delimited.
- Multi-syllable words are joined with ` -- ` (e.g. `won -- der -- ful`).
- Melismas can be marked explicitly with `_`.
- Elisions (two words sung on a single note) use the undertie `‿` (U+203F), e.g. `to‿our`.
