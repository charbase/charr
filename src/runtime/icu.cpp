// charr links its own ICU4C, never stringi's. In bundle mode (configure did not
// select an allowlisted system candidate; always on Windows) that is pristine
// ICU4C 78.3 runtime source (Unicode, Inc. and others; Unicode License v3, see
// src/icu78/LICENSE): symbols are suffixed ..._78_charr and the packaged
// trimmed icudt78l.dat is fed in via udata_setCommonData at load
// time. In system mode (CHARR_BUNDLED_ICU=0, set by configure after a
// pkg-config probe) charr links the system ICU4C, which locates its own
// complete data archive, so no data file ships or loads.

#include "icu.h"

#ifndef CHARR_BUNDLED_ICU
#define CHARR_BUNDLED_ICU 1
#endif

#include <cstdio>
#include <cstdlib>

#include <unicode/uversion.h>
#include <unicode/icudataver.h>
#include <unicode/uchar.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/ustring.h>
#include <unicode/uregex.h>

#ifndef U_CHARSET_IS_UTF8
#define U_CHARSET_IS_UTF8 0
#endif
#ifndef UCONFIG_NO_CONVERSION
#define UCONFIG_NO_CONVERSION 0
#endif
#ifndef UCONFIG_NO_NORMALIZATION
#define UCONFIG_NO_NORMALIZATION 0
#endif
#ifndef UCONFIG_NO_BREAK_ITERATION
#define UCONFIG_NO_BREAK_ITERATION 0
#endif
#ifndef UCONFIG_NO_COLLATION
#define UCONFIG_NO_COLLATION 0
#endif
#ifndef UCONFIG_NO_REGULAR_EXPRESSIONS
#define UCONFIG_NO_REGULAR_EXPRESSIONS 0
#endif

namespace charr { namespace runtime { namespace icu {

// The data buffer handed to udata_setCommonData must outlive every ICU call,
// i.e. live for the process. Loaded once; a second successful init would leak,
// but .onLoad calls this at most once.
#if CHARR_BUNDLED_ICU
void* icu_data = nullptr;
#endif
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

extern "C" {

SEXP C_charr_abi_ok(void) {
  return Rf_ScalarLogical(charport::check_abi() ? TRUE : FALSE);
}

SEXP C_charr_icu_init(SEXP path) {
  if (icu_initialized) return Rf_ScalarLogical(TRUE);

#if CHARR_BUNDLED_ICU
  void* buf = nullptr;
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
  // ICU retains this pointer. Keep it for the process even if later
  // initialization fails; freeing it would leave ICU with a dangling source.
  icu_data = buf;
#else
  (void)path;  // the system ICU locates its own complete data archive
  UErrorCode status = U_ZERO_ERROR;
#endif

  u_init(&status);
  if (U_FAILURE(status)) return Rf_ScalarLogical(FALSE);

  icu_initialized = true;
  return Rf_ScalarLogical(TRUE);
}

SEXP C_charr_icu_bundled(void) {
  return Rf_ScalarLogical(CHARR_BUNDLED_ICU ? TRUE : FALSE);
}

SEXP C_charr_icu_info(void) {
  if (!icu_initialized) Rf_error("charr's ICU is not initialized");

  UErrorCode status = U_ZERO_ERROR;
  UVersionInfo runtime_version;
  UVersionInfo data_version;
  UVersionInfo unicode_version;
  char runtime_string[U_MAX_VERSION_STRING_LENGTH];
  char data_string[U_MAX_VERSION_STRING_LENGTH];
  char unicode_string[U_MAX_VERSION_STRING_LENGTH];

  u_getVersion(runtime_version);
  u_getDataVersion(data_version, &status);
  u_getUnicodeVersion(unicode_version);
  if (U_FAILURE(status)) {
    Rf_error("could not read charr's ICU data version");
  }
  u_versionToString(runtime_version, runtime_string);
  u_versionToString(data_version, data_string);
  u_versionToString(unicode_version, unicode_string);

  const int size = 11;
  const char* field_names[size] = {
    "mode",
    "headers_version",
    "runtime_version",
    "data_version",
    "unicode_version",
    "u_charset_is_utf8",
    "no_conversion",
    "no_normalization",
    "no_break_iteration",
    "no_collation",
    "no_regular_expressions"
  };
  const int flags[6] = {
    U_CHARSET_IS_UTF8,
    UCONFIG_NO_CONVERSION,
    UCONFIG_NO_NORMALIZATION,
    UCONFIG_NO_BREAK_ITERATION,
    UCONFIG_NO_COLLATION,
    UCONFIG_NO_REGULAR_EXPRESSIONS
  };

  SEXP output = PROTECT(Rf_allocVector(VECSXP, size));
  SEXP names = PROTECT(Rf_allocVector(STRSXP, size));
  for (int i = 0; i < size; ++i) {
    SET_STRING_ELT(names, i, Rf_mkChar(field_names[i]));
  }
  SET_VECTOR_ELT(
    output, 0,
    Rf_mkString(CHARR_BUNDLED_ICU ? "bundle" : "system")
  );
  SET_VECTOR_ELT(output, 1, Rf_mkString(U_ICU_VERSION));
  SET_VECTOR_ELT(output, 2, Rf_mkString(runtime_string));
  SET_VECTOR_ELT(output, 3, Rf_mkString(data_string));
  SET_VECTOR_ELT(output, 4, Rf_mkString(unicode_string));
  for (int i = 0; i < 6; ++i) {
    SET_VECTOR_ELT(output, i + 5, Rf_ScalarLogical(flags[i] ? TRUE : FALSE));
  }
  Rf_setAttrib(output, R_NamesSymbol, names);
  UNPROTECT(2);
  return output;
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

} } } // namespace charr::runtime::icu
