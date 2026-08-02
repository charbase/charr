# charr-owned tests for Reader-backed fixed extract and replacement output.
# These are not imported from stringr.

fixed_output_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_fixed_output_input_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

fixed_output_warnings <- function(expr) {
  messages <- character()
  value <- withCallingHandlers(
    expr,
    warning = function(condition) {
      messages <<- c(messages, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, messages = messages)
}

test_that("fixed first extraction uses unmaterialized inputs and outputs", {
  latin1 <- fixed_output_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c("ababa", "caf\u00e9", NA_character_, "", latin1)
  patterns <- c("a", "\u00e9", "x", "q", "\u00e9")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  first <- with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(subject, pattern))
  expect_altrep_charvec(first)
  expect_altrep_unmaterialized(first)
  expect_identical(
    first,
    with_test_backend(FALSE, charr_test_leaf("ci_extract_first_fixed")(values, patterns))
  )

  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
})

test_that("fixed extraction covers aliases and matcher lanes", {
  alias_values <- c("a", "short", "0123456789abcdef")
  alias <- charport::as_charvec(alias_values)

  actual <- with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(alias, alias))
  expect_altrep_charvec(actual)
  expect_identical(
    actual,
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_extract_first_fixed")(alias_values, alias_values)
    )
  )
  expect_fixed_output_input_unmaterialized(alias)

  subjects <- c("zaq", "xxneedlezz", "--0123456789abcdef--")
  patterns <- c("a", "needle", "0123456789abcdef")
  subject <- charport::as_charvec(subjects)
  pattern <- charport::as_charvec(patterns)
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(subject, pattern)),
    with_test_backend(FALSE, charr_test_leaf("ci_extract_first_fixed")(subjects, patterns))
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
})

test_that("fixed extraction and replacement preserve case-insensitive matching", {
  value <- "\u00c9\u00e9\u00c9"
  pattern_value <- "\u00e9\u00e9"
  subject <- charport::as_charvec(value)
  pattern <- charport::as_charvec(pattern_value)
  extracted <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_all_fixed")(
      subject, pattern, case_insensitive = TRUE, overlap = TRUE
    )
  )
  expect_identical(
    extracted,
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_extract_all_fixed")(
        value, pattern_value, case_insensitive = TRUE, overlap = TRUE
      )
    )
  )

  replaced <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(
      subject, charport::as_charvec("\u00e9"), "x",
      case_insensitive = TRUE
    )
  )
  expect_altrep_charvec(replaced)
  expect_altrep_unmaterialized(replaced)
  expect_identical(
    replaced,
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_replace_all_fixed")(
        value, "\u00e9", "x", case_insensitive = TRUE
      )
    )
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
})

test_that("fixed extract all keeps growable outputs lazy and chains", {
  values <- c("aaaa", "ababa", "none", NA_character_, "")
  patterns <- c("aa", "aba", "x", "x", "x")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_all_fixed")(
      subject, pattern, overlap = TRUE, omit_no_match = TRUE
    )
  )

  expect_altrep_unmaterialized_list(actual)
  chained <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_count_fixed")(actual[[1]], charport::as_charvec("a"))
  )
  expect_identical(chained, c(2L, 2L, 2L))
  expect_identical(
    actual,
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_extract_all_fixed")(
        values, patterns, overlap = TRUE, omit_no_match = TRUE
      )
    )
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
})

test_that("fixed extract all preserves omit and simplify shapes", {
  values <- c("aa", "none", "", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")

  for (omit in c(FALSE, TRUE)) {
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_extract_all_fixed")(
        subject, pattern, omit_no_match = omit
      )
    )
    expected <- with_test_backend(
      FALSE,
      charr_test_leaf("ci_extract_all_fixed")(
        values, "a", omit_no_match = omit
      )
    )
    expect_identical(actual, expected)
  }

  for (simplify in list(TRUE, NA)) {
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_extract_all_fixed")(
        subject, pattern, omit_no_match = TRUE, simplify = simplify
      )
    )
    expect_altrep_charvec(actual)
    expect_altrep_unmaterialized(actual)
    expect_identical(
      actual,
      with_test_backend(
        FALSE,
        charr_test_leaf("ci_extract_all_fixed")(
          values, "a", omit_no_match = TRUE, simplify = simplify
        )
      )
    )
  }
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
})

test_that("fixed extraction preserves warning order and zero-length shapes", {
  values <- c("a", "ba", "ca")
  patterns <- c("", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expected <- fixed_output_warnings(
    with_test_backend(FALSE, charr_test_leaf("ci_extract_first_fixed")(values, patterns))
  )
  actual <- fixed_output_warnings(
    with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(subject, pattern))
  )
  expect_identical(actual, expected)
  expect_identical(
    actual$messages,
    c(
      "longer object length is not a multiple of shorter object length",
      "empty search patterns are not supported"
    )
  )

  bytes <- fixed_output_marked(c(0xff, 0xfe), "bytes")
  zero_subject <- charport::as_charvec(bytes)
  zero_pattern <- charport::as_charvec(character())
  first <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_first_fixed")(zero_subject, zero_pattern)
  )
  all <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_all_fixed")(zero_subject, zero_pattern)
  )
  simplified <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_all_fixed")(
      zero_subject, zero_pattern, simplify = TRUE
    )
  )
  expect_identical(first, character())
  expect_altrep_charvec(first)
  expect_identical(all, list())
  expect_altrep_charvec(simplified)
  expect_altrep_unmaterialized(simplified)
  expect_identical(simplified, matrix(character(), 0L, 0L))
  expect_fixed_output_input_unmaterialized(zero_subject)
  expect_fixed_output_input_unmaterialized(zero_pattern)
})

test_that("fixed extraction preserves BOM, malformed UTF-8, and byte errors", {
  bom <- enc2utf8("\ufeffa")
  malformed <- fixed_output_marked(c(0x61, 0xff, 0x62), "UTF-8")
  bad_pattern <- fixed_output_marked(0xff, "UTF-8")
  subject <- charport::as_charvec(c(bom, malformed))
  pattern <- charport::as_charvec(c("a", bad_pattern))
  actual <- with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(subject, pattern))
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_extract_first_fixed")(c(bom, malformed), c("a", bad_pattern))
  )
  expect_identical(actual, expected)
  expect_identical(charToRaw(actual[[2]]), as.raw(0xff))
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)

  bytes <- charport::as_charvec(
    fixed_output_marked(c(0xff, 0xfe), "bytes")
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")(bytes, "x")),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_extract_first_fixed")("x", bytes)),
    "bytes encoding"
  )
})

test_that("fixed first and all replacement produce lazy outputs", {
  values <- c("ababa", "cafe", "", NA_character_)
  patterns <- c("a", "e", "x", "x")
  replacements <- c("X", "\u00e9", "!", "!")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)

  cases <- list(
    first = list(
      actual = with_test_backend(
        TRUE,
        charr_test_leaf("ci_replace_first_fixed")(subject, pattern, replacement)
      ),
      expected = with_test_backend(
        FALSE,
        charr_test_leaf("ci_replace_first_fixed")(values, patterns, replacements)
      )
    ),
    all = list(
      actual = with_test_backend(
        TRUE,
        charr_test_leaf("ci_replace_all_fixed")(subject, pattern, replacement)
      ),
      expected = with_test_backend(
        FALSE,
        charr_test_leaf("ci_replace_all_fixed")(values, patterns, replacements)
      )
    )
  )
  for (case in cases) {
    expect_altrep_charvec(case$actual)
    expect_altrep_unmaterialized(case$actual)
    expect_identical(case$actual, case$expected)
  }
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)
})

test_that("fixed replacement preserves aliases, chaining, and matcher lanes", {
  alias_values <- c("a", "short", "0123456789abcdef")
  alias <- charport::as_charvec(alias_values)
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_first_fixed")(alias, alias, alias)
  )
  expect_altrep_charvec(actual)
  expect_altrep_unmaterialized(actual)
  expect_identical(actual, alias_values)
  expect_fixed_output_input_unmaterialized(alias)

  subjects <- c("zaq", "xxneedlezz", "--0123456789abcdef--")
  patterns <- c("a", "needle", "0123456789abcdef")
  subject <- charport::as_charvec(subjects)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(c("A", "N", "L"))
  replaced <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(subject, pattern, replacement)
  )
  expect_altrep_charvec(replaced)
  expect_altrep_unmaterialized(replaced)
  extracted <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_first_fixed")(replaced, replacement)
  )
  expect_identical(extracted, c("A", "N", "L"))
  expect_identical(
    replaced,
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_replace_all_fixed")(
        subjects, patterns, c("A", "N", "L")
      )
    )
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)
})

test_that("fixed replacement keeps missing replacements lazy", {
  values <- c("none", "a match", NA_character_, "")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")
  replacement <- charport::as_charvec(NA_character_)
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_first_fixed")(subject, pattern, replacement)
  )
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_replace_first_fixed")(values, "a", NA_character_)
  )
  expect_identical(actual, expected)
  expect_identical(actual, c("none", NA, NA, ""))
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)
})

test_that("sequential fixed replacement retains mutable-container semantics", {
  values <- c("ababa", "none", "Xab")
  patterns <- c("ab", "X")
  replacements <- c("X", "!")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(
      subject, pattern, replacement, vectorize_all = FALSE
    )
  )
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_replace_all_fixed")(
      values, patterns, replacements, vectorize_all = FALSE
    )
  )
  expect_altrep_charvec(actual)
  expect_altrep_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_identical(actual, c("!!a", "none", "!!"))
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)

  fast_subject <- charport::as_charvec(c("aba", "none"))
  fast_pattern <- charport::as_charvec("a")
  fast_replacement <- charport::as_charvec("X")
  fast <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(
      fast_subject, fast_pattern, fast_replacement,
      vectorize_all = FALSE
    )
  )
  expect_altrep_charvec(fast)
  expect_altrep_unmaterialized(fast)
  expect_identical(fast, c("XbX", "none"))
  expect_fixed_output_input_unmaterialized(fast_subject)
  expect_fixed_output_input_unmaterialized(fast_pattern)
  expect_fixed_output_input_unmaterialized(fast_replacement)

  lazy_na <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(
      charport::as_charvec(c("a", "b")),
      charport::as_charvec(c("a", "z")),
      charport::as_charvec(c("A", NA_character_)),
      vectorize_all = FALSE
    )
  )
  expect_identical(lazy_na, c("A", "b"))
})

test_that("sequential fixed replacement preserves early pattern exits", {
  values <- c("a", "b")
  subject <- charport::as_charvec(values)

  missing_pattern <- charport::as_charvec(c(NA_character_, "x"))
  expected <- fixed_output_warnings(
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_replace_all_fixed")(
        values, c(NA_character_, "x"), "z", vectorize_all = FALSE
      )
    )
  )
  actual <- fixed_output_warnings(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_all_fixed")(
        subject, missing_pattern, "z", vectorize_all = FALSE
      )
    )
  )
  expect_identical(actual, expected)
  expect_identical(actual$value, c(NA_character_, NA_character_))
  expect_identical(actual$messages, character())
  expect_fixed_output_input_unmaterialized(missing_pattern)

  empty_pattern <- charport::as_charvec(c("", "x"))
  expected <- fixed_output_warnings(
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_replace_all_fixed")(
        values, c("", "x"), "z", vectorize_all = FALSE
      )
    )
  )
  actual <- fixed_output_warnings(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_all_fixed")(
        subject, empty_pattern, "z", vectorize_all = FALSE
      )
    )
  )
  expect_identical(actual, expected)
  expect_identical(
    actual$messages,
    rep("empty search patterns are not supported", 2L)
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(empty_pattern)
})

test_that("fixed replacement preserves recycling and empty-pattern warnings", {
  values <- c("a", "ba", "ca")
  patterns <- c("", "a")
  replacements <- "x"
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)
  expected <- fixed_output_warnings(
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_replace_all_fixed")(values, patterns, replacements)
    )
  )
  actual <- fixed_output_warnings(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_all_fixed")(subject, pattern, replacement)
    )
  )
  expect_identical(actual, expected)
  expect_identical(
    actual$messages,
    c(
      "longer object length is not a multiple of shorter object length",
      "empty search patterns are not supported"
    )
  )
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)
})

test_that("fixed replacement preserves BOM, Latin-1, malformed, and bytes behavior", {
  bom <- enc2utf8("\ufeffa")
  latin1 <- fixed_output_marked(0xe9, "latin1")
  malformed <- fixed_output_marked(c(0x61, 0xff, 0x62), "UTF-8")
  subject <- charport::as_charvec(c(bom, "a-a", malformed))
  pattern <- charport::as_charvec(c("a", "a", "a"))
  replacement <- charport::as_charvec(c("x", latin1, "x"))
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(subject, pattern, replacement)
  )
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_replace_all_fixed")(
      c(bom, "a-a", malformed), c("a", "a", "a"),
      c("x", latin1, "x")
    )
  )
  expect_identical(actual, expected)
  expect_identical(charToRaw(actual[[3]]), as.raw(c(0x78, 0xff, 0x62)))
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(pattern)
  expect_fixed_output_input_unmaterialized(replacement)

  bytes_value <- fixed_output_marked(c(0xff, 0xfe), "bytes")
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_first_fixed")(
        charport::as_charvec(bytes_value), "z", "x"
      )
    ),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_first_fixed")(
        "a", "z", charport::as_charvec(bytes_value)
      )
    ),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_replace_first_fixed")(
        "a", charport::as_charvec(bytes_value), "x"
      )
    ),
    "bytes encoding"
  )
})

test_that("fixed replacement zero recycling skips Reader acquisition", {
  bytes <- fixed_output_marked(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes)
  replacement <- charport::as_charvec(character())
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(subject, "a", replacement)
  )
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_replace_all_fixed")(bytes, "a", character())
  )
  expect_altrep_charvec(actual)
  expect_identical(actual, expected)
  expect_identical(actual, character())
  expect_fixed_output_input_unmaterialized(subject)
  expect_fixed_output_input_unmaterialized(replacement)

  sequential <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_all_fixed")(
      charport::as_charvec(character()), bytes, bytes,
      vectorize_all = FALSE
    )
  )
  expect_altrep_charvec(sequential)
  expect_identical(sequential, character())
})
