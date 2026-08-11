# 0.1.0 beta release checklist

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
