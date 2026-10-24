# PLAN B — no-sudo helper claim (#181/#177) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/macos_helper/openscanner-helper (build-tree copy (fixed order 3: build/tools/*/<target>)); build/tools/raptor_shim/raptor_shim (build-tree copy (fixed order 2))
- Sha256: openscanner-helper `508dbd8c2beec0765dc266f5d0a620865c40b6f24cc4272550c5e440ef807865`; raptor_shim `c8f0dad26ff1f008b891923908c2055d0fdaf528200c26e9d38e4b5a75863d53`
- HEAD: `e7e1ad200b4ff10a86008ec723bd53ff78503e62`
- Other copy: none
- Preflight (openscanner-helper): pwd OK; no conflicting process; daemon: n/a; freshness: source dir not auto-found, verified by hand
- Preflight (raptor_shim): pwd OK; no conflicting process; freshness: binary 21:23:57 >= newest source live_source.cpp 20:52:26
- Interposer: n/a — neither tool links the vendor SDK
- Run start: 2026-10-21 22:23:04 / 22:25:43
- Peer: our shim (raptor_shim) querying our helper's user LaunchAgent (openscanner-helper); no vendor RPC involved in this step

## Commands (run as the user, no sudo at any step)

```
$ tools/macos_helper/install_user_agent.sh install
$ tools/macos_helper/install_user_agent.sh status
$ build/tools/raptor_shim/raptor_shim --helper-status
$ tools/macos_helper/install_user_agent.sh uninstall   # quit/release
```

## Result

- Exit codes: install `0`, status `0`, `raptor_shim --helper-status` `0`, uninstall `0`
- Verdict: **PASS**
- Raw logs: `2026-10-21-plan-b-helper-install.raw.log`, `2026-10-21-plan-b-helper-status.raw.log`, `2026-10-21-plan-b-shim-helper-status.raw.log`, `2026-10-21-plan-b-helper-release.raw.log`

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Install registers the LaunchAgent, no sudo prompt | **[PASS]** `install_user_agent: registered org.openscanner.helper in gui/501` (plist under `~/Library/LaunchAgents`, exit 0, no `sudo` invoked) |
| 2 | Claim reached | **[PASS]** helper log: `claim: idle -> claiming (start)` → `claiming -> claimed (device=iokit-usb)` → `start: claim: ok=true state=claimed` |
| 3 | `launchctl print` status | **[PASS]** `state = running`, `pid = 8338`, `arguments = {... --claim iokit --service-name org.openscanner.helper}` |
| 4 | Shim's own claim query (the no-sudo check) | **[PASS]** `[shim] helper status on org.openscanner.helper: claimed-by-helper`, exit 0 |
| 5 | Quit/release | **[PASS]** helper log: `quit: signal` → `claimed -> releasing (quit)` → `releasing -> released (quit: release complete)`, `quit: release: ok=true state=released` |

The claim did **not** refuse as the user — #177's non-sudo path reached `claimed` end to end. The cold-unplug/release variant of this same claim is not covered by this record (needs the operator to pull the cable while the agent is live; see Open questions).

## Crash sweep

`scripts/hw-postflight.sh plan-b-helper '2026-10-21 22:23:04' openscanner-helper` →
`hw-postflight: no crash report for 'openscanner-helper' at/after 2026-10-21 22:23:04 — safe to record 'no crash report (checked DiagnosticReports for openscanner-helper at/after the run start)'.`

## Open questions

- Does the cold-unplug/release path (device physically unplugged while the user-agent helper is serving) also complete cleanly? This run only exercised quit/release, not unplug/release, for the no-sudo installation — an operator is needed to pull the USB cable live.
- Is this result consistent with, or does it supersede, the earlier `2026-10-21-plan170-helper-claim-blocked.md` record (which found the **sudo** `--once` invocation of the same binary blocked on an interactive password prompt)? That record exercised a different invocation (direct `sudo openscanner-helper --once`), not the user LaunchAgent path tested here.

## Board

- Row move: none proposed by this record — reporting the observable only. #181/#177 ("the no-sudo claim") completed PASS via the user-agent path; whether this closes those rows outright or needs the unplug variant too is for the dispatcher to judge against the row's acceptance criteria.
