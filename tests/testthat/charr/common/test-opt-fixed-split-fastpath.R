test_that("optimized fixed split preserves scalar delimiter semantics", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "alpha beta gamma",
    " leading  and trailing ",
    "",
    NA_character_,
    "no-separator",
    malformed
  )

  cases <- list(
    list(),
    list(n = c(-1L, 2L), omit_empty = FALSE),
    list(n = 3L, omit_empty = TRUE),
    list(n = 3L, omit_empty = NA, tokens_only = TRUE),
    list(n = 4L, simplify = TRUE),
    list(n = 4L, simplify = NA)
  )

  for (args in cases) {
    expected <- with_backend(
      "stringi",
      do.call(
        charr_test_leaf("ci_split_fixed"),
        c(list(str = values, pattern = " "), args)
      )
    )

    for (backend in c("base", "altrep")) {
      input <- if (identical(backend, "altrep")) {
        charport::as_charvec(values)
      } else {
        values
      }
      actual <- with_backend(
        backend,
        do.call(
          charr_test_leaf("ci_split_fixed"),
          c(list(str = input, pattern = " "), args)
        )
      )
      expect_identical(actual, expected)
    }
  }
})


test_that("optimized fixed split keeps general matcher behavior", {
  values <- c(
    "abXXcdXXef", "AaA", "é::β", NA_character_, "", "tailXX"
  )
  cases <- list(
    list(pattern = "XX"),
    list(pattern = "a", case_insensitive = TRUE),
    list(pattern = c("XX", "a", "::"), omit_empty = NA)
  )

  for (args in cases) {
    expected <- with_backend(
      "stringi",
      do.call(charr_test_leaf("ci_split_fixed"), c(list(str = values), args))
    )

    for (backend in c("base", "altrep")) {
      input <- if (identical(backend, "altrep")) {
        charport::as_charvec(values)
      } else {
        values
      }
      expect_identical(
        with_backend(
          backend,
          do.call(charr_test_leaf("ci_split_fixed"), c(list(str = input), args))
        ),
        expected
      )
    }
  }
})


test_that("optimized fixed split marks ASCII fields from mixed UTF-8 input", {
  value <- enc2utf8("é plain β")
  expected <- with_backend(
    "stringi",
    charr_test_leaf("ci_split_fixed")(value, " ")
  )

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(value)
    } else {
      value
    }
    actual <- with_backend(
      backend,
      charr_test_leaf("ci_split_fixed")(input, " ")
    )
    expect_identical(actual, expected)
    expect_identical(Encoding(actual[[1]]), Encoding(expected[[1]]))
  }
})
