# charr-owned file: tools/import-upstream.R must not rename here.

# The copied stringi wrappers are loaded first because their filenames begin
# with altrep_backend-. Capture the stringr-facing entry points, then replace
# them in charr's namespace with the small R dispatch layer below.
.charr_backend_map <- c(
  ci_detect_fixed = "stri_detect_fixed",
  ci_startswith_fixed = "stri_startswith_fixed",
  ci_endswith_fixed = "stri_endswith_fixed",
  ci_count_fixed = "stri_count_fixed",
  ci_locate_first_fixed = "stri_locate_first_fixed",
  ci_locate_all_fixed = "stri_locate_all_fixed",
  ci_extract_first_fixed = "stri_extract_first_fixed",
  ci_extract_all_fixed = "stri_extract_all_fixed",
  ci_replace_first_fixed = "stri_replace_first_fixed",
  ci_replace_all_fixed = "stri_replace_all_fixed",
  ci_split_fixed = "stri_split_fixed",
  ci_sub = "stri_sub",
  ci_sub_all = "stri_sub_all",
  ci_length = "stri_length",
  ci_c = "stri_c",
  ci_flatten = "stri_flatten",
  ci_dup = "stri_dup",
  ci_trim_left = "stri_trim_left",
  ci_trim_right = "stri_trim_right",
  ci_trim_both = "stri_trim_both",
  ci_replace_na = "stri_replace_na",
  ci_detect_regex = "stri_detect_regex",
  ci_count_regex = "stri_count_regex",
  ci_locate_first_regex = "stri_locate_first_regex",
  ci_locate_all_regex = "stri_locate_all_regex",
  ci_extract_first_regex = "stri_extract_first_regex",
  ci_extract_all_regex = "stri_extract_all_regex",
  ci_replace_first_regex = "stri_replace_first_regex",
  ci_replace_all_regex = "stri_replace_all_regex",
  ci_split_regex = "stri_split_regex",
  ci_match_first_regex = "stri_match_first_regex",
  ci_match_all_regex = "stri_match_all_regex",
  ci_detect_coll = "stri_detect_coll",
  ci_startswith_coll = "stri_startswith_coll",
  ci_endswith_coll = "stri_endswith_coll",
  ci_count_coll = "stri_count_coll",
  ci_locate_first_coll = "stri_locate_first_coll",
  ci_locate_all_coll = "stri_locate_all_coll",
  ci_extract_first_coll = "stri_extract_first_coll",
  ci_extract_all_coll = "stri_extract_all_coll",
  ci_replace_first_coll = "stri_replace_first_coll",
  ci_replace_all_coll = "stri_replace_all_coll",
  ci_split_coll = "stri_split_coll",
  ci_order = "stri_order",
  ci_rank = "stri_rank",
  ci_cmp_equiv = "stri_cmp_equiv",
  ci_duplicated = "stri_duplicated",
  ci_trans_tolower = "stri_trans_tolower",
  ci_trans_toupper = "stri_trans_toupper",
  ci_trans_totitle = "stri_trans_totitle",
  ci_count_boundaries = "stri_count_boundaries",
  ci_locate_first_boundaries = "stri_locate_first_boundaries",
  ci_locate_all_boundaries = "stri_locate_all_boundaries",
  ci_extract_first_boundaries = "stri_extract_first_boundaries",
  ci_extract_all_boundaries = "stri_extract_all_boundaries",
  ci_split_boundaries = "stri_split_boundaries",
  ci_wrap = "stri_wrap",
  ci_pad_left = "stri_pad_left",
  ci_pad_right = "stri_pad_right",
  ci_pad_both = "stri_pad_both",
  ci_width = "stri_width",
  ci_escape_unicode = "stri_escape_unicode",
  ci_conv = "stri_conv",
  ci_opts_fixed = "stri_opts_fixed",
  ci_opts_regex = "stri_opts_regex",
  ci_opts_collator = "stri_opts_collator",
  ci_opts_brkiter = "stri_opts_brkiter"
)

.charr_replacement_map <- c(
  "ci_sub<-" = "stri_sub<-",
  "ci_sub_all<-" = "stri_sub_all<-"
)

.charr_state <- new.env(parent = emptyenv())
.charr_state$altrep <- FALSE
.charr_state$nthreads <- 1L
.charr_state$altrep_calls <- 0

.altrep_backend <- new.env(parent = emptyenv())
for (.ci_name in c(names(.charr_backend_map), names(.charr_replacement_map))) {
  if (!exists(.ci_name, inherits = FALSE)) {
    stop("copied stringi backend is missing ", .ci_name, call. = FALSE)
  }
  assign(.ci_name, get(.ci_name, inherits = FALSE), .altrep_backend)
}

.make_stringi_call <- function(stri_name) {
  eval(substitute(
    function(...) STRINGI_FUNCTION(...),
    list(STRINGI_FUNCTION = as.name(stri_name))
  ), envir = asNamespace("stringi"))
}

.make_altrep_call <- function(ci_name, stri_name) {
  call_env <- new.env(parent = baseenv())
  assign(stri_name, .altrep_backend[[ci_name]], envir = call_env)
  eval(substitute(
    function(...) STRINGI_FUNCTION(...),
    list(STRINGI_FUNCTION = as.name(stri_name))
  ), envir = call_env)
}

.make_backend_dispatch <- function(ci_name, stri_name) {
  force(ci_name)
  altrep_call <- .make_altrep_call(ci_name, stri_name)
  stringi_call <- .make_stringi_call(stri_name)
  function(...) {
    if (.charr_state$altrep) {
      .charr_state$altrep_calls <- .charr_state$altrep_calls + 1
      altrep_call(...)
    } else {
      stringi_call(...)
    }
  }
}

.make_backend_replacement_dispatch <- function(ci_name, stri_name) {
  force(ci_name)
  altrep_call <- .make_altrep_call(ci_name, stri_name)
  stringi_call <- .make_stringi_call(stri_name)
  function(..., value) {
    if (.charr_state$altrep) {
      .charr_state$altrep_calls <- .charr_state$altrep_calls + 1
      altrep_call(..., value = value)
    } else {
      stringi_call(..., value = value)
    }
  }
}

.backend_namespace <- environment(.make_backend_dispatch)
for (.ci_name in names(.charr_backend_map)) {
  assign(
    .ci_name,
    .make_backend_dispatch(.ci_name, .charr_backend_map[[.ci_name]]),
    .backend_namespace
  )
}
for (.ci_name in names(.charr_replacement_map)) {
  assign(
    .ci_name,
    .make_backend_replacement_dispatch(
      .ci_name, .charr_replacement_map[[.ci_name]]
    ),
    .backend_namespace
  )
}
rm(.ci_name, .backend_namespace)

#' Select charr's copied backend
#'
#' With the switch off, charr routes its string operations to installed
#' stringi. With it on, the same operations run through charr's private copy
#' of stringi's R and C++ implementation. The copied implementation currently
#' materializes ordinary R character vectors; charport integration comes next.
#'
#' Setting `CHARR_ALTREP=true` before loading charr enables the copied backend.
#'
#' `charr_threads()` records the worker count intended for the later charport
#' implementation. The copied stringi implementation does not use it yet.
#'
#' @param on `NULL` to query, or `TRUE`/`FALSE` to set.
#' @param n `NULL` to query, or a single integer >= 1 to set.
#' @return The current value when querying; the previous value (invisibly)
#'   when setting.
#' @export
charr_altrep <- function(on = NULL) {
  if (is.null(on)) {
    return(.charr_state$altrep)
  }
  if (!is.logical(on) || length(on) != 1L || is.na(on)) {
    cli::cli_abort("{.arg on} must be `TRUE` or `FALSE`.")
  }
  old <- .charr_state$altrep
  .charr_state$altrep <- on
  invisible(old)
}

#' @rdname charr_altrep
#' @export
charr_threads <- function(n = NULL) {
  if (is.null(n)) {
    return(.charr_state$nthreads)
  }
  n <- suppressWarnings(as.integer(n))
  if (length(n) != 1L || is.na(n) || n < 1L) {
    cli::cli_abort("{.arg n} must be a single integer >= 1.")
  }
  old <- .charr_state$nthreads
  .charr_state$nthreads <- n
  invisible(old)
}

# Internal routing canary used only by scaffold tests.
charr_altrep_count <- function() {
  .charr_state$altrep_calls
}

charr_icu_ok <- function() {
  .Call(C_charr_icu_ok)
}

# TRUE when charr was compiled against the vendored ICU 74.1 (with the
# trimmed data archive); FALSE when configure linked the system ICU4C.
charr_icu_bundled <- function() {
  .Call(C_charr_icu_bundled)
}

.onLoad <- function(libname, pkgname) {
  if (!isTRUE(.Call(C_charr_abi_ok))) {
    stop(
      "charr was compiled against a different charport ABI; ",
      "reinstall charport, then reinstall charr",
      call. = FALSE
    )
  }

  if (charr_icu_bundled()) {
    dat <- system.file("icu", "icudt74l.dat", package = pkgname)
    if (!nzchar(dat) || !isTRUE(.Call(C_charr_icu_init, dat))) {
      stop(
        "charr's bundled ICU data could not be initialized; reinstall charr",
        call. = FALSE
      )
    }
  } else if (!isTRUE(.Call(C_charr_icu_init, NULL))) {
    stop(
      "charr could not initialize the system ICU library; reinstall charr",
      call. = FALSE
    )
  }

  if (tolower(Sys.getenv("CHARR_ALTREP")) %in% c("1", "true", "yes")) {
    charr_altrep(TRUE)
  }
  invisible()
}
