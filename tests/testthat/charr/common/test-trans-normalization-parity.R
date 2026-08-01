normalization_parity_call <- function(backend, value) {
  if (identical(backend, "base")) {
    normalizer <- get(
      "ci_trans_nfc",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    )
    return(normalizer(value))
  }

  if (identical(backend, "altrep")) {
    return(charr:::ci_trans_nfc(charport::as_charvec(value)))
  }

  stop("unknown normalization backend", call. = FALSE)
}


test_that("NFC normalization matches stringi across input encodings", {
  latin1 <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(latin1) <- "latin1"
  values <- c(
    "plain ASCII",
    "a\u030a",
    "\ufeffAbC",
    latin1,
    "",
    NA_character_
  )
  expected <- stringi::stri_trans_nfc(values)
  expected_bom <- as.raw(c(0xef, 0xbb, 0xbf, 0x41, 0x62, 0x43))

  for (backend in c("base", "altrep")) {
    actual <- normalization_parity_call(backend, values)

    expect_identical(actual, expected)
    expect_identical(Encoding(as.character(actual)), Encoding(expected))
    expect_identical(charToRaw(actual[[3L]]), expected_bom)
  }
})


test_that("NFC normalization substitutes malformed declared UTF-8", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"
  expected <- stringi::stri_trans_nfc(malformed)
  replacement <- as.raw(c(0x61, 0xef, 0xbf, 0xbd, 0x28))

  expect_identical(charToRaw(expected), replacement)
  for (backend in c("base", "altrep")) {
    actual <- normalization_parity_call(backend, malformed)

    expect_identical(actual, expected)
    expect_identical(Encoding(as.character(actual)), Encoding(expected))
    expect_identical(charToRaw(actual[[1L]]), replacement)
  }
})


test_that("NFC normalization rejects bytes and remains reusable", {
  bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(bytes) <- "bytes"
  expected_error <- tryCatch(
    stringi::stri_trans_nfc(bytes),
    error = conditionMessage
  )
  valid <- c("a\u030a", "", NA_character_)
  expected_valid <- stringi::stri_trans_nfc(valid)

  for (backend in c("base", "altrep")) {
    expect_error(
      normalization_parity_call(backend, bytes),
      expected_error,
      fixed = TRUE
    )
    expect_identical(
      normalization_parity_call(backend, valid),
      expected_valid
    )
  }
})
