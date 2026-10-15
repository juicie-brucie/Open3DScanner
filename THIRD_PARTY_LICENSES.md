# Third-party dependency and license ledger

OpenScanner is MIT-licensed. Every dependency listed here — vendored submodule or
pip/package dependency — was chosen to be MIT-compatible: permissive (MIT/BSD-3-Clause/
Apache-2.0), or MPL-2.0 used only unmodified (file-level weak copyleft, which doesn't
extend to the rest of the project as long as we don't fork-and-modify the library's own
files). **No (L)GPL code is vendored or statically linked.**

| Dependency | License | How it's used | Vendored? |
|---|---|---|---|
| [Manifold](https://github.com/elalish/manifold) (`third_party/manifold`, pinned `v3.5.4`) | Apache-2.0 | Mesh boolean/CSG operations (combine, subtract, intersect) for the mesh-editing toolbox | git submodule |
| [libigl](https://github.com/libigl/libigl) (`third_party/libigl`, pinned `v2.6.4`) | MPL-2.0 (core `igl::` namespace only) | Smoothing, remeshing, parameterization, deformation for the mesh-editing toolbox | git submodule — **never import `igl::copyleft::*` / anything under `include/igl/copyleft/`, which is GPL-3 (CGAL-backed) and explicitly carved out of the MPL2 grant; see `docs/architecture/mesh-editing-stack.md`** |
| [meshio](https://github.com/nschloe/meshio) | MIT | Mesh format I/O for `tools/mesh_convert/` | pip (`tools/mesh_convert/requirements.txt`) |
| [Open3D](https://github.com/isl-org/Open3D) | MIT | ICP/registration, TSDF/voxel-hash fusion, Poisson/BPA reconstruction, point-cloud denoising and fusion-accuracy validation (see `docs/research/point-cloud-denoising-and-fusion.md`) | pip (not yet vendored as a submodule — ships as a large prebuilt wheel, not source we vendor) |

## Explicitly excluded (GPL/LGPL — do not add)

These came up in research as the best-known classical implementations of several
algorithms we need, and are cited for reading/reference, but must **not** be vendored,
statically linked, or added as a build dependency, because doing so would require the
combined work to be released under their copyleft terms:

| Project | License | Why it came up | Permissive substitute used instead |
|---|---|---|---|
| [CGAL](https://github.com/CGAL/cgal) | GPL/LGPL (dual) | WLOP/EAR point resampling, jet/bilateral point smoothing, mesh booleans | Open3D (fusion/reconstruction) + Manifold (booleans); WLOP-equivalent smoothing implemented directly if/when needed |
| [MeshLab](https://github.com/cnr-isti-vclab/meshlab) / [vcglib](https://github.com/cnr-isti-vclab/vcglib) | GPL-3 | Reference "Meshmixer-like" mesh-processing toolbox; mesh-domain denoising, Poisson/BPA | libigl (editing) + Open3D (reconstruction) |
| [MCUT](https://github.com/cutdigital/mcut) / Cork | LGPL-3 / GPL-3 (dual, commercial option available) | Mesh cutting/boolean alternative | Manifold (Apache-2.0) |
| `igl::copyleft::*` (subset of the libigl repo itself) | GPL-3 | CGAL-backed boolean code bundled in the same repo as libigl's MPL-2.0 core | Manifold |

## Adding a new dependency

Before adding anything new to the stack: confirm its actual license from the upstream
repo's `LICENSE` file (not a secondhand description), and if it's anything other than
MIT/BSD/Apache-2.0, or MPL-2.0 used strictly unmodified, raise it before vendoring.
