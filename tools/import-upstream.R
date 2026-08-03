#!/usr/bin/env Rscript
# tools/import-upstream.R -- upstream sync and backend-seam validation.
#
# This is the source of truth for the pinned stringr commit and the complete
# set of stringi leaves dispatched by charr. Imported stringr sources retain
# their stri_* calls. Three namespace-qualified nested calls are made
# unqualified so the selected private environment owns them, and the four pure
# option-list constructors are replaced with backend-neutral charr helpers.
#
# Usage:
#   Rscript tools/import-upstream.R check
#   Rscript tools/import-upstream.R import <stringr-checkout>
#   Rscript tools/import-upstream.R prepare
#   Rscript tools/import-upstream.R validate

UPSTREAM_REPO <- "https://github.com/tidyverse/stringr"
UPSTREAM_COMMIT <- "ae054b1d28f630fee22ddb3cb7525396e62af4fe"

# Package licensing is maintained locally in LICENSE and LICENSE.note.
UPSTREAM_SUBSET <- c(
  "DESCRIPTION", "NAMESPACE",
  "R", "data", "man", "inst"
)

UPSTREAM_TEST_SUBSET <- "tests/testthat"
IMPORTED_TEST_DIR <- file.path("tests", "testthat", "stringr-imported")

UPSTREAM_DROPPED <- c(
  "man/figures/logo.png",
  "R/stringr-package.R",
  "man/stringr-package.Rd"
)

# Every computational stringi function called by the pinned stringr sources,
# plus charr's str_reverse() and str_read_lines() extensions. `replacement`
# records the two functions also used in replacement position.
STRINGI_LEAVES <- local({
  b <- function(name, replacement = FALSE) {
    data.frame(stringi = name, replacement = replacement)
  }
  do.call(rbind, list(
    b("stri_detect_fixed"),
    b("stri_startswith_fixed"),
    b("stri_endswith_fixed"),
    b("stri_count_fixed"),
    b("stri_locate_first_fixed"),
    b("stri_locate_all_fixed"),
    b("stri_extract_first_fixed"),
    b("stri_extract_all_fixed"),
    b("stri_replace_first_fixed"), b("stri_replace_all_fixed"),
    b("stri_split_fixed"),
    b("stri_sub", replacement = TRUE),
    b("stri_sub_all", replacement = TRUE),
    b("stri_length"), b("stri_c"), b("stri_flatten"), b("stri_dup"),
    b("stri_reverse"),
    b("stri_trim_left"), b("stri_trim_right"), b("stri_trim_both"),
    b("stri_replace_na"),
    b("stri_detect_regex"), b("stri_count_regex"),
    b("stri_locate_first_regex"), b("stri_locate_all_regex"),
    b("stri_extract_first_regex"), b("stri_extract_all_regex"),
    b("stri_replace_first_regex"), b("stri_replace_all_regex"),
    b("stri_split_regex"),
    b("stri_match_first_regex"), b("stri_match_all_regex"),
    b("stri_detect_coll"), b("stri_startswith_coll"),
    b("stri_endswith_coll"), b("stri_count_coll"),
    b("stri_locate_first_coll"), b("stri_locate_all_coll"),
    b("stri_extract_first_coll"), b("stri_extract_all_coll"),
    b("stri_replace_first_coll"), b("stri_replace_all_coll"),
    b("stri_split_coll"),
    b("stri_order"), b("stri_rank"), b("stri_cmp_equiv"),
    b("stri_duplicated"),
    b("stri_trans_tolower"), b("stri_trans_toupper"),
    b("stri_trans_totitle"),
    b("stri_count_boundaries"),
    b("stri_locate_first_boundaries"), b("stri_locate_all_boundaries"),
    b("stri_extract_first_boundaries"), b("stri_extract_all_boundaries"),
    b("stri_split_boundaries"), b("stri_wrap"),
    b("stri_pad_left"), b("stri_pad_right"), b("stri_pad_both"),
    b("stri_width"), b("stri_escape_unicode"), b("stri_conv"),
    b("stri_read_lines")
  ))
})

OPTION_BUILDERS <- c(
  stri_opts_fixed = ".charr_opts_fixed",
  stri_opts_regex = ".charr_opts_regex",
  stri_opts_collator = ".charr_opts_collator",
  stri_opts_brkiter = ".charr_opts_brkiter"
)

ROUTED_QUALIFIERS <- c(
  "stri_c",
  "stri_sub_all",
  "stri_duplicated"
)

OWNED_MARKER <- "^# charr-owned"

archive_subset <- function(checkout, paths, destination = ".") {
  cmd <- sprintf(
    "git -C %s archive %s %s | tar -x -C %s",
    shQuote(checkout), UPSTREAM_COMMIT,
    paste(shQuote(paths), collapse = " "),
    shQuote(destination)
  )
  if (system(cmd) != 0L) stop("archive extraction failed")
}

snapshot_call_name <- function(call) {
  if (!is.call(call)) return(NULL)
  function_call <- call[[1L]]
  if (is.symbol(function_call)) return(as.character(function_call))
  if (is.call(function_call) &&
      as.character(function_call[[1L]]) %in% c("::", ":::")) {
    return(as.character(function_call[[3L]]))
  }
  NULL
}

snapshot_policy <- function(expression) {
  if (!is.call(expression)) return(logical())

  result <- logical()
  name <- snapshot_call_name(expression)
  if (!is.null(name) &&
      name %in% c("expect_snapshot", "expect_snapshot_error")) {
    arguments <- as.list(expression)[-1L]
    cran <- which(names(arguments) == "cran")
    result <- length(cran) == 1L && identical(arguments[[cran]], TRUE)
  }

  c(result, unlist(lapply(as.list(expression), snapshot_policy)))
}

prepare_imported_tests <- function() {
  files <- list.files(
    IMPORTED_TEST_DIR,
    pattern = "[.]R$",
    recursive = TRUE,
    full.names = TRUE
  )
  changed <- 0L
  for (file in files) {
    lines <- readLines(file, warn = FALSE)
    output <- gsub(
      "\\bexpect_snapshot_error\\((?![[:space:]]*cran[[:space:]]*=)",
      "expect_snapshot_error(cran = TRUE, ",
      lines,
      perl = TRUE
    )
    output <- gsub(
      "\\bexpect_snapshot\\((?![[:space:]]*cran[[:space:]]*=)",
      "expect_snapshot(cran = TRUE, ",
      output,
      perl = TRUE
    )
    if (!identical(output, lines)) {
      writeLines(output, file)
      changed <- changed + sum(output != lines)
    }
  }

  policy <- unlist(lapply(files, function(file) {
    unlist(lapply(parse(file), snapshot_policy))
  }))
  if (length(policy) > 0L && !all(policy)) {
    stop("every imported snapshot assertion must set cran = TRUE")
  }
  cat(sprintf(
    "prepared %d imported test file(s); %d snapshot line(s) rewritten\n",
    length(files), changed
  ))
}

import_upstream_tests <- function(checkout) {
  stage <- tempfile("charr-stringr-tests-")
  dir.create(stage)
  on.exit(unlink(stage, recursive = TRUE, force = TRUE), add = TRUE)
  archive_subset(checkout, UPSTREAM_TEST_SUBSET, stage)

  source <- file.path(stage, UPSTREAM_TEST_SUBSET)
  entries <- list.files(
    source,
    all.files = TRUE,
    no.. = TRUE,
    full.names = TRUE
  )
  if (length(entries) == 0L) stop("upstream testthat directory is empty")

  if (dir.exists(IMPORTED_TEST_DIR)) {
    unlink(IMPORTED_TEST_DIR, recursive = TRUE, force = TRUE)
  }
  dir.create(IMPORTED_TEST_DIR, recursive = TRUE)
  copied <- file.copy(entries, IMPORTED_TEST_DIR, recursive = TRUE)
  if (!all(copied)) stop("could not copy the imported stringr tests")

  prepare_imported_tests()
}

prepare_line <- function(line) {
  for (name in ROUTED_QUALIFIERS) {
    line <- gsub(paste0("\\bstringi::", name, "\\b"), name, line)
  }
  for (from in names(OPTION_BUILDERS)) {
    to <- OPTION_BUILDERS[[from]]
    line <- gsub(paste0("\\bstringi::", from, "\\b"), to, line)
    line <- gsub(paste0("\\b", from, "\\b"), to, line)
  }
  line
}

stringr_r_files <- function() {
  files <- list.files("R", pattern = "[.]R$", full.names = TRUE)
  files <- files[!grepl("^altrep_backend-", basename(files))]
  owned <- vapply(files, function(f) {
    first <- readLines(f, n = 1L, warn = FALSE)
    length(first) && grepl(OWNED_MARKER, first)
  }, logical(1))
  files[!owned]
}

dispatch_leaf_map <- function() {
  expressions <- parse("R/aaa-backend-core.R")
  environment <- new.env(parent = baseenv())
  for (expression in expressions) {
    eval(expression, envir = environment)
    if (exists(".charr_leaf_map", envir = environment, inherits = FALSE)) {
      return(environment$.charr_leaf_map)
    }
  }
  stop("R/aaa-backend-core.R does not define .charr_leaf_map")
}

cmd_validate <- function() {
  calls <- unique(unlist(lapply(stringr_r_files(), function(f) {
    parsed <- getParseData(parse(f, keep.source = TRUE))
    parsed$text[
      parsed$token == "SYMBOL_FUNCTION_CALL" &
        grepl("^(ci|stri)_|^[.]charr_opts_", parsed$text)
    ]
  })))

  ci_calls <- sort(calls[grepl("^ci_", calls)])
  used <- sort(unique(calls[grepl("^stri_", calls)]))
  expected <- sort(STRINGI_LEAVES$stringi)
  used_options <- sort(unique(calls[grepl("^[.]charr_opts_", calls)]))
  expected_options <- sort(unname(OPTION_BUILDERS))
  problems <- character()

  qualified_source <- unlist(lapply(
    stringr_r_files(),
    readLines,
    warn = FALSE
  ))
  remaining_qualified <- ROUTED_QUALIFIERS[vapply(
    ROUTED_QUALIFIERS,
    function(name) any(grepl(
      paste0("\\bstringi::", name, "\\b"),
      qualified_source
    )),
    logical(1)
  )]
  if (length(remaining_qualified) > 0L) {
    problems <- c(
      problems,
      paste0(
        "nested calls bypass backend routing: ",
        paste(remaining_qualified, collapse = ", ")
      )
    )
  }

  leaf_map <- dispatch_leaf_map()
  expected_bindings <- c(
    STRINGI_LEAVES$stringi,
    paste0(STRINGI_LEAVES$stringi[STRINGI_LEAVES$replacement], "<-")
  )
  expected_bindings <- sort(expected_bindings)
  actual_bindings <- sort(names(leaf_map))
  expected_private <- sub("^stri_", "ci_", names(leaf_map))
  if (
    !identical(actual_bindings, expected_bindings) ||
      !identical(unname(leaf_map), expected_private)
  ) {
    problems <- c(problems, "R dispatch leaf map differs from the import manifest")
  }

  if (length(ci_calls)) {
    problems <- c(
      problems,
      paste0(
        "stringr sources call private ci_* leaves: ",
        paste(ci_calls, collapse = ", ")
      )
    )
  }
  if (length(setdiff(used, expected))) {
    problems <- c(
      problems,
      paste0(
        "stringi calls absent from leaf registry: ",
        paste(setdiff(used, expected), collapse = ", ")
      )
    )
  }
  if (length(setdiff(expected, used))) {
    problems <- c(
      problems,
      paste0(
        "leaf registry entries unused by stringr or charr extensions: ",
        paste(setdiff(expected, used), collapse = ", ")
      )
    )
  }
  if (!identical(used_options, expected_options)) {
    problems <- c(
      problems,
      paste0(
        "backend-neutral option builders differ from the manifest: used ",
        paste(used_options, collapse = ", "),
        "; expected ", paste(expected_options, collapse = ", ")
      )
    )
  }
  if (length(problems)) stop(paste(problems, collapse = "\n"))
  cat(sprintf(
    "validate: %d stringi leaves and %d neutral option builders covered\n",
    length(used), length(used_options)
  ))
}

cmd_prepare <- function() {
  files <- stringr_r_files()
  total <- 0L
  for (f in files) {
    lines <- readLines(f, warn = FALSE)
    code <- !grepl("^\\s*#", lines)
    out <- lines
    out[code] <- vapply(
      lines[code], prepare_line, character(1),
      USE.NAMES = FALSE
    )
    n <- sum(out != lines)
    if (n > 0L) {
      writeLines(out, f)
      cat(sprintf("  %-24s %d line(s) rewritten\n", basename(f), n))
      total <- total + n
    }
  }
  cat(sprintf("prepare: %d backend-seam line(s) rewritten total\n", total))
  cmd_validate()
}

cmd_check <- function() {
  out <- system2("git", c("ls-remote", UPSTREAM_REPO, "HEAD"), stdout = TRUE)
  head <- sub("\\s.*$", "", out[[1]])
  cat(sprintf("recorded: %s\nupstream: %s\n", UPSTREAM_COMMIT, head))
  if (identical(head, UPSTREAM_COMMIT)) {
    cat("up to date with upstream HEAD\n")
  } else {
    cat(
      "upstream has moved; bump UPSTREAM_COMMIT, run `import`, ",
      "and review the diff\n",
      sep = ""
    )
  }
}

cmd_import <- function(checkout) {
  stopifnot(dir.exists(checkout))
  at <- system2("git", c("-C", checkout, "rev-parse", "HEAD"), stdout = TRUE)
  if (!identical(at, UPSTREAM_COMMIT)) {
    stop(
      "checkout is at ", at, ", not UPSTREAM_COMMIT ", UPSTREAM_COMMIT
    )
  }
  archive_subset(checkout, UPSTREAM_SUBSET)
  import_upstream_tests(checkout)
  file.remove(UPSTREAM_DROPPED[file.exists(UPSTREAM_DROPPED)])
  cat(sprintf("imported upstream subset at %s\n", UPSTREAM_COMMIT))
  cmd_prepare()
  cat(
    "review the diff: stri_* names should remain; only the three routed ",
    "qualifiers and neutral option constructors should differ\n",
    sep = ""
  )
}

main <- function(args) {
  if (!file.exists("DESCRIPTION") || !dir.exists("R")) {
    stop("run from the charr package root")
  }
  mode <- if (length(args)) args[[1]] else "help"
  switch(
    mode,
    check = cmd_check(),
    import = cmd_import(args[[2]]),
    prepare = cmd_prepare(),
    validate = cmd_validate(),
    cat(
      "usage: Rscript tools/import-upstream.R ",
      "check | import <stringr-checkout> | prepare | validate\n",
      sep = ""
    )
  )
}

main(commandArgs(trailingOnly = TRUE))
