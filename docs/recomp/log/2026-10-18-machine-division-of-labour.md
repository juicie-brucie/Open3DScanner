- 2026-10-18 — operator rule: **the machines split by fork.** Sessions on this Mac
  work the **macOS** fork only; the Windows 11 machine's agents own the **Windows**
  fork. No further Mac-session work on Windows artifacts — row #57 (the Win64 gate)
  was the last such item, and its hand-off is the macOS export list
  (`docs/recon/findings/macos-exports.txt`) plus the `win64-surface.md` findings.
  The Windows lane takes it from there: the export diff, the 36 Windows-only
  surfaces, the fork's seeding and contract pin. Cross-machine traffic is
  knowledge hand-offs over the coordination channel, not shared work.
