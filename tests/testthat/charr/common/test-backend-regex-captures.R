# Charr-owned tests for Reader-backed regex locate and match operations.

regex_capture_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_regex_capture_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

regex_capture_conditions <- function(expr) {
  warnings <- character()
  value <- withCallingHandlers(
    force(expr),
    warning = function(condition) {
      warnings <<- c(warnings, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = warnings)
}

test_that("regex locate and match accept one exact charvec alias", {
  values <- c("a", "b", "c")
  alias <- charport::as_charvec(values)

  located <- with_test_backend(
    TRUE, charr_test_leaf("ci_locate_first_regex")(alias, alias)
  )
  matched <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_first_regex")(alias, alias)
  )

  expect_identical(
    located, stringi::stri_locate_first_regex(values, values)
  )
  expect_regex_capture_unmaterialized(matched)
  expect_identical(
    matched, stringi::stri_match_first_regex(values, values)
  )
  expect_regex_capture_unmaterialized(alias)
})

test_that("regex captures preserve values, names, and matrix attributes", {
  values <- c("\U0001f600a", "x", NA_character_)
  pattern_value <- "(?<emoji>\U0001f600)?(?<letter>a)?"
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(pattern_value)

  for (get_length in c(FALSE, TRUE)) {
    first <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_locate_first_regex")(
        subject, pattern, capture_groups = TRUE,
        get_length = get_length
      )
    )
    all <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_locate_all_regex")(
        subject, pattern, capture_groups = TRUE,
        get_length = get_length
      )
    )
    expect_identical(
      first,
      stringi::stri_locate_first_regex(
        values, pattern_value, capture_groups = TRUE,
        get_length = get_length
      )
    )
    expect_identical(
      all,
      stringi::stri_locate_all_regex(
        values, pattern_value, capture_groups = TRUE,
        get_length = get_length
      )
    )
  }

  first_match <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_match_first_regex")(
      subject, pattern, cg_missing = "MISS"
    )
  )
  all_match <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_match_all_regex")(
      subject, pattern, cg_missing = "MISS"
    )
  )

  expect_regex_capture_unmaterialized(first_match)
  expect_altrep_unmaterialized_list(all_match)
  expect_identical(
    first_match,
    stringi::stri_match_first_regex(
      values, pattern_value, cg_missing = "MISS"
    )
  )
  expect_identical(
    all_match,
    stringi::stri_match_all_regex(
      values, pattern_value, cg_missing = "MISS"
    )
  )
  expect_regex_capture_unmaterialized(subject)
  expect_regex_capture_unmaterialized(pattern)
})

test_that("vectorized regex patterns keep per-element capture metadata", {
  values <- c("a1", "b", "c3")
  patterns <- c(
    "(?<letter>a)(?<digit>1)",
    "(?<only>b)",
    "(?<letter>c)(?<number>3)"
  )
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  located <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_locate_all_regex")(
      subject, pattern, capture_groups = TRUE
    )
  )
  matched <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_all_regex")(subject, pattern)
  )

  expect_identical(
    located,
    stringi::stri_locate_all_regex(
      values, patterns, capture_groups = TRUE
    )
  )
  expect_identical(
    matched, stringi::stri_match_all_regex(values, patterns)
  )
  expect_altrep_unmaterialized_list(matched)
  expect_regex_capture_unmaterialized(subject)
  expect_regex_capture_unmaterialized(pattern)
})

test_that("regex captures preserve empty-input and no-match shapes", {
  subject <- charport::as_charvec(character())
  pattern_value <- "(?<left>a)(?<right>b)?"
  pattern <- charport::as_charvec(pattern_value)

  first <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_first_regex")(subject, pattern)
  )
  all <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_all_regex")(subject, pattern)
  )
  expect_regex_capture_unmaterialized(first)
  expect_identical(
    first,
    stringi::stri_match_first_regex(character(), pattern_value)
  )
  expect_identical(
    all,
    stringi::stri_match_all_regex(character(), pattern_value)
  )

  no_match <- charport::as_charvec("zzz")
  omitted_location <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_locate_all_regex")(
      no_match, pattern, omit_no_match = TRUE,
      capture_groups = TRUE, get_length = TRUE
    )
  )
  omitted <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_match_all_regex")(
      no_match, pattern, omit_no_match = TRUE
    )
  )
  expect_regex_capture_unmaterialized(omitted[[1]])
  expect_identical(
    omitted_location,
    stringi::stri_locate_all_regex(
      "zzz", pattern_value, omit_no_match = TRUE,
      capture_groups = TRUE, get_length = TRUE
    )
  )
  expect_identical(
    omitted,
    stringi::stri_match_all_regex(
      "zzz", pattern_value, omit_no_match = TRUE
    )
  )
  expect_regex_capture_unmaterialized(subject)
  expect_regex_capture_unmaterialized(pattern)
  expect_regex_capture_unmaterialized(no_match)
})

test_that("regex captures preserve warnings, recycling, and lazy errors", {
  values <- c("a", NA_character_)
  subject <- charport::as_charvec(values)
  empty_pattern <- charport::as_charvec("")

  expect_identical(
    regex_capture_conditions(
      stringi::stri_locate_all_regex(values, "")
    ),
    regex_capture_conditions(
      with_test_backend(
        TRUE, charr_test_leaf("ci_locate_all_regex")(subject, empty_pattern)
      )
    )
  )
  expect_identical(
    regex_capture_conditions(
      stringi::stri_match_first_regex(values, "")
    ),
    regex_capture_conditions(
      with_test_backend(
        TRUE, charr_test_leaf("ci_match_first_regex")(subject, empty_pattern)
      )
    )
  )

  recycled_values <- c("a", "b", "aa")
  recycled_patterns <- c("(a)", "(b)")
  recycled_subject <- charport::as_charvec(recycled_values)
  recycled_pattern <- charport::as_charvec(recycled_patterns)
  expect_identical(
    regex_capture_conditions(
      stringi::stri_match_all_regex(
        recycled_values, recycled_patterns
      )
    ),
    regex_capture_conditions(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_match_all_regex")(
          recycled_subject, recycled_pattern
        )
      )
    )
  )

  missing <- charport::as_charvec(NA_character_)
  invalid <- charport::as_charvec("[")
  expect_error(
    stringi::stri_locate_first_regex(NA_character_, "["),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_error(
    with_test_backend(
      TRUE, charr_test_leaf("ci_locate_first_regex")(missing, invalid)
    ),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_error(
    stringi::stri_match_first_regex(NA_character_, "["),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_error(
    with_test_backend(
      TRUE, charr_test_leaf("ci_match_first_regex")(missing, invalid)
    ),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_regex_capture_unmaterialized(subject)
  expect_regex_capture_unmaterialized(empty_pattern)
  expect_regex_capture_unmaterialized(recycled_subject)
  expect_regex_capture_unmaterialized(recycled_pattern)
  expect_regex_capture_unmaterialized(missing)
  expect_regex_capture_unmaterialized(invalid)
})

test_that("regex captures preserve marked-input normalization", {
  latin1 <- regex_capture_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- regex_capture_marked_string(c(0x80), "UTF-8")
  values <- c(latin1, "\ufeffa", malformed, NA_character_)
  locate_patterns <- c("\u00e9", "^\ufeff", "\ufffd", "x")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(locate_patterns)

  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_locate_all_regex")(subject, pattern)
    ),
    stringi::stri_locate_all_regex(values, locate_patterns)
  )

  first <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_first_regex")(subject, "(.)")
  )
  all <- with_test_backend(
    TRUE, charr_test_leaf("ci_match_all_regex")(subject, "(.)")
  )
  expect_regex_capture_unmaterialized(first)
  expect_altrep_charvec_list(all)
  expect_identical(
    first, stringi::stri_match_first_regex(values, "(.)")
  )
  expect_identical(
    all, stringi::stri_match_all_regex(values, "(.)")
  )
  expect_regex_capture_unmaterialized(subject)
  expect_regex_capture_unmaterialized(pattern)
})

test_that("regex match preserves the cg_missing encoding mark", {
  latin1_missing <- regex_capture_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  missing <- charport::as_charvec(latin1_missing)

  first <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_match_first_regex")(
      "a", "(z)?", cg_missing = missing
    )
  )
  all <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_match_all_regex")(
      "a", "(z)?", cg_missing = missing
    )
  )
  expected_first <- stringi::stri_match_first_regex(
    "a", "(z)?", cg_missing = latin1_missing
  )
  expected_all <- stringi::stri_match_all_regex(
    "a", "(z)?", cg_missing = latin1_missing
  )

  expect_regex_capture_unmaterialized(first)
  expect_regex_capture_unmaterialized(all[[1]])
  expect_identical(Encoding(first[1, 2]), Encoding(expected_first[1, 2]))
  expect_identical(charToRaw(first[1, 2]), charToRaw(expected_first[1, 2]))
  expect_identical(
    Encoding(all[[1]][1, 2]), Encoding(expected_all[[1]][1, 2])
  )
  expect_identical(
    charToRaw(all[[1]][1, 2]), charToRaw(expected_all[[1]][1, 2])
  )
  expect_regex_capture_unmaterialized(missing)
})

test_that("regex captures reject bytes-marked subjects and patterns", {
  bytes <- regex_capture_marked_string(c(0xff, 0xfe), "bytes")
  bytes_input <- charport::as_charvec(bytes)

  expect_error(
    stringi::stri_locate_all_regex(bytes, "x"), "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE, charr_test_leaf("ci_locate_all_regex")(bytes_input, "x")
    ),
    "bytes encoding"
  )
  expect_error(
    stringi::stri_match_all_regex(bytes, "(.)"), "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE, charr_test_leaf("ci_match_all_regex")(bytes_input, "(.)")
    ),
    "bytes encoding"
  )
  expect_error(
    stringi::stri_match_first_regex("x", bytes), "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE, charr_test_leaf("ci_match_first_regex")("x", bytes_input)
    ),
    "bytes encoding"
  )
  expect_regex_capture_unmaterialized(bytes_input)
})
