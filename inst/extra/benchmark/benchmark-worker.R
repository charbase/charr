# One fresh R process measures one operation, condition, and repetition. Package
# startup, corpus loading, auxiliary input construction, and garbage collection
# are outside the timed region.

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 3L) {
  stop(
    "usage: benchmark-worker.R <metadata|preflight|time> ",
    "<package-lib> <main|claude> ..."
  )
}

mode <- args[[1L]]
package_lib <- normalizePath(args[[2L]], mustWork = TRUE)
branch_kind <- args[[3L]]
stopifnot(
  mode %in% c("metadata", "preflight", "time"),
  branch_kind %in% c("main", "claude")
)

.libPaths(c(package_lib, .libPaths()))
suppressMessages(library(charr, lib.loc = package_lib))
loaded_package <- normalizePath(system.file(package = "charr"), mustWork = TRUE)
expected_package <- normalizePath(file.path(package_lib, "charr"), mustWork = TRUE)
stopifnot(identical(loaded_package, expected_package))
charr_ns <- asNamespace("charr")

if (branch_kind == "claude") {
  if (exists("charr_threads", envir = charr_ns, inherits = FALSE)) {
    get("charr_threads", envir = charr_ns, inherits = FALSE)(1L)
  }
  get("charr_altrep", envir = charr_ns, inherits = FALSE)(TRUE)
}

emit_metadata <- function(key, value) {
  value <- gsub("[\t\r\n]", " ", as.character(value))
  cat(sprintf("META\t%s\t%s\n", key, value))
}

if (mode == "metadata") {
  if (exists("charr_icu_info", envir = charr_ns, inherits = FALSE)) {
    info <- get("charr_icu_info", envir = charr_ns, inherits = FALSE)()
    charr_icu_version <- info[["runtime_version"]]
    charr_icu_mode <- info[["mode"]]
  } else {
    info <- suppressWarnings(
      get("ci_info", envir = charr_ns, inherits = FALSE)(FALSE)
    )
    charr_icu_version <- info[["ICU.version"]]
    bundled <- get("charr_icu_bundled", envir = charr_ns, inherits = FALSE)()
    charr_icu_mode <- if (isTRUE(bundled)) "bundle" else "system"
  }
  stringi_info <- suppressWarnings(stringi::stri_info())
  stringi_icu_mode <- if (isTRUE(stringi_info[["ICU.system"]])) {
    "system"
  } else {
    "bundle"
  }

  emit_metadata("package_path", loaded_package)
  emit_metadata("package_version", as.character(utils::packageVersion("charr")))
  emit_metadata("charr_icu_version", charr_icu_version)
  emit_metadata("charr_icu_mode", charr_icu_mode)
  emit_metadata("stringi_icu_version", stringi_info[["ICU.version"]])
  emit_metadata("stringi_icu_mode", stringi_icu_mode)
  emit_metadata("r_version", R.version.string)
  quit(save = "no")
}

if (length(args) != 11L) {
  stop(
    "preflight/time mode requires: <ops-file> <operation> <condition> ",
    "<text> <charvec-rds> <prepared-rds> <input-n> <output|rep>"
  )
}

ops_file <- normalizePath(args[[4L]], mustWork = TRUE)
operation <- args[[5L]]
condition <- args[[6L]]
corpus_text <- normalizePath(args[[7L]], mustWork = TRUE)
corpus_charvec <- normalizePath(args[[8L]], mustWork = TRUE)
prepared_path <- normalizePath(args[[9L]], mustWork = TRUE)
input_n <- as.integer(args[[10L]])
stopifnot(!is.na(input_n), input_n > 0L)

source(ops_file)
op <- bench_ops[[operation]]
if (is.null(op)) {
  stop("unknown operation: ", operation)
}

if (branch_kind == "main") {
  stopifnot(condition %in% c("stringi", "base", "altrep"))
  leaf_map <- get(".charr_leaf_map", envir = charr_ns, inherits = FALSE)
  backend_environments <- get(
    ".charr_backend_environments", envir = charr_ns, inherits = FALSE
  )
  expected_stringi <- sub("^ci_", "stri_", names(bench_ops))
  stopifnot(
    identical(names(leaf_map), expected_stringi),
    identical(unname(leaf_map), names(bench_ops))
  )
  backend_environment <- backend_environments[[condition]]
  resolve <- function(ci_name) {
    index <- match(ci_name, unname(leaf_map))
    if (is.na(index)) {
      stop("operation is absent from .charr_leaf_map: ", ci_name)
    }
    get(names(leaf_map)[[index]], envir = backend_environment, inherits = FALSE)
  }
} else {
  stopifnot(identical(condition, "claude_altrep"))
  missing <- names(bench_ops)[!vapply(
    names(bench_ops), exists, logical(1L), envir = charr_ns, inherits = FALSE
  )]
  if (length(missing)) {
    stop("Claude package is missing benchmark leaves: ", paste(missing, collapse = ", "))
  }
  resolve <- function(ci_name) {
    get(ci_name, envir = charr_ns, inherits = FALSE)
  }
}

input_mode <- if (condition %in% c("claude_altrep", "altrep")) {
  "charvec"
} else {
  "plain"
}
prepared <- if (is.null(op$fixture)) NULL else readRDS(prepared_path)
ctx <- list(corpus = corpus_text, prepared = prepared)
if (identical(operation, "ci_read_lines")) {
  x <- NULL
  input_class <- "file"
} else if (identical(input_mode, "charvec")) {
  x <- readRDS(corpus_charvec)
  info <- charport::charport_info(x)
  stopifnot(
    charport::is_charvec(x),
    !isTRUE(info$is_materialized),
    length(x) == input_n
  )
  input_class <- "charvec"
} else {
  x <- readLines(corpus_text, encoding = "UTF-8", warn = FALSE)
  stopifnot(is.character(x), length(x) == input_n)
  input_class <- "character"
}

leaf <- resolve(operation)
bench_validate_fixture(operation, prepared, input_n, input_mode)
aux <- if (is.null(op$prep)) NULL else op$prep(resolve, x, ctx)
gc(FALSE)

normalize_result <- function(value) {
  if (is.character(value)) {
    missing <- is.na(value)
    payload <- lapply(seq_along(value), function(i) {
      if (missing[[i]]) NULL else charToRaw(value[[i]])
    })
    return(list(
      type = "character",
      length = length(value),
      missing = missing,
      encoding = Encoding(value),
      payload = payload,
      attributes = normalize_attributes(value)
    ))
  }
  if (is.list(value)) {
    return(list(
      type = typeof(value),
      values = lapply(value, normalize_result),
      attributes = normalize_attributes(value)
    ))
  }

  attributes_value <- normalize_attributes(value)
  attributes(value) <- NULL
  list(
    type = typeof(value),
    value = value,
    attributes = attributes_value
  )
}

normalize_attributes <- function(value) {
  attributes_value <- attributes(value)
  if (is.null(attributes_value)) {
    return(NULL)
  }
  lapply(
    attributes_value,
    function(attribute) normalize_result(attribute)
  )
}

if (identical(mode, "preflight")) {
  output_path <- args[[11L]]
  result <- op$thunk(leaf, x, ctx, aux)
  bench_validate_result(operation, result, aux)
  result <- normalize_result(result)
  saveRDS(result, output_path, compress = FALSE)
  cat(sprintf("CHECK\t%s\t%s\n", output_path, input_class))
  quit(save = "no")
}

rep <- as.integer(args[[11L]])
stopifnot(!is.na(rep), rep >= 1L)

started <- proc.time()[["elapsed"]]
result <- op$thunk(leaf, x, ctx, aux)
seconds <- proc.time()[["elapsed"]] - started

cat(sprintf("TIME\t%.9f\t%s\n", seconds, input_class))
rm(result)
