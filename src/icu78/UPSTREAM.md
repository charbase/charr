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
public headers from `common/unicode/` and `i18n/unicode/`. These 965 files were
imported from the official archive. Charr carries eleven source adjustments for
CRAN compiler diagnostics and platform compatibility, listed under
"Source-package adjustments" below.

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

## Source-package adjustments

The bundled runtime sources come from the official ICU4C 78.3 archive, with
eleven small changes for CRAN compiler diagnostics and platform compatibility:

- `common/locmap.cpp` uses an overlap-safe move when shortening the Windows
  language tags `quz` and `prs`. The upstream `strcat()` call had overlapping
  source and destination ranges.
- `common/unistr.cpp` marks the static destructor-instantiation helper
  `[[maybe_unused]]` instead of suppressing `-Wunused-function`.
- `i18n/decNumber.cpp` leaves the compiler's `-Warray-bounds` diagnostics
  enabled around three upstream decimal routines. `decGetInt()` returns
  `BADINT` when a partial unit's digit count is outside `1 .. DECDPUN-1`.
  ICU defines `DECDPUN` as 1, so that branch is only reached when negating
  the exponent overflows; defined exponents still take the whole-unit path.
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
- `common/putil.cpp` copies one byte fewer than the code-page buffer in
  `getCodepageFromPOSIXID()` and still writes the terminating NUL. The
  previous `strncpy` bound was the destination size, which GCC reports as
  `-Wstringop-truncation`. A shorter source still zero-fills, and a longer
  source still keeps the same prefix.
- `common/udata.cpp` declares the linked-in data symbol with stubdata's
  `ICU_Data_Header` type. The old `DataHeader` declaration was a different
  type for the same object and tripped `-Wodr` under LTO. Call sites that
  need a `DataHeader *` cast the address; the object bytes are unchanged.
- `common/rbbidata.h`, `common/rbbi.cpp`, `common/rbbitblb.cpp`, and
  `common/rbbidata.cpp` read and write `RBBIStateTableRowT::fNextState`
  with pointer arithmetic on its first element. The array is a trailing
  struct-hack member of declared length 1, allocated to the category count.
  A subscript makes `-fsanitize=bounds` report that declared length.
- `i18n/regeximp.h` and `i18n/rematch.cpp` use the same pointer arithmetic
  for every `REStackFrame::fExtra` access, including addresses of elements.
  The frame is allocated to `fFrameSize`; the declared length is 1.

The platform-specific optimization and macro-state pragmas remain unchanged;
they do not suppress compiler diagnostics.

ICU's deprecation attribute is disabled only while compiling bundled ICU
objects. ICU 78.3 still calls deprecated public entry points inside its own
implementation, and some replacement APIs call these older routines too.
Charr's objects retain the annotations, and other compiler diagnostics remain
enabled.
