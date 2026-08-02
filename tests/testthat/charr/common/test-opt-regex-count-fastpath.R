test_that("optimized regex counting matches stringi on direct UTF-8 input", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "one two three", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a",
    "\ufeff alpha", "no-space", malformed, "", NA_character_
  )
  patterns <- c(
    "(?<=\\s)(\\p{L}[\\p{L}\\p{M}]*)",
    "^\\ufeff", "\\ufffd", "^$"
  )

  operations <- list(
    scalar = function(x) charr_test_leaf("ci_count_regex")(x, patterns[[1L]]),
    recycled = function(x) charr_test_leaf("ci_count_regex")(x, patterns),
    insensitive = function(x) {
      charr_test_leaf("ci_count_regex")(
        x, "CAF\u00c9", opts_regex = list(case_insensitive = TRUE)
      )
    },
    zero_width = function(x) charr_test_leaf("ci_count_regex")(x, "(?=a)")
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    expect_identical(with_backend("altrep", operation(input)), expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("optimized regex counting retains conversion fallbacks", {
  latin1 <- iconv("caf\u00e9 noir", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "plain text", NA_character_)

  expected <- with_backend(
    "stringi", charr_test_leaf("ci_count_regex")(values, "\\p{L}+")
  )
  expect_identical(
    with_backend("base", charr_test_leaf("ci_count_regex")(values, "\\p{L}+")),
    expected
  )
  expect_identical(
    with_backend(
      "altrep",
      charr_test_leaf("ci_count_regex")(charport::as_charvec(values), "\\p{L}+")
    ),
    expected
  )

  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"
  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(bytes)
    } else {
      bytes
    }
    expect_error(
      with_backend(backend, charr_test_leaf("ci_count_regex")(input, "x")),
      "bytes encoding"
    )
  }
})


test_that("regex counting still compiles patterns lazily around missing input", {
  for (backend in c("stringi", "base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(NA_character_)
    } else {
      NA_character_
    }
    expect_identical(
      with_backend(backend, charr_test_leaf("ci_count_regex")(input, "[")),
      NA_integer_
    )
  }
})
