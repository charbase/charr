#!/usr/bin/env Rscript

# On Windows, R can replace raw supplementary characters with U+FFFD when the
# same string literal contains a \u or \U escape. Scan the source spelling so
# the check runs before R parses the affected literal.

CP_NEWLINE <- utf8ToInt("\n")
CP_COMMENT <- utf8ToInt("#")
CP_BACKSLASH <- utf8ToInt("\\")
CP_DASH <- utf8ToInt("-")
CP_QUOTES <- utf8ToInt("\"'")
CP_RAW_PREFIXES <- utf8ToInt("rR")
CP_RAW_OPENERS <- utf8ToInt("([{")
CP_UNICODE_ESCAPE <- utf8ToInt("uU")

skip_raw_string <- function(code, start) {
  size <- length(code)
  if (start + 2L > size ||
      !code[[start]] %in% CP_RAW_PREFIXES ||
      !code[[start + 1L]] %in% CP_QUOTES) {
    return(NA_integer_)
  }

  quote <- code[[start + 1L]]
  cursor <- start + 2L
  dashes <- 0L
  while (cursor <= size && code[[cursor]] == CP_DASH) {
    dashes <- dashes + 1L
    cursor <- cursor + 1L
  }
  if (cursor > size || !code[[cursor]] %in% CP_RAW_OPENERS) {
    return(NA_integer_)
  }

  opener <- code[[cursor]]
  closer <- switch(
    as.character(opener),
    "40" = 41L,
    "91" = 93L,
    "123" = 125L,
    NA_integer_
  )
  cursor <- cursor + 1L
  while (cursor <= size) {
    if (code[[cursor]] == closer) {
      after <- cursor + 1L
      matched_dashes <- 0L
      while (after <= size && code[[after]] == CP_DASH &&
             matched_dashes < dashes) {
        matched_dashes <- matched_dashes + 1L
        after <- after + 1L
      }
      if (matched_dashes == dashes && after <= size &&
          code[[after]] == quote) {
        return(after + 1L)
      }
    }
    cursor <- cursor + 1L
  }
  NA_integer_
}

scan_file <- function(path) {
  lines <- readLines(path, warn = FALSE, encoding = "UTF-8")
  text <- paste(lines, collapse = "\n")
  code <- utf8ToInt(text)
  if (!any(code > 0xffffL) || !grepl("\\\\[uU]", text, perl = TRUE)) {
    return(character())
  }
  size <- length(code)
  cursor <- 1L
  line <- 1L
  problems <- character()

  while (cursor <= size) {
    current <- code[[cursor]]
    if (current == CP_NEWLINE) {
      line <- line + 1L
      cursor <- cursor + 1L
      next
    }
    if (current == CP_COMMENT) {
      while (cursor <= size && code[[cursor]] != CP_NEWLINE) {
        cursor <- cursor + 1L
      }
      next
    }

    raw_end <- skip_raw_string(code, cursor)
    if (!is.na(raw_end)) {
      line <- line + sum(code[cursor:(raw_end - 1L)] == CP_NEWLINE)
      cursor <- raw_end
      next
    }

    if (!current %in% CP_QUOTES) {
      cursor <- cursor + 1L
      next
    }

    quote <- current
    literal_line <- line
    cursor <- cursor + 1L
    has_supplementary <- FALSE
    has_unicode_escape <- FALSE

    while (cursor <= size) {
      current <- code[[cursor]]
      if (current == quote) {
        cursor <- cursor + 1L
        break
      }
      if (current == CP_BACKSLASH) {
        slash_start <- cursor
        while (cursor <= size && code[[cursor]] == CP_BACKSLASH) {
          cursor <- cursor + 1L
        }
        slash_count <- cursor - slash_start
        if (slash_count %% 2L == 1L && cursor <= size) {
          if (code[[cursor]] %in% CP_UNICODE_ESCAPE) {
            has_unicode_escape <- TRUE
          }
          if (code[[cursor]] == CP_NEWLINE) {
            line <- line + 1L
          }
          cursor <- cursor + 1L
        }
        next
      }
      if (current > 0xffffL) {
        has_supplementary <- TRUE
      }
      if (current == CP_NEWLINE) {
        line <- line + 1L
      }
      cursor <- cursor + 1L
    }

    if (has_supplementary && has_unicode_escape) {
      problems <- c(
        problems,
        sprintf(
          "%s:%d: raw supplementary character and Unicode escape share one literal",
          path, literal_line
        )
      )
    }
  }
  problems
}

roots <- c("R", "tests", "tools", "vignettes")
files <- sort(unique(unlist(lapply(
  roots,
  function(root) {
    if (!dir.exists(root)) {
      return(character())
    }
    list.files(
      root, pattern = "[.][Rr]$", recursive = TRUE, full.names = TRUE
    )
  }
))))
problems <- unlist(lapply(files, scan_file), use.names = FALSE)

if (length(problems) > 0L) {
  cat(
    "Non-portable R string literals found. Use raw supplementary characters ",
    "or Unicode escapes, but do not mix them in one literal:\n",
    sep = ""
  )
  cat(paste0("  ", problems, collapse = "\n"), "\n", sep = "")
  quit(status = 1L)
}

cat("R Unicode literal portability check passed\n")
