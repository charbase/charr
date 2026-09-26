# Driver for `make test-gctorture`. Runs a focused set of stringr operations
# under gctorture(TRUE) on the two optimized backends, so every allocation
# triggers a collection and an unprotected SEXP is reclaimed at once instead of
# by chance. Each tortured result is compared with the same call made without
# torture on a fresh copy of the same input, and one native error path runs
# under torture as well. The inputs are short because torture multiplies the
# cost of every allocation.
#
# Development only; charr must already be installed in the library path.
#
# Usage, from the package root:
#   Rscript tools/gctorture-tests.R [base] [altrep]

arguments <- commandArgs(trailingOnly = TRUE)
backends <- if (length(arguments) > 0L) arguments else c("base", "altrep")
if (!all(backends %in% c("base", "altrep"))) {
  stop("backends must be base and/or altrep", call. = FALSE)
}

suppressPackageStartupMessages(library(charr))
# Workers never reach R, so torture has nothing to add to threaded runs.
charr_threads(1)

latin1 <- iconv("caf\u00e9 cr\u00e8me", "UTF-8", "latin1")
Encoding(latin1) <- "latin1"
stopifnot(identical(Encoding(latin1), "latin1"))

values <- c(
  "banana split", NA, "", "na\u00efve caf\u00e9", "\u65e5\u672c \u8a9e",
  latin1, "  Apple pie  ", "A"
)

# Each input kind builds a fresh vector, so the untortured call cannot leave
# state (a materialized ALTREP payload, a cached conversion) on the input the
# tortured call reads.
input_kinds <- list(
  base = list(character = function() values),
  altrep = list(
    character = function() values,
    charvec = function() charport::as_charvec(values)
  )
)

cases <- list(
  str_detect = function(x) str_detect(x, "[a\u00e9]"),
  str_detect_fixed = function(x) str_detect(x, fixed("an")),
  str_count = function(x) str_count(x, "a"),
  str_replace_all = function(x) str_replace_all(x, "(a|\u00e9)", "[\\1]"),
  str_split = function(x) str_split(x, " "),
  str_sub = function(x) str_sub(x, 2L, -2L),
  str_sub_assign = function(x) {
    str_sub(x, 1L, 1L) <- "Z"
    x
  },
  str_order = function(x) str_order(x, locale = "en"),
  str_sort = function(x) str_sort(x, locale = "en"),
  str_unique = function(x) str_unique(x, locale = "en", strength = 1L),
  str_to_upper = function(x) str_to_upper(x),
  str_to_title = function(x) str_to_title(x),
  str_c = function(x) str_c(x, "-", x),
  str_c_collapse = function(x) str_c(x[!is.na(x)], collapse = "|"),
  str_pad = function(x) str_pad(x, 14L, side = "both"),
  str_trim = function(x) str_trim(x),
  str_length = function(x) str_length(x),
  str_locate_all = function(x) str_locate_all(x, "a"),
  str_extract_all = function(x) str_extract_all(x, "\\w+"),
  str_wrap = function(x) str_wrap(x, width = 5L)
)

# Every second pattern is invalid, so the operation fails partway through the
# vector, after earlier elements matched, with its native owners live.
error_case <- function(x) {
  str_detect(x, rep_len(c("a", "("), length(x)))
}

under_torture <- function(code) {
  gctorture(TRUE)
  on.exit(gctorture(FALSE), add = TRUE)
  code
}

error_message <- function(code) {
  tryCatch({
    code
    NA_character_
  }, error = function(condition) conditionMessage(condition))
}

failures <- character()
record <- function(label, ok) {
  message(if (ok) "ok       " else "MISMATCH ", label)
  if (!ok) {
    failures <<- c(failures, label)
  }
}

started <- proc.time()[["elapsed"]]
for (backend in backends) {
  charr_backend(backend)
  kinds <- input_kinds[[backend]]
  for (kind in names(kinds)) {
    make_input <- kinds[[kind]]
    for (name in names(cases)) {
      case <- cases[[name]]
      expected <- case(make_input())
      input <- make_input()
      # identical() reads every element of both results, which materializes
      # ALTREP results while torture is still on.
      same <- under_torture({
        result <- case(input)
        identical(result, expected)
      })
      record(paste(backend, kind, name, sep = "/"), same)
    }

    expected <- error_message(error_case(make_input()))
    if (is.na(expected)) {
      stop("the error case did not raise an error", call. = FALSE)
    }
    input <- make_input()
    tortured <- under_torture(error_message(error_case(input)))
    record(
      paste(backend, kind, "error", sep = "/"),
      identical(tortured, expected)
    )
  }
}
gctorture(FALSE)
elapsed <- proc.time()[["elapsed"]] - started

if (length(failures) > 0L) {
  message(
    "gctorture: ", length(failures), " mismatch(es) after ",
    round(elapsed), "s: ", paste(failures, collapse = ", ")
  )
  quit(status = 1L)
}
message("gctorture: all results agree (", round(elapsed), "s)")
