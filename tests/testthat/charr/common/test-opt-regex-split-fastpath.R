test_that("optimized regex split matches stringi on scalar patterns", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "one two three", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a",
    "\U0001f600 alpha", paste0("\ufeff", " beta"), malformed,
    "no-space", "", NA_character_
  )
  patterns <- c("\\s+", "(?=.)", "\\ufffd", "[[:alpha:]]+")

  for (pattern in patterns) {
    expected <- with_backend(
      "stringi", charr_test_leaf("ci_split_regex")(values, pattern)
    )
    expect_identical(
      with_backend("base", charr_test_leaf("ci_split_regex")(values, pattern)),
      expected
    )

    input <- charport::as_charvec(values)
    actual <- with_backend(
      "altrep", charr_test_leaf("ci_split_regex")(input, pattern)
    )
    expect_identical(actual, expected)
    expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("optimized regex split retains vectorized split options", {
  values <- c("a,,b,c", ",a,", "", NA_character_)
  cases <- list(
    list(pattern = ",", n = c(-1L, 2L)),
    list(pattern = ",", n = 3L, omit_empty = TRUE),
    list(pattern = ",", n = 3L, omit_empty = NA, tokens_only = TRUE),
    list(pattern = c(",", "(?=.)"), n = 4L, simplify = TRUE),
    list(pattern = c(",", "(?=.)"), n = 4L, simplify = NA)
  )

  for (args in cases) {
    expected <- with_backend(
      "stringi",
      do.call(charr_test_leaf("ci_split_regex"), c(list(str = values), args))
    )
    expect_identical(
      with_backend(
        "base",
        do.call(charr_test_leaf("ci_split_regex"), c(list(str = values), args))
      ),
      expected
    )

    input <- charport::as_charvec(values)
    expect_identical(
      with_backend(
        "altrep",
        do.call(charr_test_leaf("ci_split_regex"), c(list(str = input), args))
      ),
      expected
    )
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("regex split keeps bytes errors and lazy pattern compilation", {
  for (backend in c("stringi", "base", "altrep")) {
    missing <- if (identical(backend, "altrep")) {
      charport::as_charvec(NA_character_)
    } else {
      NA_character_
    }
    expect_identical(
      with_backend(
        backend,
        charr_test_leaf("ci_split_regex")(missing, "[")
      ),
      list(NA_character_)
    )
  }

  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"
  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(bytes)
    } else {
      bytes
    }
    expect_error(
      with_backend(backend, charr_test_leaf("ci_split_regex")(input, "x")),
      "bytes encoding"
    )
  }
})
