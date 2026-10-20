# 2026-10-19 — row #147: runtime analysis of our own code (device-free baseline)

**Row:** #147 — *Runtime analysis of our own code — where the time and allocations actually go.*
**Status after this entry:** `TODO` → **`DONE`** (baseline recorded **and** the hot-path analysis is in §6–§9).
**Wall:** `docs/recomp/log` + code reads. **No code change in this row** — the only files touched are this log and
`docs/recomp/PLAN.md` (the row's own status). No device, no vendor SDK: every binary measured here is linked against
libc++/libSystem/CoreFoundation only (`otool -L` output in §2.2).

---

## 1. What this entry is, and what it is not

It is a **device-free baseline of our own code**: CPU samples, a syscall view, allocation behaviour, and the
tick's CPU-vs-wall split, for three surfaces — the `scan_server` tick loop, the `raptor_client` replay path, and
the WebSocket broadcast — measured with the platform's own tooling (`sample`, `xctrace` Time Profiler, `vmmap`,
`ps`), plus a zero-work control that measures the platform's own wait floor.

It is **not** a hardware measurement. The live tick's vendor calls (the publish getter, the exposure re-assert, the
cloud release, the pose) are not in these numbers and cannot be: they need a device. Where a live-path number is
extrapolated from a synthetic one, it is marked **extrapolated** and never mixed into a measured figure.

**Method note (important for anyone repeating this):** the baseline was taken on `origin/main` = `f20fc46`, which
already carries row #139's pacing-wait segment and row #141's exposure/release segments, so the *product's own
instrument* now reports the wait, the overshoot and the residual. An earlier pass of this measurement was taken on
`77c7edd` (before #139/#141 landed); it was discarded and re-taken on `f20fc46`, because the tick's instrument —
the thing being measured — had changed underneath it.

---

## 2. Method (reproducible)

### 2.1 Machine state

| | |
| --- | --- |
| OS | macOS 27.0.1 (26A434), arm64 |
| CPU / RAM | Apple M1, 8 cores / 17 GB |
| Load average during the runs | **2.0 – 7.2** (other rooms were building in parallel the whole time) |
| Tooling | `/usr/bin/sample` (v7), `xcrun xctrace` (Instruments 27.1), `/usr/bin/vmmap`, `/usr/bin/heap`, `ps` |
| Not available | `dtrace` (SIP on, no root: *"DTrace requires additional privileges"*), `fs_usage` (needs root). The syscall view below therefore comes from `sample`'s kernel frames and from `xctrace`'s `__sendto`/`__recvfrom`/`poll` leaves, not from a syscall tracer. |

**Noise caveat:** the per-window medians are stable to ±5 % across the 5 s windows of a run; the **max** outliers
(e.g. `post max=35.81 ms` in one window) are the machine's other tenants, not the tick. Only medians are used below.

### 2.2 Build

No pre-existing `build/` existed in this worktree; a **fresh** build dir was used and **no Debug build was mixed in**:

```bash
git submodule update --init --depth 1                    # third_party/{libigl,manifold}, needed by mesh/
cmake -S . -B build/row147 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -g"       # -g added for symbolication; optimisation unchanged
cmake --build build/row147 -j8 --target scan_server raptor_client
```

* Build dir: **`build/row147`** (git-ignored). Flags: `-O3 -DNDEBUG -g -std=gnu++17 -arch arm64` (verified in
  `build/row147/tools/scan_server/CMakeFiles/scan_server.dir/flags.make`).
* `-g` only adds debug info; the code is the same optimised code that ships. It is what makes `xctrace` attribute
  samples to `frame_protocol.h:68` instead of a bare address.
* `otool -L build/row147/tools/scan_server/scan_server` → `CoreFoundation`, `libc++.1.dylib`, `libSystem.B.dylib`.
  **`otool -L .../raptor_client` → `libc++.1.dylib`, `libSystem.B.dylib`.** No vendor SDK is linked, so nothing in
  this baseline can have come from the device path.

### 2.3 The harness (four small scripts, all outside the repo)

The repository was not modified for this row, so the harness lives outside it and is reproduced in
§11 (Appendix). Ports and paths are the only things that need adjusting to re-run it.

| script | what it is | why |
| --- | --- | --- |
| `ws_client.py` | a dependency-free RFC 6455 client: handshake, then drain and count frames | the "UI connected" condition (the browser page's socket without the browser's render cost) |
| `mock_rpcd.py` | a loopback msgpack-RPC daemon on the two interfaces the capture uses | the device-free peer for `raptor_client --replay`. It answers `ob_rpc_get_frame` with the **11-element array of `docs/recon/findings/frame-codec.md` §10.3** (e4 = a 12,250 B band, the size the capture measured), so one reply is 12,354 B |
| `make_replay.py` | rebuilds the replay input from `docs/recon/findings/streaming-replay-payloads.hex` | two inputs: **paced** (the capture's own inter-arrival deltas preserved, the capture repeated) and **unpaced** (the same frames, every stamp zeroed). The frame *bytes* are never changed — only the `+<ms>` prefix |
| `wait_control.cpp` | a 40-line program that sleeps and nothing else | the **zero-work reference**: what the platform's own wait costs, with no code of ours in the loop |

### 2.4 The commands

```bash
# --- the platform's wait floor (zero work in the loop) ---
clang++ -O2 -std=gnu++17 -o wait_control wait_control.cpp -framework CoreFoundation
./wait_control 0 300      # std::this_thread::sleep_for(33 ms)  — the synthetic tick's pacing
./wait_control 1 100      # CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false) — the LIVE tick's pacing

# --- surface A/B: the scan_server tick (0, 1 and 8 clients) ---
./build/row147/tools/scan_server/scan_server 18787 &          # synthetic; no vendor SDK
python3 ws_client.py 18787 40 &                              # x1 or x8 for the client conditions
sample <pid> 10 -file /tmp/row147/scan-<cond>-sample.sample.txt
xcrun xctrace record --template 'Time Profiler' --attach <pid> --time-limit 20s \
                     --output /tmp/row147/scan-<cond>-tp.tp.trace
xcrun xctrace export --input /tmp/row147/scan-<cond>-tp.tp.trace \
       --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' > tp-<cond>.xml
xcrun xctrace record --template 'Allocations' --attach <pid> --time-limit 20s \
                     --output /tmp/row147/scan-<cond>-alloc.alloc.trace
vmmap -summary <pid>          # the live allocation footprint, sampled every 5 s
ps -o time= -p <pid>          # the process CPU time, before and after the window

# --- surface C: the raptor_client replay ---
python3 mock_rpcd.py 49021 49022 180 &
/usr/bin/time -l ./build/row147/tools/raptor_client/raptor_client \
      --replay /tmp/row147-unpaced.hex 127.0.0.1 --ir-port 49021 --depth-port 49022 --quiet
/usr/bin/time -l ./build/row147/tools/raptor_client/raptor_client \
      --replay /tmp/row147-paced.hex   127.0.0.1 --ir-port 49021 --depth-port 49022 --quiet
sample <pid> 10 -file /tmp/row147/raptor-unpaced-sample.sample.txt
xcrun xctrace record --template 'Time Profiler' --attach <pid> --time-limit 20s \
                     --output /tmp/row147/raptor-unpaced-tp.tp.trace
```

Every run was **fixed-duration**: 30 s for the `cpu` condition (server alive, no profiler attached — this is the
unperturbed baseline), 10 s of `sample` inside a 30 s process life, 20 s of `xctrace` inside a 30 s process life,
and the replay files sized for ~30 s and ~60 s. The client/UI state was held constant within each condition
(0, 1 or 8 WS clients, connected before the window and verified with
`lsof -a -p <pid> -iTCP:<port> -sTCP:ESTABLISHED`).

**Tooling limitation, stated plainly:** the **Allocations** template *records* (the trace is written), but on this
macOS its *Statistics* and *Allocations List* tables are **not reachable through `xctrace export`** — they are
track details, not data tables, and the TOC exposes no allocation schema (four candidate schema names and the
whole `data/table` set were tried; all empty). The allocation numbers below therefore come from `vmmap -summary`
(live allocation count and bytes, sampled over time) and from the Time Profiler's allocator leaves, with the
per-tick allocation *sizes* read from the source. `MallocStackLogging=1` produced no output on this build.

---

## 3. Baseline A — the `scan_server` tick loop (synthetic, device-free)

The synthetic path (`scan_server 18787`, `main.cpp:1796` → `RunSyntheticServer`) is the device-free stand-in for the
live tick: same shape (build a frame → broadcast → pace), same instrument, no vendor SDK. It streams a 48×48 = **2,304
point** sphere (**27,697 B** per frame) at a nominal 30 Hz.

**The product's own instrument** (`[perf] window` lines, row #139/#141's build) — medians over the 5 s windows of a
30 s run:

| condition | tick p50 | **wait p50** | **overshoot p50** | **residual p50** | post p50 | bcast p50 | clients |
| --- | --- | --- | --- | --- | --- | --- | --- |
| no client | 36.9 – 37.5 ms | 36.8 – 37.2 ms | **3.5 – 3.9 ms** | **0.01 ms** | 0.12 – 0.20 ms | 0.00 ms | 0 |
| 1 client | 36.7 – 37.5 ms | 36.5 – 37.2 ms | 3.1 – 3.8 ms | 0.01 ms | 0.20 ms | 0.10 ms | 1 |
| 8 clients | 37.1 – 37.7 ms | 36.6 – 37.1 ms | 3.2 – 3.8 ms | 0.01 ms | 0.19 – 0.20 ms | 0.29 – 0.31 ms | 8 |

* **Realised rate: 27.0 – 27.4 ticks/s, not the nominal 30.** The reason is in §5.
* **CPU** (30 s, no profiler attached; `ps -o time=`): 160 ms (0 clients) / 250 ms (1) / 420 ms (8) →
  **5.3 / 8.3 / 14.0 ms of CPU per second** → **~195 / ~306 / ~514 µs of CPU per tick**.
* **CPU vs wall: 195 µs of CPU in a 37.0 ms tick = 0.53 %.** The tick is 99.5 % *waiting*.

### 3.1 The tick's period, accounted for

The tick's period is the sum of the instrument's own segments (medians of the same run; they agree to ~0.1 ms,
because medians of different distributions do not add exactly):

```
  tick period (p50)                      36.9 - 37.5 ms
  = the wait segment (p50)               36.8 - 37.2 ms
  + the post segment (p50)                0.12 - 0.20 ms
  + the bcast segment (p50)                0.00 - 0.31 ms
  + the residual (p50)                     0.01 ms
```

And the wait itself:

```
  wait (p50)                             36.8 - 37.2 ms
  = the sleep requested                   33.00 ms   (milliseconds(int64_t(kFrameIntervalMs)) — the 33.33 ms
                                                        nominal is truncated to whole milliseconds)
  + the platform's own overshoot           3.9 - 4.1 ms  (the zero-work control: 36.85 - 37.07 ms realised
                                                         for a 33.00 ms request)
```

The product's own `overshoot` segment is measured against the **33.33 ms nominal**, not against the 33.00 ms it
actually requests, so it reports 3.5 - 3.9 ms: the same platform overshoot, 0.33 ms lower. The two instruments
agree once that is accounted for.

**So the period is a wait, and the wait is the platform's** — but the decomposition above is a *medians* identity
(±0.1 ms), not an exact one; the unaccounted fraction is smaller than the spread of the medians themselves.

### 3.2 Allocation behaviour (device-free)

`vmmap -summary`, the live malloc-zone count/bytes, sampled every 5 s:

| condition | live allocations | live bytes | RSS |
| --- | --- | --- | --- |
| no client, 30 s | 374 → 382 (+8) | 86 K (flat) | 264.4 → 265.5 MB |
| 8 clients, 23 s | 400 → 407 (+7) | 87 K (flat) | 265.6 → 265.7 MB |

**No growth per second**: the tick allocates and frees every cycle, and the retained set is flat. The +7/+8 over
30 s is the accept/handshake path plus the status string, not a per-tick accumulation.

The *churn* (derived from the source, cross-checked by the Time Profiler's allocator leaves, which are ~2 % of the
tick's CPU): per tick the synthetic path allocates **2 blocks** — the `std::vector<Vec3> frame_points = base_points;`
copy (27,648 B, `main.cpp:603`) and the encode buffer (27,684 B, `frame_protocol.h:42`) — plus, per client per
frame, the 2–4 B WebSocket header vector (`ws_broadcast_server.cpp:241`). At 27.2 ticks/s that is **~55 allocations/s
and ~1.5 MB/s of churn** with no client, ~270 allocations/s with eight.

---

## 4. Baseline B — the `raptor_client` replay path (device-free)

`raptor_client --replay <file> 127.0.0.1 --ir-port … --depth-port … --quiet` against the loopback mock. The frame
bytes are the capture's own (`docs/recon/findings/streaming-replay-payloads.hex`); only the `+<ms>` pacing prefix
is rewritten by `make_replay.py`.

| | unpaced (72,000 frames) | paced (4,321 frames, the capture's deltas) |
| --- | --- | --- |
| wall | 27.27 s (2,640 frames/s) | **58.61 s** for a capture span of 45.12 s |
| CPU | 1.29 user + 0.87 sys = **2.16 s → 30 µs/frame (7.9 %)** | 0.22 + 0.34 = **0.56 s → 130 µs/frame (0.96 %)** |
| peak RSS | 12.9 MB | 3.1 MB |
| live footprint over 46 s | — | **flat**: 5,109 allocations, 782 KB, RSS 110.3 → 110.6 MB |

**Two findings in the paced number.** (1) The replay is *sleep-bound*: 130 µs of CPU per frame, of which most is the
`sleep_for` call itself, not the work. (2) **The pacing drifts:** the capture's 45.12 s of inter-arrival waits become
**58.6 s** of wall — an extra **~3.1 ms per wait**, which is the same platform overshoot §5 measures, now
accumulated over 4,321 waits. A long replay therefore does not reproduce the capture's timing: it reproduces its
*bytes* and drifts in its *clock*.

### 4.1 The replay client's hot path (Time Profiler, 20 s, 1,561 ms of sampled CPU)

| leaf | self time | share |
| --- | --- | --- |
| `Reader::bin(unsigned int)` (`main.cpp:231`) | 434.0 ms | **27.8 %** |
| `Reader::next()` | 348.0 ms | **22.3 %** |
| `__recvfrom` | 273.0 ms | 17.5 % |
| `__sendto` | 189.0 ms | 12.1 % |
| `poll` | 181.0 ms | 11.6 % |
| memmove / memset / malloc / free / the `Value` tree | ~55 ms | ~3.5 % |

**Half the client's CPU (782 ms of 1,561 ms) is the msgpack binary copy**, byte at a time:

```cpp
Value bin(uint32_t n) { Value v; v.kind = Value::Kind::Bin; v.bin.resize(n);
                         for (uint32_t i = 0; i < n; ++i) v.bin[i] = next(); return v; }   // main.cpp:231
```

`next()` is a non-inlined bounds-checked accessor, so a 12,250 B band costs 12,250 calls. The socket syscalls
(recv + send + poll) are the other 41 %.

---

## 5. The zero-work control: the platform's own wait floor

`wait_control` does nothing but wait. Three runs each, on the same machine, at the same time as the profiles:

| wait | requested | realised p50 (3 runs) | overshoot | CPU |
| --- | --- | --- | --- | --- |
| `std::this_thread::sleep_for(33 ms)` (the synthetic tick) | 33 ms | **36.85 / 37.07 / 36.90 ms** | **+3.9 ms (+11.7 %)** | 0.068 – 0.074 % of wall |
| `CFRunLoopRunInMode(default, 0.1, false)` (**the live tick**, `main.cpp:1106`) | 100 ms | **103.19 / 103.75 / 103.75 ms** | **+3.2 – 3.8 ms (+3.2 – 3.8 %)** | 0.164 – 0.168 % of wall |

The overshoot is a property of the platform's timer, not of one particular sleep length, and it is not constant: two
further requests, measured the same way (one run each, zero work), give

| request | realised p50 | overshoot | the rate that actually comes out |
| --- | --- | --- | --- |
| 41 ms (what a 24 Hz pace would ask for) | 44.62 ms | +3.6 ms | **22.4 Hz** |
| 20 ms (what a 50 Hz pace would ask for) | 24.29 ms | +4.3 ms | **41.2 Hz** |

This is the single most important number in the entry, because it sets two floors:

* the **synthetic** tick cannot exceed **27.1 Hz** on this machine — the nominal 30 Hz is unreachable with *zero*
  work, because the sleep is truncated to 33 ms and the platform adds ~3.9 ms;
* the **live** tick cannot exceed **9.68 Hz** — the 100 ms run-loop wait costs 103.2 – 103.8 ms before any work.

And it is *the same overshoot* the product's own `overshoot` segment reports (3.5 – 3.9 ms), measured by two
independent instruments.

---

## 6. Baseline C — the WebSocket broadcast, under one and several clients

`engine/src/ws_broadcast_server.cpp` is the target; it is exercised from inside the `scan_server` tick.

| clients | `bcast` p50 (the product's segment) | `bcast_per_client` (the product's derived mean) | `__sendto` share of the tick's CPU |
| --- | --- | --- | --- |
| 0 | 0.00 ms | — | — (the send is skipped: `ConnectedClientCount() > 0`, `main.cpp:612`) |
| 1 | 0.10 ms | 98 – 103 µs | 14 of ~33 our-code samples (10 s) |
| 8 | 0.29 – 0.31 ms | **35.9 – 38.4 µs** | **153 ms of 265 ms = 57.7 %** (20 s Time Profiler) |

* The cost is **linear in the client count**: ~37 µs per client per tick for a 27,697 B frame — dominated by the
  kernel copying the frame into each client's socket buffer.
* **Two syscalls per client per frame**: `SendFrame` sends the 2–4 B header, then the payload
  (`ws_broadcast_server.cpp:255` and `:257`).
* **The broadcast is synchronous and holds `clients_mutex` for the whole fan-out**
  (`ws_broadcast_server.cpp:503-517`), with a **100 ms `SO_SNDTIMEO` per client** (`:304-307`). One stalled client
  can therefore hold the tick for up to 100 ms — the tick has no queue and no drop policy.
* The single-client number (98 µs) is higher than the per-client marginal cost at eight (37 µs) because the first
  frame on a fresh connection also pays the kernel's socket-buffer setup.

---

## 7. The top three hot paths (self time, device-free)

1. **`encode_cloud_snapshot`'s per-float encoder** — `engine/include/openscanner/frame_protocol.h:55-58`
   (`put_f32` → four `push_back`s per float, one per byte).
   **63 % of the tick's CPU with no client** (67 ms of 106 ms in a 20 s Time Profiler run; 18 of ~26 our-code
   `sample` hits at `main.cpp:607`). The frame is 27,684 B written **one byte per `push_back`**.
2. **The WebSocket send** — `__sendto` under `WsBroadcastServer::BroadcastBinary` → `SendFrame` → `SendAll`.
   0 % with no client, **57.7 % of the tick's CPU with 8 clients** (153 ms of 265 ms).
3. **The `raptor_client`'s msgpack binary copy** — `Reader::bin` + `Reader::next` (`tools/raptor_client/main.cpp:231`).
   **50 % of the replay client's CPU** (782 ms of 1,561 ms).

(4th, in the tick: the per-tick `std::vector<Vec3> frame_points = base_points;` copy plus `RotateY`, ~4 % of the
tick's CPU — **synthetic-path only**; the live path decimates the vendor's buffer in place and does not copy it.)

---

## 8. What would make this faster — and what is unavoidable

### Our code (in order of payoff, all measured above)

1. **The encoder (`frame_protocol.h:39-73`).** The payload is `count × 3` floats and the output is the same bytes
   little-endian; one `resize` + one `memcpy` replaces 27,648 bounds-checked `push_back`s per frame. This is the
   single biggest win in the tick (63 % of its CPU, no client). *Unchanged behaviour, different bytes-in-the-box.*
2. **The WebSocket send (2 syscalls → 1).** `SendFrame` sends a 2–4 B header and then the payload separately; one
   buffer with the header in front, written once, halves the syscalls per client per frame — 57.7 % of the tick's
   CPU at eight clients is in `send`.
3. **The broadcast's shape.** It is a synchronous fan-out under a mutex in the tick thread, with a 100 ms send
   timeout per client. A latest-frame slot plus a per-client writer thread would take the network out of the tick's
   critical path entirely (this is #113/#148's structural question; the profile says the *cost* is per-client and
   the *risk* is the stall, and both live in the same 12 lines).
4. **`Reader::bin` (`raptor_client/main.cpp:231`).** A `memcpy` out of the read buffer instead of a byte-at-a-time
   `next()` loop halves the replay client's CPU. Two smaller items in the same file: `Reader::arr` `push_back`s
   without `reserve` (the 11-element array reallocates ~5 times per reply) and every `Value` carries a `std::string`,
   two vectors and a `std::map` (so each parsed value constructs and destroys a map).
5. **The replay's re-parse.** `ReadMessageWithDeadline` re-parses the whole buffer from the start after every
   `recv`; a 12.3 kB reply arriving in two chunks copies the band twice. *Not measured here* (the mock's single
   `sendall` usually arrives whole) — the fix is known, the number is not.

### Unavoidable (or a decision, not an optimisation)

1. **The pacing wait, and the platform's overshoot on top of it.** 36.9 ms of a 37.0 ms tick is the wait; ~3.5-3.9 ms
   of that is the platform's own timer slack, measured with zero work in the loop. No amount of optimisation moves
   it. Beating it means changing the *cadence* (a decision — #113) or using a deadline-based timer instead of
   `sleep_for`/`CFRunLoopRunInMode`.
2. **The kernel copy per client.** 27,697 B into every client's socket buffer, per frame, is inherent to
   broadcasting to N clients. It shrinks only with a smaller payload or a different transport.
3. **The device's own rate.** The vendor's profile *advertises* 50 fps nominal
   (`docs/recon/findings/streaming-replay-payloads.md`), but the source rate actually seen in the #112 hardware log is
   **≈24.0 Hz** (8,119 device frames over 338.6 s), and our publish is **8.7 Hz** because of our own pacing (row
   #151's note on main). A ~9.7 Hz tick cannot track a 24 Hz source no matter how fast the tick is; that gap is
   #113's question and is not a performance bug in this code.
4. **The msgpack parse** of a 12.3 kB reply is inherent to the protocol — only its *byte-at-a-time* implementation
   is avoidable (§8, item 4).

---

## 9. What this says about #134, #136, #139/#141 (and #113) — the contradiction check

**#139 ("the tick's dominant cost is the pacing wait, and it is uninstrumented") — CONFIRMED and quantified here;
the row itself is now `DONE` on main** (the pacing wait is timed and separated — PR #352 — and the instrument was
completed in #357/#141). What this row adds is the **device-free half** of that answer: the wait is 36.8 – 37.2 ms
of a 36.9 – 37.7 ms tick (99.4 %), and the platform's zero-work floor for that wait is 36.85 – 37.07 ms (§5) — i.e.
**the wait's overshoot is the platform's, not ours**. On the live path the wait is `CFRunLoopRunInMode(0.1)`, whose
floor is **103.2 – 103.8 ms** → the live tick's ceiling on this machine is **9.68 Hz** — and the hardware run (#112)
measured 8 – 9 Hz. **The live tick is at its platform ceiling; it is not "slow", it is paced.** (The live-path
figure is extrapolated from the same wait, measured device-free on the same machine the hardware run used. The live
half — the wait as the *live* tick experiences it, with the vendor calls inside the tick — is #141's job, whose status
on main is `HW`.)

**#134 ("~9.6 ms/tick outside the four timed segments") — REFINED, and its own attribution is consistent with this
measurement.** #134's audit attributed the residual to the **untimed work** — the two untimed vendor calls (the
exposure re-assert, the cloud release) plus `BuildStatusJson` / the `printf` / `RecordFrame` — and to the
**uninstrumented pacing wait**; it did *not* attribute it to allocations. This row measures the two halves of that
claim, device-free:

* with the wait now timed (row #139), the synthetic tick's *untimed residual* is **0.01 ms at p50** (0.02 – 0.05 at
  p95) — the tick has no hidden work left;
* of the hardware run's 9.6 ms residual, **~3.2 – 3.9 ms is the platform's own wait overshoot** (measured here with
  zero work in the loop) — it was never our code, and it was never visible to #134 because the wait was outside
  every segment;
* the remainder is the live path's untimed work, which #141's instrument now times explicitly (`exposure`, `release`
  segments). **The next hardware run settles that split**; the device-free half is measured here.

**The hypothesis this row was asked to test — that the untimed residual might be dominated by an *allocation path* —
is not supported.** The live allocation footprint is flat (86 – 87 KB, +7-8 allocations over 30 s), the tick's
allocations sit *inside* the timed `post` segment (the two per-tick vectors), and the allocator's share of the
tick's CPU is ~2 %. There is no allocation path in the residual to find. **The residual is a wait.**

**#136 (the pipeline map; `DONE` on main) — complemented, not contradicted.** The map is the recon deliverable; what
this profile adds is the *cost* of two of the stages it names: the *encode* is 63 % of the tick's CPU (no client) and
the *broadcast* is 57.7 % at eight clients, linear in the client count, synchronous in the tick thread, with a 100 ms
per-client send timeout. Both sit on the path between "we have points" and "the UI has points" — the stages #136's
map lists as the ones carrying data. Nothing here changes the map's findings; it prices two of its arrows.

**#113 (the tick's margin above the device's rate; `TODO`) — a ceiling to plan against, nothing more.** The synthetic
path's floor is 36.85 ms (**27.1 Hz**, so the nominal 30 Hz is already unreachable) and the live path's floor is
103.2 – 103.8 ms (**9.68 Hz**). The hardware run's 8 – 9 Hz is at that ceiling. Any plan that needs a faster tick
must change the cadence (or the pacing mechanism), not the code inside the tick — #113 is the row that decides
whether to do that.

---

## 10. Caveats

* **The synthetic path is not the live path.** The vendor calls (publish getter, exposure re-assert, cloud release,
  pose) and the decimation loop are absent; their cost is not in these numbers. The live tick's own numbers remain
  #112's measurement, and the next hardware run with row #141's instrument will re-measure them.
* **Extrapolation, flagged:** the live path decimates to 20,000 points (`kLiveBroadcastPointBudget`) against the
  synthetic 2,304, so the encoder's cost scales ~8.7×: the measured `post` p50 of 0.20 ms would become ~1.7 ms,
  which sits in the same range as #112's measured live `post` p50 of 1.18 ms. Consistent, but an extrapolation.
* **The mock is Python**, so the reply round trip is slower than a real daemon's and the replay client is
  poll-bound (the `poll` leaf, 11.6 %). The *client's own* CPU (the parse) is unaffected by that.
* **The machine was shared** (load 2.0 – 7.2, other rooms building). Medians are stable; the maxima are not.
* **The replay's tally counts the daemon's 11-element array as "empty"** (its classifier only knows
  Bin/Error/Timeout), so a replay against a real daemon would report `bin=0` and exit 1. An observation about the
  diagnostic, recorded here because the profile's numbers depend on how the reply is classified — **not changed in
  this row**.
* One captured `send_data` frame in the findings file carries **four bytes after its msgpack array**, so a strict
  reader of that line stalls; the mock resynchronises on the next frame header. A property of the *capture*, noted
  for whoever writes a strict parser.

---

## 11. Appendix — the harness (outside the repo; reproduce from here)

`ws_client.py` (the "UI connected" condition):

```python
import base64, os, socket, struct, sys, time
port = int(sys.argv[1]); seconds = float(sys.argv[2])
s = socket.create_connection(("127.0.0.1", port), timeout=5.0)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(("GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (port, key)).encode())
buf = b""
while b"\r\n\r\n" not in buf:
    buf += s.recv(4096)
head, _, rest = buf.partition(b"\r\n\r\n")
assert b"101" in head.split(b"\r\n")[0], head
s.settimeout(1.0); frames = nbytes = 0; pending = rest; deadline = time.time() + seconds
while time.time() < deadline:
    try: chunk = s.recv(262144)
    except socket.timeout: continue
    if not chunk: break
    pending += chunk
    while True:
        if len(pending) < 2: break
        b0, b1 = pending[0], pending[1]; opcode = b0 & 0x0F; ln = b1 & 0x7F; off = 2
        if ln == 126: ln = struct.unpack(">H", pending[2:4])[0]; off = 4
        elif ln == 127: ln = struct.unpack(">Q", pending[2:10])[0]; off = 10
        if len(pending) < off + ln: break
        payload = pending[off:off+ln]; pending = pending[off+ln:]
        if opcode == 0x2: frames += 1; nbytes += len(payload)
print("binary_frames=%d bytes=%d" % (frames, nbytes))
```

`mock_rpcd.py` (the loopback daemon) — the parts that matter:

```python
# the 11-element array of docs/recon/findings/frame-codec.md 10.3
def frame_result(is_ir):
    out = bytearray(b"\x9b")                       # fixarray(11)
    out += b"\x00"                                 # e0
    out += b"\xcf" + struct.pack(">Q", 1735689600000000)   # e1 (us)
    out += b"\xce" + struct.pack(">I", 0)         # e2
    out += b"\xff"                                 # e3 (format 255)
    out += b"\xc5" + struct.pack(">H", 12250) + bytes(((i*37) & 0xFF) for i in range(12250))  # e4/e5
    out += b"\xc4" + bytes([64]) + bytes(64)      # e6
    out += bytes([64])                            # e7
    out += b"\xc4" + bytes([6]) + bytes(6)        # e8
    out += bytes([6])                             # e9
    out += bytes([2 if is_ir else 0])             # e10
    return bytes(out)
# one connection per port; read the request, reply [1, msgid, nil, result];
# resynchronise on the next 0x94 when a frame carries trailing bytes.
```

`make_replay.py` — the pacing rewrite: the capture's own inter-arrival deltas are computed with the client's own
rule (the first delta from t=0, later ones from the previous stamp, a backward step clamps to 0), and the extended
file is the cumulative sum of those deltas, so **the capture's bytes and its pacing are both preserved**; the
`unpaced` variant is the same frames with every stamp zeroed.

`wait_control.cpp` — the zero-work reference:

```cpp
for (int i = 0; i < n; ++i) {
    if (mode == 0) std::this_thread::sleep_for(std::chrono::milliseconds(33));
    else            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    /* record the period */
}
```

---

## 12. Files

* This log: `docs/recomp/log/2026-10-19-row-147-runtime-baseline.md`
* The row: `docs/recomp/PLAN.md` #147 → `DONE`
* **No source file was changed by this row.**

---

## 13. Corrections after review (2026-10-20)

This log was reviewed after its first merge (PR #361) and corrected here. The reviewer's three substantive points,
and what changed:

1. **The §9 attribution of the "allocation path" hypothesis to #134 was wrong.** #134's own audit attributes the
   untimed residual to the *untimed work* (the two untimed vendor calls, `BuildStatusJson`, the `printf`,
   `RecordFrame`) and to the *uninstrumented pacing wait* — it never says "allocation". The hypothesis that the
   residual might be allocation-dominated is the one this row was **asked** to test; §9 now says so, and it now
   states plainly that #134's own attribution is *consistent* with the measurement, which refines it by quantifying
   the platform's share (~3.2 – 3.9 ms of the 9.6 ms).
2. **The §3.1 period decomposition was presented as an exact identity ("nothing is missing") when it is a medians
   identity with a ±0.1 ms spread.** §3.1 now shows the identity as the instrument's own segments (tick = wait + post
   + bcast + residual), separates the *requested* sleep (33.00 ms) from the *nominal* (33.33 ms) the `overshoot`
   segment is measured against, and states the agreement as ±0.1 ms rather than as an exact closure.
3. **The cross-row claims were overstated** (they read as if this row closed #139 and #136). §9 now states the status
   of each row on main (#139 `DONE`, #136 `DONE`, #141 `HW`, #134 `SPEC-READY`, #113 `TODO`) and positions this row
   as the *device-free half* of #139/#141's answer and as a complement to #136's map — not a substitute.

The reviewer's fourth point (that the PR looked like 169 files) was an artefact of a **stale local `main` branch**
at `51e7960` in this worktree: the merged PR was 1 commit and 2 files (`gh pr view 361` → `commits: 1, files: 2`).
No source file was changed by this row at any point.


---

## 14. Handoff to #151 (the publish-rate load test)

Row **#151** ("What publish rate can the pipeline actually sustain?", `TODO` on main) asks for a **device-free load
test**: run the synthetic server at 10 / 24 / 50 Hz and record the per-tick work, the broadcast cost at 1 and 8
clients, and the browser's frame age and drop rate. This log is the baseline it starts from, and it also hands #151
one constraint that changes how the test must be written:

* **The synthetic server cannot actually reach 24 or 50 Hz on this platform.** The pace is
  `milliseconds(int64_t(1000.0/30.0))` — the nominal is truncated to **whole milliseconds** — and the platform adds
  **3.6 – 4.3 ms** of its own overshoot to every wait (measured with zero work, §5). Measured, at the requests
  those rates imply: a **41 ms** request (24 Hz) realises **44.62 ms** → **22.4 Hz**; a **20 ms** request (50 Hz)
  realises **24.29 ms** → **41.2 Hz**. Any "run at N Hz" number must subtract the overshoot from the budget, or it
  will measure the timer, not the pipeline.
* **At those rates the binding limit will not be our work.** The whole tick costs **195 µs** of CPU (no client) and
  **514 µs** at 8 clients (§3) — i.e. 0.5 % of a 37.0 ms tick, 1.1 % of the 44.6 ms a 24 Hz request realises, 2.1 % of
  the 24.3 ms a 50 Hz request realises. The per-tick work and the
  broadcast cost at 1 and 8 clients are already in §3 and §6; what #151 still has to measure is the **browser's**
  side (frame age, drop rate), which is the one surface this row did not touch.
* **The two knobs that would show up in #151's numbers** are the ones §8 names: the encoder (63 % of the tick's CPU
  with no client) and the per-client `send` (57.7 % at 8 clients) — the latter scales linearly with the client count
  and is synchronous inside the tick, so it is the first thing to show up when the frame rate rises.
* **The pacing itself is a decision, not a measurement**: the platform's floor for the live tick's 100 ms run-loop
  wait is 103.2 – 103.8 ms (**9.68 Hz**), and #151's own note reaches the same number independently. Raising the
  publish rate means changing the cadence (and the pacing mechanism), which is #113's question.
