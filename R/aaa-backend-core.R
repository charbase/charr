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
