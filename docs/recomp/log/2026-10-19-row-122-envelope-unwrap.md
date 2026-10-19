- 2026-10-19 — build-3 (build room, `wt-build3`, branch `build/plan122-envelope-unwrap`): **#122's
  envelope is unwrapped in both frame paths.** Device-free: the replay was never run against the
  daemon, and `--live` was only run against local loopback mocks.

  **The helper.** `Result(reply)` peels the msgpack-RPC envelope `[1, msgid, nil, result]`
  (protocol-spec.md §1.1): the payload is at index 3, and a non-nil index 2 is an error (the
  rpclib convention) — surfaced as `Kind::Error` with its text. A reply that is not an envelope is
  returned unchanged, so a peer answering with a bare payload still works, and a timeout is not an
  array, so it survives the unwrap. `call()` now returns the envelope and the callers unwrap it, so
  there is one place that knows the reply's shape.

  **Correction to the framing.** The board says "`Live()` and `Replay()` only test `reply.kind == Bin`
  at the top level". That is true of `Replay()` and **not** of `Live()`: `Live()` went through
  `call()`, which already returned `resp.array[3]`. What `Live()` missed was the **error slot** —
  `call()` never checked index 2, so a daemon error reply came back as `nil` and was counted as an
  *empty* payload. That is the more consequential half of #105's "zero frames": the daemon's error
  was invisible. `Replay()` did read the envelope (`kind=7`, i.e. `Kind::Array`, for all 36).

  **Verified without the device** (record:
  `tests/results/2026-10-19-raptor-client-envelope-unwrap.md`, raw log beside it). A mock answering
  `[1, msgid, nil, bin(3)]` on both interfaces:
  the pre-#122 build reports `done: bin=0 empty=36 error=0` (every frame `kind=7`); this build
  reports `done: bin=36 empty=0 error=0` with every frame `bin(3)`. The live path against the same
  mock shows `GET_FRAME #1 -> bin(3)`. Against a mock answering the **error** reply
  `[1, msgid, "boom", nil]`, the old build prints `start_stream -> non-error` and then counts every
  frame as `empty (kind=0)`; this build prints `start_stream failed: boom` and stops.

  **Selftest** (no device, in the existing `--dry-run` mode, already registered with ctest):
  `ReplyResultSelftest` pins the envelope shapes — a success envelope yields the inner bin, a
  non-nil error slot is an error with its text, a bare payload is returned unchanged, a non-response
  array is left alone, and a timeout survives the unwrap. It would fail if the unwrap were wrong.

  **For the hardware re-run (the acceptance):** the live `GET_FRAME` lines must show `bin(N)`, and
  the replay must complete 36/36. Two things the run can now distinguish that it could not before:
  a frame that comes back with **no payload** (the daemon's `nil` result — a real answer, and the
  live path's `empty` bucket) versus a frame that comes back as an **error** (now reported as an
  error, with the daemon's text). #105's "zero frames" is, for the first time, observable as one of
  those two.
