- 2026-10-19 — build-3 (build room, `wt-build3`, branch `build/plan120-replay-routing`): **#120's
  routing is implemented**; the code is ready for the hardware re-run. Device-free throughout — the
  replay was never run against the daemon.

  **Suffix parsing.** Each frame's method name is read out of the raw bytes (`[0, msgid, method,
  params]` — the method is the third msgpack element) and its trailing device+interface token is
  taken as the suffix. The frame is **not** re-encoded: the bytes that go out are the captured
  bytes, suffix string included. The parser reads only the array header, the two leading scalars and
  the method's string header, so a frame whose params carry arbitrary binary is handled.

  **Routing.** The observed map is `0-2-12` (IR) → port 10013 and `0-2-10` (depth) → port 10010
  (protocol-spec.md §1.1). One connection per interface actually present in the file; each frame
  goes out on its interface's connection, in file order, with the per-frame ordering and #117's
  delays unchanged. Each connection is used in strict request/response order, so at most one reply
  is ever outstanding on it. The ports are overridable with `--ir-port` / `--depth-port`. An unknown
  suffix is **an error naming the frame** (`unknown interface suffix '0-2-23' (known: 0-2-12,
  0-2-10)`), and every frame is routed *before* the first byte is sent, so a bad file fails without
  touching the daemon. A single positional port argument is now rejected as ambiguous.

  **Diagnostics.** The dry-run cadence line gains the per-interface split:
  `interfaces: IR 13 (span 256 ms) / depth 23 (span 211 ms)` — the 13/23 split matches the capture
  file's composition documented in `streaming-replay-payloads.md` §6, which is an independent check
  that the suffix parse is right. Each frame's line now shows its suffix, interface and port. The
  timeout message names the frame **and** its interface:
  `[replay] frame 7 (0-2-10): no reply within 2000 ms`.

  **Verified without the device** (record: `tests/results/2026-10-19-raptor-client-interface-routing.md`,
  raw log beside it). Against a two-port mock daemon (one port per interface, each replying only on
  its own connection): the IR port received **13** frames and the depth port **23**, all 36 answered,
  and the mock's arrival log shows the two connections genuinely interleaved — the routing is
  right, not just the parse. Against a silent peer the run fails with the named frame and
  interface. The dry-run's 36 printed frames carry byte heads identical to the `.hex` file's, so the
  request bytes are unchanged.

  **Selftests** (no device, both already registered with ctest): `--dry-run` now also runs
  `ReplayInterfaceSelftest`, which pins the suffix parse against the capture's own two frames
  (`ob_rpc_get_frame0-2-12` / `...0-2-10`), that parsing leaves the frame bytes untouched, the
  suffix→port table, that an unknown suffix and a suffix-less method are rejected, and the port
  overrides. `--selftest-timeout` pins the timeout message's wording.

  **For the hardware re-run (the acceptance):** the replay must either complete **36/36** or name the
  unanswered frame **and its interface**. The command line now takes `--ir-port` / `--depth-port`
  instead of a single port argument; the hw room should record the per-interface reply counts and
  which interface any unanswered frame went to.
