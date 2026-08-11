# Reporting and contributing songs

OP-songs is a public corpus. A useful contribution identifies the exact song,
explains the musical or textual source, contains only relevant files, and passes
the same structural checks as the corpus. GitHub requires an account to submit
an issue or pull request.

## Report a problem without editing TOML

Open the affected language of the song and choose **Help ▸ Report a Song
Problem…**. OPE opens OP-songs' maintained issue form. It prefills:

- song number and title;
- the OpenPsalm song-page URL;
- current language and TOML filename;
- selected voice part and measure, when a score selection exists; and
- the OPE version used to report it.

Choose the closest problem category, give a verse/measure/part location, and
state both the current result and the expected result. If possible, cite the
hymnal, edition, page, recording, or other basis for the correction.

Only attach or quote source material you have the right to share publicly. A
short factual excerpt may be enough to locate a correction; do not upload an
entire copyrighted hymnal merely because it is convenient.

## Submit an edited correction or new song

Choose **File ▸ Prepare Contribution…** while the edited file is still open.
OPE keeps the bytes first opened in this session even if you saved afterward,
so the bundle can still describe the session's before and after states.

1. Start from a fresh OP-songs `main` checkout or a newly downloaded managed
   snapshot. Do not build a correction on an unknown old copy.
2. Edit the affected song in OPE and resolve every error in the Problems dock.
3. Choose **Prepare Contribution…** and select a destination directory. Saving
   first is allowed but not required. If the disk file changed outside OPE, the
   operation stops until you reload and reconcile it.
4. OPE writes the exact proposed bytes to temporary storage and runs syntax,
   byte-round-trip, note re-emission, and semantic checks. A translation is
   checked beside its base `song.toml`, exactly as the seeder merges it.
5. Inspect the created `changes.patch`, `PREFLIGHT.md`, proposed TOML, optional
   `copyright.txt`, and `SHA256SUMS.txt`. The ZIP contains the same files.
6. Click **Open GitHub Issue**. For a correction or translation, OPE copies the
   preflight report; drag the ZIP into the details field and paste the report.
   For a brand-new song, drag the generated `song.toml` into the details field,
   or paste the complete TOML code block that OPE copies. Explain the source and
   intent in either case.
7. Include warnings that remain and explain why they are intentional. Never
   describe a warning-bearing change as “all checks clean” without qualification.

OPE's byte-minimal serializer helps keep diffs focused, but the human review of
the diff remains required.

## Additional rules for a new song

Use **File ▸ New Song…** to create the draft. OPE automatically chooses a local
workspace folder needed to edit and validate the song; you do not choose or
deconflict an ID. That internal folder is not submitted as a proposed corpus ID
and does not appear in the generated `song.toml`, report, patch path, or issue.
The OP-songs maintainer assigns the numbered directory when accepting it.

The wizard creates full-measure rests so the first draft is rhythmically valid.
Replace those rests with the arrangement, enter complete lyric sections and
phrase breaks, and supply accurate copyright lines. Then follow OP-songs'
[import checklist](https://github.com/squinky86/OP-songs/blob/main/docs/song-style-guide.md#import-checklist).

Software cannot determine whether a work is public domain in every jurisdiction
or whether permission covers this repository. Do not label a work public domain
without a defensible basis. Do not add a copyrighted work unless permission is
documented in the form required by OP-songs; retain the relevant
`copyright.txt` with the submission when required.

Before submitting, inspect `song.toml` and every warning, then explain the
musical and rights sources in the issue. Attach `song.toml` directly; if file
attachment is inconvenient, include its complete contents as a TOML code block.

## What remains before 0.1

The current bundle validates exact proposed bytes and blocks errors, but it does
not yet download the resolved upstream commit for comparison. Before beta it
will reject stale baselines for updates, add corpus-level path and integrity
checks, and make warning acknowledgement more explicit. New songs deliberately
carry no proposed upstream ID. OPE will not store a personal access token or
silently push to OP-songs.
