#!/usr/bin/env Rscript
# tools/import-upstream.R -- upstream sync and backend rename for charr.
#
# charr is a hard fork of stringr. This script is the single source of truth
# for three things:
#
#   1. which upstream stringr commit the fork tracks (UPSTREAM_COMMIT);
#   2. which stringi entry points are renamed to charr's ci_* backend seam
#      (BACKENDS below -- the same registry that drives charr_backends());
#   3. the generated alias shims in R/backend-shim.R.
#
# The rename is a plain, idempotent, token-level search and replace over the
# upstream-derived files in R/. Roxygen and comment lines are left untouched,
# so documentation keeps referring to stringi. charr-owned files opt out with
# a "# charr-owned" first line and are never rewritten.
#
# Usage:
#   Rscript tools/import-upstream.R check             compare recorded commit
#                                                     against upstream HEAD
#   Rscript tools/import-upstream.R import <stringr>  re-extract the upstream
#                                                     subset from a local
#                                                     stringr checkout, then
#                                                     rename + shims
#   Rscript tools/import-upstream.R rename            apply stri_* -> ci_*
#                                                     renames in R/
#   Rscript tools/import-upstream.R shims             regenerate
#                                                     R/backend-shim.R
#
# A future rebase is: bump UPSTREAM_COMMIT, run `import`, review the diff.

UPSTREAM_REPO <- "https://github.com/tidyverse/stringr"
UPSTREAM_COMMIT <- "ae054b1d28f630fee22ddb3cb7525396e62af4fe"

# Paths extracted verbatim from upstream at import time; everything in the
# subset is kept byte-identical until `rename` runs.
UPSTREAM_SUBSET <- c(
  "DESCRIPTION", "NAMESPACE", "LICENSE", "LICENSE.md",
  "R", "data", "man", "tests", "inst"
)

# Upstream paths charr drops or replaces; removed again after each import so
# a rebase can't resurrect them. (DESCRIPTION/LICENSE/LICENSE.md are also
# charr's own -- an import overwrites them and the review diff restores; the
# files here are the ones whose upstream version must not exist at all.)
UPSTREAM_DROPPED <- c(
  "man/figures/logo.png",       # stringr hex logo
  "R/stringr-package.R",        # renamed to R/charr-package.R
  "man/stringr-package.Rd"      # renamed to man/charr-package.Rd
)

# Backend registry ------------------------------------------------------
#
# Every stringi entry point stringr calls on a code line, except the
# stri_opts_* constructors (pure option-list builders; no string data flows
# through them, and charr's native code consumes the option lists they
# produce).
#
# status:
#   "stringi"  ci_* is a generated alias for the stringi function
#              (R/backend-shim.R); behavior is stringi's by construction.
#   "native"   ci_* is hand-written in R/backend-native.R with the
#              gate-and-fallback pattern; the generator emits only the
#              registry row.
#
# replacement: the entry point is also used in replacement position
# (`fn(x, ...) <- value`), so the `fn<-` form needs an alias too.

BACKENDS <- local({
  b <- function(name, status = "stringi", replacement = FALSE) {
    data.frame(stringi = name, status = status, replacement = replacement)
  }
  do.call(rbind, list(
    # fixed-pattern family (phase 1 native candidates)
    b("stri_detect_fixed"), b("stri_startswith_fixed"), b("stri_endswith_fixed"),
    b("stri_count_fixed"),
    b("stri_locate_first_fixed"), b("stri_locate_all_fixed"),
    b("stri_extract_first_fixed"), b("stri_extract_all_fixed"),
    b("stri_replace_first_fixed"), b("stri_replace_all_fixed"),
    b("stri_split_fixed"),
    # scan/byte ops (phase 1 native candidates)
    b("stri_sub", replacement = TRUE), b("stri_sub_all", replacement = TRUE),
    b("stri_length"), b("stri_c"), b("stri_flatten"), b("stri_dup"),
    b("stri_trim_left"), b("stri_trim_right"), b("stri_trim_both"),
    b("stri_replace_na"),
    # regex family (phase 2, ICU engine)
    b("stri_detect_regex"), b("stri_count_regex"),
    b("stri_locate_first_regex"), b("stri_locate_all_regex"),
    b("stri_extract_first_regex"), b("stri_extract_all_regex"),
    b("stri_replace_first_regex"), b("stri_replace_all_regex"),
    b("stri_split_regex"),
    b("stri_match_first_regex"), b("stri_match_all_regex"),
    # collation family (phase 2, ICU engine)
    b("stri_detect_coll"), b("stri_startswith_coll"), b("stri_endswith_coll"),
    b("stri_count_coll"),
    b("stri_locate_first_coll"), b("stri_locate_all_coll"),
    b("stri_extract_first_coll"), b("stri_extract_all_coll"),
    b("stri_replace_first_coll"), b("stri_replace_all_coll"),
    b("stri_split_coll"),
    b("stri_order"), b("stri_rank"), b("stri_cmp_equiv"), b("stri_duplicated"),
    # case family (phase 2, ICU engine)
    b("stri_trans_tolower"), b("stri_trans_toupper"), b("stri_trans_totitle"),
    # boundary family (phase 2, ICU engine)
    b("stri_count_boundaries"),
    b("stri_locate_first_boundaries"), b("stri_locate_all_boundaries"),
    b("stri_extract_first_boundaries"), b("stri_extract_all_boundaries"),
    b("stri_split_boundaries"),
    b("stri_wrap"),
    # remaining surface (kept on stringi indefinitely unless promoted)
    b("stri_pad_left"), b("stri_pad_right"), b("stri_pad_both"),
    b("stri_width"), b("stri_escape_unicode"), b("stri_conv")
  ))
})
BACKENDS$charr <- sub("^stri_", "ci_", BACKENDS$stringi)

SHIM_PATH <- file.path("R", "backend-shim.R")
OWNED_MARKER <- "^# charr-owned"

# rename ----------------------------------------------------------------

rename_line <- function(line, table) {
  for (i in order(-nchar(table$stringi))) {
    from <- table$stringi[i]
    to <- table$charr[i]
    line <- gsub(paste0("\\bstringi::", from, "\\b"), to, line)
    line <- gsub(paste0("\\b", from, "\\b"), to, line)
  }
  line
}

cmd_rename <- function() {
  files <- list.files("R", pattern = "\\.R$", full.names = TRUE)
  total <- 0L
  for (f in files) {
    lines <- readLines(f, warn = FALSE)
    if (length(lines) && grepl(OWNED_MARKER, lines[[1]])) next
    code <- !grepl("^\\s*#", lines)
    out <- lines
    out[code] <- vapply(lines[code], rename_line, character(1),
                        table = BACKENDS, USE.NAMES = FALSE)
    n <- sum(out != lines)
    if (n > 0L) {
      writeLines(out, f)
      cat(sprintf("  %-24s %d line(s) rewritten\n", basename(f), n))
      total <- total + n
    }
  }
  cat(sprintf("rename: %d line(s) rewritten total\n", total))
}

# shims -----------------------------------------------------------------

cmd_shims <- function() {
  alias <- BACKENDS[BACKENDS$status == "stringi", ]
  lines <- c(
    "# charr-owned file (generated). tools/import-upstream.R must not rename here.",
    "# Generated by tools/import-upstream.R -- do not edit by hand; edit the",
    "# BACKENDS registry in that script and rerun `Rscript tools/import-upstream.R shims`.",
    "#",
    "# Each ci_* symbol is charr's name for the stringi entry point it stands",
    "# in for. Rows with status \"stringi\" are pass-through wrappers below;",
    "# rows with status \"native\" are implemented in R/backend-native.R and do",
    "# not appear here. Wrappers (not aliases) on purpose: an error raised",
    "# inside stringi then carries the stri_* call, so error output matches",
    "# upstream stringr exactly.",
    "",
    "charr_backend_table <- data.frame(",
    sprintf("  stringi = c(%s),",
            paste0(strwrap(paste0('"', BACKENDS$stringi, '"', collapse = ", "),
                           width = 72, prefix = "    ", initial = ""),
                   collapse = "\n")),
    sprintf("  charr = c(%s),",
            paste0(strwrap(paste0('"', BACKENDS$charr, '"', collapse = ", "),
                           width = 72, prefix = "    ", initial = ""),
                   collapse = "\n")),
    sprintf("  backend = c(%s),",
            paste0(strwrap(paste0('"', ifelse(BACKENDS$status == "native",
                                              "charr", "stringi"),
                                  '"', collapse = ", "),
                           width = 72, prefix = "    ", initial = ""),
                   collapse = "\n")),
    "  stringsAsFactors = FALSE",
    ")",
    "",
    paste0(alias$charr, " <- function(...) ", alias$stringi, "(...)"),
    paste0("`", alias$charr[alias$replacement], "<-` <- function(...) `",
           alias$stringi[alias$replacement], "<-`(...)")
  )
  writeLines(lines, SHIM_PATH)
  cat(sprintf("shims: wrote %s (%d aliases, %d replacement aliases, %d native)\n",
              SHIM_PATH, nrow(alias), sum(alias$replacement),
              sum(BACKENDS$status == "native")))
}

# check -----------------------------------------------------------------

cmd_check <- function() {
  out <- system2("git", c("ls-remote", UPSTREAM_REPO, "HEAD"), stdout = TRUE)
  head <- sub("\\s.*$", "", out[[1]])
  cat(sprintf("recorded: %s\nupstream: %s\n", UPSTREAM_COMMIT, head))
  if (identical(head, UPSTREAM_COMMIT)) {
    cat("up to date with upstream HEAD\n")
  } else {
    cat("upstream has moved; to rebase: bump UPSTREAM_COMMIT, run `import`, review the diff\n")
  }
}

# import ----------------------------------------------------------------

cmd_import <- function(checkout) {
  stopifnot(dir.exists(checkout))
  at <- system2("git", c("-C", checkout, "rev-parse", "HEAD"), stdout = TRUE)
  if (!identical(at, UPSTREAM_COMMIT)) {
    stop(sprintf("checkout %s is at %s, not UPSTREAM_COMMIT %s;\ncheck out the recorded commit (or bump UPSTREAM_COMMIT first)",
                 checkout, at, UPSTREAM_COMMIT))
  }
  cmd <- sprintf("git -C %s archive %s %s | tar -x -C .",
                 shQuote(checkout), UPSTREAM_COMMIT,
                 paste(shQuote(UPSTREAM_SUBSET), collapse = " "))
  if (system(cmd) != 0L) stop("archive extraction failed")
  file.remove(UPSTREAM_DROPPED[file.exists(UPSTREAM_DROPPED)])
  cat(sprintf("imported upstream subset at %s\n", UPSTREAM_COMMIT))
  cmd_rename()
  cmd_shims()
  cat("review the diff: upstream files should show only ci_* renames\n")
}

# main ------------------------------------------------------------------

main <- function(args) {
  if (!file.exists("DESCRIPTION") || !dir.exists("R")) {
    stop("run from the charr package root")
  }
  mode <- if (length(args)) args[[1]] else "help"
  switch(mode,
    check = cmd_check(),
    import = cmd_import(args[[2]]),
    rename = cmd_rename(),
    shims = cmd_shims(),
    cat("usage: Rscript tools/import-upstream.R check | import <stringr-checkout> | rename | shims\n")
  )
}

main(commandArgs(trailingOnly = TRUE))
