- 2026-10-19 — **selftest rerun for #120/#122** (`tests/results/2026-10-19-raptor-client-selftest-rerun.md`): `ctest -R
  raptor_client_selftest` PASS and the direct binary exit 0, with the two new blocks called out explicitly —
  `ReplayInterfaceSelftest()` 18/18 (#120: suffix parsing, routing table, overrides, rejections) and
  `ReplyResultSelftest()` 8/8 (#122: envelope vs payload, error slot, bare payload, timeout). The record maps each
  printed line to its `check()` call by reading `main.cpp`, not by naming. No board move: this confirms the selftests in
  isolation and does not touch #105/#127's open question — why the daemon's replies carry no binary frame data.
