.charr_backend_value <- function(value = getOption("charr_backend", "altrep")) {
  # Keep the successful path small: every public operation resolves this
  # option once, while the detailed error path is necessarily rare.
  if (identical(value, "altrep")) {
    return(value)
  }
  if (identical(value, "base")) {
    return(value)
  }
  if (identical(value, "stringi")) {
    return(value)
  }

  cli::cli_abort(
    "{.option charr_backend} must be one of {.val stringi}, {.val base}, or {.val altrep}."
  )
}

#' Select charr's string backend
#'
#' `charr_backend()` selects the implementation used by charr's public string
#' functions. Dispatch happens once, in R, when a public operation starts.
#' Nested calls stay within the selected backend.
#'
#' The available backends are:
#'
#' - `"stringi"`: the original stringr route through installed stringi.
#' - `"base"`: charr's optimized backend returning ordinary R character
#'   vectors.
#' - `"altrep"`: charr's optimized charport backend returning ALTREP character
#'   vectors where appropriate. This is the default.
#'
#' The selection is stored in the global `charr_backend` option. Changing the
#' option affects later public calls; it does not change an operation already
#' in progress.
#'
#' @param value `NULL` to query the current backend, or one of `"stringi"`,
#'   `"base"`, or `"altrep"` to select a backend.
#' @return The current backend when querying. When setting, the previous
#'   backend is returned invisibly.
#' @export
#' @examples
#' old <- charr_backend("base")
#' charr_backend()
#' charr_backend(old)
charr_backend <- function(value = NULL) {
  old <- getOption("charr_backend", "altrep")
  if (is.null(value)) {
    return(.charr_backend_value(old))
  }

  value <- .charr_backend_value(value)
  options(charr_backend = value)
  invisible(old)
}

.charr_threads_value <- function(value) {
  if (length(value) == 1L && is.numeric(value) && is.finite(value) &&
      value >= 1 && value == trunc(value)) {
    if (value <= .Machine$integer.max) {
      return(as.integer(value))
    }
    return(value)
  }

  cli::cli_abort(
    "{.option charr_threads} must be a single whole number of at least {.val {1L}}."
  )
}

#' Set the number of worker threads
#'
#' `charr_threads()` sets how many threads charr's `"altrep"` backend may use
#' for the operations that have a data-parallel shape. Operations without that
#' shape, and the `"base"` and `"stringi"` backends, always run on one thread.
#'
#' An eligible operation uses the requested count, capped by its number of
#' tasks and an internal safety limit of 256. Charr does not apply an
#' input-size heuristic, so even a small eligible input starts workers when the
#' count is greater than one. A few modes must stay serial because their
#' results depend on the order in which elements run.
#'
#' The count defaults to `1`, so charr is single-threaded until asked
#' otherwise. It is held in native code rather than in an R option, because
#' threading happens only in native code. This accessor is the only way to
#' change it: setting an option named `charr_threads` has no effect.
#'
#' @param value `NULL` to query the current count, or a positive whole number
#'   of threads. [parallel::detectCores()] is the usual source of an upper
#'   bound; note that CRAN limits checks to two cores.
#' @return The current count when querying. When setting, the previous count
#'   is returned invisibly.
#' @export
#' @examples
#' old <- charr_threads(2)
#' charr_threads()
#' charr_threads(old)
charr_threads <- function(value = NULL) {
  if (is.null(value)) {
    return(.Call(C_charr_threads, NULL))
  }

  invisible(.Call(C_charr_threads, .charr_threads_value(value)))
}

.charr_chunking_value <- function(value, option) {
  if (length(value) == 1L && is.numeric(value) && is.finite(value) &&
      value >= 1 && value == trunc(value)) {
    if (value <= .Machine$integer.max) {
      return(as.integer(value))
    }
    return(value)
  }

  cli::cli_abort(
    "{.option {option}} must be a single whole number of at least {.val {1L}}."
  )
}

#' Tune how parallel work is divided
#'
#' These control how a data-parallel operation cuts its work into chunks once
#' [charr_threads()] has decided how many threads to use. Neither affects
#' whether threads are used at all, and neither changes any result.
#'
#' Threads draw chunks one at a time rather than taking a fixed slice each, so
#' a thread that finishes a cheap chunk comes back for another. That is what
#' keeps uneven input balanced: element cost is not uniform, and one long
#' string can outweigh a thousand short ones.
#'
#' `charr_chunks_per_worker()` sets how many chunks each thread should get,
#' defaulting to `128`. Higher values balance skewed input better and cost a
#' little more bookkeeping. `charr_min_chunk()` sets the smallest number of
#' elements a chunk may hold, defaulting to `256`, which stops a large thread
#' count from cutting the work finer than it is worth handing out.
#'
#' With `n` elements and `w` threads the chunk size is
#' `ceiling(n / (w * chunks_per_worker))`, raised to at least `min_chunk` and
#' then lowered if needed so that there are never fewer chunks than threads. A
#' short vector therefore still spreads across every thread.
#'
#' Like [charr_threads()], both settings are held in native code rather than in
#' an R option, because they are only ever read there. These accessors are the
#' only way to change them.
#'
#' @param value `NULL` to query the current setting, or a positive whole
#'   number.
#' @return The current setting when querying. When setting, the previous value
#'   is returned invisibly.
#' @name charr_chunking
#' @examples
#' old <- charr_chunks_per_worker(256)
#' charr_chunks_per_worker()
#' charr_chunks_per_worker(old)
NULL

#' @rdname charr_chunking
#' @export
charr_chunks_per_worker <- function(value = NULL) {
  if (is.null(value)) {
    return(.Call(C_charr_chunks_per_worker, NULL))
  }

  invisible(.Call(
    C_charr_chunks_per_worker,
    .charr_chunking_value(value, "charr_chunks_per_worker")
  ))
}

#' @rdname charr_chunking
#' @export
charr_min_chunk <- function(value = NULL) {
  if (is.null(value)) {
    return(.Call(C_charr_min_chunk, NULL))
  }

  invisible(.Call(
    C_charr_min_chunk,
    .charr_chunking_value(value, "charr_min_chunk")
  ))
}

.charr_leaf_map <- c(
  stri_detect_fixed = "ci_detect_fixed",
  stri_startswith_fixed = "ci_startswith_fixed",
  stri_endswith_fixed = "ci_endswith_fixed",
  stri_count_fixed = "ci_count_fixed",
  stri_locate_first_fixed = "ci_locate_first_fixed",
  stri_locate_all_fixed = "ci_locate_all_fixed",
  stri_extract_first_fixed = "ci_extract_first_fixed",
  stri_extract_all_fixed = "ci_extract_all_fixed",
  stri_replace_first_fixed = "ci_replace_first_fixed",
  stri_replace_all_fixed = "ci_replace_all_fixed",
  stri_split_fixed = "ci_split_fixed",
  stri_sub = "ci_sub",
  `stri_sub<-` = "ci_sub<-",
  stri_sub_all = "ci_sub_all",
  `stri_sub_all<-` = "ci_sub_all<-",
  stri_length = "ci_length",
  stri_c = "ci_c",
  stri_flatten = "ci_flatten",
  stri_dup = "ci_dup",
  stri_reverse = "ci_reverse",
  stri_trim_left = "ci_trim_left",
  stri_trim_right = "ci_trim_right",
  stri_trim_both = "ci_trim_both",
  stri_replace_na = "ci_replace_na",
  stri_detect_regex = "ci_detect_regex",
  stri_count_regex = "ci_count_regex",
  stri_locate_first_regex = "ci_locate_first_regex",
  stri_locate_all_regex = "ci_locate_all_regex",
  stri_extract_first_regex = "ci_extract_first_regex",
  stri_extract_all_regex = "ci_extract_all_regex",
  stri_replace_first_regex = "ci_replace_first_regex",
  stri_replace_all_regex = "ci_replace_all_regex",
  stri_split_regex = "ci_split_regex",
  stri_match_first_regex = "ci_match_first_regex",
  stri_match_all_regex = "ci_match_all_regex",
  stri_detect_coll = "ci_detect_coll",
  stri_startswith_coll = "ci_startswith_coll",
  stri_endswith_coll = "ci_endswith_coll",
  stri_count_coll = "ci_count_coll",
  stri_locate_first_coll = "ci_locate_first_coll",
  stri_locate_all_coll = "ci_locate_all_coll",
  stri_extract_first_coll = "ci_extract_first_coll",
  stri_extract_all_coll = "ci_extract_all_coll",
  stri_replace_first_coll = "ci_replace_first_coll",
  stri_replace_all_coll = "ci_replace_all_coll",
  stri_split_coll = "ci_split_coll",
  stri_order = "ci_order",
  stri_rank = "ci_rank",
  stri_cmp_equiv = "ci_cmp_equiv",
  stri_duplicated = "ci_duplicated",
  stri_trans_tolower = "ci_trans_tolower",
  stri_trans_toupper = "ci_trans_toupper",
  stri_trans_totitle = "ci_trans_totitle",
  stri_count_boundaries = "ci_count_boundaries",
  stri_locate_first_boundaries = "ci_locate_first_boundaries",
  stri_locate_all_boundaries = "ci_locate_all_boundaries",
  stri_extract_first_boundaries = "ci_extract_first_boundaries",
  stri_extract_all_boundaries = "ci_extract_all_boundaries",
  stri_split_boundaries = "ci_split_boundaries",
  stri_wrap = "ci_wrap",
  stri_pad_left = "ci_pad_left",
  stri_pad_right = "ci_pad_right",
  stri_pad_both = "ci_pad_both",
  stri_width = "ci_width",
  stri_escape_unicode = "ci_escape_unicode",
  stri_conv = "ci_conv",
  stri_read_lines = "ci_read_lines"
)

.charr_base_leaf_bindings <- new.env(parent = emptyenv())

.charr_register_base_leaf_bindings <- function(bindings) {
  if (is.environment(bindings)) {
    bindings <- as.list(bindings, all.names = TRUE)
  }
  if (!is.list(bindings) || is.null(names(bindings)) || any(names(bindings) == "")) {
    stop("base leaf bindings must be a named list or environment", call. = FALSE)
  }
  if (anyDuplicated(names(bindings))) {
    stop("base leaf binding names must be unique", call. = FALSE)
  }

  missing <- setdiff(names(.charr_leaf_map), names(bindings))
  unknown <- setdiff(names(bindings), names(.charr_leaf_map))
  if (length(missing) > 0L || length(unknown) > 0L) {
    stop(
      "base leaf binding manifest differs from the dispatch manifest",
      call. = FALSE
    )
  }
  is_function <- vapply(bindings, is.function, logical(1))
  if (!all(is_function)) {
    stop(
      "base leaf bindings must be functions: ",
      paste(sprintf("`%s`", names(bindings)[!is_function]), collapse = ", "),
      call. = FALSE
    )
  }

  for (name in names(bindings)) {
    assign(name, bindings[[name]], envir = .charr_base_leaf_bindings)
  }
  invisible(NULL)
}

.charr_clone_function <- function(source, environment) {
  clone <- eval(
    call("function", formals(source), body(source)),
    envir = environment
  )
  attributes(clone) <- attributes(source)
  clone
}
