# Recovery notes (spec side of the wall)

This directory holds the **specification** recovered from the vendor binaries. It
is written by the `recon` agent and read by the `builder` agent.

The clean-room rule for this repo:

> The recon agent produces specs, not code. The builder agent consumes specs, not binaries.

So what lives here is deliberately *not* decompiled code:

| Path | Contents |
| --- | --- |
| `findings/<target>.md` | Signature, struct layout, semantics, lifetime, error codes, and **OPEN QUESTIONS**. |
| (elsewhere) `engine/include/openscanner/obscan_api.h` | The recovered C ABI contract itself. |

Raw decompiler output, Ghidra snapshots, and vendor binaries do **not** belong
here. They live in `.recon-cache/` (gitignored), which the `builder` agent is
denied from reading. If you find raw listings in this directory, they should move
to `.recon-cache/` — a findings file is a spec, not a code dump.
