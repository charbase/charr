// charr links its own ICU4C, never stringi's. In bundle mode (configure found
// no usable system ICU; always on Windows) that is the vendored ICU4C 74.1
// (Unicode, Inc. and others; Unicode License v3, see src/icu74/LICENSE) using
// stringi's source layout and portability patches (Marek Gagolewski;
// BSD-3-Clause, see inst/COPYRIGHTS): symbols are suffixed ..._74_charr and
// the packaged trimmed icudt74l.dat is fed in via udata_setCommonData at load
// time. In system mode (CHARR_BUNDLED_ICU=0, set by configure after a
// pkg-config probe) charr links the system ICU4C, which locates its own
// complete data archive, so no data file ships or loads.

#include "charr_icu.h"

#ifndef CHARR_BUNDLED_ICU
#define CHARR_BUNDLED_ICU 1
#endif

#include <cstdio>
#include <cstdlib>

#include <unicode/uversion.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>
#include <unicode/uregex.h>

// Copied from stringi as part of src/altrep_backend/ci_prepare_arg.cpp.
bool ci__is_C_locale(const char* str);

namespace {

// The data buffer handed to udata_setCommonData must outlive every ICU call,
// i.e. live for the process. Loaded once; a second successful init would leak,
// but .onLoad calls this at most once.
void* icu_data = nullptr;
bool icu_initialized = false;

// Compile a UTF-8 regex pattern and test whether it matches anywhere in a
// UTF-8 subject. Returns false on any ICU error (including data not loaded).
bool regex_matches(const char* pattern, const char* subject) {
  UErrorCode status = U_ZERO_ERROR;

  UChar upat[64];
  UChar usub[64];
  int32_t plen = 0;
  int32_t slen = 0;
  u_strFromUTF8(upat, 64, &plen, pattern, -1, &status);
  u_strFromUTF8(usub, 64, &slen, subject, -1, &status);
  if (U_FAILURE(status)) return false;

  URegularExpression* re = uregex_open(upat, plen, 0, nullptr, &status);
  if (U_FAILURE(status)) return false;

  uregex_setText(re, usub, slen, &status);
  UBool found = U_FAILURE(status) ? FALSE : uregex_find(re, 0, &status);
  uregex_close(re);
  if (U_FAILURE(status)) return false;
  return found == TRUE;
}

} // namespace

extern "C" {

SEXP C_charr_abi_ok(void) {
  return Rf_ScalarLogical(charport::check_abi() ? TRUE : FALSE);
}

SEXP C_charr_icu_init(SEXP path) {
  if (icu_initialized) return Rf_ScalarLogical(TRUE);

  void* buf = nullptr;
#if CHARR_BUNDLED_ICU
  if (TYPEOF(path) != STRSXP || XLENGTH(path) != 1) return Rf_ScalarLogical(FALSE);

  const char* file = Rf_translateChar(STRING_ELT(path, 0));
  FILE* f = std::fopen(file, "rb");
  if (!f) return Rf_ScalarLogical(FALSE);

  if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return Rf_ScalarLogical(FALSE); }
  long size = std::ftell(f);
  if (size <= 0) { std::fclose(f); return Rf_ScalarLogical(FALSE); }
  std::rewind(f);

  buf = std::malloc(static_cast<size_t>(size));
  if (!buf) { std::fclose(f); return Rf_ScalarLogical(FALSE); }
  size_t got = std::fread(buf, 1, static_cast<size_t>(size), f);
  std::fclose(f);
  if (got != static_cast<size_t>(size)) { std::free(buf); return Rf_ScalarLogical(FALSE); }

  UErrorCode status = U_ZERO_ERROR;
  udata_setCommonData(buf, &status);
  if (U_FAILURE(status)) { std::free(buf); return Rf_ScalarLogical(FALSE); }
#else
  (void)path;  // the system ICU locates its own complete data archive
  UErrorCode status = U_ZERO_ERROR;
#endif

  // Match stringi's initialization semantics now that the copied backend can
  // reach the complete ICU surface, including converters and locale data.
  u_init(&status);
  if (U_FAILURE(status)) {
    u_cleanup();
    std::free(buf);
    return Rf_ScalarLogical(FALSE);
  }

  if (ci__is_C_locale(uloc_getDefault())) {
    status = U_ZERO_ERROR;
    uloc_setDefault("en_US_POSIX", &status);
    if (U_FAILURE(status)) {
      u_cleanup();
      std::free(buf);
      return Rf_ScalarLogical(FALSE);
    }
  }

  icu_data = buf;  // bundle mode: keep alive for the process (else nullptr)
  icu_initialized = true;
  return Rf_ScalarLogical(TRUE);
}

SEXP C_charr_icu_bundled(void) {
  return Rf_ScalarLogical(CHARR_BUNDLED_ICU ? TRUE : FALSE);
}

SEXP C_charr_icu_version(void) {
  return Rf_mkString(U_ICU_VERSION);
}

SEXP C_charr_icu_ok(void) {
  return Rf_ScalarLogical(icu_initialized ? TRUE : FALSE);
}

SEXP C_charr_icu_smoke(void) {
  if (!icu_initialized) return Rf_ScalarLogical(FALSE);
  // "a.c" ~ "abc" proves the regex engine runs; "\p{L}" ~ "é" proves the
  // character-property DATA actually loaded, not just the code.
  bool ok = regex_matches("a.c", "abc") &&
            regex_matches("\\p{L}", "\xc3\xa9");  // "é" in UTF-8
  return Rf_ScalarLogical(ok ? TRUE : FALSE);
}

} // extern "C"
