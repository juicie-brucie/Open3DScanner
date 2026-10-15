# Mesh-editing stack (Meshmixer-style tools)

OpenScanner needs a post-scan mesh editing toolbox roughly equivalent to what Autodesk
Meshmixer provides: boolean combine/subtract, smoothing, remeshing, hole filling, and
decimation on top of the scanned mesh. Meshmixer itself is closed-source freeware — not
open source and nothing to vendor — so this is a "recomp": re-deliver the same class of
functionality from permissively-licensed building blocks we can actually ship under
OpenScanner's MIT license.

## Why not just vendor MeshLab / vcglib / CGAL

They're the most feature-complete classical mesh-processing codebases and the obvious
first search result for "Meshmixer alternative" — but MeshLab and its vcglib core are
**GPL-3**, and CGAL (which both MeshLab's and libigl's boolean/remeshing code often lean
on) is **GPL/LGPL dual-licensed**. Statically linking or vendoring GPL code into
OpenScanner would force the combined binary under GPL terms, which conflicts directly
with keeping OpenScanner MIT. So: useful as reference/reading material (cited in
`docs/research/point-cloud-denoising-and-fusion.md`), but **not a dependency we ship**.

## The permissive stack we use instead

| Need | Library | License | Why |
|---|---|---|---|
| Boolean ops (combine, subtract, intersect) | [Manifold](https://github.com/elalish/manifold) | **Apache-2.0** | Purpose-built, guaranteed-manifold mesh boolean library; powers OpenSCAD and Blender's "Exact" boolean solver. Bindings for Python, C++, TS/WASM, Rust, C#, Java, Swift. Directly replaces the GPL-tied CGAL/MCut/Cork boolean implementations other projects reach for. |
| Smoothing, remeshing, parameterization, deformation | [libigl](https://github.com/libigl/libigl) | **MPL-2.0** (core `igl::` namespace) | Header-only geometry-processing library, matrix-in/matrix-out API, used widely in research and production tooling. MPL-2.0 is file-level weak copyleft: fine to use unmodified under an MIT project, we just can't silently fork-and-modify its files without releasing those specific changes. |
| Mesh format I/O / conversion | [meshio](https://github.com/nschloe/meshio) | **MIT** | See `tools/mesh_convert/` (§ below). |
| Point cloud → mesh (Poisson/BPA), ICP, TSDF fusion | [Open3D](https://github.com/isl-org/Open3D) | **MIT** | Covers the scan-fusion side; see the denoising/fusion research doc. |

### Hard rule: never use `igl::copyleft::*`

libigl ships two licenses side by side in its repo root — `LICENSE.MPL2` for the main
library, and `LICENSE.GPL` that applies **only** to the `include/igl/copyleft/`
subtree (the namespace name is a deliberate warning label). That subtree is where
libigl's CGAL-backed boolean/CSG code lives — **do not import anything from
`igl::copyleft::` or `igl/copyleft/`**, including transitively. Boolean ops go through
Manifold instead, which has no such restriction.

A repo-level check for this is worth adding to CI once the build is wired up:
`grep -rn "igl/copyleft\|igl::copyleft" <source dirs>` should always return nothing.

## Vendoring approach

Both Manifold and libigl are added as git submodules under `third_party/`, pinned to a
specific commit (not tracking a branch), with `shallow = true` set in `.gitmodules` so
clones don't pull full history. This keeps OpenScanner's own repo small (a submodule is
just a commit pointer, not the vendored source) while making a specific, reviewable
upstream version part of the build.

```
third_party/
├── manifold/   # Apache-2.0 — boolean/CSG mesh ops
└── libigl/     # MPL-2.0 (core only — never igl/copyleft/*) — smoothing, remeshing, parameterization
```

See `THIRD_PARTY_LICENSES.md` at the repo root for the full dependency/license ledger,
including the pip-installed dependencies (Open3D, meshio) that aren't vendored as
submodules.
