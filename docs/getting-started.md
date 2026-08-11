# Getting started with OpenPsalm Editor

This guide takes a new user from an empty installation to a safely saved song.
OpenPsalm Editor (OPE) edits the TOML source files used by OpenPsalm; it does not
keep a separate song database. The file shown in the Source tab is the source of
truth.

## 1. Choose where the songs come from

OPE supports a managed snapshot or a directory you manage yourself. Both look
the same in the song browser, but updating them is intentionally different.

### Managed snapshot: simplest first setup

Choose **File ▸ Download Latest OP-songs…**. The confirmation dialog shows the
exact application-data directory that will be used. Read that path before
continuing.

OPE then performs these steps:

1. Downloads a ZIP snapshot of the head of the public OP-songs `main` branch.
2. Keeps the download in a temporary staging directory. The current corpus has
   not changed yet.
3. Rejects paths that could escape the staging directory, links and special
   files, more than 10,000 entries, individual files over 32 MiB, a download
   over 32 MiB, or more than 256 MiB of expanded data.
4. Parses every discovered base song and translation.
5. Confirms that saving each untouched TOML would produce identical bytes.
6. Regenerates every notation token and confirms that it means the same thing
   after parsing again.
7. Runs all error, warning, and informational validation rules. Translation
   overlays are checked after merging them with their base song, as the
   OpenPsalm seeder sees them.
8. Installs only if there are no parse failures, round-trip changes, notation
   mismatches, or validation errors.

The installed `.openpsalm-snapshot.json` records the source branch, requested
and final download URLs, UTC download time, HTTP ETag, archive SHA-256, archive
root, and OPE version. A resolved Git commit SHA will be added before 0.1.0.

If this is an update, OPE moves the old directory to a sibling path ending in
`.backup-YYYYMMDD-HHMMSS`. It does not merge the download with local edits. If
activation of the new directory fails, OPE attempts to restore the old one.

Warnings do not block a snapshot because the current corpus intentionally has
style findings that are not seed failures. The completion message states the
number of warnings; errors always block installation.

### Existing checkout: best for Git users

Press **Folder…** in the Songs dock, or choose **File ▸ Preferences…**, and pick
the OP-songs repository root. Pick the directory containing `1/`, `2/`, and the
other numbered directories—not a particular numbered directory and not a
`song.toml` file.

OPE never runs Git commands on this directory. Pull, branch, diff, commit, and
restore operations remain under your control. Press **F5** after pulling or
making changes outside OPE.

The managed-download command always targets OPE's application-data directory.
It does not overwrite the external directory selected in Preferences.

## 2. Open and inspect a song

Use the search box in the Songs dock. Search by title, subtitle, or exact song
number, then click the result.

- **Score** shows notation and red measure-total errors. Click a note to select
  it; the Inspector edits the selected part.
- **Lyrics** edits global and per-part text and shows lyric slots aligned with
  notes. The alignment grid is the safest place to diagnose a missing or extra
  syllable.
- **Source** previews the exact bytes OPE will write. Unknown TOML fields remain
  untouched. Advanced fields can be edited in a system text editor; OPE will
  detect the external change before its next save.
- **Problems** lists rule IDs, severity, location, and explanation. Clicking a
  navigable finding moves to its note or lyric slot.

## 3. Make and validate an edit

Edit through the score, Lyrics tab, Song dock, or Inspector. The window title
and language tab gain a dot when that language has unsaved work. Translations
have independent dirty state and undo history.

Watch the Problems dock while editing. An **Error** means the song may not seed
or may seed incorrectly. OPE allows an explicit “Save anyway” for recovery and
expert work, but such a file is not ready to contribute. A **Warning** identifies
a likely style or consistency problem that needs judgment. **Info** documents
preserved or unusual structure.

For a terminal or CI check, run:

```sh
ope-check --check /path/to/OP-songs --errors-only
```

A contribution-ready corpus has zero parse failures, round-trip changes,
re-emission mismatches, and errors. Warnings should be reviewed, not blindly
suppressed.

## 4. Save without losing external work

Choose **Save Current** for the selected language or **Save All** for every dirty
language. OPE compares the file on disk with the exact bytes originally opened
immediately before saving. If another program changed or deleted that file, OPE
stops and asks before overwriting. Cancel and compare/reload unless you are sure
the external version is no longer needed.

Writes use an atomic replacement file on the destination filesystem. New songs
and translations remain only in memory until the first explicit save; cancelling
their dialogs creates no directories.

## 5. Recover a managed corpus backup

The current UI retains and reports the backup path but does not yet have a
one-click restore command. To restore manually:

1. Close OPE so no corpus file is open.
2. Locate the managed destination shown by the download dialog.
3. Rename the current `OP-songs` directory to a clearly marked temporary name.
4. Rename the chosen `OP-songs.backup-YYYYMMDD-HHMMSS` directory to `OP-songs`.
5. Start OPE and press **F5**.

Do not delete the current directory until the restored backup opens and passes
`ope-check`. A restore/delete-backup interface is planned before 0.1.0.

## Where to learn the song format

The authoritative references live in OP-songs:

- [Song TOML format](https://github.com/squinky86/OP-songs/blob/main/docs/song-toml-format.md)
- [Song style guide](https://github.com/squinky86/OP-songs/blob/main/docs/song-style-guide.md)

The format guide explains syntax. The style guide explains which valid
construct to choose for melismas, ties, lyric slots, phrase breaks, dynamics,
translations, and other musical decisions.
