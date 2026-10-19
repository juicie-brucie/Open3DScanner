# Spec targets — the device's advertised rates and the numbers we hold ourselves to

The vendor figures below are **claims from the Creality Raptor Pro product material**, not
observations. Observations keep their usual homes and tags (`[inferred]` in
`docs/recon/findings/`, `[hw-confirmed: <run>]` in `tests/results/`); this page exists so the
targets we optimize toward have one place to point at.

## Advertised (Creality Raptor Pro)

| Mode | Rate | Ceiling |
| --- | --- | --- |
| Blue laser, 22 cross lines | up to 660,000 points/s | up to 60 FPS |
| Blue laser, 7 parallel lines | up to 420,000 points/s | up to 60 FPS |
| NIR structured light | up to 3,580,000 measurements/s | up to 30 FPS |

- Accuracy up to 0.02 mm (blue) / 0.075 mm (NIR); volumetric 0.02 + 0.08 mm/m (blue),
  0.075 + 0.1 mm/m (NIR).
- Working distance 160–400 mm (blue) / 170–1000 mm (NIR); single capture 270×170 mm @300 mm,
  341×232 mm @400 mm, 397×290 mm @500 mm (22-line blue).
- Host requirements — **macOS**: Apple M1/M2/M3, 16 GB+. **Windows**: i7-10th gen+ / NVIDIA
  8 GB VRAM+ / 32 GB recommended (minimum i7-7th gen / 6 GB VRAM / 16 GB).

The vendor's "up to 60 FPS" is a **capture-rate ceiling for the blue-laser modes**. It is not
the rate of the fused point-cloud publish that our UI consumes, and no measured number in this
repo is a 60 fps observation yet.

## What we have measured

- **The captured scan session ran the IR/depth streams at 50 fps *by request*.** The vendor
  client calls `ob_rpc_start_stream` with `[streamtype, 9, 1920, 1200, 50]` on both interfaces
  (streamtype 6 = IR on port 10013, 7 = depth on 10010), and the flash profile tuple read by
  op-31 is `{7,9,1920,1200,50}` — `protocol-spec.md` §3 and §7.
- The vendor client polled `ob_rpc_get_frame` at **~51.2 Hz** (0.01952 s mean interval) —
  consistent with the requested 50 fps (`streaming-replay-payloads.md`).
- The observed **UI data rate is 2–8 fps** on the M1 16 GB host against our own 10 Hz tick
  ceiling (`#111`, `#112`).

## What this means

1. **51.2 Hz is the configured 50 fps profile, not an M1 ceiling.** The rate matches the
   request and is steady; a host that could not sustain the stream would show a lower, jittery
   rate. The capture therefore says nothing yet about whether this host can sustain 60 fps —
   the session never asked for it.
2. **The advertised 60 fps is a mode/profile we have not exercised.** The only profile list
   observed so far (23 entries, `[0,0,1280,800,30]` … `[0,0,640,400,10]`) was the RGB
   interface; the IR/depth lists still need enumerating (`#114`).
3. **The 2–8 fps gap is host-side**, two orders below both the advertised rate and the
   configured 50 fps feed. That is what `#111`/`#112` measure.

## Test hosts

| Host | Spec | Status |
| --- | --- | --- |
| iMac M1, 16 GB | at the vendor's stated macOS minimum (M1/M2/M3, 16 GB+); the Windows *recommended* spec is higher (32 GB) | current test host |
| MacBook Air M2, 16 GB | supported | available for `#115` |
| Windows 5900X / RTX 2080 Ti / 32 GB | above the Windows recommended spec | available for `#115` |

## Targets

- **#113** — live tick with comfortable margin above the device's actual mode rate.
- **#114** — enumerate stream profiles per interface: does a 60 fps profile exist, and which
  profile does each scanning mode use?
- **#115** — cross-machine check: raw delivery rate and publish/tick rate on all three hosts.
