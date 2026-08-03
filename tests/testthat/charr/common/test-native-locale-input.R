native_locale_is_latin1 <- function() {
  !isTRUE(l10n_info()[["UTF-8"]]) && isTRUE(l10n_info()[["Latin-1"]])
}

native_locale_marked <- function(bytes, encoding = "unknown") {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

native_locale_fixture <- function() {
  if (isTRUE(l10n_info()[["UTF-8"]])) {
    bytes <- as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9))
  } else if (native_locale_is_latin1()) {
    bytes <- as.raw(c(0x63, 0x61, 0x66, 0xe9))
  } else {
    skip("The startup locale is neither UTF-8 nor Latin-1 compatible")
  }

  native <- native_locale_marked(bytes)
  decoded <- iconv(native, from = "", to = "UTF-8", sub = NA_character_)
  expect_false(is.na(decoded))
  Encoding(decoded) <- "UTF-8"

  target <- native_locale_marked(c(0xc3, 0xa9), "UTF-8")
  list(bytes = bytes, native = native, decoded = decoded, target = target)
}

native_locale_operations <- function(target) {
  list(
    utf8_container = function(value) str_detect(value, fixed(target)),
    utf16_container = function(value) {
      str_detect(value, coll(target, locale = "en", strength = 3L))
    },
    direct_length = str_length
  )
}

native_locale_expect_operations <- function(input, decoded, target) {
  for (operation in native_locale_operations(target)) {
    expect_identical(operation(input), operation(decoded))
  }
  invisible(NULL)
}


test_that("native inputs are decoded under the startup locale", {
  skip_if_selected_stringi_cannot_compare_native()

  fixture <- native_locale_fixture()
  inputs <- list(
    ordinary = fixture$native,
    charvec = charport::as_charvec(fixture$native)
  )

  for (input in inputs) {
    native_locale_expect_operations(
      input, fixture$decoded, fixture$target
    )
  }

  expect_identical(charToRaw(fixture$native), fixture$bytes)
  expect_identical(Encoding(fixture$native), "unknown")
})


test_that("str_read_lines uses the startup native encoding by default", {
  skip_if_selected_stringi_cannot_compare_native()

  fixture <- native_locale_fixture()
  path <- tempfile("charr-native-lines-")
  on.exit(unlink(path), add = TRUE)
  writeBin(fixture$bytes, path)

  for (encoding in list(NULL, "")) {
    expect_identical(
      str_read_lines(path, encoding = encoding),
      fixture$decoded
    )
  }
})


test_that("ci_encode uses the startup native encoding as its default target", {
  if (identical(charr_backend(), "stringi")) {
    skip("ci_encode is an optimized-backend internal")
  }

  value <- native_locale_marked(c(0xc3, 0xa9), "UTF-8")
  utf8_bytes <- charToRaw(value)
  expected <- iconv(value, from = "UTF-8", to = "", sub = NA_character_)
  skip_if(is.na(expected), "The startup encoding cannot represent U+00E9")
  expected_bytes <- charToRaw(expected)

  for (target in list(NULL, "")) {
    character_output <- charr_test_leaf("ci_encode")(
      value, "UTF-8", target, FALSE
    )
    raw_output <- charr_test_leaf("ci_encode")(
      utf8_bytes, "UTF-8", target, TRUE
    )
    expect_identical(charToRaw(character_output), expected_bytes)
    expect_identical(raw_output, list(expected_bytes))
    if (!isTRUE(l10n_info()[["UTF-8"]])) {
      expect_identical(Encoding(character_output), "unknown")
    }
  }
})


test_that("ci_encode rejects an unrepresentable default target", {
  if (identical(charr_backend(), "stringi")) {
    skip("ci_encode is an optimized-backend internal")
  }
  skip_if(
    isTRUE(l10n_info()[["UTF-8"]]),
    "Every valid Unicode value is representable in UTF-8"
  )

  value <- native_locale_marked(c(0xf0, 0x9f, 0x99, 0x82), "UTF-8")
  utf8_bytes <- charToRaw(value)
  skip_if_not(
    is.na(suppressWarnings(
      iconv(value, from = "UTF-8", to = "", sub = NA_character_)
    )),
    "The startup encoding represents the test value"
  )

  for (target in list(NULL, "")) {
    expect_error(
      charr_test_leaf("ci_encode")(value, "UTF-8", target, FALSE),
      "failed to convert UTF-8 to R native encoding"
    )
    expect_error(
      charr_test_leaf("ci_encode")(utf8_bytes, "UTF-8", target, TRUE),
      "failed to convert UTF-8 to R native encoding"
    )
  }
})
