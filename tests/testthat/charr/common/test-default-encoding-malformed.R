# The default source encoding (`encoding = NULL`, equivalently `""`) means "these
# bytes are in R's native encoding", not "assume UTF-8" and not "detect it".
#
# On a UTF-8 platform that conversion is an identity, so the optimized
# backends pass the bytes directly to ICU. ICU substitutes U+FFFD for malformed
# input and warns, matching stringi. Non-UTF-8 locales use Riconv, as covered
# by test-native-locale-input.R.


test_that("a malformed byte substitutes rather than failing the call", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]),
    "Native encoding is not UTF-8"
  )

  # "a", an isolated 0xff, "b" -- 0xff can never begin a UTF-8 sequence.
  malformed <- as.raw(c(0x61, 0xff, 0x62))
  expected <- suppressWarnings(
    with_backend("stringi", str_conv(malformed, "UTF-8"))
  )
  expect_identical(expected, "a�b")

  for (backend in c("base", "altrep")) {
    value <- suppressWarnings(
      with_backend(backend, charr:::ci_encode(malformed, NULL, "UTF-8"))
    )
    expect_identical(value, expected)
    expect_identical(Encoding(value), "UTF-8")
  }
})


test_that("str_read_lines reads a file containing a malformed byte", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]),
    "Native encoding is not UTF-8"
  )

  path <- tempfile()
  on.exit(unlink(path), add = TRUE)
  writeBin(as.raw(c(0x61, 0x62, 0x0a, 0x63, 0xff, 0x64, 0x0a)), path)

  expected <- suppressWarnings(with_backend("stringi", str_read_lines(path)))
  expect_identical(expected, c("ab", "c�d"))

  for (backend in c("base", "altrep")) {
    expect_identical(
      suppressWarnings(with_backend(backend, str_read_lines(path))),
      expected
    )
  }
})


test_that("the default source encoding still round-trips valid input", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]),
    "Native encoding is not UTF-8"
  )

  values <- c("plain", "", "café", "中文", "\U0001f469 family")
  path <- tempfile()
  on.exit(unlink(path), add = TRUE)
  writeLines(values, path, useBytes = FALSE)

  expected <- with_backend("stringi", str_read_lines(path))
  expect_identical(expected, values)

  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_read_lines(path)), expected)
    # NULL and "" name the same default.
    expect_identical(
      with_backend(backend, charr:::ci_read_lines(path, "")),
      expected
    )
  }
})


test_that("a NA byte source is preserved on the default encoding path", {
  input <- list(as.raw(c(0x61, 0x62)), NULL, as.raw(0x63))

  expected <- with_backend("stringi", charr:::ci_encode(input, NULL, "UTF-8"))
  expect_identical(expected, c("ab", NA_character_, "c"))

  for (backend in c("base", "altrep")) {
    expect_identical(
      with_backend(backend, charr:::ci_encode(input, NULL, "UTF-8")),
      expected
    )
  }
})
