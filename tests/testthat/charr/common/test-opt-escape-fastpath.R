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
  expected <- with_backend("stringi", charr:::ci_escape_unicode(values))

  expect_identical(
    with_backend("base", charr:::ci_escape_unicode(values)),
    expected
  )

  input <- charport::as_charvec(values)
  actual <- with_backend("altrep", charr:::ci_escape_unicode(input))
  expect_false(charport::charport_info(input)$is_materialized)
  expect_false(charport::charport_info(actual)$is_materialized)
  expect_identical(actual, expected)
  expect_identical(Encoding(actual), Encoding(expected))
})


test_that("optimized escape retains invalid-input behavior", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"

  for (value in list(malformed, bytes)) {
    expected <- tryCatch(
      with_backend("stringi", charr:::ci_escape_unicode(value)),
      error = conditionMessage
    )
    expect_error(
      with_backend("base", charr:::ci_escape_unicode(value)),
      expected,
      fixed = TRUE
    )
    expect_error(
      with_backend(
        "altrep",
        charr:::ci_escape_unicode(charport::as_charvec(value))
      ),
      expected,
      fixed = TRUE
    )
  }
})
