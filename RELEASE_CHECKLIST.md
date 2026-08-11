# 0.0.1 alpha release checklist

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
- [ ] Tag `v0.0.1`, publish the archive and SHA-256 checksum, and label the
  release clearly as alpha software.
