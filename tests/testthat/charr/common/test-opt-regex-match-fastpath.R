regex_match_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("optimized regex match preserves captures and marked input", {
  malformed <- regex_match_marked(c(0x61, 0x80, 0x62), "UTF-8")
  values <- c(
    "alpha beta", "caf\u00e9 noir", "\U0001f600 word",
    paste0("\ufeff", "bom text"), malformed, "no-space", NA_character_
  )
  pattern <- "(?<space>\\s+)(?<word>[\\p{L}\\p{M}]+)?"

  operations <- list(
    function(x) charr:::ci_match_first_regex(x, pattern, cg_missing = "MISS"),
    function(x) charr:::ci_match_last_regex(x, pattern, cg_missing = "MISS"),
    function(x) charr:::ci_match_all_regex(x, pattern, cg_missing = "MISS"),
    function(x) charr:::ci_match_all_regex(
      x, pattern, omit_no_match = TRUE, cg_missing = "MISS"
    )
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    expect_identical(with_backend("altrep", operation(input)), expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("optimized regex match retains vectorized capture shapes", {
  values <- c("a1", "b", "c33", NA_character_)
  patterns <- c(
    "(?<letter>a)(?<number>1)",
    "(?<letter>b)(?<optional>z)?",
    "(?<letter>c)(?<number>3+)",
    "(?<missing>x)"
  )

  operations <- list(
    function(x, p) charr:::ci_match_first_regex(x, p),
    function(x, p) charr:::ci_match_all_regex(x, p)
  )
  for (operation in operations) {
    expected <- with_backend("stringi", operation(values, patterns))
    expect_identical(
      with_backend("base", operation(values, patterns)),
      expected
    )
    expect_identical(
      with_backend(
        "altrep",
        operation(
          charport::as_charvec(values),
          charport::as_charvec(patterns)
        )
      ),
      expected
    )
  }
})


test_that("optimized regex match rejects bytes-marked input", {
  bytes <- regex_match_marked(c(0xff, 0xfe), "bytes")

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(bytes)
    } else {
      bytes
    }
    expect_error(
      with_backend(backend, charr:::ci_match_first_regex(input, "(.)")),
      "bytes encoding"
    )
    expect_error(
      with_backend(backend, charr:::ci_match_all_regex("x", input)),
      "bytes encoding"
    )
  }
})
