# Charr-owned tests for Reader-backed collation character outputs.

coll_character_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_coll_character_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

expect_coll_character_list_unmaterialized <- function(x) {
  expect_true(all(vapply(x, charport::is_charvec, logical(1))))
  expect_true(all(vapply(
    x,
    function(value) !charport::charport_info(value)$is_materialized,
    logical(1)
  )))
}

coll_character_conditions <- function(expr) {
  warnings <- character()
  error <- NULL
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        warnings <<- c(warnings, conditionMessage(condition))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      error <<- conditionMessage(condition)
      NULL
    }
  )
  list(value = value, warnings = warnings, error = error)
}

test_that("collation character routes share exact aliases and chain outputs", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  values <- c("a", "b", "å")
  alias <- charport::as_charvec(values)
  opts <- list(locale = "en", strength = 3L)

  first <- charr:::ci_extract_first_coll(
    alias, alias, opts_collator = opts
  )
  all <- charr:::ci_extract_all_coll(
    alias, alias, opts_collator = opts
  )
  split <- charr:::ci_split_coll(
    first, alias, n = 2L, opts_collator = opts
  )
  expect_coll_character_unmaterialized(first)
  expect_coll_character_list_unmaterialized(all)
  expect_coll_character_list_unmaterialized(split)
  expect_identical(
    first,
    stringi::stri_extract_first_coll(
      values, values, opts_collator = opts
    )
  )
  expect_identical(
    all,
    stringi::stri_extract_all_coll(
      values, values, opts_collator = opts
    )
  )
  expect_identical(
    split,
    stringi::stri_split_coll(
      values, values, n = 2L, opts_collator = opts
    )
  )
  expect_coll_character_unmaterialized(alias)
})

test_that("collation extract preserves tailored slices and output shapes", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  values <- c("😀ä-a-A", "u\u0308xÜ", "none", "", NA_character_)
  patterns <- c("a", "ü", "a", "x", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  first <- charr:::ci_extract_first_coll(
    subject, pattern, opts_collator = opts
  )
  all <- charr:::ci_extract_all_coll(
    subject, pattern, opts_collator = opts
  )
  omitted <- charr:::ci_extract_all_coll(
    subject, pattern, omit_no_match = TRUE,
    opts_collator = opts
  )
  simplified <- charr:::ci_extract_all_coll(
    subject, pattern, simplify = TRUE, opts_collator = opts
  )
  simplified_na <- charr:::ci_extract_all_coll(
    subject, pattern, simplify = NA, opts_collator = opts
  )

  expect_coll_character_unmaterialized(first)
  expect_coll_character_list_unmaterialized(all)
  expect_coll_character_list_unmaterialized(omitted)
  expect_coll_character_unmaterialized(simplified)
  expect_coll_character_unmaterialized(simplified_na)
  expect_identical(
    first,
    stringi::stri_extract_first_coll(
      values, patterns, opts_collator = opts
    )
  )
  expect_identical(
    all,
    stringi::stri_extract_all_coll(
      values, patterns, opts_collator = opts
    )
  )
  expect_identical(
    omitted,
    stringi::stri_extract_all_coll(
      values, patterns, omit_no_match = TRUE,
      opts_collator = opts
    )
  )
  expect_identical(
    simplified,
    stringi::stri_extract_all_coll(
      values, patterns, simplify = TRUE,
      opts_collator = opts
    )
  )
  expect_identical(
    simplified_na,
    stringi::stri_extract_all_coll(
      values, patterns, simplify = NA,
      opts_collator = opts
    )
  )
  expect_coll_character_unmaterialized(subject)
  expect_coll_character_unmaterialized(pattern)
})

test_that("collation split preserves fields, limits, and simplify padding", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  values <- c("😀äAäB", "u\u0308Üx", "åaaÅ", "none", "", NA_character_)
  patterns <- c("a", "ü", "å", "a", "x", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  all <- charr:::ci_split_coll(
    subject, pattern, opts_collator = opts
  )
  limited <- charr:::ci_split_coll(
    subject, pattern, n = 2L, opts_collator = opts
  )
  tokens <- charr:::ci_split_coll(
    subject, pattern, n = 2L, tokens_only = TRUE,
    opts_collator = opts
  )
  omit_na <- charr:::ci_split_coll(
    subject, pattern, n = 3L, omit_empty = NA,
    opts_collator = opts
  )
  simplified <- charr:::ci_split_coll(
    subject, pattern, n = 2L, simplify = TRUE,
    opts_collator = opts
  )
  simplified_na <- charr:::ci_split_coll(
    subject, pattern, n = 2L, simplify = NA,
    opts_collator = opts
  )

  expect_coll_character_list_unmaterialized(all)
  expect_coll_character_list_unmaterialized(limited)
  expect_coll_character_list_unmaterialized(tokens)
  expect_coll_character_list_unmaterialized(omit_na)
  expect_coll_character_unmaterialized(simplified)
  expect_coll_character_unmaterialized(simplified_na)
  expect_identical(
    all, stringi::stri_split_coll(values, patterns, opts_collator = opts)
  )
  expect_identical(
    limited,
    stringi::stri_split_coll(
      values, patterns, n = 2L, opts_collator = opts
    )
  )
  expect_identical(
    tokens,
    stringi::stri_split_coll(
      values, patterns, n = 2L, tokens_only = TRUE,
      opts_collator = opts
    )
  )
  expect_identical(
    omit_na,
    stringi::stri_split_coll(
      values, patterns, n = 3L, omit_empty = NA,
      opts_collator = opts
    )
  )
  expect_identical(
    simplified,
    stringi::stri_split_coll(
      values, patterns, n = 2L, simplify = TRUE,
      opts_collator = opts
    )
  )
  expect_identical(
    simplified_na,
    stringi::stri_split_coll(
      values, patterns, n = 2L, simplify = NA,
      opts_collator = opts
    )
  )
  expect_coll_character_unmaterialized(subject)
  expect_coll_character_unmaterialized(pattern)
})

test_that("collation character routes preserve input encoding behavior", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  latin1 <- coll_character_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- coll_character_marked_string(c(0x80), "UTF-8")
  values <- c(latin1, "\ufeffapple", malformed, NA_character_)
  patterns <- c("é", "a", "\ufffd", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  extracted <- charr:::ci_extract_first_coll(subject, pattern)
  split <- charr:::ci_split_coll(subject, pattern, n = 2L)
  expected_extracted <- stringi::stri_extract_first_coll(values, patterns)
  expected_split <- stringi::stri_split_coll(values, patterns, n = 2L)

  expect_coll_character_unmaterialized(extracted)
  expect_coll_character_list_unmaterialized(split)
  expect_identical(extracted, expected_extracted)
  expect_identical(split, expected_split)
  expect_identical(Encoding(extracted), Encoding(expected_extracted))
  expect_identical(charToRaw(extracted[[1]]), charToRaw(expected_extracted[[1]]))
  expect_identical(charToRaw(extracted[[3]]), charToRaw(expected_extracted[[3]]))
  expect_coll_character_unmaterialized(subject)
  expect_coll_character_unmaterialized(pattern)
})

test_that("collation character warnings and errors match stringi", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  values <- c("a", "b", NA_character_)
  subject <- charport::as_charvec(values)
  empty <- charport::as_charvec("")

  expect_identical(
    coll_character_conditions(
      stringi::stri_extract_all_coll(values, "")
    ),
    coll_character_conditions(
      charr:::ci_extract_all_coll(subject, empty)
    )
  )
  expect_identical(
    coll_character_conditions(
      stringi::stri_split_coll(values, "")
    ),
    coll_character_conditions(
      charr:::ci_split_coll(subject, empty)
    )
  )
  recycled_values <- c("a", "b", "c")
  recycled_patterns <- c("a", "b")
  recycled_subject <- charport::as_charvec(recycled_values)
  recycled_pattern <- charport::as_charvec(recycled_patterns)
  expect_identical(
    coll_character_conditions(
      stringi::stri_split_coll(
        recycled_values, recycled_patterns, n = c(1L, 2L)
      )
    ),
    coll_character_conditions(
      charr:::ci_split_coll(
        recycled_subject, recycled_pattern, n = c(1L, 2L)
      )
    )
  )
  numeric <- list(numeric = TRUE)
  expect_identical(
    coll_character_conditions(
      stringi::stri_split_coll(
        "x02", "x2", n = .Machine$integer.max,
        opts_collator = numeric
      )
    )$error,
    coll_character_conditions(
      charr:::ci_split_coll(
        "x02", "x2", n = .Machine$integer.max,
        opts_collator = numeric
      )
    )$error
  )
  expect_identical(
    coll_character_conditions(
      stringi::stri_extract_first_coll(
        "x02", "x2", opts_collator = numeric
      )
    )$error,
    coll_character_conditions(
      charr:::ci_extract_first_coll(
        "x02", "x2", opts_collator = numeric
      )
    )$error
  )
  expect_coll_character_unmaterialized(subject)
  expect_coll_character_unmaterialized(empty)
  expect_coll_character_unmaterialized(recycled_subject)
  expect_coll_character_unmaterialized(recycled_pattern)
})

test_that("collation character routes reject bytes-marked records", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  bytes <- coll_character_marked_string(c(0xff, 0xfe), "bytes")
  bytes_input <- charport::as_charvec(bytes)

  cases <- list(
    list(
      oracle = function() stringi::stri_extract_first_coll(bytes, "x"),
      actual = function() charr:::ci_extract_first_coll(bytes_input, "x")
    ),
    list(
      oracle = function() stringi::stri_split_coll("x", bytes),
      actual = function() charr:::ci_split_coll("x", bytes_input)
    )
  )

  for (case in cases) {
    expected <- coll_character_conditions(case$oracle())
    actual <- coll_character_conditions(case$actual())
    expect_identical(actual$warnings, expected$warnings)
    expect_identical(actual$error, expected$error)
  }
  expect_coll_character_unmaterialized(bytes_input)
})
