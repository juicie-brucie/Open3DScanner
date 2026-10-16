#pragma once

#include <string>
#include <vector>

namespace openscanner {

// Re-launches the current executable with `extra_args` appended, waits for
// it to finish (or for `timeout_seconds` to elapse, whichever comes first),
// and reports how it ended.
//
// Why this exists: some OBScan SDK calls (confirmed: obscan_context_destroy,
// see README.md "Known issues") crash inside the vendor's own bundled
// ncnn+OpenMP runtime, and that crash happens on background worker threads
// the vendor library spawns internally — not just the calling thread. A
// signal handler on the calling thread cannot safely recover from a fault
// on a different thread (there is no safe way to siglongjmp across
// threads), so in-process recovery doesn't work for this failure mode.
// Running the risky call in a disposable child process does: if the child
// is killed by a fatal signal, the parent simply observes that in its exit
// status and carries on.
//
// The same call can also fail a different way on the same hardware: rather
// than crashing, obscan_context_destroy's ncnn+Vulkan (MoltenVK on macOS)
// SuperPoint network construction/teardown has been observed to pin
// multiple CPU cores indefinitely with no further logging at all —
// confirmed to still be going after 15+ minutes with zero progress, where
// every other call in the same lifecycle (including a normal run of this
// same call) completes in well under a second. A bare `waitpid`/
// `WaitForSingleObject(..., INFINITE)` has no way to tell "slow" apart from
// "never coming back," so a hang here previously required a human to
// `kill -9` the worker by hand every time. `timeout_seconds` bounds that:
// if the child hasn't exited by then, it's forcibly killed and treated the
// same as a crash (via `out_reason`), so the caller never blocks forever.
//
// On POSIX the timeout is two-stage: the child first gets SIGTERM and
// `term_grace_seconds` to stop its scan and release the device through the
// vendor SDK's own teardown, and only then SIGKILL. A hard kill skips that
// release path, and a scanner killed mid-stream has been confirmed to need
// a physical power cycle before it streams again — so workers that touch
// the device should handle SIGTERM (and SIGINT, which the launcher ignores
// while it waits so Ctrl-C reaches only the worker's cleanup path).
//
// Returns true if the child exited normally (any exit code, including
// non-zero) and fills `out_exit_code`. Returns false if the child was
// killed by a fatal signal (POSIX), crashed (Windows access violation), or
// timed out (whether it then shut down on SIGTERM or had to be force-
// killed), and fills `out_reason` with a short description instead.
bool RunSelfAsWorker(const std::vector<std::string>& extra_args, int* out_exit_code, std::string* out_reason,
                      int timeout_seconds = 60, int term_grace_seconds = 20);

}  // namespace openscanner
