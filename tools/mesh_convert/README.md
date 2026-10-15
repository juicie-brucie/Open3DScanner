# mesh_convert

Mesh format conversion CLI for OpenScanner, built on
[meshio](https://github.com/nschloe/meshio) (MIT licensed, pure Python, no native
build step). Converts a scanned mesh between STL, OBJ, PLY, glTF/GLB, VTK/VTU, and
everything else meshio reads/writes — the formats a scan → clean → print/export
pipeline actually needs to move between.

## Install

```sh
pip install -r requirements.txt
```

## Usage

```sh
python mesh_convert.py scan.ply scan.stl
python mesh_convert.py scan.ply scan.stl --binary   # force binary STL
python mesh_convert.py model.obj model.glb
python mesh_convert.py --list-formats
```

## Why meshio and not Assimp/MeshLab/etc.

meshio is MIT-licensed and pure Python with no compiled dependency beyond numpy, which
keeps this tool usable from any part of the stack without a native build step. See
`THIRD_PARTY_LICENSES.md` at the repo root and `docs/architecture/mesh-editing-stack.md`
for why GPL-licensed alternatives (MeshLab, vcglib, CGAL-based converters) are
deliberately not used here.
