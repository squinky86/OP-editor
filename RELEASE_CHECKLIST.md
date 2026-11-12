# Release checklist

## 0.2.0 beta — 2026-11-11

- [x] Consolidate the unpublished 0.2.0 and 0.2.1 changes under 0.2.0, including
  later editing fixes, and align CMake, vcpkg, and workflow package versions.
- [x] Retain the clef implementation plan with its completed status and link to
  the implementation validation record.
- [x] Build RelWithDebInfo with warnings as errors and run the complete suite:
  14/15 targets passed; the external catalog exception is recorded below.
- [x] Build Debug with warnings as errors and Address/UndefinedBehaviorSanitizer;
  run the complete suite with the documented Qt leak-check exclusion. After
  fixing the status-message lifetime bug below, rerun the UI workflow, version,
  and screenshot targets in both builds: all three pass without sanitizer
  diagnostics. The external catalog exception remains unchanged.
- [x] Run the local catalog acceptance check on disposable copies, including
  song 369, SATB songs, and a translation; inspect compact and large screenshots.
- [x] Run the full corpus checker and record any existing catalog limitations.
- [x] Install into an empty prefix, start both binaries, and inspect a CPack
  archive for the version, licenses, documentation, and executables.

Local validation uses Qt 6.10.2 and GCC 15.2.0. The 859-file sibling catalog has
zero parse failures and unchanged-save differences, two existing note
regeneration mismatches (238/Tenor and 251/Alto), and three errors from the
unsupported `\pocorit` marker in song 817. The latter also fails
`CorpusTests::theCorpusHasNoMeasureErrors`. The existing pre-release checker and
the 0.2.0 checker produce byte-identical diagnostic logs. No catalog files were
changed. The sanitizer run also found a use-after-free in the phrase-break
shortcut's status message; the release fixes it by retaining the part name
across the document update, with status-message regression coverage.

After local validation, commit and push all changes, then build and verify the
Debian 13 amd64 and static Windows x64 packages in CI. Push `v0.2.0` and publish
the beta release with both packages and their SHA-256 checksums from that exact
commit. Record final CI results and artifact links on the GitHub release.

## 0.1.0 beta (historical checklist)

- [ ] Complete every 0.1 engineering phase in `ROADMAP_0.1.md`, or explicitly
  move it out of the beta scope in the changelog.
- [ ] From a clean first-run profile, download OP-songs HEAD and confirm the
  destination, progress, validation summary, library switch, and first song open.
- [x] Replace an existing managed corpus and verify its timestamped backup; force
  an install failure and verify automatic rollback.
- [x] Test offline, TLS/proxy failure, truncated ZIP, traversal/link entries,
  entry/size limits, corrupt TOML, and a corpus-level validation error. None may
  alter the installed corpus.
- [ ] Exercise problem-report and edited/new-song contribution workflows with a
  normal GitHub account, including cancel/sign-in/offline paths and lawful source
  attachment instructions.
- [ ] Verify the exact submitted TOML reparses and passes the same checker in CI;
  verify provenance and hashes in the generated review artifacts. Confirm a new
  song is attached as `song.toml` (or pasted as TOML) without a proposed ID.
- [ ] Proofread and follow every installed 0.1 task guide on Linux and Windows
  using keyboard-only navigation.

- [ ] Build with warnings as errors in Debug and RelWithDebInfo.
- [ ] Run `ctest --output-on-failure` in both builds.
- [ ] Run the Debug suite with `-DOPE_ENABLE_SANITIZERS=ON`.
- [ ] Run `ope-check --check /path/to/OpenPsalm/songs --errors-only` and require
  zero parse failures, round-trip changes, re-emission mismatches, and errors.
- [ ] Run the screenshot smoke test without an audio server.
- [ ] Install into an empty prefix and start both installed binaries.
- [ ] Create the CPack archive and inspect its version, license, README,
  changelog, and executables.
- [ ] In a disposable corpus copy, exercise Save Current, Save All, undo,
  translation creation/discard, new-song creation/discard, and an external-edit
  conflict.
- [ ] Check score, lyrics, source, docks, dialogs, keyboard traversal, and focus
  visibility at compact and large window sizes.
- [ ] Tag `v0.1.0`, publish the artifacts and SHA-256 checksums, and label the
  release clearly as beta software.
