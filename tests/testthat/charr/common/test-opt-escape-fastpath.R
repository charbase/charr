test_that("optimized escape matches stringi across input encodings", {
  latin1 <- iconv("caf\u00e9", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(
    "plain ASCII",
    "quotes: \\\"' and controls:\t\n\r",
    "caf\u00e9 \U0001f642 u\u0308 \u4e2d",
    "\ufeffone BOM",
    "\ufeff\ufefftwo BOMs",
    latin1,
    "",
    NA_character_
  )
  expected <- with_backend("stringi", charr_test_leaf("ci_escape_unicode")(values))

  expect_identical(
    with_backend("base", charr_test_leaf("ci_escape_unicode")(values)),
    expected
  )

  input <- charport::as_charvec(values)
  actual <- with_backend("altrep", charr_test_leaf("ci_escape_unicode")(input))
  expect_false(charport::charport_info(input)$is_materialized)
  expect_false(charport::charport_info(actual)$is_materialized)
  expect_identical(actual, expected)
  expect_identical(Encoding(actual), Encoding(expected))
})


escape_fastpath_error <- function(expr) {
  tryCatch(
    {
      force(expr)
      NA_character_
    },
    error = conditionMessage
  )
}

escape_fastpath_normalize_error <- function(message) {
  gsub("(stri|ci)_enc_toutf8", "enc_toutf8", message, perl = TRUE)
}


test_that("optimized escape retains invalid-input behavior", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"

  for (value in list(malformed, bytes)) {
    expected <- escape_fastpath_error(
      with_backend("stringi", charr_test_leaf("ci_escape_unicode")(value))
    )
    actual_base <- escape_fastpath_error(
      with_backend("base", charr_test_leaf("ci_escape_unicode")(value))
    )
    actual_altrep <- escape_fastpath_error(
      with_backend(
        "altrep",
        charr_test_leaf("ci_escape_unicode")(charport::as_charvec(value))
      )
    )

    expect_false(is.na(expected))
    expect_identical(
      escape_fastpath_normalize_error(actual_base),
      escape_fastpath_normalize_error(expected)
    )
    expect_identical(
      escape_fastpath_normalize_error(actual_altrep),
      escape_fastpath_normalize_error(expected)
    )
  }
})
