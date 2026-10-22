# 2026-10-21 — the T1/T2/#167 batch: the vendor-path baseline

## The records (PR #482)

- **`plan-t1a-replay`** — the recorded bundle replayed into a running **RPCServer** FAILED
  (no reply within 2000 ms; 0/36 frames). The earlier blocked attempt's raw was overwritten by
  the retry; it is not landed.
- **`plan-t1b-live-ir`** — the **vendor RPC** IR stream (the daemon as peer; ports open;
  `start_stream` true): 30 data / 3 empty, 66.8 MB, p50 117 ms, every reply `Bin(2304000)`.
  **Not the UVC live source.** The record's "live" title/board claims are not applied.
- **`plan-t2-ui-ab` + `-rerun`** — the **vendor-daemon** UI A/B with the plane toggle
  (`clients=2.0`, ~9.7–9.8/s). **Not the vendor↔shim A/B** — `raptor_shim`/`daemon_switch`
  appear zero times. The rerun lacks its binary sha/HEAD; run 1's operator reads are empty;
  the blocked record's raw was overwritten.
- **`plan167-race-rerun`** — 5 clean starts (`count=1` first probe 5/5, `scan_start` 5/5,
  exit 137 SIGKILL 5/5, no crash). The race is intermittent; no coverage of `count=0`.
  #167 is already `DONE`; the record's proposal is stale.

The blocked pair and the 2026-10-19 smoke batch are **not** in the PR (lost raws; the batch is
already on `main` byte-identical).

## The findings

- **Our live source remains untested**: both the UVC capture and the vendor↔shim A/B are open;
  the operator's runs were the vendor path.
- **The frame geometry is a contradiction**: `Bin(2304000)` is `1920×1200×1` (#174) or
  `960×600×4`/`480×300×16` (#175); the UVC IR is 960×600 Y8 (576,000) — a factor-4 mismatch.
  Filed as **#194**.
- **The #167 clean sample** (5/5) does not clear the race; the old 2026-10-19 smoke batch
  carries the same `list:(1)`/`count=0` signature a day earlier (#173).
- **The T2 vendor baseline** does not close #151/#93/#153; the operator's rerun reads
  (coverage/overlay/picture) are attributed.

## The tester's repairs

The blocked pair's lost raws; the T2 rerun's binary/HEAD; the T1b's stop cause (the 64 MiB byte
bound, not the 30 s claim) and its one-frame shortfall.
