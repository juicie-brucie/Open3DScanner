# 2026-10-21 — the telemetry, retro-analysis and USB-daemon wave

A device-free wave: complete the telemetry surfaces, mine the landed 2026-10-20 logs and
captures for everything they can still answer, and build the Phase D daemon seeds. No
product switch; no live USB source (the descriptor map is still partial).

## Telemetry — built

- **#141** (PR #438, `ff3d167`) — the tick instrument: the pacing wait, `tick - nominal`,
  the **untimed residual**, the exposure/cloud-release segments, the schema stamp; the
  live distribution is the hardware card.
- **#175** (PR #441, `f365ce2`) — the `raptor_client` frame path: per-call latency,
  payload/wire bytes/s, time-to-first-frame, nearest-rank p50/p95/max.
- Already on main: #163 broadcast counters, #164 encoder, #142 HUD frame age, #147
  baseline profile, #156 full-frame harness. The telemetry table is complete in code.

## Retro analysis — what the landed data settled

- **#143** (PR #437, `3cda68f`) — pull semantics: latest-available, **0 % empty**,
  **~6–7 Hz** sequential poll, a **sample** model; the old ~41 %/~84 Hz model is a
  `less 16000` BPF capture artifact. The observed array has **e1=0** and **e2** carrying
  the time — a correction to #131's element naming.
- **#176** (PR #442, `83029d6`) — the 2026-10-20 live-telemetry distribution: tick vs
  nominal, the residual, exposure/broadcast, the device cadence and the tracking
  histograms. The #93 histogram is the **lens-occlusion rerun**
  (`plan93-markerless-rerun.raw.log`: `SUCCESS=5615 LOST=31 FAILURE_MARKERLESS=1343`),
  not the raceloss crash record.
- **#114** (PR #443, `bb2b788`) — stream profiles: RGB 23 / IR 54 / depth 89, the
  `1920x1200@60` tuples; the mode→profile join is #145's operator capture.
- **#150** (PR #440, `a62ab7f`) — IMU retro: the axis geometry, the temperature
  candidates (÷64/÷62/÷100+offset in room range; ÷16 excluded), the gyro inert; the
  operator poses/soak remain.
- **#71** (PR #446, `40df45d`) — plane-toggle retro: the per-tick point count is not
  sufficient; the `plane-removal.md` §3 contradiction resolved; the next test named. The
  effect is not confirmed; `HW`.

Records landed for citability: #434 (the #132 gap-free capture), #448 (the #71 attempt-9
A/B).

## Phase D — the USB daemon

- **#170** (PR #428, `0f39d74`) — the privileged helper skeleton: launchd/`SMAppService`,
  XPC claim/release, non-seizing IOKit claim, device-free tests; the live claim/release
  is the operator card.
- **#171** (PR #445, `acd1a0f`) — the raw capture path: a bounded UVC sink, the #101
  laser/exposure command path, and **one descriptor table** with only `known`/`unknown`
  entries (no guesses); the UVC/HID transports refuse `not-configured`; the mock and the
  sink tests pass. The live "IR+depth with no vendor process" is the operator card.
- **#172** (PR #447, `cbc28d2`) — the RPC shim: the `ob_rpc_*` surface with suffix
  routing, the 11-element whole-frame array, the daemon's unknown-method wording, the
  discovery no-answer, and an **offline #132 bundle source**; `raptor_client` runs
  against it unchanged. The live USB source plugs into the same `FrameSource`; the vendor
  A/B is the hardware card.
- **#169** stays `RECON`: the descriptor map is partial — the operator's USB descriptor
  capture (interfaces/endpoints/UVC extension units/HID reports) is the gate for the
  live source and #172's real backend.

## Board moves

`DONE`: #105, #114, #124, #127, #131, #143, #149, #176. `HW`: #141, #171, #172, #175
(instruments/code landed; live cards owed). `SPEC-READY`: #150. `HW` stays: #71, #93,
#151, #167, #170. `RECON`: #169, #173.

## Operator cards

1. **#169** USB descriptor capture (`system_profiler`/protocol analyzer) — fills #171's
   descriptor table and unblocks #172's live source.
2. **#170** live claim/release with the vendor daemon stopped.
3. **#171** live IR+depth capture with no vendor process.
4. **#172** the vendor A/B on a known scene (`raptor_client` unchanged).
5. #167's enumeration device test; #93's HUD on 8787; #151's browser render.
6. T4/T5 captures (#145/#150/#126); the pixel scale (#126); Windows (#115).
