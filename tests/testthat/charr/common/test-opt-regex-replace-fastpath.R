regex_replace_fastpath_input <- function(backend, value) {
  if (identical(backend, "altrep")) charport::as_charvec(value) else value
}

regex_replace_fastpath_events <- function(expr) {
  warnings <- character()
  error <- NULL
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        warnings <<- c(warnings, conditionMessage(condition))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      error <<- conditionMessage(condition)
      NULL
    }
  )
  list(warnings = warnings, error = error, value = value)
}

test_that("optimized regex replacement matches stringi on multilingual text", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  latin1 <- iconv("caf\u00e9 noir", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  native <- iconv("jalape\u00f1o verde", from = "UTF-8", to = "")
  Encoding(native) <- "unknown"
  values <- c(
    "one two three", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a",
    "a\u0301 propos", "\ufeff alpha", malformed, latin1, native, "", NA_character_
  )
  pattern <- "(?<=\\s)(\\p{L}[\\p{L}\\p{M}]*)"
  replacement <- "<$1>"
  operations <- list(
    all = function(x, p, r) charr_test_leaf("ci_replace_all_regex")(x, p, r),
    first = function(x, p, r) charr_test_leaf("ci_replace_first_regex")(x, p, r)
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values, pattern, replacement))
    for (backend in c("base", "altrep")) {
      subject <- regex_replace_fastpath_input(backend, values)
      actual <- with_backend(backend, operation(subject, pattern, replacement))
      expect_identical(actual, expected)
      expect_identical(Encoding(actual), Encoding(expected))
      if (identical(backend, "altrep")) {
        expect_true(charport::is_charvec(actual))
        expect_false(charport::charport_info(actual)$is_materialized)
        expect_false(charport::charport_info(subject)$is_materialized)
      }
    }
  }
})

test_that("optimized regex replacement preserves vectorization and ICU syntax", {
  values <- c("abab", "a1b2", "", NA_character_)
  patterns <- c("(a)", "(?=[0-9])", "^$", "x", "b", "z")
  replacements <- c("[$0:$1]", "<$0>", "empty")
  operations <- list(
    all = function(x, p, r) charr_test_leaf("ci_replace_all_regex")(x, p, r),
    first = function(x, p, r) charr_test_leaf("ci_replace_first_regex")(x, p, r)
  )

  for (operation in operations) {
    expected <- regex_replace_fastpath_events(
      with_backend("stringi", operation(values, patterns, replacements))
    )
    for (backend in c("base", "altrep")) {
      subject <- regex_replace_fastpath_input(backend, values)
      pattern <- regex_replace_fastpath_input(backend, patterns)
      replacement <- regex_replace_fastpath_input(backend, replacements)
      actual <- regex_replace_fastpath_events(
        with_backend(backend, operation(subject, pattern, replacement))
      )
      expect_identical(actual, expected)
      if (identical(backend, "altrep")) {
        expect_false(charport::charport_info(subject)$is_materialized)
        expect_false(charport::charport_info(pattern)$is_materialized)
        expect_false(charport::charport_info(replacement)$is_materialized)
      }
    }
  }

  for (backend in c("base", "altrep")) {
    missing <- regex_replace_fastpath_input(backend, NA_character_)
    expect_identical(
      with_backend(
        backend,
        charr_test_leaf("ci_replace_all_regex")(missing, "[", "replacement")
      ),
      NA_character_
    )
    expect_identical(
      regex_replace_fastpath_events(
        with_backend(
          backend,
          charr_test_leaf("ci_replace_all_regex")(
            regex_replace_fastpath_input(backend, "abc"), "(a)", "$9"
          )
        )
      ),
      regex_replace_fastpath_events(
        with_backend(
          "stringi", charr_test_leaf("ci_replace_all_regex")("abc", "(a)", "$9")
        )
      )
    )

    edge_cases <- list(
      list(string = "abc", pattern = "", replacement = "x"),
      list(string = "abc", pattern = NA_character_, replacement = "x"),
      list(string = c("aba", "xyz"), pattern = "a", replacement = NA_character_),
      list(string = "aa", pattern = "(?=a)", replacement = "X")
    )
    for (case in edge_cases) {
      expected <- regex_replace_fastpath_events(
        with_backend(
          "stringi",
          charr_test_leaf("ci_replace_all_regex")(
            case$string, case$pattern, case$replacement
          )
        )
      )
      actual <- regex_replace_fastpath_events(
        with_backend(
          backend,
          charr_test_leaf("ci_replace_all_regex")(
            regex_replace_fastpath_input(backend, case$string),
            regex_replace_fastpath_input(backend, case$pattern),
            regex_replace_fastpath_input(backend, case$replacement)
          )
        )
      )
      expect_identical(actual, expected)
    }
  }
})

test_that("optimized regex replacement rejects bytes and preserves BOMs", {
  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"
  bom <- rawToChar(as.raw(c(0xef, 0xbb, 0xbf, 0x61, 0x62, 0x63)))
  Encoding(bom) <- "UTF-8"

  expected_bom <- with_backend(
    "stringi", charr_test_leaf("ci_replace_first_regex")(bom, ".", "Z")
  )
  byte_cases <- list(
    list(string = bytes, pattern = "x", replacement = "y"),
    list(string = "a", pattern = bytes, replacement = "y"),
    list(string = "a", pattern = "z", replacement = bytes)
  )
  for (backend in c("base", "altrep")) {
    input <- regex_replace_fastpath_input(backend, bom)
    expect_identical(
      with_backend(backend, charr_test_leaf("ci_replace_first_regex")(input, ".", "Z")),
      expected_bom
    )
    for (case in byte_cases) {
      expect_identical(
        regex_replace_fastpath_events(
          with_backend(
            backend,
            charr_test_leaf("ci_replace_all_regex")(
              regex_replace_fastpath_input(backend, case$string),
              regex_replace_fastpath_input(backend, case$pattern),
              regex_replace_fastpath_input(backend, case$replacement)
            )
          )
        ),
        regex_replace_fastpath_events(
          with_backend(
            "stringi",
            charr_test_leaf("ci_replace_all_regex")(
              case$string, case$pattern, case$replacement
            )
          )
        )
      )
    }
  }
})
