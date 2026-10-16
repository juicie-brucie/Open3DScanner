# Recon cache

Local, gitignored staging area (`.recon-cache/`) for the inputs and outputs of the
ABI recovery pass. Machine-specific, in the same spirit as
[`rea-ghidra-setup.md`](rea-ghidra-setup.md).

## `vendor/` — symlinks to the shipped binaries

| Link | Target |
| --- | --- |
| `liborbbec_scan.3.5.3.dylib` | `/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.3.5.3.dylib` |
| `libOrbbecSDK.1.7.1.dylib` | `/Library/Creality/libOrbbecSDK.1.7.1.dylib` |
| `GameAssembly.dylib` | `/Applications/CrealityScan.app/Contents/Frameworks/GameAssembly.dylib` |
| `global-metadata.dat` | `/Applications/CrealityScan.app/Contents/Resources/Data/il2cpp_data/Metadata/global-metadata.dat` |

They are **symlinks, not copies**. The originals stay put and nothing ~209 MB is
duplicated into the tree. The primary target is
`liborbbec_scan.3.5.3.dylib` — a fat Mach-O (x86_64 + arm64), about 72 MB of `__TEXT`
with ~90k functions. `liborbbec_scan.dylib` and `liborbbec_scan.3.5.0.dylib` in the
bundle are symlinks to it, so only one version is actually installed.

The version is **pinned deliberately**: reproducible analysis means analysis of a
known artifact. If CrealityScan updates, re-point the links at the new version on
purpose, and note the change.

## Snapshots

REA writes `<target>.analysis.json` here (at the top of `.recon-cache/`). Rule:
never re-run analysis for a snapshot that already exists. A second Ghidra JVM on
this dylib is the most expensive mistake in the workflow.

## arm64-only slice

The target is fat. To produce an arm64-only slice for analysis:

```sh
lipo -thin arm64 .recon-cache/vendor/liborbbec_scan.3.5.3.dylib \
     -output .recon-cache/liborbbec_scan_arm64.dylib
```

## The builder cannot see any of this

`openscanner/builder` is denied `read` on `.recon-cache/**` **and** on the real
paths `/Applications/CrealityScan.app/**` and `/Library/Creality/**`. The second pair
matters because the symlinks point outside the project — denying only the link path
would leave the target readable. `rea_*` is denied outright, so Ghidra can only ever
be launched from a recon session.
