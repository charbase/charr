# Charr's ICU 78.3 data

The official ICU4C 78.3 source archive contains the complete little-endian
common data archive at `icu/source/data/in/icudt78l.dat`. Charr trims that
archive to the ICU services reachable through its public API, compresses it
for the source package, and decompresses it during installation.

The full official archive is 33,107,232 bytes with SHA-256:

```
d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b
```

The checked-in trimmed archive decompresses to 13,478,992 bytes with SHA-256
`0f40045bffbcc40bf53e05198282af67a5cda820388a1d9b04ed36f1dcf55338`.
Its source-package artifact is 3,846,908 bytes with SHA-256
`2682cdd764b5e2b36d81bc9f6292035b2cb340896603e3e8ba3197fbada2c0cb`.

## Rebuild

From the repository root:

```sh
tools/icu78/extract-full-data.sh
Rscript tools/trim-icudt.R local/icu78/icudt78l.dat
```

The extraction helper downloads the pinned official source archive when
needed, verifies its SHA-512, extracts the full data archive, and verifies the
full-data SHA-256. Set `ICU_SOURCE_ARCHIVE` to use an existing source archive
without network access. `ICU_CACHE_DIR` controls the download cache.

`tools/trim-icudt.R` documents the keep/drop policy and its required canaries.
For the ICU 78.3 import, the full and trimmed archives passed the same service
canaries, and the wider comparison produced no trimming-specific failure.
