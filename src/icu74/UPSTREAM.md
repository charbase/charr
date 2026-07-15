# ICU4C 74.1 and stringi provenance

The upstream sources and data come from the official ICU 74.1 release:

- Release: <https://github.com/unicode-org/icu/releases/tag/release-74-1>
- Source archive: `icu4c-74_1-src.tgz`
- Source archive SHA-512:
  `32c28270aa5d94c58d2b1ef46d4ab73149b5eaa2e0621d4a4c11597b71d146812f5e66db95f044e8aaa11b94e99edd4a48ab1aa8efbe3d72a73870cd56b564c2`
- Data archive: `icu4c-74_1-data.zip`
- Data archive SHA-512:
  `7e411e0f190428588a872a3c65477eed60063f6fef0c84d09822c3b6b7ebba3c956a484cd91da1e26f93360f4b3e08da6ba226b674f2d139c44f86fdb2ac90a3`

Charr uses the vendoring layout and portability patches maintained by stringi,
as recorded in stringi commit
`19e9586ba39b3320df49355e32bd18d74ed6098f` (v1.8.7 development tree).
In particular, stringi's `.devel/icu_upgrade_patches-74.md` documents the
upstream import, directory layout, source manifests, data acquisition, and the
cross-platform patching pass. Stringi's contributions are BSD-3-Clause; see
`inst/COPYRIGHTS`.

`common/`, `i18n/`, and `stubdata/` contain stringi's corresponding vendored
C++ sources and private headers. `unicode/` combines the public headers from
ICU's `icu/source/common/unicode/` and `icu/source/i18n/unicode/`, matching the
installed-header layout.

The runtime data file is installed as `inst/icu/icudt74l.dat`. It is rebuilt
from the official raw source-data release, not copied from stringi or from the
prebuilt archive in ICU's source tarball. `tools/icu74/build-data.sh` performs
the pinned, reproducible build. The complete archive is retained while the
complete copied backend is established; whole-backend minimization and its
removal proof are deferred until that surface is stable.

The resulting archive is 30,783,664 bytes. Its SHA-256 is
`bdbdad8d28c7d178e9506a75614e070a2a3310b89618f1c753349906a4aad454`.

Charr does not add patches inside the ICU source tree. Private symbol suffixing
and static-build configuration are supplied by `src/Makevars` and
`src/Makevars.win`. The adjacent `src/uconfig_local.h`, adapted from stringi,
routes ICU's otherwise-fatal internal invariant macros through R's error
handling instead of calling `abort()`.
