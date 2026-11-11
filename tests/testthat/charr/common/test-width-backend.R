# charr-owned file: targeted equivalence tests for Reader-backed width.

width_cp <- function(x) charr_test_leaf("ci_width")(x)


test_that("width preserves ICU property and emoji-context semantics", {
  strings <- charr_test_leaf("ci_replace_all_fixed")(c(
    "a", "Ａ", "ｱ", "Ω", "ü", "\t", "🙂", "👩‍👧", "🇺🇸", "", NA
  ), "not present", "")
  expect_identical(is_charr_result(strings), charr_altrep())

  expect_identical(
    width_cp(strings),
    c(1L, 2L, 1L, 1L, 1L, 0L, 2L, 2L, 2L, 0L, NA_integer_)
  )
})


test_that("width stays inside adjacent Reader records", {
  strings <- charr_test_leaf("ci_trim_both")(c(
    " a ", " very_long_👩‍👧_record ", " z ", " ü ", " Ａ "
  ))
  expect_identical(is_charr_result(strings), charr_altrep())
  expect_identical(width_cp(strings), c(1L, 19L, 1L, 1L, 2L))
})


test_that("width validates UTF-8 and rejects CE_BYTES", {
  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charr_test_leaf("ci_replace_all_fixed")("a", "a", malformed_byte)
  expect_identical(is_charr_result(malformed), charr_altrep())
  expect_error(width_cp(malformed), "invalid UTF-8")

  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(width_cp(bytes), "bytes encoding")
})


test_that("width reads native UTF-8 bytes and still converts Latin-1", {
  skip_if_not(isTRUE(l10n_info()[["UTF-8"]]))
  native <- function(value) {
    result <- rawToChar(charToRaw(enc2utf8(value)))
    Encoding(result) <- "unknown"
    result
  }
  latin1 <- iconv("caf\u00e9", "UTF-8", "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(
    native("caf\u00e9"), native("e\u0301"), native("\uff21"),
    native("\U0001f469\u200d\U0001f467"), native("\ufeffa"),
    latin1, "", NA_character_
  )
  expect_identical(Encoding(values)[1:5], rep("unknown", 5L))
  expected <- rep(c(4L, 1L, 2L, 2L, 1L, 4L, 0L, NA_integer_), 20L)
  values <- rep(values, 20L)
  for (backend in c("base", "altrep")) {
    for (threads in c(1L, charr_test_threads())) {
      old_threads <- charr_threads(threads)
      input <- if (backend == "altrep") charport::as_charvec(values) else values
      actual <- with_backend(backend, str_width(input))
      charr_threads(old_threads)
      expect_identical(actual, expected)
      if (backend == "altrep") expect_lazy_result(input)
    }
  }
})


test_that("width rejects malformed native UTF-8 in serial and parallel paths", {
  skip_if_not(isTRUE(l10n_info()[["UTF-8"]]))
  malformed <- rawToChar(as.raw(c(0x61, 0xc3)))
  Encoding(malformed) <- "unknown"
  expect_identical(Encoding(malformed), "unknown")
  values <- rep(c("ok", malformed, NA_character_), 40L)
  for (backend in c("base", "altrep")) {
    for (threads in c(1L, charr_test_threads())) {
      old_threads <- charr_threads(threads)
      input <- if (backend == "altrep") charport::as_charvec(values) else values
      expect_error(with_backend(backend, str_width(input)), "invalid UTF-8")
      charr_threads(old_threads)
      if (backend == "altrep") expect_lazy_result(input)
    }
  }
})
