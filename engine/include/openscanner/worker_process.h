#pragma once

#include <string>
#include <vector>

namespace openscanner {

// Re-launches the current executable with `extra_args` appended, waits for
// it to finish, and reports how it ended.
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
// Returns true if the child exited normally (any exit code, including
// non-zero) and fills `out_exit_code`. Returns false if the child was
// killed by a fatal signal (POSIX) or crashed (Windows access violation),
// and fills `out_reason` with a short description instead.
bool RunSelfAsWorker(const std::vector<std::string>& extra_args, int* out_exit_code, std::string* out_reason);

}  // namespace openscanner
