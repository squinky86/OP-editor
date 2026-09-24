# 0.2.0 clef and TTBB validation

Local implementation review: 2026-09-23, Qt 6.10.2 and GCC 15.2.0.

## Scope

QtCore now owns canonical clefs, sounding-pitch staff anchors, role order,
labels, staff membership, stems and arrangement-lead selection. Validator,
score, Inspector, lyrics, transport and New Song share those policies. The
parser and span-based serializer remain the format foundation. Small overlay
helpers create only the part fields, notes or lyric map being edited.

The default tests use authored fixtures. They require neither an OpenPsalm
checkout nor the song catalog. Clef changes preserve source pitches and synth
samples. Tests exercise metadata omission/normalization, invalid drafts,
overlays, undo, role sorting, shared unisons/crossings, all 16 TTBB voice subsets,
tempo ownership, CLI checks, contribution preflight and an offline downloaded
TTBB snapshot.

## Commands and results

```sh
cmake -S . -B build-newclefs -DCMAKE_BUILD_TYPE=Debug
cmake --build build-newclefs -j6
ctest --test-dir build-newclefs --output-on-failure

cmake -S . -B build-newclefs-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DOPE_ENABLE_SANITIZERS=ON
cmake --build build-newclefs-sanitize -j5
ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir build-newclefs-sanitize --output-on-failure
```

Both suites passed all 14 test targets. The optional local catalog check skips
unless explicitly enabled; it was also run separately with isolated preferences
and temporary copies of songs 369, 8, 13, 101, 103, 273 and 162/es. It checked
save/reopen, unchanged and metadata-edited playback, all voice subsets, three
stanzas, phrase rulers, pitch editing/undo, zoom, and 760×760 / 1440×1000 windows.
The resulting score and window screenshots were inspected.

The local binary is `build-newclefs/src/ope`; `--version` reports **0.2.0**.
Run the local catalog gate using the command in the README. Test artifacts and
reference MIDI comparisons for this run are in `/tmp/ope-newclefs-review/`.
The synth tests render deterministic samples; subjective listening remains a
maintainer review activity.

## Corpus baseline

The freshly rebuilt baseline checker examined 373 files: no parse failures,
no unchanged round-trip differences, no validation errors, 239 warnings and
two existing notation regeneration mismatches. After implementation it found
234 warnings and the same two mismatches:

- Song 238, Tenor, measure 11: `a8-([` regenerates as `a8-(`.
- Song 251, Alto, measure 3: `dis'4-)/+24` regenerates as `dis'4/+24-)`.

Consequently the full `ope-check --errors-only` command still returns failure
for those existing regeneration issues. The corpus test suite's narrower
round-trip/token assertions pass. No catalog files were repaired or written.
Song 369 itself passes all checker gates with zero warnings and errors.

The sibling catalog was being edited during this work. Local acceptance froze
a copy of song 369 with SHA-256
`8a24ac5a1f638f82c3b1d9e0c68676e6c8d8b53eda900debebe7cb9505147321`.
That copy retains 32 measures in 3/4, B-flat major, tempo 96 and three stanzas.
Its seven four-measure phrase boundaries are split between required and
optional breaks. Its current tenor checkpoints differ from the planning file:

| Part | Opening MIDI | Final MIDI | MIDI range |
|---|---|---|---|
| Tenor1 | 58 | 58 | 58–67 |
| Tenor2 | 53 | 62 | 53–65 |
| Baritone | 50 | 53 | 50–60 |
| Bass | 46 | 46 | 41–58 |

Acceptance verifies preservation of that supplied source, and reports the
differences from the planning snapshot instead of changing the music. The
standalone synthetic fixture independently retains the plan's original opening
and final pitch constants. No second octave shift is applied to either input.

## Tempo comparison and monitoring limits

A scratch Rust harness linked the current OpenPsalm library and generated
ensemble and Tenor-II-only MIDI from the same synthetic lead span used by
`DocumentTests`. Independent MIDI decoding confirmed eight tempo changes at
ticks 180 through 1440 (480 PPQ), followed by restoration at tick 1920. The
second voice's half-note onsets were 0, 1.131498500, 2.687817125 and 3.687817125
seconds; the end was 4.687817125 seconds. OPE asserts those constants and keeps
the same timing when the lead is muted.

This establishes tempo parity for the exercised cases. Pre-existing monitoring
differences remain: a simple per-event hairpin velocity ramp, full-length
staccatos, fine-tuplet rounding at 48 PPQ, and an interpolated measure cursor.
These limits are also described in the getting-started guide.
