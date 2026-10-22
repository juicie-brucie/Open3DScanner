# 2026-10-21 — integrating the Xcode/UI worktree

## The source

The `Open3DScanner-wt-xcode` room (the user's, outside the roster as LEGACY) on
`xcode/user-test`, HEAD `2f88ce3` (= origin/main at the time), with the changes **uncommitted**:

- modified (+289/−57): `tools/macos_app/CMakeLists.txt`, `ui/app.js`, `ui/coverage.js`,
  `ui/render/webgl2_renderer.js`, `ui/render/webgpu_renderer.js`;
- untracked: `tools/macos_app/OpenScanner.entitlements` (478 B) and `build-xcode/`.

## The review

The intent aligns: the density colouring is the visual half of G2/T6; the camera controls are
thin-client UI; the port default fixes the app-origin path (A1/#61); the Xcode signing is
development tooling; no vendor code or header change. Not landable as-is:

- **The full-mesh density is a ~75 MB spike** — `coverage.observe` on the full-detail cloud
  (up to 6.3M vertices) allocates count×3 floats and uploads them at teardown. Cap or skip
  it for the full mesh, or compute it worker-/shader-side; keep density for the live 20k path
  only.
- **The per-point scaling** — at 200k/24 Hz the colour buffer is ≈4.8 MB/frame (~115 MB/s
  extra upload) plus 200k log2/frame: measure it against the tier or move it.
- **The semantics** — density is per-voxel **hit count**; #76's overlay is visited vs seen.
  The diff stops drawing the overlay. Decide keep-both or state that density replaces it; if
  T6 is the goal, hit count is a proxy.
- **The camera** — `viewUserControlled` never resets (auto-fit off forever, including new
  scans); follow mode overwrites pan; the Y-flip is a behaviour change, to verify with the
  pan direction on a known scan.
- **The repo hygiene** — `OpenScanner.entitlements` is referenced by CMake but untracked:
  commit it. `build-xcode/` must not be committed; the `.gitignore` is the operator's.
- **The bridge-era entitlement** — the no-sandbox comment cites the vendor dylib; true today,
  revisit after Phase C/D. Do not let "no sandbox / no hardened runtime" become the
  distribution decision by default.

## The split

- **#190** (build1) — the Xcode build/signing + the entitlements + the `ui/app.js` port
  default (that hunk only).
- **#191** — the live-cloud density colours + both renderers + the live-20k path, with the
  fixes above.
- **#192** — the camera controls, with the reset/follow/Y-flip checks.
- **#193** (deferred, BLOCKED) — the full-mesh density, only after #191 with a stated
  budget or a worker/shader implementation.

The source's branch has no commits beyond main; the integration branches are cut from
`origin/main`. `build-xcode/` is not committed.
