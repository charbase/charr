test_that("optimized scalar substring paths match stringi", {
  latin1 <- iconv("café monde", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "aé\U0001f642üz",
    "abcdefghij",
    "漢字かな交じり",
    latin1,
    paste0("\ufeff", "abcdef"),
    malformed,
    "",
    NA_character_
  )

  operations <- list(
    sub = function(x) charr:::ci_sub(x, 2L, 8L),
    replace = function(x) {
      charr:::ci_sub_replace(x, 2L, 5L, replacement = "X")
    },
    sub_all = function(x) {
      charr:::ci_sub_all(x, list(2L), list(8L))
    },
    replace_all = function(x) {
      charr:::ci_sub_replace_all(
        x, list(2L), list(5L), replacement = "X"
      )
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))

    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend("altrep", operation(charport::as_charvec(values))),
      expected
    )
  }
})


test_that("optimized scalar substring paths retain missing replacements", {
  values <- c("aé\U0001f642üz", "abcdef", NA_character_)

  expected <- with_backend(
    "stringi",
    charr:::ci_sub_replace_all(
      values, list(2L), list(5L), replacement = NA_character_
    )
  )
  expected_omit <- with_backend(
    "stringi",
    charr:::ci_sub_replace_all(
      values, list(2L), list(5L), omit_na = TRUE,
      replacement = NA_character_
    )
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
        charr:::ci_sub_replace_all(
          input, list(2L), list(5L), replacement = NA_character_
        )
      ),
      expected
    )
    expect_identical(
      with_backend(
        backend,
        charr:::ci_sub_replace_all(
          input, list(2L), list(5L), omit_na = TRUE,
          replacement = NA_character_
        )
      ),
      expected_omit
    )
  }
})


test_that("optimized scalar substring paths retain malformed UTF-8 bytes", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x62, 0x63)))
  Encoding(malformed) <- "UTF-8"

  operations <- list(
    function(x) charr:::ci_sub(x, 2L, 3L),
    function(x) {
      charr:::ci_sub_replace(x, 2L, 3L, replacement = "X")
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(malformed))
    expect_identical(with_backend("base", operation(malformed)), expected)
    expect_identical(
      with_backend(
        "altrep", operation(charport::as_charvec(malformed))
      ),
      expected
    )
  }
})


test_that("optimized scalar replacement normalizes its replacement once", {
  latin1 <- iconv("café", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c("abcdef", "aé🙂z", "\ufeffabcdef", NA_character_)
  replacements <- list("X", latin1, "\ufeffxy", malformed, NA_character_)

  for (replacement in replacements) {
    for (omit_na in c(FALSE, TRUE)) {
      operation <- function(x, replacement) {
        charr:::ci_sub_replace(
          x, 2L, 3L, omit_na = omit_na,
          replacement = replacement
        )
      }
      expected <- with_backend(
        "stringi", operation(values, replacement)
      )

      expect_identical(
        with_backend("base", operation(values, replacement)),
        expected
      )
      expect_identical(
        with_backend(
          "altrep",
          operation(
            charport::as_charvec(values),
            charport::as_charvec(replacement)
          )
        ),
        expected
      )
    }
  }
})
