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

## Source-package adjustments

The bundled runtime sources come from the official ICU4C 78.3 archive, with
seven small changes for CRAN compiler diagnostics and platform compatibility:

- `common/locmap.cpp` uses an overlap-safe move when shortening the Windows
  language tags `quz` and `prs`. The upstream `strcat()` call had overlapping
  source and destination ranges.
- `common/unistr.cpp` marks the static destructor-instantiation helper
  `[[maybe_unused]]` instead of suppressing `-Wunused-function`.
- `i18n/decNumber.cpp` leaves the compiler's `-Warray-bounds` diagnostics
  enabled around three upstream decimal routines. Their logic is unchanged.
- `i18n/formattedvalue.cpp` omits the unused `ufmtval_getString()` definition,
  as stringi does. GCC reports a false `-Wreturn-local-addr` diagnostic for
  the upstream implementation.
- `i18n/number_skeletons.cpp` keeps the temporary `UnicodeString` alias alive
  through `CurrencyUnit` construction instead of suppressing
  `-Wdangling-pointer`.
- `i18n/collationiterator.h` and `i18n/collationdatabuilder.cpp` finish
  constructing the builder's `CollationData` before binding it to the base
  iterator. The upstream constructor read that derived member too early.
- `i18n/windtfmt.cpp` and `i18n/winnmfmt.cpp` skip the optional
  `ResolveLocaleName()` lookup on 32-bit Windows. Rtools40 does not provide the
  symbol, and ICU already falls back to the Windows user locale when the lookup
  is unavailable. This follows the fix for stringi issue 501.

The platform-specific optimization and macro-state pragmas remain unchanged;
they do not suppress compiler diagnostics.

ICU's deprecation attribute is disabled only while compiling bundled ICU
objects. ICU 78.3 still calls deprecated public entry points inside its own
implementation, and some replacement APIs call these older routines too.
Charr's objects retain the annotations, and other compiler diagnostics remain
enabled.
