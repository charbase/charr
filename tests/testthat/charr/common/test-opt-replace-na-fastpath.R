test_that("optimized replace-NA handles direct and conversion inputs", {
  latin1 <- iconv("é", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c("plain", NA_character_, "βeta", latin1, "\ufeffbom")
  replacement <- "中"
  expected <- with_backend(
    "stringi",
    charr:::ci_replace_na(values, replacement)
  )

  expect_identical(
    with_backend("base", charr:::ci_replace_na(values, replacement)),
    expected
  )
  expect_identical(
    with_backend(
      "altrep",
      charr:::ci_replace_na(
        charport::as_charvec(values),
        charport::as_charvec(replacement)
      )
    ),
    expected
  )
})

test_that("optimized replace-NA drops source attributes like stringi", {
  values <- structure(c("a", NA_character_), names = c("first", "second"))
  expected <- with_backend("stringi", charr:::ci_replace_na(values, "x"))

  expect_identical(
    with_backend("base", charr:::ci_replace_na(values, "x")),
    expected
  )
})
