test_that("optimized fixed extract keeps scalar ASCII matches and shapes", {
  malformed <- rawToChar(as.raw(c(
    0x61, 0xc3, 0x28, 0x20, 0x62, 0x20, 0x63
  )))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "é a b",
    paste0("\ufeff", " a "),
    malformed,
    "no-match",
    "",
    NA_character_
  )

  operations <- list(
    first = function(x) charr:::ci_extract_first_fixed(x, " "),
    last = function(x) charr:::ci_extract_last_fixed(x, " "),
    all = function(x) charr:::ci_extract_all_fixed(x, " "),
    all_omit = function(x) {
      charr:::ci_extract_all_fixed(x, " ", omit_no_match = TRUE)
    },
    matrix_empty = function(x) {
      charr:::ci_extract_all_fixed(
        x, " ", omit_no_match = TRUE, simplify = TRUE
      )
    },
    matrix_na = function(x) {
      charr:::ci_extract_all_fixed(
        x, " ", omit_no_match = TRUE, simplify = NA
      )
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    actual <- with_backend("altrep", operation(input))
    if (is.list(actual)) {
      expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
    } else {
      expect_true(charport::is_charvec(actual))
    }
    expect_identical(actual, expected)
  }
})


test_that("cached fixed-extract children retain copy-on-write isolation", {
  values <- c("a a", "b b")
  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }
    output <- with_backend(
      backend,
      charr:::ci_extract_all_fixed(input, " ", omit_no_match = TRUE)
    )
    output[[1L]][1L] <- "X"
    expect_identical(output, list("X", " "))
  }
})


test_that("optimized fixed extract pads missing rows like stringi", {
  values <- c("aa", "none", "", NA_character_)
  operation <- function(x) {
    charr:::ci_extract_all_fixed(
      x, "a", omit_no_match = TRUE, simplify = TRUE
    )
  }
  expected <- with_backend("stringi", operation(values))

  expect_identical(with_backend("base", operation(values)), expected)
  expect_identical(
    with_backend("altrep", operation(charport::as_charvec(values))),
    expected
  )
})


test_that("optimized fixed extract falls back for conversion and options", {
  latin1 <- iconv("café ici", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "A a A", NA_character_)

  operations <- list(
    multibyte = function(x) charr:::ci_extract_first_fixed(x, "é"),
    insensitive = function(x) {
      charr:::ci_extract_all_fixed(x, "a", case_insensitive = TRUE)
    },
    recycled = function(x) {
      charr:::ci_extract_all_fixed(x, c("i", "A"), overlap = TRUE)
    }
  )

  for (operation in operations) {
    expected <- suppressWarnings(
      with_backend("stringi", operation(values))
    )
    expect_identical(
      suppressWarnings(with_backend("base", operation(values))),
      expected
    )
    expect_identical(
      suppressWarnings(with_backend(
        "altrep", operation(charport::as_charvec(values))
      )),
      expected
    )
  }
})


test_that("fixed extract keeps completed rows across mixed encodings", {
  marked <- function(bytes, encoding) {
    value <- rawToChar(as.raw(bytes))
    Encoding(value) <- encoding
    value
  }

  latin1 <- marked(c(0x61, 0xe9, 0x20, 0x61), "latin1")
  native <- marked(c(0x61, 0x20, 0xc3, 0xa9, 0x20, 0x61), "unknown")
  malformed <- marked(c(0x61, 0xff, 0x20, 0x61), "UTF-8")
  values <- c(
    "a a", "none", latin1, native, malformed, NA_character_, ""
  )

  operations <- list(
    first = function(x) charr:::ci_extract_first_fixed(x, " "),
    last = function(x) charr:::ci_extract_last_fixed(x, " "),
    all = function(x) charr:::ci_extract_all_fixed(x, " "),
    all_omit = function(x) {
      charr:::ci_extract_all_fixed(x, " ", omit_no_match = TRUE)
    },
    matrix_empty = function(x) {
      charr:::ci_extract_all_fixed(
        x, " ", omit_no_match = TRUE, simplify = TRUE
      )
    },
    matrix_na = function(x) {
      charr:::ci_extract_all_fixed(
        x, " ", omit_no_match = TRUE, simplify = NA
      )
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend(
        "altrep", operation(charport::as_charvec(values))
      ),
      expected
    )
  }
})


test_that("fixed extract still rejects bytes after a direct prefix", {
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"
  values <- c("a a", "none", bytes)
  operations <- list(
    function(x) charr:::ci_extract_first_fixed(x, " "),
    function(x) charr:::ci_extract_last_fixed(x, " "),
    function(x) charr:::ci_extract_all_fixed(x, " "),
    function(x) {
      charr:::ci_extract_all_fixed(x, " ", simplify = TRUE)
    }
  )

  for (operation in operations) {
    expect_error(
      with_backend("base", operation(values)),
      "bytes encoding"
    )
    expect_error(
      with_backend(
        "altrep", operation(charport::as_charvec(values))
      ),
      "bytes encoding"
    )
  }
})


test_that("fixed extract validates bytes before an empty pattern", {
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"
  operations <- list(
    function(x) charr:::ci_extract_first_fixed(x, ""),
    function(x) charr:::ci_extract_last_fixed(x, ""),
    function(x) charr:::ci_extract_all_fixed(x, ""),
    function(x) {
      charr:::ci_extract_all_fixed(x, "", simplify = TRUE)
    },
    function(x) {
      charr:::ci_extract_all_fixed(x, "", simplify = NA)
    }
  )

  for (operation in operations) {
    expect_error(
      with_backend("base", operation(bytes)),
      "bytes encoding"
    )
    expect_error(
      with_backend(
        "altrep", operation(charport::as_charvec(bytes))
      ),
      "bytes encoding"
    )
  }
})
