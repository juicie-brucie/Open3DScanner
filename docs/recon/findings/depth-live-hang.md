# The depth-interface live hang — Row #161

*Static analysis only. Source: `tests/results/2026-10-20-plan127-live-reply-dump-retry.md`
(the hardware retry record) and `tools/raptor_client/main.cpp` (the client). No device
run was performed for this finding; the bounded-wait retry is a test-queue card, not
this task.*

**Every claim below is `[inferred]` — derived from the code and the test record. No
gated hardware run backs any of it.**

## 0. The problem in one paragraph

`raptor_client --live 127.0.0.1 10010 5 --dump-replies` (depth interface, port 10010)
**hung for 65 seconds with zero output** — not even the initial `[live] connected...`
line — and was terminated with SIGTERM (exit 143). The raw log file is 0 bytes. The
identical binary, daemon, and session on port 10013 (IR) returned 30 of 33 data frames
moments earlier. The stall is depth-interface-specific and occurs **before any output
is flushed**.

## 1. Stall candidates, with evidence

### 1.1 Client-side connect blocking (`rpc.connect()` at main.cpp:1116)

**Evidence AGAINST:**
- The test record says "the process started, connected (per the command succeeding at
  the OS level — the process existed and was running)". This suggests the TCP connect
  succeeded at the OS level.
- If `connect()` failed, lines 1117-1118 would print an error message to stderr and
  return 1. We see zero output, not an error message.

**Verdict:** Unlikely. If connect failed, we'd see the error message. The test record's
observation that the process "connected per the OS level" suggests the TCP handshake
completed.

### 1.2 Client-side read blocking in `start_stream` reply (main.cpp:1123 → 408-422)

**Evidence FOR:**
- `ReadMessage()` at lines 408-422 has **no timeout** — it's an unbounded loop calling
  `recv()` until the msgpack value parses complete. If the daemon accepts the connection
  but doesn't reply to `start_stream`, the client hangs forever in `recv()`.
- The `[live] connected` line at line 1121 has **no `fflush(stdout)` after it**. The next
  `fflush` is at line 1130 after `start_stream` succeeds. If stdout is fully-buffered
  (redirected to a file or pipe), the line stays in the buffer and never appears.
- The test record says "ps showed the process alive but at 0:00.00 CPU time (blocked,
  not spinning)". This is consistent with a blocked `recv()` in the kernel.
- The raw log file is 0 bytes, which means no frames were received. This is consistent
  with a hang in `start_stream` before any `get_frame` calls.

**Evidence AGAINST:** None strong.

**Verdict:** **Most likely.** This is the scenario that best fits all the evidence.

### 1.3 Buffering/flush issue (main.cpp:1121)

**Evidence FOR:**
- The `[live] connected` line at line 1121 has no `fflush(stdout)` after it.
- If stdout is fully-buffered (redirected to a file), the line stays in the buffer.
- The next `fflush` is at line 1130, which is after `start_stream` succeeds.

**Evidence AGAINST:**
- This is a **secondary effect**, not the root cause. Even if the line were flushed,
  the process would still hang in `ReadMessage()` if the daemon doesn't reply.
- If the hang were only a buffering issue, we'd expect to see output eventually (when
  the buffer fills or the process exits). But the process was killed after 65 seconds
  with zero output.

**Verdict:** Contributing factor, but not the root cause. The buffering makes the hang
invisible; the unbounded `recv()` is the hang itself.

### 1.4 Daemon-side depth path

**Evidence FOR:**
- IR worked moments earlier, so the daemon is running and accepting connections.
- The depth interface (port 10010) might be in a different state or deadlocked.
- The daemon accepts the TCP connection but doesn't reply to `start_stream` on the
  depth interface.

**Evidence AGAINST:** None. This is consistent with the symptoms.

**Verdict:** **Plausible root cause.** The daemon's depth interface handler might be
deadlocked, slow, or misconfigured.

## 2. The most likely scenario

The daemon accepts the TCP connection on port 10010 (depth), but when the client sends
`ob_rpc_start_stream0-2-10`, the daemon's depth interface handler **doesn't reply** (or
replies very slowly). The client's `ReadMessage()` blocks forever in `recv()`. The
`[live] connected` line is buffered in stdout (no `fflush()` after line 1121) and never
appears.

**Why this fits all the evidence:**
- Zero output: the connect line is buffered, the process hangs in `recv()` before any
  other output.
- 0:00 CPU: the process is blocked in the kernel's `recv()`, not spinning.
- 0-byte raw log: no frames were received because the hang is in `start_stream`, before
  any `get_frame` calls.
- IR worked: the daemon is running, but the depth interface's handler is the problem.

## 3. The bounded-wait depth retry

### 3.1 The command

Use `timeout` to bound the wait:

```bash
timeout 30 ./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 5 --dump-replies
```

This kills the process after 30 seconds if it hasn't exited. The exit code will be 124
(timeout) if it hangs, or the process's actual exit code if it completes.

**Why 30 seconds:** The nominal run is 5 seconds. The IR run completed in moments. If
the depth run hasn't produced output in 30 seconds, it's genuinely hung, not slow.

### 3.2 The instrumentation (for the builder)

Add explicit `fflush(stdout)` after critical output lines to make the hang visible:

1. **After line 1121** (the `[live] connected` line):
   ```cpp
   std::printf("[live] connected %s:%u (%s, suffix %s, pull_ms=%d, per-call logging=%s)\n",
               host.c_str(), port, route.name.c_str(), route.suffix.c_str(), pull_ms,
               log ? "on" : "off");
   std::fflush(stdout);  // ADD THIS
   ```

2. **After line 1129** (the `[live] start_stream ->` line):
   ```cpp
   std::printf("[live] start_stream -> %s\n",
               started.kind == Value::Kind::Bool && started.b ? "true" : "non-error");
   std::fflush(stdout);  // ALREADY HERE, but verify
   ```

3. **Add timestamps** to measure delays:
   ```cpp
   auto t0 = std::chrono::steady_clock::now();
   std::printf("[live] connected %s:%u ... at t=%lld ms\n", ...,
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   t0 - std::chrono::steady_clock::now()).count());
   std::fflush(stdout);
   ```

4. **Add a trace before `StartStream()`**:
   ```cpp
   std::printf("[live] calling start_stream...\n");
   std::fflush(stdout);
   Value started = StartStream(rpc, route, 1920, 1200, 50);
   ```

### 3.3 The fix (for the builder)

**Option A: Use `readReplyWithDeadline()` for the live path**

The replay path already uses `readReplyWithDeadline()` (main.cpp:362-366), which has a
timeout. The live path uses the unbounded `ReadMessage()`. Change the live path to use
the bounded version:

In `RpcClient::call()` (main.cpp:326-341), change:
```cpp
Value resp = ReadMessage();
```
to:
```cpp
Value resp = ReadMessageWithDeadline(5000);  // 5-second timeout
```

Or add a parameter to `call()` to specify the timeout, and pass it from the live path.

**Option B: Add a socket-level timeout**

Use `setsockopt(SO_RCVTIMEO)` to set a receive timeout on the socket:

In `RpcClient::connect()` (main.cpp:311-321), add:
```cpp
struct timeval tv = {5, 0};  // 5-second timeout
setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

This makes `recv()` return with `EAGAIN` or `EWOULDBLOCK` after 5 seconds of no data.
The client can then retry or report a timeout.

**Option C: Both**

Use both `readReplyWithDeadline()` and `SO_RCVTIMEO` for defense in depth. The socket
timeout bounds the kernel-level wait; the `readReplyWithDeadline()` bounds the
application-level wait.

### 3.4 What the record must capture

The retry record must capture:
1. **The exit code** — 0 (success), 124 (timeout), or the process's actual exit code.
2. **The output** — all stdout/stderr, including the `[live] connected` line (if it
   appears) and any error messages.
3. **The timestamps** — when each step occurred (connect, start_stream, get_frame,
   stop_stream).
4. **The raw log file** — the bytes of any frames received (if any).

If the hang is reproduced:
- Exit code 124 (timeout) confirms the hang.
- Zero output confirms the buffering issue.
- The timestamps (if instrumented) show where the hang occurs.

If the hang is not reproduced:
- The depth interface works, and the original hang was a transient issue.
- The record shows a successful run with frames received.

## 4. The fix or the builder row

**If the static analysis is correct** (the daemon's depth interface doesn't reply to
`start_stream`):

**The fix is Option A or B above** — add a timeout to the live path's RPC calls. This
prevents the hang and makes it a timeout error instead.

**The instrumentation is §3.2** — add `fflush()` and timestamps to make the hang visible
and measureable.

**The builder row is** — this finding recommends the fix and instrumentation for the
builder. The builder implements the fix and the instrumentation, then the test queue
runs the bounded-wait retry.

**If the static analysis is wrong** (the hang is elsewhere):

The instrumentation (§3.2) will reveal where the hang actually occurs. The timestamps
will show which step is slow or blocked. The builder can then fix the actual problem.

## 5. OPEN QUESTIONS

1. **Why doesn't the daemon reply to `start_stream` on the depth interface?**
   - Is the depth interface's handler deadlocked?
   - Is the depth interface misconfigured?
   - Is the depth interface in a different state than IR?
   - **Needs:** A daemon-side trace or log to see what the depth interface's handler is
     doing when the client sends `start_stream`.

2. **Is the hang reproducible?**
   - Was the original hang a transient issue, or does it happen every time?
   - **Needs:** The bounded-wait retry to reproduce the hang (or confirm it's gone).

3. **Is the depth interface's stream type (7) correct?**
   - The client sends `stream_type=7` for depth (main.cpp:946). Is this the correct
     value for the depth interface?
   - **Needs:** Cross-check with the capture or the daemon's documentation.

4. **Are there other unbounded reads in the live path?**
   - The live path uses `ReadMessage()` for all RPC calls (main.cpp:334). Are there
     other places where the client can hang?
   - **Needs:** A review of all `ReadMessage()` calls in the live path.

## 6. Method / provenance

- `tests/results/2026-10-20-plan127-live-reply-dump-retry.md` — the hardware retry
  record (IR pass, depth hang).
- `tools/raptor_client/main.cpp` — the client code (live path, RPC calls, read methods).
- No device run was performed for this finding. All claims are `[inferred]` from the
  code and the test record.
- The bounded-wait retry is a test-queue card, not this task.

## 7. Related rows

- **#127** — the original hardware retry (IR pass, depth hang). This finding diagnoses
  the depth hang.
- **#132** — the depth capture. This finding unblocks #132 by providing a bounded-wait
  retry plan.
- **#105** — the original "zero frames" question for IR. This finding is unrelated to
  #105 (which is about IR, not depth).
