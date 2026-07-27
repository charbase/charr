fixed_bounds_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


fixed_bounds_case <- function(pattern) {
  latin1_miss <- fixed_bounds_marked(0xe9, "latin1")
  latin1_hit <- fixed_bounds_marked(
    c(charToRaw(pattern), as.raw(0xe9)),
    "latin1"
  )
  values <- c(latin1_miss, latin1_hit)

  if (isTRUE(l10n_info()[["UTF-8"]])) {
    native_suffix <- charToRaw("\u00e9")
    native_miss <- fixed_bounds_marked(native_suffix, "unknown")
    native_hit <- fixed_bounds_marked(
      c(charToRaw(pattern), native_suffix),
      "unknown"
    )
    values <- c(values, native_miss, native_hit)
  }

  groups <- length(values) %/% 2L
  matched <- rep(c(FALSE, TRUE), groups)
  decoded <- rep(c("\u00e9", paste0(pattern, "\u00e9")), groups)

  list(
    values = values,
    pattern = pattern,
    matched = matched,
    decoded = decoded
  )
}


test_that("fixed operations stay inside converted input records", {
  for (pattern in c("Z", "YZ")) {
    case <- fixed_bounds_case(pattern)
    matched <- case$matched
    match_length <- nchar(pattern, type = "chars")

    expect_identical(
      str_detect(case$values, fixed(pattern)),
      matched
    )
    expect_identical(
      str_count(case$values, fixed(pattern)),
      as.integer(matched)
    )

    expected_locate <- cbind(
      start = ifelse(matched, 1L, NA_integer_),
      end = ifelse(matched, match_length, NA_integer_)
    )
    storage.mode(expected_locate) <- "integer"
    expect_identical(
      str_locate(case$values, fixed(pattern)),
      expected_locate
    )

    expect_identical(
      str_extract(case$values, fixed(pattern)),
      ifelse(matched, pattern, NA_character_)
    )

    expected_split <- Map(
      function(value, found) {
        if (found) c("", "\u00e9") else value
      },
      case$decoded,
      matched
    )
    expect_identical(
      str_split(case$values, fixed(pattern)),
      unname(expected_split)
    )

    expected_replace <- ifelse(
      matched,
      "X\u00e9",
      "\u00e9"
    )
    expect_identical(
      str_replace(case$values, fixed(pattern), "X"),
      expected_replace
    )
    expect_identical(
      str_replace_all(case$values, fixed(pattern), "X"),
      expected_replace
    )
  }
})


test_that("fixed operations handle multibyte case-insensitive patterns", {
  value <- "xx\u00c9Ayy\u00e9a"
  pattern <- fixed("\u00e9a", ignore_case = TRUE)

  expect_identical(str_detect(value, pattern), TRUE)
  expect_identical(str_count(value, pattern), 2L)
  expect_identical(
    str_locate(value, pattern),
    structure(
      c(3L, 4L),
      dim = c(1L, 2L),
      dimnames = list(NULL, c("start", "end"))
    )
  )
  expect_identical(str_extract(value, pattern), "\u00c9A")
  expect_identical(str_split(value, pattern), list(c("xx", "yy", "")))
  expect_identical(str_replace(value, pattern, "X"), "xxXyy\u00e9a")
  expect_identical(str_replace_all(value, pattern, "X"), "xxXyyX")
})
