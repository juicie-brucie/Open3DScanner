# REA + Ghidra setup for reverse-engineering this project

Reference document for the local REA (reverse-engineering) tooling used to
analyse CrealityScan's `liborbbec_scan` dylib from this project. It records the
exact client registrations and server environment variables so the setup can be
reproduced, verified, or rolled back — and so an agent can be pointed at this
file directly.

Everything below is machine-specific to `brucemackay`'s macOS (Apple Silicon)
machine. Treat the paths as values to substitute, not as portable defaults.

## Why this configuration exists

The research docs in [`docs/research/`](../research/) were produced with manual
`nm` / `otool -tV` disassembly rather than a decompiler. The stated reason was:

> "Ghidra's full auto-analysis of this binary consistently exceeds this
> environment's tool timeout regardless of request scope (confirmed repeatedly,
> including for single-function requests)."

That "wall" was traced to REA's own Ghidra provider deadline, not to Ghidra:

- `DEFAULT_GHIDRA_STARTUP_TIMEOUT_MS = 330_000` (330 s) in
  `src/config/providerStartupTimeout.ts` of the REA repository.
- The deadline covers import, default auto-analysis, bridge connection and
  health readiness. It is raised with `REA_GHIDRA_STARTUP_TIMEOUT_MS`.

The target is large — `/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.3.5.3.dylib`
is a **218 MB universal (arm64 + x86_64)** Mach-O, roughly **72 MB of `__TEXT`
code** with ~90k functions and ~156k symbols. Import succeeds; the previous
failure was purely the analysis deadline.

## Registered clients

REA is registered with three clients. Each launches the published package:

```
npx -y rea-agents@6.5.0 mcp
```

| Client | Registration file | Server key |
| --- | --- | --- |
| OpenCode | `~/.config/opencode/opencode.json` | `mcp.rea` |
| VS Code | `~/Library/Application Support/Code/User/mcp.json` | `servers.rea` |
| Claude Code | `~/.claude.json` | `mcpServers.rea` |

The shared REA skill is installed at
`~/.agents/skills/reverse-engineer-anything`.

Note: OpenCode's V2 documentation places MCP servers under `mcp.servers`. This
machine's entry is the older `mcp.rea` shape, which the installed OpenCode
accepts and loads (the REA tools appear as `tools.rea.*`). Left as-is rather than
risk breaking a working registration.

## Server environment

The same environment is passed to the `rea` server on all three clients:

| Variable | Value | Purpose |
| --- | --- | --- |
| `GHIDRA_INSTALL_DIR` | `/opt/homebrew/opt/ghidra/libexec` | Extracted Ghidra release root (contains `support/`, `Ghidra/`, `ghidraRun`). |
| `JAVA_HOME` | `/opt/homebrew/Cellar/openjdk@21/21.0.12.1/libexec/openjdk.jdk/Contents/Home` | Ghidra 12.1 needs JDK 21. Homebrew's default `openjdk` is 27 (too new) and plain `java` is not on `PATH`. |
| `REA_GHIDRA_STARTUP_TIMEOUT_MS` | `2700000` | 45 minutes. Replaces the 330 s default that aborted analysis of this binary. Accepts `1`–`2147483647` ms. |
| `REA_GHIDRA_DISABLED_ANALYZERS` | `Decompiler Parameter ID,Decompiler Switch Analysis` | Comma-separated exact Ghidra analyzer names disabled before auto-analysis. Both are decompiler-based and dominate the cost at ~90k functions. REA decompiles on demand, so on-demand queries still work. |
| `GHIDRA_HEADLESS_MAXMEM` | `8G` | Ghidra JVM heap. REA defaults to `2G`, which is thin for a 72 MB code image on this 16 GB host. |

Example (OpenCode shape):

```json
"environment": {
  "GHIDRA_HEADLESS_MAXMEM": "8G",
  "GHIDRA_INSTALL_DIR": "/opt/homebrew/opt/ghidra/libexec",
  "JAVA_HOME": "/opt/homebrew/Cellar/openjdk@21/21.0.12.1/libexec/openjdk.jdk/Contents/Home",
  "REA_GHIDRA_DISABLED_ANALYZERS": "Decompiler Parameter ID,Decompiler Switch Analysis",
  "REA_GHIDRA_STARTUP_TIMEOUT_MS": "2700000"
}
```

### On the disabled analyzers

`REA_GHIDRA_DISABLED_ANALYZERS` requires **exact** Ghidra analyzer names that are
registered for the selected program. A wrong name aborts the headless import
before auto-analysis, so it fails fast but must be correct.

Both names above were confirmed against Ghidra 12.1.4's own analyzer classes
(`DecompilerFunctionAnalyzer` → `Decompiler Parameter ID`,
`DecompilerSwitchAnalyzer` → `Decompiler Switch Analysis`) and then validated by
running an analysis of a small arm64 Mach-O sibling
(`libline_laser_param_preprocessing`) that recorded:

```
"analyzer_preset": "ghidra-default+disabled:Decompiler Parameter ID,Decompiler Switch Analysis"
```

`Objective-C Message Analyzer` (the example in REA's own docs) was deliberately
**not** used: it is only reliably registered for Objective-C/Swift programs, and
this library is C++.

The disabled-analyzer list is part of REA's analysis profile digest, so a session
fails to open if a committed profile and the setting disagree. Change the value
between sessions, not during one.

## How analysis behaves (important)

- **The Ghidra database is ephemeral.** REA imports one target into a fresh
  temporary project. Nothing is persisted between provider sessions.
- **Analysis runs once per session.** `open_binary` only binds the target; the
  *first analysis query* (e.g. `binary_overview`) triggers import +
  auto-analysis. Later calls in the same session are fast.
- **Snapshots are not a saved database.** `--snapshot` (CLI) / `snapshot_path`
  (MCP) cache successful per-operation results only; a new or live query can
  still require provider startup.
- **Client timeout vs provider deadline are separate.**
  - OpenCode's MCP `execution` timeout defaults to **12 hours**, so a long
    analysis call will not be cancelled client-side.
  - REA's `REA_GHIDRA_STARTUP_TIMEOUT_MS` is the effective analysis budget.
  - REA's client docs describe a close/reopen recovery flow if a caller
    deadline expires while the provider is still working.

## Target and workflow notes

- Work on a **thin arm64 slice**, not the full universal binary:
  ```bash
  lipo -thin arm64 \
    /Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.3.5.3.dylib \
    -output /path/to/liborbbec_scan_arm64.dylib
  ```
- There is nothing meaningful to strip: the file has no `__DWARF` segment and
  `strip -S` removes only ~2.8 MB while keeping all symbols.
- The small sibling dylibs (`libline_laser_param_preprocessing`,
  `libsoc_linelaser_param_processing`, ~600 KB each) analyse in seconds and are
  useful for validating settings before committing to a long run.

## Verification

```bash
# Registration + toolchain health (all ghidra* checks should be healthy)
npx -y rea-agents@6.5.0 doctor --json

# End-to-end analysis of the thin arm64 slice (CLI has no client deadline)
export GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec
export JAVA_HOME=/opt/homebrew/Cellar/openjdk@21/21.0.12.1/libexec/openjdk.jdk/Contents/Home
export REA_GHIDRA_STARTUP_TIMEOUT_MS=2700000
export REA_GHIDRA_DISABLED_ANALYZERS="Decompiler Parameter ID,Decompiler Switch Analysis"
export GHIDRA_HEADLESS_MAXMEM=8G
npx -y rea-agents@6.5.0 analyze /path/to/liborbbec_scan_arm64.dylib --provider ghidra --format json
```

Clients read the environment when they start the REA server, so **restart the
client** after changing any value above.

`rea doctor`'s overall `healthy: false` is expected on this machine: it reflects
optional items only (Hopper not installed, IDA not configured, JADX not
supplied, and Claude Desktop / Copilot CLI detected but not registered). Every
`ghidra*` check reports healthy.

## Rollback

- Claude Code's config was backed up before editing:
  `~/.claude.json.rea.backup.20261016165201` (and earlier
  `*.recovery` backups in the home directory).
- To re-apply through the supported path, export the values above and run
  `rea setup --client <client> --yes --json`. Setup automatically carries
  `REA_GHIDRA_STARTUP_TIMEOUT_MS` (and the other selected settings) into the
  registration; `REA_GHIDRA_DISABLED_ANALYZERS` and `GHIDRA_HEADLESS_MAXMEM`
  must be present in the registration `environment`/`env` object directly.
- To revert, remove the three added keys from each registration and restart the
  client.

## Known gaps / next steps

- The vendor pipeline work in `docs/research/` left several items open that a
  working decompiler can now close: `detect_markers_one_view_aio_impl`'s
  pixel-scan logic, the landmark → `TrackedFrame` path, `FuseMap::AddPoint`'s
  per-point weight, the CGAL Delaunay consumer, and
  `compute_cloud_overlap_fastKDtree`.
- `engine/include/openscanner/obscan_api.h` states its struct offsets are
  inferred from IL2CPP metadata and "have not yet been independently verified
  against the native struct via disassembly" — a natural first target now that
  Ghidra analysis completes.
- Hopper is intentionally not installed (Ghidra is the chosen engine); IDA and
  JADX are unconfigured.
