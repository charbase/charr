test_that("optimized length and width accept unregistered character ALTREP", {
  n <- 1000L
  expected_length <- with_backend(
    "stringi",
    str_length(as.character(seq_len(n)))
  )
  expected_width <- with_backend(
    "stringi",
    str_width(as.character(seq_len(n)))
  )

  for (backend in c("base", "altrep")) {
    input <- as.character(seq_len(n))
    info <- charport::charport_info(input)

    expect_true(info$is_altrep)
    expect_false(info$is_registered)
    expect_false(info$is_materialized)
    expect_identical(
      with_backend(backend, str_length(input)), expected_length
    )
    expect_identical(
      with_backend(backend, str_width(input)), expected_width
    )
  }
})


test_that("optimized length and width preserve encoding semantics", {
  latin1 <- iconv("caf\u00e9", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(
    "", "plain ASCII", "line\tbreak\n", latin1, "e\u0301", "\uff21",
    "\U0001f469\u200d\U0001f467", "\U0001f1fa\U0001f1f8",
    "\ufeffstart", NA_character_
  )

  expected_length <- with_backend("stringi", str_length(values))
  expected_width <- with_backend("stringi", str_width(values))

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }
    expect_identical(
      with_backend(backend, str_length(input)), expected_length
    )
    expect_identical(
      with_backend(backend, str_width(input)), expected_width
    )
  }
})


test_that("optimized length and width reject malformed and bytes input", {
  malformed <- rawToChar(as.raw(0xc3))
  Encoding(malformed) <- "UTF-8"
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"

  for (backend in c("base", "altrep")) {
    prepare <- function(value) {
      if (identical(backend, "altrep")) {
        charport::as_charvec(value)
      } else {
        value
      }
    }
    expect_error(
      with_backend(backend, str_length(prepare(malformed))),
      "invalid UTF-8"
    )
    expect_error(
      with_backend(backend, str_width(prepare(malformed))),
      "invalid UTF-8"
    )
    expect_error(
      with_backend(backend, str_length(prepare(bytes))),
      "bytes encoding"
    )
    expect_error(
      with_backend(backend, str_width(prepare(bytes))),
      "bytes encoding"
    )
  }
})


test_that("optimized length converts coercion warnings to recoverable errors", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  input <- list(c("alpha", "beta"))
  for (backend in c("base", "altrep")) {
    expect_error(
      with_backend(backend, str_length(input)),
      "argument is not an atomic vector; coercing",
      fixed = TRUE
    )
    expect_identical(with_backend(backend, str_length("alpha")), 5L)
  }
})
