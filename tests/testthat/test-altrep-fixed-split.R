# Charr-owned tests for Reader-backed fixed splitting.
# These are not imported from stringr.

fixed_split_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_fixed_split_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

capture_fixed_split_warnings <- function(code) {
  warnings <- character()
  value <- withCallingHandlers(
    force(code),
    warning = function(cnd) {
      warnings <<- c(warnings, conditionMessage(cnd))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = warnings)
}

test_that("fixed split returns unmaterialized fields and supports chaining", {
  values <- c("caf\u00e9|tea", "a|b|c", NA_character_, "plain")
  patterns <- c("|", "|", "x", "-")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  actual <- with_altrep(
    TRUE,
    charr:::ci_split_fixed(subject, pattern)
  )
  expected <- stringi::stri_split_fixed(values, patterns)

  expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
  invisible(lapply(actual, expect_fixed_split_unmaterialized))
  expect_identical(actual, expected)
  expect_identical(
    with_altrep(TRUE, charr:::ci_count_fixed(actual[[2]], "|")),
    integer(3)
  )
  expect_fixed_split_unmaterialized(actual[[2]])
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
})

test_that("fixed split handles aliases and every matcher lane", {
  long_pattern <- "0123456789abcdef"
  values <- c("a-a-a", "abcabcabc", paste0(long_pattern, "x", long_pattern))
  patterns <- c("-", "abc", long_pattern)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expect_identical(
    with_altrep(TRUE, charr:::ci_split_fixed(subject, pattern)),
    stringi::stri_split_fixed(values, patterns)
  )
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)

  shared_values <- c("same", "two", long_pattern)
  shared <- charport::as_charvec(shared_values)
  expect_identical(
    with_altrep(TRUE, charr:::ci_split_fixed(shared, shared)),
    stringi::stri_split_fixed(shared_values, shared_values)
  )
  expect_fixed_split_unmaterialized(shared)
})

test_that("fixed split preserves n and tokens_only state transitions", {
  values <- c("a_b_c__d", "_a__b_", "abc")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("_")

  cases <- list(
    list(n = 4L, omit_empty = FALSE, tokens_only = FALSE),
    list(n = 4L, omit_empty = FALSE, tokens_only = TRUE),
    list(n = 4L, omit_empty = TRUE, tokens_only = TRUE),
    list(n = 2L, omit_empty = TRUE, tokens_only = FALSE),
    list(n = 2L, omit_empty = TRUE, tokens_only = TRUE),
    list(n = c(-1L, 0L, 1L), omit_empty = FALSE, tokens_only = FALSE),
    list(n = NA_integer_, omit_empty = FALSE, tokens_only = FALSE)
  )

  for (args in cases) {
    actual <- with_altrep(
      TRUE,
      do.call(
        charr:::ci_split_fixed,
        c(list(str = subject, pattern = pattern), args)
      )
    )
    expected <- do.call(
      stringi::stri_split_fixed,
      c(list(str = values, pattern = "_"), args)
    )
    expect_identical(actual, expected)
  }
  boundary_subject <- charport::as_charvec(values[1])
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        boundary_subject, pattern, n = .Machine$integer.max
      )
    ),
    "incorrect argument `n`; value too large",
    fixed = TRUE
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        charport::as_charvec(""), pattern,
        n = .Machine$integer.max
      )
    ),
    list("")
  )
  expect_fixed_split_unmaterialized(boundary_subject)
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
})

test_that("fixed split preserves all omit_empty modes", {
  values <- c("a__b", "", "_a_", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("_")

  for (omit_empty in list(FALSE, TRUE, NA)) {
    expect_identical(
      with_altrep(
        TRUE,
        charr:::ci_split_fixed(
          subject, pattern, omit_empty = omit_empty
        )
      ),
      stringi::stri_split_fixed(
        values, "_", omit_empty = omit_empty
      )
    )
  }
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
})

test_that("fixed split preserves simplify values and zero shapes", {
  values <- c("a_b", "x", "")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("_")

  for (simplify in list(TRUE, NA)) {
    actual <- with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        subject, pattern, n = 3L, simplify = simplify
      )
    )
    expect_fixed_split_unmaterialized(actual)
    expect_identical(
      actual,
      stringi::stri_split_fixed(
        values, "_", n = 3L, simplify = simplify
      )
    )
  }

  bytes <- fixed_split_marked(c(0xff, 0xfe), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  empty_pattern <- charport::as_charvec(character())
  expect_identical(
    expect_no_warning(
      with_altrep(
        TRUE,
        charr:::ci_split_fixed(bytes_subject, empty_pattern)
      )
    ),
    list()
  )
  matrix_result <- expect_no_warning(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        bytes_subject, empty_pattern, simplify = NA
      )
    )
  )
  expect_fixed_split_unmaterialized(matrix_result)
  expect_identical(matrix_result, matrix(character(), 0L, 0L))
  matrix_with_min <- expect_no_warning(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        bytes_subject, empty_pattern, n = 3L, simplify = TRUE
      )
    )
  )
  expect_fixed_split_unmaterialized(matrix_with_min)
  expected_with_min <- stringi::stri_split_fixed(
    bytes, character(), n = 3L, simplify = TRUE
  )
  expect_identical(dim(expected_with_min), c(0L, 3L))
  expect_identical(matrix_with_min, expected_with_min)
  expect_fixed_split_unmaterialized(bytes_subject)
  expect_fixed_split_unmaterialized(empty_pattern)
})

test_that("fixed split preserves repeated-delimiter matching", {
  values <- c("aaaa", "ababa", "xxxx")
  patterns <- c("aa", "aba", "xx")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expect_identical(
    with_altrep(TRUE, charr:::ci_split_fixed(subject, pattern)),
    stringi::stri_split_fixed(values, patterns)
  )

  options <- stringi::stri_opts_fixed(overlap = TRUE)
  expected <- capture_fixed_split_warnings(
    stringi::stri_split_fixed(values, patterns, opts_fixed = options)
  )
  actual <- capture_fixed_split_warnings(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(
        subject, pattern, opts_fixed = options
      )
    )
  )
  expect_identical(actual, expected)
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
})

test_that("fixed split preserves recycling and empty-pattern warnings", {
  values <- c("a_b", "x_y", "z")
  patterns <- c("_", "-")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expected <- capture_fixed_split_warnings(
    stringi::stri_split_fixed(values, patterns)
  )
  actual <- capture_fixed_split_warnings(
    with_altrep(TRUE, charr:::ci_split_fixed(subject, pattern))
  )
  expect_identical(actual, expected)

  empty_pattern <- charport::as_charvec("")
  expected <- capture_fixed_split_warnings(
    stringi::stri_split_fixed(values, "")
  )
  actual <- capture_fixed_split_warnings(
    with_altrep(
      TRUE,
      charr:::ci_split_fixed(subject, empty_pattern)
    )
  )
  expect_identical(actual, expected)
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
  expect_fixed_split_unmaterialized(empty_pattern)
})

test_that("fixed split normalizes Latin-1 and BOM inputs", {
  latin1 <- fixed_split_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x7c, 0x74, 0x68, 0xe9),
    "latin1"
  )
  bom_pattern <- enc2utf8("\ufeff|")
  values <- c(latin1, "\ufeffa|b")
  patterns <- c("|", bom_pattern)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  actual <- with_altrep(TRUE, charr:::ci_split_fixed(subject, pattern))
  expect_identical(actual, stringi::stri_split_fixed(values, patterns))
  expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
  expect_fixed_split_unmaterialized(subject)
  expect_fixed_split_unmaterialized(pattern)
})

test_that("fixed split preserves malformed UTF-8 and long field runs", {
  malformed <- fixed_split_marked(
    c(0x61, 0xff, 0x7c, 0x62), "UTF-8"
  )
  malformed_subject <- charport::as_charvec(malformed)
  fields <- with_altrep(
    TRUE,
    charr:::ci_split_fixed(malformed_subject, "|")
  )[[1]]
  expect_fixed_split_unmaterialized(fields)
  expect_identical(
    lapply(fields, charToRaw),
    list(as.raw(c(0x61, 0xff)), as.raw(0x62))
  )

  value <- paste(rep(c("a", "", "\u00e9"), 128L), collapse = "|")
  subject <- charport::as_charvec(rep(value, 4L))
  actual <- with_altrep(
    TRUE,
    charr:::ci_split_fixed(subject, "|", omit_empty = NA)
  )
  expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
  expect_identical(
    actual,
    rep(list(rep(c("a", NA_character_, "\u00e9"), 128L)), 4L)
  )
  expect_fixed_split_unmaterialized(malformed_subject)
  expect_fixed_split_unmaterialized(subject)
})

test_that("fixed split rejects bytes after preserving zero recycling", {
  bytes <- fixed_split_marked(c(0xff, 0xfe), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  bytes_pattern <- charport::as_charvec(bytes)
  plain_subject <- charport::as_charvec("plain")
  plain_pattern <- charport::as_charvec("x")

  expect_error(
    with_altrep(TRUE, charr:::ci_split_fixed(bytes_subject, plain_pattern)),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_split_fixed(plain_subject, bytes_pattern)),
    "bytes encoding"
  )
  expect_fixed_split_unmaterialized(bytes_subject)
  expect_fixed_split_unmaterialized(bytes_pattern)
  expect_fixed_split_unmaterialized(plain_subject)
  expect_fixed_split_unmaterialized(plain_pattern)
})
