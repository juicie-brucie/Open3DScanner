# Correction — main thread's stack in the three archived crash reports

**Date:** 2026-10-17
**Raised by:** PLAN.md row #20 (filed from the #56 verification, observation 2).
**Touches:** [`2026-10-17-plan8-device-message-listener-crash-analysis.md`](./2026-10-17-plan8-device-message-listener-crash-analysis.md),
PLAN.md row #5, and the design basis for #11/#15's re-run.

This file is additive, per `tests/hardware/README.md:10` — the run records and
the existing crash-analysis file are kept unedited.

## What was re-read

The three archived crash reports, now on `main`:

- `tests/results/2026-10-17-plan8-device-message-listener.crash.ips`
- `tests/results/2026-10-17-device-message-listener-test.crash.ips`
- `tests/results/2026-10-17-device-message-listener-test-run2.crash.ips`

All three are `EXC_BAD_ACCESS` / `SIGSEGV` at `0x0`, faulting thread index 5 (the
NetDeviceWatcher/GVCP thread, per the existing crash analysis). Thread 0
(`com.apple.main-thread`) in **all three** reports carries the identical frame
sequence:

```
__psynch_mutexwait
_pthread_mutex_firstfit_lock_wait
_pthread_mutex_firstfit_lock_slow
std::__1::recursive_mutex::lock()
device::OrbbecInternalDevice::stop()
device::OrbbecInternalDevice::~OrbbecInternalDevice()
device::OrbbecDevice::~OrbbecDevice()
OBDeviceManagement::release()
obscan_context_destroy
(anonymous namespace)::RunTests(...)   [main.cpp:185, inlined]
main                                    [main.cpp:203]
start
```

So at the moment of the fault, the main thread was inside `obscan_context_destroy()`
→ `OBDeviceManagement::release()` → `~OrbbecDevice` → `~OrbbecInternalDevice` →
`OrbbecInternalDevice::stop()`, **blocked waiting on a `recursive_mutex`** — not
idle, not past teardown, not pre-teardown. This holds identically across all
three runs, so it is not an artifact of one run's timing.

## Against row #5

Row #5 reads (of this same `plan8` run): "it never reached
`obscan_context_destroy()`, so it neither supports nor contradicts this claim."

**Contradicted.** The main thread *did* reach `obscan_context_destroy()` — it
was inside it, blocked on the mutex `OrbbecInternalDevice::stop()` needs, at
the instant the watcher thread faulted. Row #5's own subject (the alleged
"reproducible crash in `obscan_context_destroy`/the `destroy` call") is not
settled by this: the proximate fault is still the watcher thread's NULL deref
in `get_active_device()`, not a crash *inside* the destroy call itself — the
main thread is waiting, not crashing. But the premise used to wave off row #5
("never reached destroy") is false and needs to be dropped.

## Against the crash analysis's line 56

The crash analysis (line 56) says: "The crashing thread never ran the teardown
calls, so the run doc's reasoning about the teardown sequence cannot hold."

**Narrowly true, broader conclusion overreaches.** "The crashing thread never
ran the teardown calls" is still correct — thread 5's own stack has no
`obscan_context_destroy` frame in any of the three reports. What the analysis
did not have (the reports were not yet archived when it was written) is that a
*different* thread — main — was concurrently inside that same teardown,
blocked trying to acquire a `recursive_mutex` on the device being destroyed.
That changes what "the teardown sequence cannot hold" can be taken to mean:
teardown was not absent from this crash, it was racing it. The watcher thread's
`get_active_device()` call and the main thread's `OrbbecInternalDevice::stop()`
call are plausibly contending for the same device object's lock/lifetime at the
same moment — consistent with #15's reading (the create→destroy→create burst
racing `remove()`), now extended: the *final* destroy at end-of-test is itself
in a lock-wait when the watcher-thread callback faults, not a bystander.

## What this does not settle

Whether the main thread's `stop()` had already invalidated state that
`get_active_device()` read (a genuine teardown race), or whether both threads
were independently contending for the same recursive mutex with no shared-state
corruption (benign contention, unrelated NULL cause) is still open. Settling it
needs the mutex identity (is `stop()`'s recursive mutex the same one
`get_active_device()`/`update_device_info()` take?) and ordering from a
controlled re-run — recon's territory for the mutex identity, a hardware re-run
for the ordering.

## Consequence for #11 / #15

#15's re-run instruction ("target the remove-side race, not the re-add") holds,
but is now incomplete on its own: the re-run should also capture whether
`obscan_context_destroy()` is in flight (e.g. an explicit log line immediately
before the destroy call, so the raw log can show the teardown call started
before the fault) rather than inferring it only from a post-hoc `.crash.ips`
read.

## Evidence

| File | What it is |
| --- | --- |
| `2026-10-17-plan8-device-message-listener.crash.ips` | Archived crash report, re-read here |
| `2026-10-17-device-message-listener-test.crash.ips` | Archived crash report, re-read here |
| `2026-10-17-device-message-listener-test-run2.crash.ips` | Archived crash report, re-read here |
