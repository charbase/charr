#!/usr/bin/env Rscript

# Rebuild charr's initial backend-altrep scaffold from the exact stringi source
# snapshot recorded here. The copied implementation intentionally still uses
# stringi's ordinary STRSXP containers; charport/ALTREP adaptation comes later.

STRINGI_COMMIT <- "19e9586ba39b3320df49355e32bd18d74ed6098f"

R_EXCLUDE <- c(
  "install.R",          # stringi's package installer, not runtime code
  "stringi_package.R",  # stringi's package-level roxygen marker
  "internal_test.R"     # stringi's private test-only entry points
)

rename_backend <- function(x) {
  # A copied wrapper must call another copied wrapper locally. Leaving the
  # namespace qualifier would ask installed stringi for the renamed ci_* name.
  x <- gsub("stringi:::", "", x, fixed = TRUE)
  x <- gsub("stringi::", "", x, fixed = TRUE)
  gsub("stri_", "ci_", x, fixed = TRUE)
}

write_copy <- function(from, to, comment, strip_roxygen = FALSE) {
  lines <- readLines(from, warn = FALSE)
  lines <- rename_backend(lines)
  if (strip_roxygen) {
    lines <- sub("^#'", "#", lines)
  }
  while (length(lines) && !nzchar(lines[[length(lines)]])) {
    lines <- lines[-length(lines)]
  }
  writeLines(c(comment, lines), to, useBytes = TRUE)
}

patch_registration <- function(path) {
  lines <- readLines(path, warn = FALSE)

  include_at <- grep('^#include "ci_stringi[.]h"$', lines)
  stopifnot(length(include_at) == 1L)
  lines <- append(lines, '#include "../charr_icu.h"', after = include_at)

  null_at <- grep("^    \\{NULL,\\s+NULL,\\s+0\\}$", lines)
  stopifnot(length(null_at) == 1L)
  helpers <- c(
    '    STRI__MK_CALL("C_charr_abi_ok",                     C_charr_abi_ok,                   0),',
    '    STRI__MK_CALL("C_charr_icu_init",                   C_charr_icu_init,                 1),',
    '    STRI__MK_CALL("C_charr_icu_version",                C_charr_icu_version,              0),',
    '    STRI__MK_CALL("C_charr_icu_ok",                     C_charr_icu_ok,                   0),',
    '    STRI__MK_CALL("C_charr_icu_smoke",                  C_charr_icu_smoke,                0),'
  )
  lines <- append(lines, helpers, after = null_at - 1L)

  lines <- sub("R_init_stringi", "R_init_charr", lines, fixed = TRUE)
  lines <- sub("R_unload_stringi", "R_unload_charr", lines, fixed = TRUE)
  lines <- sub(
    'R_RegisterCCallable("stringi"',
    'R_RegisterCCallable("charr"',
    lines,
    fixed = TRUE
  )

  init_at <- grep('extern "C" void R_init_charr', lines, fixed = TRUE)
  register_at <- grep("^    R_registerRoutines", lines)
  stopifnot(length(init_at) == 1L, length(register_at) == 1L)
  # ICU common data is installed from R's .onLoad, after the DLL has
  # registered C_charr_icu_init. Do not run stringi's DLL-time u_init or data
  # directory discovery here.
  lines <- lines[-seq.int(init_at + 2L, register_at - 1L)]

  writeLines(lines, path, useBytes = TRUE)
}

main <- function(args) {
  if (length(args) != 1L) {
    stop("usage: Rscript tools/import-stringi-backend.R /path/to/stringi")
  }

  checkout <- normalizePath(args[[1]], mustWork = TRUE)
  at <- system2(
    "git", c("-C", checkout, "rev-parse", "HEAD"),
    stdout = TRUE
  )
  if (!identical(at, STRINGI_COMMIT)) {
    stop(
      "stringi checkout is at ", at, ", expected ", STRINGI_COMMIT
    )
  }

  r_source <- file.path(checkout, "R")
  cpp_source <- file.path(checkout, "src")
  stopifnot(dir.exists(r_source), dir.exists(cpp_source))

  old_r <- list.files(
    "R", pattern = "^altrep_backend-.*[.]R$", full.names = TRUE
  )
  if (length(old_r)) unlink(old_r)
  unlink(file.path("src", "altrep_backend"), recursive = TRUE)
  dir.create(file.path("src", "altrep_backend"))

  r_files <- setdiff(
    list.files(r_source, pattern = "[.]R$", full.names = FALSE),
    R_EXCLUDE
  )
  for (name in r_files) {
    write_copy(
      file.path(r_source, name),
      file.path("R", paste0("altrep_backend-", name)),
      paste0(
        "# Copied from stringi ", STRINGI_COMMIT,
        "; stri_* renamed to ci_*."
      ),
      strip_roxygen = TRUE
    )
  }

  generated_r <- file.path("R", paste0("altrep_backend-", r_files))
  qualified_ci <- vapply(generated_r, function(path) {
    any(grepl("stringi::+ci_", readLines(path, warn = FALSE)))
  }, logical(1))
  if (any(qualified_ci)) {
    stop(
      "copied backend retains qualified ci_* calls in: ",
      paste(basename(generated_r[qualified_ci]), collapse = ", ")
    )
  }

  cpp_manifest <- scan(
    file.path(cpp_source, "stri_cpp.txt"),
    what = character(), quiet = TRUE
  )
  cpp_manifest <- setdiff(cpp_manifest, "\\")
  headers <- list.files(
    cpp_source, pattern = "^stri_.*[.]h$", full.names = FALSE
  )
  cpp_files <- c(cpp_manifest, headers)

  for (name in cpp_files) {
    out_name <- rename_backend(name)
    write_copy(
      file.path(cpp_source, name),
      file.path("src", "altrep_backend", out_name),
      paste0(
        "// Copied from stringi ", STRINGI_COMMIT,
        "; stri_* renamed to ci_*.",
        " See inst/COPYRIGHTS."
      )
    )
  }
  patch_registration(
    file.path("src", "altrep_backend", "ci_stringi.cpp")
  )

  provenance <- c(
    "# Copied stringi backend",
    "",
    paste0("Upstream commit: `", STRINGI_COMMIT, "`."),
    "",
    "The R and C++ sources in this backend are copied from stringi and",
    "mechanically rename the `stri_*` implementation namespace to `ci_*`.",
    "They retain stringi's BSD-3-Clause notices; see `inst/COPYRIGHTS`.",
    "",
    "This is the semantic scaffold. It initially uses stringi's ordinary",
    "materializing containers. Later work replaces those containers with",
    "charport readers and charvec result builders without changing semantics."
  )
  writeLines(
    provenance,
    file.path("src", "altrep_backend", "UPSTREAM.md")
  )

  cat(
    "copied ", length(r_files), " R files and ", length(cpp_files),
    " C++ source/header files from stringi ", STRINGI_COMMIT, "\n",
    sep = ""
  )
}

main(commandArgs(trailingOnly = TRUE))
