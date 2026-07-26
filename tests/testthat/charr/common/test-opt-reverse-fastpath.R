test_that("optimized reverse retains code-point and encoding semantics", {
  latin1 <- iconv("caf\u00e9", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(
    ascii = "abcdef",
    utf8 = "a\u00e9\U0001f642u\u0308z",
    bom = "\ufeffabc",
    double_bom = "\ufeff\ufeffabc",
    latin1 = latin1,
    empty = "",
    missing = NA_character_
  )
  expected <- with_backend("stringi", str_reverse(values))

  expect_identical(
    with_backend("base", str_reverse(values)),
    expected
  )

  input <- charport::as_charvec(values)
  actual <- with_backend("altrep", str_reverse(input))
  expect_false(charport::charport_info(input)$is_materialized)
  expect_false(charport::charport_info(actual)$is_materialized)
  expect_identical(actual, expected)
})

test_that("optimized reverse validates mixed input before returning output", {
  malformed <- rawToChar(as.raw(0xc3))
  Encoding(malformed) <- "UTF-8"
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"

  for (values in list(c("abc", malformed), c("abc", bytes))) {
    messages <- c(
      stringi = tryCatch(
        with_backend("stringi", str_reverse(values)),
        error = conditionMessage
      ),
      base = tryCatch(
        with_backend("base", str_reverse(values)),
        error = conditionMessage
      ),
      altrep = tryCatch(
        with_backend("altrep", str_reverse(charport::as_charvec(values))),
        error = conditionMessage
      )
    )

    expect_match(messages, "invalid UTF-8|bytes encoding", all = TRUE)
  }
})
