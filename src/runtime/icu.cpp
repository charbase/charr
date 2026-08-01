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

CHARR_CXX_HELPER bool initialize_icu_native(const char* path) noexcept
{
#if CHARR_BUNDLED_ICU
  if (icu_data == nullptr) {
    if (path == nullptr)
      return false;

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
      return false;

    if (std::fseek(file, 0, SEEK_END) != 0) {
      std::fclose(file);
      return false;
    }
    const long size = std::ftell(file);
    if (size <= 0) {
      std::fclose(file);
      return false;
    }
    std::rewind(file);

    void* data = std::malloc(static_cast<std::size_t>(size));
    if (data == nullptr) {
      std::fclose(file);
      return false;
    }
    const std::size_t read = std::fread(
      data, 1, static_cast<std::size_t>(size), file
    );
    std::fclose(file);
    if (read != static_cast<std::size_t>(size)) {
      std::free(data);
      return false;
    }

    UErrorCode data_status = U_ZERO_ERROR;
    udata_setCommonData(data, &data_status);
    if (U_FAILURE(data_status)) {
      std::free(data);
      return false;
    }

    // ICU retains the accepted data for the rest of the process.
    icu_data = data;
  }
#else
  (void)path;
#endif

  UErrorCode status = U_ZERO_ERROR;
  u_init(&status);
  if (U_FAILURE(status))
    return false;

  icu_initialized = true;
  return true;
}

extern "C" {

SEXP C_charr_abi_ok(void) noexcept {
  return Rf_ScalarLogical(charport::check_abi() ? TRUE : FALSE);
}

SEXP C_charr_icu_init(SEXP path) noexcept {
  if (icu_initialized)
    return Rf_ScalarLogical(TRUE);

#if CHARR_BUNDLED_ICU
  if (TYPEOF(path) != STRSXP || XLENGTH(path) != 1)
    return Rf_ScalarLogical(FALSE);
  const char* file = Rf_translateChar(STRING_ELT(path, 0));
#else
  const char* file = nullptr;
  (void)path;
#endif

  return Rf_ScalarLogical(initialize_icu_native(file) ? TRUE : FALSE);
}

SEXP C_charr_icu_bundled(void) noexcept {
  return Rf_ScalarLogical(CHARR_BUNDLED_ICU ? TRUE : FALSE);
}

SEXP C_charr_icu_info(void) noexcept {
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

} // extern "C"

} } } // namespace charr::runtime::icu
