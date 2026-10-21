# 2026-10-21 — the fresh hardware batch: #170's live gate, the instruments, and the daemon completion

The second 2026-10-21 Claude batch, intaken in three passes. The big result is **#170's
live claim/release gate: PASS** — on real hardware. It ran as root via a passwordless
grant; the privilege model is the new #177.

## #170 — the live claim/release gate (PASS)

- RPCServer up → **non-seizing refusal**: `ok=false state=released`, `0xe00002c5`,
  exit 1; RPCServer still PID 76444 (untouched).
- RPCServer booted out → `claiming -> claimed (device=iokit-usb)`, exit 0.
- `--claim iokit` → `start: serving`; SIGTERM → `claimed -> releasing (quit)`;
  **cold USB pull** → `claimed -> releasing (unplug) -> released (unplug: release
  complete)`.
- **Not covered:** no live XPC `claim`/`release` op (listener only), no replug
  re-claim, no release latency (no timestamps), no binary sha; the test ran as root.

## Stopping the vendor daemon (the test's path, now noted)

The vendor's RPCServer is a **user LaunchAgent**, not a system daemon:
`com.creality.RPCServer`, GUI domain, `/Library/LaunchAgents/com.creality.RPCServer.plist`,
program `/usr/bin/sudo /Library/Creality/RPCServer`. The stop sequence (app quit →
`launchctl bootout gui/$(id -u)/com.creality.RPCServer` → root-process fallback →
`pgrep` verification) is in the hardware queue (PR #451). The bootout is no-admin; the
vendor app re-registers the agent on launch.

## The instruments and the depth/live data

- **#141 v2**: schema stamp + `tick_overshoot` live; 295 ticks, tick p50 ~102.8 ms,
  `tick_overshoot` 2.80, residual 0.01 ms. Exposure/release/broadcast read 0.00 with
  `clients=0` — those segments are unexercised, not disproved.
- **#167 2nd**: `count=1` on the first probe, 5/5, no crash. The raw log has no exit
  codes; the SIGTERM wording is not evidenced. #173 stays for the race root cause.
- **#105/#127 depth**: the live array(11), `[4]` the whole frame, `data=58 empty=16`;
  the `[10]` IR/depth comparison contradicts the earlier IR value — flagged.
- **#153**: exposure PASS again (`applies_total=0`, mean/max 0.00); the
  **picture-unchanged is not observed** — the record says a direct comparison is
  unavailable. `HW`.
- **#93 / #151 (held)**: the HUD half has no rendered line and the record cites the
  prior rerun's histogram against this session's; the browser record's numbers are
  client-side, outside the log's window, and its run start postdates the log. Both are
  held for the tester's repair; the rows stay `HW`.

## The record landing

PR #450 landed 8 records + raw logs, 3 `_` support files, and the four repair addenda:
`four-new-selftests`, `plan151-content-cadence`, `streaming-retry` (record + raw +
addendum together) and the `plan71-attempt9` repair alone (the record was already on
main). The repair addenda are write-once corrections; the records' bytes are unmodified.

## The daemon completion

- **#177** (recon) — the privilege model: replace the `sudo`-as-root path. Two tiers:
  (A) now, unsigned/self-signed: user LaunchAgent or embedded XPC, stable self-signed
  identity, TCC for camera/input monitoring, **no per-run sudo**, the only admin step
  the vendor-agent stop; (B) signed: `SMAppService` one-time approval + notarization,
  same interface. The exact no-sudo four-step test.
- **#178** (build1) — the descriptor probe (IOKit interfaces/endpoints/UVC VS/HID, the
  exact #169 table fields, no guesses) and the live UVC/HID source that replaces the
  refusing seams.
- **#179** (build3) — the shim's `LiveSource` adapter and the **default-off** product
  switch; the helper owns the device; no vendor stop by the code.

## Board

`DONE`: #170, #141, #167. `HW`: #153, #93, #151. `SPEC-READY`: #150. `RECON`: #169,
#173, #177. `BUILD`: #171, #172, #178, #179. #100 remains the Phase D parent.

## Operator cards

1. **#177's no-sudo test** — the #170 four steps as the user (the key privilege result).
2. **#169/#178** — run `--probe` with the device; fill the descriptor table; then the
   live IR+depth capture with no vendor process.
3. The vendor stop before a run (the HW-queue commands; the GUI-domain bootout).
4. #172/#179's vendor A/B; the product switch once the live source and #126 allow it.
5. The T2 repairs (#93 HUD line, #151 clock) — the tester.
