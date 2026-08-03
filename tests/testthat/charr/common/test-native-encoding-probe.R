# Covers NativeToUtf8::native_is_utf8(), the single facility deciding whether
# R's native encoding is UTF-8. It probes the same Riconv converter that the
# conversion path uses instead of inferring the answer from a locale name.

native_probe_marked <- function(bytes, encoding = "unknown") {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

native_probe_is_latin1 <- function() {
  !isTRUE(l10n_info()[["UTF-8"]]) && isTRUE(l10n_info()[["Latin-1"]])
}


test_that("stringi follows its reported native-encoding capability", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]) || native_probe_is_latin1(),
    "The startup locale is neither UTF-8 nor Latin-1 compatible"
  )

  value <- native_probe_marked(c(0xc3, 0xa9))
  r_length <- if (isTRUE(l10n_info()[["UTF-8"]])) 1L else 2L
  stringi_length <- if (isTRUE(stringi::stri_info()[["ICU.UTF8"]])) {
    1L
  } else {
    r_length
  }

  expect_identical(with_backend("stringi", str_length(value)), stringi_length)
  expect_identical(
    stringi_can_compare_native(),
    identical(stringi_length, r_length)
  )
})


test_that("the native-encoding probe follows the startup encoding", {
  skip_if_selected_stringi_cannot_compare_native()

  if (isTRUE(l10n_info()[["UTF-8"]])) {
    bytes <- c(0x63, 0x61, 0x66, 0xc3, 0xa9)
  } else if (native_probe_is_latin1()) {
    bytes <- c(0x63, 0x61, 0x66, 0xe9)
  } else {
    skip("The startup locale is neither UTF-8 nor Latin-1 compatible")
  }

  value <- native_probe_marked(bytes)
  decoded <- iconv(value, from = "", to = "UTF-8", sub = NA_character_)
  expect_false(is.na(decoded))
  expect_identical(
    charToRaw(decoded),
    as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9))
  )

  expect_identical(str_length(value), 4L)
})


test_that("native byte meaning is fixed by the startup encoding", {
  skip_if_selected_stringi_cannot_compare_native()

  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]) || native_probe_is_latin1(),
    "The startup locale is neither UTF-8 nor Latin-1 compatible"
  )

  # The bytes encode one character in UTF-8 and two in ISO-8859-1 or
  # Windows-1252. Keeping the fixture unmarked makes that distinction explicit
  # without changing LC_CTYPE after R has interned the string.
  value <- native_probe_marked(c(0xc3, 0xa9))
  original_bytes <- charToRaw(value)
  expected_length <- if (isTRUE(l10n_info()[["UTF-8"]])) 1L else 2L
  expected_utf8 <- if (isTRUE(l10n_info()[["UTF-8"]])) {
    as.raw(c(0xc3, 0xa9))
  } else {
    as.raw(c(0xc3, 0x83, 0xc2, 0xa9))
  }

  decoded <- iconv(value, from = "", to = "UTF-8", sub = NA_character_)
  expect_identical(charToRaw(decoded), expected_utf8)
  expect_identical(str_length(value), expected_length)
  expect_identical(charToRaw(value), original_bytes)
  expect_identical(Encoding(value), "unknown")
})


test_that("a BOM is stripped only where stringi strips it", {
  # stringi is inconsistent here by design and charr reproduces it: str_length
  # counts a leading BOM as a code point, while the operations that build a
  # UTF-8 container strip it. Pin both halves so neither drifts.
  bom <- "﻿abc"

  expect_identical(with_backend("stringi", str_length(bom)), 4L)
  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_length(bom)), 4L)
    expect_identical(with_backend(backend, str_sub(bom, 1L, 1L)), "a")
    expect_identical(with_backend(backend, str_length(str_reverse(bom))), 3L)
    expect_identical(
      with_backend(backend, str_length(str_to_upper(bom))), 3L
    )
    expect_identical(with_backend(backend, str_count(bom, fixed("a"))), 1L)
  }
})


test_that("BOM handling matches stringi for native-marked UTF-8 input", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]),
    "The startup locale is not UTF-8"
  )

  # Container-building operations strip a BOM carried in native UTF-8 bytes.
  # native_is_utf8() supplies the encoding fact used by normalization.
  value <- native_probe_marked(c(0xef, 0xbb, 0xbf, 0x61, 0x62, 0x63))

  for (operation in list(
    function(x) str_sub(x, 1L, 1L),
    function(x) str_length(str_reverse(x)),
    function(x) str_count(x, fixed("a"))
  )) {
    expected <- with_backend("stringi", operation(value))
    expect_identical(operation(value), expected)
  }
})


test_that("native UTF-8 input leaves validation to the consumer", {
  skip_if_not(
    isTRUE(l10n_info()[["UTF-8"]]),
    "The startup locale is not UTF-8"
  )

  native <- native_probe_marked(c(0x61, 0xff, 0x62))
  declared <- native_probe_marked(c(0x61, 0xff, 0x62), "UTF-8")

  expect_identical(str_detect(native, fixed("b")), TRUE)
  expect_identical(str_detect(native, regex("b")), TRUE)
  expect_error(
    charr_test_leaf("ci_wrap")(
      native, normalize = FALSE, simplify = FALSE
    ),
    "invalid UTF-8 byte sequence",
    fixed = TRUE
  )
  expect_identical(
    str_detect(native, fixed("b")),
    str_detect(declared, fixed("b"))
  )
})
