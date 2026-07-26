test_that("optimized join paths match stringi on direct UTF-8 input", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    alpha = "plain",
    bom = paste0("\ufeff", "value"),
    unicode = "caf\u00e9",
    malformed = malformed,
    empty = "",
    missing = NA_character_
  )
  times <- c(2L, 1L, 3L, 2L, 0L, NA_integer_)

  operations <- list(
    join = function(x) charr:::ci_c(x, "!"),
    flatten = function(x) {
      charr:::ci_flatten(x, "|", na_empty = TRUE, omit_empty = FALSE)
    },
    flatten_omit = function(x) {
      charr:::ci_flatten(x, "|", na_empty = NA, omit_empty = TRUE)
    },
    duplicate = function(x) charr:::ci_dup(x, times)
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend(
        "altrep",
        operation(charport::as_charvec(unname(values)))
      ),
      unname(expected)
    )
  }
})


test_that("optimized join paths retain conversion and byte errors", {
  latin1 <- iconv("caf\u00e9", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "x", NA_character_)

  operations <- list(
    join = function(x) charr:::ci_c(x, "!"),
    flatten = function(x) {
      charr:::ci_flatten(x, "|", na_empty = TRUE)
    },
    duplicate = function(x) charr:::ci_dup(x, c(2L, 1L, 0L))
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend(
        "altrep",
        operation(charport::as_charvec(values))
      ),
      expected
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
      with_backend(backend, charr:::ci_c(input, "x")),
      "bytes encoding"
    )
    expect_error(
      with_backend(backend, charr:::ci_flatten(input, "|")),
      "bytes encoding"
    )
    expect_error(
      with_backend(backend, charr:::ci_dup(input, 2L)),
      "bytes encoding"
    )
  }
})
