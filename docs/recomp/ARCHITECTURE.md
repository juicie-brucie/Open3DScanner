# Recomp architecture

How the OpenScanner clean-room ABI reconstruction is organized: which agents do
what, where the walls are, and how work moves between them.

## The principle

OpenScanner is a clean-room reimplementation of the OBScan SDK's C ABI. We recover
the interface by decompiling vendor binaries, then write **original** code against
it. Everything below exists to make this single principle enforced, not aspirational:

> **Recon produces specs, not code. Builder consumes specs, not binaries.**

The recovered ABI header `engine/include/openscanner/obscan_api.h` is the keystone
artifact: the only thing both sides agree on, and the thing a human reviews.

## Roles

| Agent | Mode | Writes | Model |
| --- | --- | --- | --- |
| `openscanner/recon` | primary | `obscan_api.h`, `docs/recon/**` | `opencode/ll-alpha-free#max` |
| `openscanner/builder` | primary | `engine/src`, `mesh`, `ui`, `tools` | `opencode/beam-free#high` |
| `openscanner/recon-sweep` | subagent | `docs/recon/findings/**` (draft) | `opencode/deepseek-v4.1-flash-free#low` |
| `openscanner/verifier` | all | *nothing* (read-only) | `opencode/space-bunny-free#max` |
| hardware tester | external | `tests/results/**` | Claude Code, driven by hand |

### Why these models

Error costs are asymmetric, so model spend follows the cost, not the job title:

- **Spec errors (recon)** are invisible and propagate into every downstream artifact.
  Highest cost → strongest model with the deepest reasoning variant.
- **Verification errors (verifier)** let bugs through silently. Second highest cost,
  and it runs a **different model family than the builder** so their blind spots are
  uncorrelated.
- **Implementation errors (builder)** are caught by the verifier and then by hardware.
  Recoverable → mid tier.
- **Sweep errors (recon-sweep)** are bounded by the snapshot it reads and reviewed by
  recon. Cheapest → fastest model, lowest variant.

Subagents inherit the parent's model when unset, so `recon-sweep` is pinned
explicitly — otherwise fan-out would run on recon's `#max` model, which is backwards.

## The walls

| | `edit` | `read .recon-cache` | `rea_*` | may launch |
| --- | --- | --- | --- | --- |
| `recon` | header + `docs/recon` | allow | allow | `recon-sweep`, `verifier` |
| `builder` | src dirs; **deny header** | **deny** | **deny** | `verifier` only |
| `recon-sweep` | `docs/recon` only | allow | **deny** | — |
| `verifier` | **deny all** | **deny** | **deny** | — |

Three rules carry the design:

1. **`builder` is denied `rea_*`.** Ghidra can therefore only ever launch from a recon
   session. This is the structural fix for duplicate ~8 GB JVMs on one dylib.
2. **`recon-sweep` is denied `rea_*`.** It can read a snapshot but never analyze. That
   is what makes fan-out safe: analysis stays serialized in `recon`; only *reading*
   fans out. Nesting depth is one, so `recon → recon-sweep` is the deepest chain.
3. **`builder` may launch only `verifier`.** A subagent carries its *own* permissions,
   not its parent's — so every launchable agent is a potential hole in the wall. The
   verifier is itself walled (no vendor access), so nothing leaks through it. Sealing
   builder against everything else costs nothing; it can grep/read its own source
   directly.

## Where things live

Agent and command definitions are **global**, not in this repo, so the repo stays
clean and the definitions sit next to the `rea` MCP server they depend on:

```
~/.config/opencode/
  opencode.json                       # rea MCP server
  agents/openscanner/{recon,builder,recon-sweep,verifier}.md
  commands/openscanner/{recon,build,reconcile,verify}.md
```

The repo holds only the work products:

```
Open3DScanner/
  engine/include/openscanner/obscan_api.h   # THE CONTRACT (recon writes)
  docs/recon/findings/*.md                  # narrative spec (recon writes)
  docs/recomp/PLAN.md                       # task board (you own)
  docs/recomp/ARCHITECTURE.md               # this file
  tests/hardware/*                          # test programs (builder)
  tests/results/*                           # observed behavior (hardware agent)
  .recon-cache/                             # vendor bins + REA snapshots (gitignored)
```

## The loop

```
   PLAN.md task
        │
        ▼
   ┌─ recon ── analyze once ──► .recon-cache/<target>.analysis.json
   │     │                          │
   │     │                    recon-sweep ×N (read snapshot, no Ghidra)
   │     ▼                          ▼
   │   obscan_api.h ◄── reconcile drafts ── docs/recon/findings/*.md
   │        │
   │   [human reviews the header diff]   ← the one checkpoint that matters
   │        ▼
   │   ┌─ builder ── engine/src + smoke test
   │   │     │
   │   │     └── verifier ──► spec-vs-code mismatch report
   │   ▼
   │   hardware agent ──► tests/results/<date>-<case>.md
   │        │
   └────────┴─ /openscanner/reconcile ─► spec wrong → recon
                                         impl wrong → builder
                                         test wrong → fix the test
```

Two review gates, deliberately different kinds:

- **verifier** — static, cheap, automated: "does the code disagree with the header?"
- **hardware** — empirical, manual: "does the header disagree with the real device?"

## Claude Pro's place

The Anthropic integration in OpenCode offers only API-key and environment
credentials, **no OAuth** — so a Claude Pro subscription cannot drive OpenCode.
Claude Pro is therefore a human-attended, out-of-band resource:

| Use | Surface |
| --- | --- |
| Hardware testing on the scanner box | Claude Code |
| Contract review (the human checkpoint) | claude.ai |
| Periodic cross-family verification | claude.ai |
| Strategy, planning, spec synthesis | claude.ai |

**The rule that keeps it clean-room:** Claude Pro may sit on the **recon side** of
the wall, never the **builder side**. Analyzing decompiled output, reviewing a spec,
or verifying spec-vs-code is fine. Showing it decompiled output and asking it to
write implementation code is not — the defensibility of `engine/src/` rests on it
being written from the header alone.

Because Anthropic offers no subscription path, the ways to get paid-tier models into
OpenCode are: **OpenCode Go**, an **OpenAI (ChatGPT) OAuth** login, a **GitHub
Copilot** OAuth login, or **OpenRouter** credits.

## Running it

Start two **new** sessions (an agent is fixed per session) in the project directory:

```
session A — agent: openscanner/recon      /openscanner/recon <target>
session B — agent: openscanner/builder    /openscanner/build <item>
```

Then, after a hardware run: `/openscanner/reconcile`. And any time:
`/openscanner/verify <item>`.

## Open items

- **Git discipline:** builder is the only code writer, so one branch with a commit per
  contract change is enough for now. Add a worktree only when two writers collide.
- **Hardware seam:** decide whether the Claude Code agent works in this checkout
  (writing only `tests/results/**`) or its own clone.
- **Vendor binaries:** `.recon-cache/` must be populated before the first
  `/openscanner/recon` run.
