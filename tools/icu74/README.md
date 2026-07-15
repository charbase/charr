# Charr's ICU 74.1 data

`inst/icu/icudt74l.dat` is rebuilt from the official ICU4C 74.1 source and
source-data releases. It is not copied from stringi and the build never uses
system ICU headers, libraries, or data.

The checked-in file is currently the complete ICU 74.1 archive. That is the
correct baseline for the complete copied stringi backend: collation, boundary
analysis, case mapping, conversion, wrapping, regular expressions, and other
services reach different portions of ICU data. The former 14-entry archive was
proved only for charr's deleted regex-detection prototype and is not evidence
of sufficiency for this backend.

Whole-backend minimization is deliberately deferred until the copied backend
passes the entire stringr suite and its reachable operations are stable. At
that point every retained data entry must have both a source-level reachability
argument and a removal canary covering all 67 stringr-facing entry points,
including nested calls. Missing data must fail the proof; it must never cause a
route to installed stringi.

## Rebuild

Requirements are a C/C++ toolchain, Python 3, `make` or `gmake`, `unzip`, and
either `curl` or `wget`. Run from the repository root:

```sh
tools/icu74/build-data.sh
```

The script downloads the two pinned official release archives, verifies their
SHA-512 hashes, removes the prebuilt data archive from the source tree, builds
the data from raw sources, and verifies the resulting SHA-256. Set
`ICU_SOURCE_ARCHIVE` and `ICU_DATA_ARCHIVE` to paths of existing downloads for
an offline build. `ICU_WORK_DIR`, `ICU_KEEP_WORK=1`, and `JOBS` control the
temporary build directory and parallelism.

The reproducible result is 30,783,664 bytes with SHA-256:

```
bdbdad8d28c7d178e9506a75614e070a2a3310b89618f1c753349906a4aad454
```

That byte sequence is also identical to ICU 74.1's official general archive
and stringi's ICU 74.1 archive. Equality is a verification observation, not the
source of charr's checked-in file.
