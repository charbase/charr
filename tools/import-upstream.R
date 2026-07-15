#!/usr/bin/env Rscript
# tools/import-upstream.R -- upstream sync and backend rename for charr.
#
# This is the source of truth for the pinned stringr commit and the complete
# set of stringi entry points renamed to charr's ci_* backend seam.
#
# Usage:
#   Rscript tools/import-upstream.R check
#   Rscript tools/import-upstream.R import <stringr-checkout>
#   Rscript tools/import-upstream.R rename
#   Rscript tools/import-upstream.R validate

UPSTREAM_REPO <- "https://github.com/tidyverse/stringr"
UPSTREAM_COMMIT <- "ae054b1d28f630fee22ddb3cb7525396e62af4fe"

UPSTREAM_SUBSET <- c(
  "DESCRIPTION", "NAMESPACE", "LICENSE", "LICENSE.md",
  "R", "data", "man", "tests", "inst"
)

UPSTREAM_DROPPED <- c(
  "man/figures/logo.png",
  "R/stringr-package.R",
  "man/stringr-package.Rd"
)

# Every stringi function called by the pinned stringr R sources, including the
# four pure option-list constructors. `replacement` records the two functions
# also used in replacement position.
BACKENDS <- local({
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
    b("stri_opts_fixed"), b("stri_opts_regex"),
    b("stri_opts_collator"), b("stri_opts_brkiter")
  ))
})
BACKENDS$charr <- sub("^stri_", "ci_", BACKENDS$stringi)

OWNED_MARKER <- "^# charr-owned"

rename_line <- function(line, table) {
  for (i in order(-nchar(table$stringi))) {
    from <- table$stringi[i]
    to <- table$charr[i]
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

cmd_validate <- function() {
  calls <- unique(unlist(lapply(stringr_r_files(), function(f) {
    parsed <- getParseData(parse(f, keep.source = TRUE))
    parsed$text[
      parsed$token == "SYMBOL_FUNCTION_CALL" &
        grepl("^(ci|stri)_", parsed$text)
    ]
  })))

  residual <- sort(calls[grepl("^stri_", calls)])
  used <- sort(unique(calls[grepl("^ci_", calls)]))
  expected <- sort(BACKENDS$charr)
  problems <- character()
  if (length(residual)) {
    problems <- c(
      problems,
      paste0("unrenamed stringi calls: ", paste(residual, collapse = ", "))
    )
  }
  if (length(setdiff(used, expected))) {
    problems <- c(
      problems,
      paste0(
        "ci calls absent from registry: ",
        paste(setdiff(used, expected), collapse = ", ")
      )
    )
  }
  if (length(setdiff(expected, used))) {
    problems <- c(
      problems,
      paste0(
        "registry entries unused by stringr: ",
        paste(setdiff(expected, used), collapse = ", ")
      )
    )
  }
  if (length(problems)) stop(paste(problems, collapse = "\n"))
  cat(sprintf("validate: %d stringi entry points fully covered\n", length(used)))
}

cmd_rename <- function() {
  files <- stringr_r_files()
  total <- 0L
  for (f in files) {
    lines <- readLines(f, warn = FALSE)
    code <- !grepl("^\\s*#", lines)
    out <- lines
    out[code] <- vapply(
      lines[code], rename_line, character(1),
      table = BACKENDS, USE.NAMES = FALSE
    )
    n <- sum(out != lines)
    if (n > 0L) {
      writeLines(out, f)
      cat(sprintf("  %-24s %d line(s) rewritten\n", basename(f), n))
      total <- total + n
    }
  }
  cat(sprintf("rename: %d line(s) rewritten total\n", total))
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
  cmd <- sprintf(
    "git -C %s archive %s %s | tar -x -C .",
    shQuote(checkout), UPSTREAM_COMMIT,
    paste(shQuote(UPSTREAM_SUBSET), collapse = " ")
  )
  if (system(cmd) != 0L) stop("archive extraction failed")
  file.remove(UPSTREAM_DROPPED[file.exists(UPSTREAM_DROPPED)])
  cat(sprintf("imported upstream subset at %s\n", UPSTREAM_COMMIT))
  cmd_rename()
  cat("review the diff: upstream files should show only ci_* renames\n")
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
    rename = cmd_rename(),
    validate = cmd_validate(),
    cat(
      "usage: Rscript tools/import-upstream.R ",
      "check | import <stringr-checkout> | rename | validate\n",
      sep = ""
    )
  )
}

main(commandArgs(trailingOnly = TRUE))
