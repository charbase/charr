test_that("optimized regex detection matches stringi on direct UTF-8 input", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "one two", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a", "\ufeff alpha",
    "no-space", malformed, "", NA_character_
  )
  patterns <- c(
    "(?<=\\s)(\\p{L}[\\p{L}\\p{M}]*)",
    "^\\ufeff", "\\ufffd", "^$"
  )

  operations <- list(
    scalar = function(x) {
      charr_test_leaf("ci_detect_regex")(x, patterns[[1L]])
    },
    recycled = function(x) charr_test_leaf("ci_detect_regex")(x, patterns),
    negated = function(x) {
      charr_test_leaf("ci_detect_regex")(x, patterns[[1L]], negate = TRUE)
    },
    limited = function(x) {
      charr_test_leaf("ci_detect_regex")(x, patterns[[1L]], max_count = 2L)
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    expect_identical(with_backend("altrep", operation(input)), expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }

  recycled_subject <- values[1:2]
  recycled_patterns <- c("one", "nope", "two", "^$")
  expected <- with_backend(
    "stringi",
    charr_test_leaf("ci_detect_regex")(recycled_subject, recycled_patterns)
  )
  expect_identical(
    with_backend(
      "base",
      charr_test_leaf("ci_detect_regex")(recycled_subject, recycled_patterns)
    ),
    expected
  )
  expect_identical(
    with_backend(
      "altrep",
      charr_test_leaf("ci_detect_regex")(
        charport::as_charvec(recycled_subject), recycled_patterns
      )
    ),
    expected
  )
})


test_that("optimized regex detection retains conversion fallbacks", {
  latin1 <- iconv("caf\u00e9 noir", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "plain text", NA_character_)

  expected <- with_backend(
    "stringi", charr_test_leaf("ci_detect_regex")(values, "\\p{L}+")
  )
  expect_identical(
    with_backend("base", charr_test_leaf("ci_detect_regex")(values, "\\p{L}+")),
    expected
  )
  expect_identical(
    with_backend(
      "altrep",
      charr_test_leaf("ci_detect_regex")(charport::as_charvec(values), "\\p{L}+")
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
      with_backend(
        backend,
        charr_test_leaf("ci_detect_regex")(input, "x", max_count = 0L)
      ),
      "bytes encoding"
    )
  }
})
