# Device enumeration state after RPC-client session — Row #173

*Capture-only analysis from hardware tester records. No vendor code claims beyond
log observation. No device run performed for this finding.*

**Every claim below is `[inferred]` from the log evidence.** No gated hardware run
backs any of it.

## 0. The problem in one paragraph

After an RPC-client session (`scan_server` or `raptor_client`), subsequent
`scan_server --live` starts observe a split state: `DeviceManager` logs the device
as present ("Device changed! removed: 0, added: 1", "Current device(s) list: (1)"),
but `obscan_device_management_get_device_num` returns `count=0` for all probes
(6 attempts over 4.75s). This persists across 5 consecutive starts over ~1 minute
and is only resolved by power-cycling the scanner. The device itself is confirmed
working (a later `raptor_client` streaming retry after power-cycle returned full
frame data on both IR and depth ports).

## 1. Evidence summary

**Source records** (read-only, from the hardware tester's unlanded records):
- `~/DevReposiMac/Open3DScanner-wt-hw/tests/results/2026-10-20-plan167-livestart-5reruns.md`
- `~/DevReposiMac/Open3DScanner-wt-hw/tests/results/2026-10-20-plan167-livestart-5reruns.raw.log`
- `~/DevReposiMac/Open3DScanner-wt-hw/tests/results/2026-10-20-streaming-retry-167-161-168.md`

**Exact observations** (quoted from the raw log):
- Five `scan_server --live` starts at 22:20:06.822240, 22:20:19.905489,
  22:20:32.998165, 22:20:46.076661, 22:20:59.158247 (~13s apart)
- Every run logs:
  - `DeviceManager.cpp:188 Device changed! removed: 0, added: 1`
  - `Current device(s) list: (1)`
  - `- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0`
  - `Ethernet.cpp:244 rpc client connection state:1`
- Then **six** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0` lines
  (30 probes total across 5 runs, all returning 0)
- Then `device enumeration settled after 6 probe(s), 4750 ms of backoff.`
- Then `No device detected after 6 probe(s) over 4750 ms (budget 4750 ms)`
- Exit code 2 for all 5 runs

**Post-power-cycle confirmation:**
- After operator power-cycled the scanner, a later `raptor_client --live` streaming
  retry (22:50:59) succeeded:
  - IR port 10013: 50/50 data frames (exit 0)
  - Depth port 10010: 55/55 data frames (exit 0)
- This confirms the device itself is functional; the issue is a state problem in the
  daemon/SDK after a prior client session

## 2. The two paths and their state

The enumeration logic in `tools/scan_server/main.cpp` (lines 968-1034) uses two
separate mechanisms:

### 2.1 Path A: `DeviceManager` callbacks (asynchronous, hotplug-driven)

```cpp
status = lib.obscan_device_management_add_device_msg_listener(device_mgmt, &g_device_listener);
```

- `DeviceManager` logs device presence via callbacks triggered by USB hotplug events
- These callbacks fire asynchronously when the USB subsystem detects a device
- The logs show `DeviceManager.cpp:188` firing immediately after context creation
- This path tracks **physical device presence** (USB-level visibility)
- `[inferred]` This path is independent of the RPCServer daemon's device availability state

### 2.2 Path B: `obscan_device_management_get_device_num` query (synchronous, daemon-driven)

```cpp
const ObScanStatus probe_status = lib.obscan_device_management_get_device_num(device_mgmt, &count);
```

- This is a synchronous query to the SDK/daemon for "available" devices
- The probe policy (from `device_probe.h`) retries 6 times with exponential backoff:
  - 250ms → 500ms → 1000ms → 1500ms (capped) → 1500ms → 1500ms
  - Total budget: 4.75s
- In the observed state, this returns `count=0` for all 30 probes across 5 runs
- `[inferred]` This path queries the RPCServer daemon for "active" or "available" devices
- `[inferred]` The daemon holds the device in a "busy" or "claimed" state after a client session

### 2.3 The split

| Aspect | Path A (DeviceManager callbacks) | Path B (get_device_num) |
|---|---|---|
| **Trigger** | USB hotplug events (asynchronous) | Synchronous query to daemon |
| **Tracks** | Physical device presence (USB-level) | "Available" devices (daemon-level) |
| **State in observed issue** | Device present (logs it) | count=0 (no available devices) |
| **Dependency** | USB subsystem | RPCServer daemon state |

**The key observation:** The device is physically present (Path A sees it) but not
"available" to new SDK contexts (Path B returns 0). This suggests the RPCServer
daemon holds the device in a state that prevents new enumeration, even though the
USB subsystem still sees it.

## 3. The exact condition that leaves the count at 0

**Hypothesis** `[inferred]`: After an RPC-client session ends, the RPCServer daemon
does not properly release the device from its "active" or "claimed" state. The
daemon's exclusive USB access (`ioreg UsbExclusiveOwner = RPCServer` per
`usb-descriptor-map.md`) is not fully released, so subsequent `get_device_num`
queries return 0 even though the USB hotplug subsystem still sees the device.

**Supporting evidence:**
- The device is confirmed working after power-cycle (the power-cycle releases all
  daemon state and USB exclusive access)
- The issue persists across 5 consecutive starts over ~1 minute (not a transient
  timing issue that the 4.75s probe budget would fix)
- `DeviceManager` sees the device immediately (USB hotplug works), but
  `get_device_num` never returns >0 (daemon state is stuck)

**What is NOT settled:**
- Whether restarting the RPCServer daemon would clear the state (untested)
- Whether there is an SDK-level API to force-release the device (no such API is
  documented in `obscan_api.h`)
- Whether the issue is in the daemon's teardown logic or in the SDK's context
  cleanup (would require daemon-side tracing or SDK source)
- Whether the issue is specific to unclean teardown (e.g., SIGTERM vs normal exit)
  or happens even after clean `obscan_context_destroy`

## 4. The cheapest workaround/test

### 4.1 Confirmed workaround: Power-cycle the scanner

- **Action:** Physically power-cycle the scanner (unplug/replug or power button)
- **Effect:** Releases all daemon state and USB exclusive access
- **Evidence:** Confirmed in the streaming retry record (power-cycle → full frame data)
- **Cost:** Physical intervention, ~10s downtime
- **Limitation:** Not automatable in CI; requires operator presence

### 4.2 Untested workaround: Restart the RPCServer daemon

- **Action:** `pkill -f RPCServer` (or `killall RPCServer`) then let launchd restart it
- **Hypothesis:** Restarting the daemon would release its USB exclusive access and
  clear the "claimed" state
- **Evidence:** None (untested)
- **Cost:** ~2s daemon restart, no physical intervention
- **Risk:** May break other SDK users if they are active
- **Test recipe:**
  1. Run `scan_server --live` until it exits with code 2 (count=0)
  2. `pkill -f RPCServer`
  3. Wait 2s for launchd to restart the daemon
  4. Run `scan_server --live` again
  5. Observe whether `get_device_num` returns count>0

### 4.3 Diagnostic test: Check daemon state before/after

- **Action:** Log the daemon's state (PID, uptime, USB exclusive access) before and
  after a client session
- **Commands:**
  - `ps aux | grep RPCServer` (PID and uptime)
  - `ioreg -l | grep -A 5 "UsbExclusiveOwner"` (USB exclusive access)
  - Compare before first `scan_server` start vs after it exits with code 2
- **Hypothesis:** The daemon's USB exclusive access persists after the client session
- **Cost:** No intervention, just logging
- **Limitation:** Requires `ioreg` access (may need sudo)

## 5. Device-dependent reproduction

**Prerequisites:**
- Creality Raptor Pro scanner (SN CR0886100SN or equivalent)
- RPCServer daemon running (`ps aux | grep RPCServer`)
- `scan_server` binary built from `tools/scan_server/`

**Exact test procedure:**

1. **Baseline: Confirm device is working**
   ```bash
   ./build/tools/scan_server/scan_server --live 18899
   ```
   - Expected: `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`
   - Let it run for ~5s, then Ctrl+C to exit cleanly
   - Verify exit code 0

2. **Trigger the issue: Run a second `scan_server` immediately**
   ```bash
   ./build/tools/scan_server/scan_server --live 18899
   ```
   - Observe: `DeviceManager` logs "Current device(s) list: (1)"
   - Observe: `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0` (6 times)
   - Observe: Exit code 2 ("No device detected")

3. **Confirm the issue persists: Run a third `scan_server`**
   ```bash
   ./build/tools/scan_server/scan_server --live 18899
   ```
   - Expected: Same as step 2 (count=0, exit 2)

4. **Test workaround A: Restart the daemon**
   ```bash
   pkill -f RPCServer
   sleep 2
   ./build/tools/scan_server/scan_server --live 18899
   ```
   - Observe: Does `get_device_num` return count=1 now?
   - Record result (PASS if count=1, FAIL if count=0)

5. **Test workaround B: Power-cycle the scanner**
   ```bash
   # Physically power-cycle the scanner (unplug/replug or power button)
   sleep 5
   ./build/tools/scan_server/scan_server --live 18899
   ```
   - Expected: `get_device_num -> OB_SCAN_SUCCESS, count=1`, exit 0
   - This is the confirmed workaround

6. **Diagnostic: Check daemon state**
   ```bash
   ps aux | grep RPCServer
   ioreg -l | grep -A 5 "UsbExclusiveOwner"
   ```
   - Record PID, uptime, and USB exclusive access state
   - Compare before step 1 vs after step 3

**Acceptance criteria:**
- Step 2 and 3 reproduce the issue (count=0, exit 2)
- Step 4 clarifies whether daemon restart is a viable workaround
- Step 5 confirms power-cycle as the definitive workaround
- Step 6 provides diagnostic data for root-cause analysis

## 6. OPEN QUESTIONS

1. **Does restarting the RPCServer daemon clear the state?**
   - **Needs:** The test in §4.4 (restart daemon, then run `scan_server`)
   - **Why it matters:** If yes, this is a CI-friendly workaround; if no, the issue
     is deeper (SDK or hardware state)

2. **Is the issue in the daemon's teardown or the SDK's context cleanup?**
   - **Needs:** Daemon-side tracing (would require REA/Ghidra analysis of RPCServer)
     or SDK source access
   - **Why it matters:** Determines whether the fix is in our code (cleaner teardown)
     or requires a vendor fix

3. **Does clean `obscan_context_destroy` avoid the issue, or does it happen even after
   clean teardown?**
   - **Needs:** Run `scan_server` with normal Ctrl+C exit (clean teardown), then
     immediately run again
   - **Why it matters:** If clean teardown avoids the issue, we can document the
     proper exit path; if not, the issue is in the SDK/daemon regardless of how we
     exit

4. **Is there an SDK-level API to force-release the device?**
   - **Needs:** Review `obscan_api.h` for any "release" or "reset" API
   - **Why it matters:** If yes, we can add a workaround in `scan_server` teardown

5. **Does the issue happen with `raptor_client` (no SDK context) or only with
   `scan_server` (SDK context)?**
   - **Needs:** Run `raptor_client --live` then immediately run `scan_server --live`
   - **Why it matters:** If `raptor_client` (which doesn't create an SDK context) also
     triggers the issue, the problem is in the daemon; if only `scan_server` triggers
     it, the problem is in the SDK context cleanup

6. **Is the 4.75s probe budget sufficient, or should it be extended?**
   - **Observation:** The probe budget (250ms → 1500ms, 6 attempts, 4.75s total) was
     added for row #167 to handle transient `count=0`
   - **Observation:** In this case, even after 4.75s, `count` stays at 0
   - **Hypothesis:** This is not a transient timing issue but a persistent state issue
   - **Needs:** Extend the probe budget to 10s or 30s and observe whether `count`
     eventually becomes >0
   - **Why it matters:** If extending the budget fixes it, the issue is a slow
     daemon recovery; if not, the issue is a persistent state problem

## 7. Method / provenance

- `~/DevReposiMac/Open3DScanner-wt-hw/tests/results/2026-10-20-plan167-livestart-5reruns.md`
  and `.raw.log` — the 5-run observation of the split state
- `~/DevReposiMac/Open3DScanner-wt-hw/tests/results/2026-10-20-streaming-retry-167-161-168.md`
  and `.raw.log` — the post-power-cycle confirmation
- `tools/scan_server/main.cpp` — the enumeration logic (lines 968-1034)
- `tools/scan_server/device_probe.h` — the probe policy (250ms → 1500ms, 6 attempts, 4.75s)
- `docs/recon/findings/usb-descriptor-map.md` — RPCServer's exclusive USB access
- `docs/recon/findings/device-message-listener.md` — DeviceManager callbacks
- `engine/include/openscanner/obscan_api.h` — SDK API (no "release" or "reset" API observed)

**No gated hardware run backs any of the hypotheses.** The test recipe in §5 is the
device-dependent reproduction that would settle the open questions.

## 8. Related rows

- **#167** — The original device-enumeration race (crash on transient `count=0`).
  Row #167 added the probe budget to handle transient `count=0`, but this finding
  shows a **persistent** `count=0` that the probe budget cannot fix.
- **#151** — The device-side race/crash in `scan_server` (worker crashed inside the
  vendor SDK). This may be related if the crash leaves the daemon in a bad state.
- **#132** — The gap-free capture (blocked by the depth hang, row #161). This finding
  is a prerequisite for #132's depth capture (the device must be enumerable first).
