# ICU4C 78.3 provenance

The runtime source and data come from the official ICU4C 78.3 release:

- Release: <https://github.com/unicode-org/icu/releases/tag/release-78.3>
- Source archive: `icu4c-78.3-sources.tgz`
- Source archive SHA-256:
  `3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0`
- Source archive SHA-512:
  `04a49455e1489030c520a4bfd2664fa2171e7938d08f2acdbbcb1fda976639fd8b1f0704f2eec89ba59a7b6d118ceaab6ec5a096e40d9085a0895d91ce225245`

`common/`, `i18n/`, and `stubdata/` contain every `.cpp` and `.h` runtime file
from the corresponding official source directories. `unicode/` combines the
public headers from `common/unicode/` and `i18n/unicode/`. These 965 files are
byte-identical to the official archive; charr applies no source patches.

Package-specific static-build settings are supplied by `src/Makevars`,
`src/Makevars.win`, and `src/uconfig_local.h`. Bundled symbols are suffixed
`..._78_charr`. `DECNUMDIGITS=4` is a build define rather than a modification
to ICU's `decNumber.h`.

The full little-endian data archive in the official source release is
`icu/source/data/in/icudt78l.dat`. It is 33,107,232 bytes with SHA-256
`d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b`.
`tools/trim-icudt.R` reduces it to the services reachable through charr's API.
The resulting 13,478,992-byte archive has SHA-256
`0f40045bffbcc40bf53e05198282af67a5cda820388a1d9b04ed36f1dcf55338`;
the checked-in `data/icudt78l.dat.xz` has SHA-256
`2682cdd764b5e2b36d81bc9f6292035b2cb340896603e3e8ba3197fbada2c0cb`.

The full and trimmed 78.3 archives passed the same ICU service canaries and
produced no trimming-specific failure in the wider backend comparison.
