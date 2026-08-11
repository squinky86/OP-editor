# OpenPsalm Editor 0.1 release plan

Version 0.1.0 is the first beta. Its product promise is larger than “the editor
can write TOML”: a person who knows the hymn, but does not know Git or the
OpenPsalm seeder, should be able to obtain the corpus, make a safe correction or
addition, prove that the result is structurally sound, and hand maintainers a
useful contribution.

This plan separates safety gates from convenience features. A convenient path
must never bypass a gate.

## User journeys that define the beta

### Start with the current public corpus

1. Choose **File ▸ Download Latest OP-songs…**.
2. See the exact managed destination and an explicit replacement warning.
3. Download the head snapshot of `squinky86/OP-songs`'s `main` branch.
4. Extract into an isolated staging directory. Reject absolute paths, parent
   traversal, links and special files, multiple archive roots, excessive entry
   counts, and excessive compressed or expanded sizes.
5. Before installation, parse every base song and translation, prove an
   unchanged save is byte-identical, re-emit and reparse every notation token,
   and run all validation rules against effective translation overlays.
6. Activate the staged tree with directory renames. Move an existing managed
   corpus to a timestamped backup and restore it if activation fails.
7. Switch the song browser to the managed corpus and report the number of files
   checked. Never merge a downloaded snapshot over local edits.

The first implementation of this journey is now present and records download
URL, time, HTTP ETag, archive SHA-256, and application version in the installed
snapshot. Before beta, also resolve and record the commit SHA; add
progress/cancellation around corpus validation; and add a restore/delete-backup
UI.

### Report a problem without editing

1. Open the affected song and choose **Help ▸ Report a Song Problem…**.
2. Open OP-songs' maintained `song-problem.yml` GitHub form in the browser.
3. Prefill song number, title, language, filename, editor version, and any
   selected part/measure/verse that can be expressed accurately.
4. Let the reporter describe the expected result and attach a lawful source.

The first implementation prefills song identity, language, filename, editor
version, song-page URL, and the selected part/measure when available. An offline
copyable report and the edited-song submission bundle remain.

### Submit an edited correction or a new song

The first guided contribution bundle is implemented. It validates the exact
proposed bytes, retains the session-opened baseline across saves, generates
review artifacts, copies submission text, and opens a prefilled GitHub form.
New songs are submitted as an identity-free `song.toml`; maintainers assign the
corpus ID.
The remaining beta work is called out in this workflow:

1. Explain that contributions are public, require a GitHub account, and must not
   include copyrighted material without permission.
2. Flush all pending editor fields and require the user to resolve external-file
   conflicts. Packaging may occur before or after Save; the opened baseline is
   retained separately for the session.
3. Run a **submission preflight** over the exact bytes that will be attached:
   TOML syntax; supported filename and numeric directory; base/overlay
   relationship; known language; required metadata; copyright/permission
   declarations; measure totals; notation token fidelity; lyric-slot counts;
   phrase boundaries; cross-part rules; parse-after-write; and unchanged-field
   preservation.
4. For an update, compare against the resolved downloaded HEAD and show a
   file-level and semantic summary. Block unrelated files and warn when the
   baseline is stale. For a new song, do not propose or deconflict an upstream
   ID: validate the standalone `song.toml` and leave directory assignment to a
   maintainer.
5. Distinguish errors (submission blocked), warnings (explicit acknowledgement),
   and information. Every finding needs a rule ID, plain-language explanation,
   exact location, and navigation back to the editor.
6. For corrections and translations, produce a small contribution bundle
   containing the changed TOML files, any required `copyright.txt`, a unified
   diff, the preflight report, provenance, and SHA-256 hashes. For a new song,
   make the standalone `song.toml` the primary attachment and provide a complete
   TOML code block as fallback. Never include unrelated files or credentials.
7. Open the matching GitHub issue form with a concise prefilled description and
   copy the full Markdown report to the clipboard. Because browser issue forms
   cannot reliably upload local files, give explicit drag-and-drop instructions
   for the applicable file or bundle. A later authenticated GitHub integration may open a branch
   and pull request, but 0.1 must not store a personal access token.
8. After the browser opens, keep the bundle available and show its path until
   the user confirms that submission is complete.

An issue is appropriate for problem reports and proposed additions. A tested
TOML change is ultimately easier to review as a pull request, so the generated
issue should contain enough provenance and attachments for a maintainer to
convert it without re-collecting information.

## Integrity model

All entry points—Save, `ope-check`, corpus download, and contribution
submission—must call shared checks. The release must not grow separate “GUI
validation” and “CI validation” definitions.

Required gates for beta:

- Archive safety and download size limits before extraction.
- TOML parse errors with line, column, source line, and caret.
- Byte-identical no-op serialization and parse-after-edit serialization.
- Lossless note-token re-emission, including pitches, durations, tuplets,
  articulations, dynamics, spanners, and offsets.
- Schema and semantic validation for base files and merged overlays.
- Corpus-level identity checks: directory ID, filename/language, duplicate IDs,
  duplicate languages, and translation base presence.
- Contribution-diff checks: allowlisted paths, no accidental deletions, no
  generated/editor files, and no unrelated song changes.
- Copyright checks that are explicit but honest: software can require complete
  declarations and permission files; it cannot determine public-domain status.
- Hashes and resolved upstream commit recorded for every installed snapshot and
  contribution bundle.

## Documentation required for 0.1

The README remains the quick start. The beta also needs task-oriented, verbose
documentation installed with the application:

- `docs/getting-started.md`: install, managed download, existing checkout,
  first edit, validation, save, and backup recovery.
- `docs/editing-songs.md`: score, lyrics, translations, source view, findings,
  keyboard and accessibility workflows.
- `docs/contributing.md`: problem report versus edited contribution versus new
  song; GitHub account expectations; preflight; bundle; licensing; maintainer
  review lifecycle.
- `docs/validation.md`: every rule ID, severity, rationale, example, and fix.
- `docs/troubleshooting.md`: network/TLS, proxy, permissions, disk space,
  corrupt archive, failed validation, external edits, and backup restoration.
- Links to OP-songs' format reference and prescriptive style guide, with a note
  that the repository documents are authoritative when they differ.

Every dialog added for beta needs matching documentation and at least one
keyboard-only path. Screenshots are useful, but instructions must remain usable
without them.

## Engineering phases

- [x] Expose the existing whole-corpus checker as structured data without
  weakening the command-line interface.
- [x] Add bounded, path-safe ZIP extraction and rollback-safe snapshot install.
- [x] Add managed HEAD download, explicit overwrite warning, full pre-install
  checks, retained backup, and automatic library switching.
- [x] Add a prefilled OP-songs song-problem action.
- [x] Record source, time, ETag, archive hash, archive root, and editor version
  in each managed snapshot.
- [ ] Resolve/display the upstream commit SHA and restore/manage backups.
- [ ] Add corpus identity and contribution-diff validation rules.
- [x] Build exact-byte correction/new-song preflight, correction bundles,
  identity-free new-song TOML/code-block handoff, and browser handoff without
  storing GitHub credentials.
- [ ] Compare updates against independently resolved HEAD, reject stale
  baselines, and support one bundle containing coordinated
  base-plus-translation edits. New-song IDs remain maintainer-assigned.
- [ ] Expand the installed task documentation and in-app contextual help.
- [ ] Add tests for interrupted downloads, HTTP/TLS errors, archive limits,
  install rollback, stale baselines, dirty documents, overlay contributions,
  and keyboard-only workflows.
- [ ] Exercise the live OP-songs HEAD in CI as a scheduled compatibility job;
  keep release builds pinned to a recorded commit for reproducibility.
- [ ] Complete the beta release gates in `RELEASE_CHECKLIST.md`, then bump CMake,
  vcpkg, workflow, About, package, and changelog versions together.

## Deliberate non-goals for 0.1

- No background auto-update and no silent replacement.
- No merging a remote snapshot into a locally edited corpus.
- No GitHub password or personal-access-token storage.
- No promise that automated checks prove copyright ownership or musical
  correctness.
- No direct push to OP-songs without a human-visible review artifact.
