regex_extract_first_backend <- function(backend, x, pattern) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_extract_first_regex(x, pattern))
  }
  native <- if (identical(backend, "base")) {
    charr:::C_charr_base_ci_extract_first_regex
  } else {
    charr:::C_ci_extract_first_regex
  }
  .Call(native, x, pattern, NULL)
}


regex_extract_all_backend <- function(
  backend, x, pattern, simplify = FALSE, omit_no_match = FALSE
) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_extract_all_regex(
      x, pattern, simplify = simplify, omit_no_match = omit_no_match
    ))
  }
  native <- if (identical(backend, "base")) {
    charr:::C_charr_base_ci_extract_all_regex
  } else {
    charr:::C_ci_extract_all_regex
  }
  .Call(native, x, pattern, simplify, omit_no_match, NULL)
}


test_that("optimized regex extraction matches stringi on scalar patterns", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "one two three", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a",
    "\U0001f600 alpha", paste0("\ufeff", " beta"), malformed,
    "no-space", "", NA_character_
  )

  operations <- list(
    first = function(backend, x) {
      regex_extract_first_backend(
        backend, x, "(?<=\\s)(\\p{L}[\\p{L}\\p{M}]*)"
      )
    },
    all = function(backend, x) {
      regex_extract_all_backend(
        backend, x, "\\p{L}+", omit_no_match = TRUE
      )
    },
    zero_width = function(backend, x) {
      regex_extract_all_backend(
        backend, x, "(?=\\p{L})", omit_no_match = TRUE
      )
    },
    matrix = function(backend, x) {
      regex_extract_all_backend(
        backend, x, "\\p{L}+", omit_no_match = TRUE, simplify = NA
      )
    }
  )

  for (operation in operations) {
    expected <- operation("stringi", values)
    base_result <- operation("base", values)
    expect_identical(base_result, expected)
    if (is.character(base_result)) {
      expect_false(charport::is_charvec(base_result))
    }

    input <- charport::as_charvec(values)
    altrep_result <- operation("altrep", input)
    expect_identical(altrep_result, expected)
    if (is.list(altrep_result)) {
      expect_true(all(vapply(
        altrep_result, charport::is_charvec, logical(1)
      )))
    } else {
      expect_true(charport::is_charvec(altrep_result))
    }
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("regex extraction keeps malformed UTF-8 source slices", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  operations <- list(
    replacement = function(backend, x) {
      regex_extract_first_backend(backend, x, "\\ufffd")
    },
    spanning = function(backend, x) {
      regex_extract_first_backend(backend, x, "a\\ufffd\\s")
    },
    all_spanning = function(backend, x) {
      regex_extract_all_backend(
        backend, x, "(?:a)?\\ufffd\\s", omit_no_match = TRUE
      )
    }
  )

  expected_bytes <- list(
    replacement = as.raw(0xff),
    spanning = as.raw(c(0x61, 0xff, 0x20)),
    all_spanning = as.raw(c(0x61, 0xff, 0x20))
  )
  for (name in names(operations)) {
    operation <- operations[[name]]
    expected <- operation("stringi", malformed)
    expected_value <- if (is.list(expected)) expected[[1L]][[1L]] else expected
    expect_identical(charToRaw(expected_value), expected_bytes[[name]])
    expect_identical(Encoding(expected_value), "UTF-8")

    for (backend in c("base", "altrep")) {
      input <- if (identical(backend, "altrep")) {
        charport::as_charvec(malformed)
      } else {
        malformed
      }
      actual <- operation(backend, input)
      expect_identical(actual, expected)
      actual_value <- if (is.list(actual)) actual[[1L]][[1L]] else actual
      expect_identical(charToRaw(actual_value), expected_bytes[[name]])
      expect_identical(Encoding(actual_value), "UTF-8")
    }
  }
})


test_that("optimized regex extraction retains conversion and recycling rules", {
  skip_if_stringi_cannot_compare_native()

  latin1 <- iconv("caf\u00e9 noir", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  native <- enc2native("na\u00efve text")
  Encoding(native) <- "unknown"
  values <- c(latin1, native, "plain text", NA_character_)

  operations <- list(
    first = function(backend, x) {
      regex_extract_first_backend(backend, x, "\\p{L}+")
    },
    all = function(backend, x) {
      regex_extract_all_backend(
        backend, x, "\\p{L}+", omit_no_match = TRUE
      )
    },
    recycled = function(backend, x) {
      regex_extract_first_backend(
        backend, x[1:3], c("\\p{L}+", "text")
      )
    },
    empty = function(backend, x) {
      regex_extract_first_backend(backend, x, "")
    },
    missing = function(backend, x) {
      regex_extract_all_backend(backend, x, NA_character_)
    }
  )

  for (operation in operations) {
    expected <- suppressWarnings(operation("stringi", values))
    expect_identical(
      suppressWarnings(operation("base", values)),
      expected
    )
    expect_identical(
      suppressWarnings(operation("altrep", charport::as_charvec(values))),
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
      regex_extract_first_backend(backend, input, "x"),
      "bytes encoding"
    )
    expect_error(
      regex_extract_all_backend(backend, input, "x"),
      "bytes encoding"
    )
  }
})
