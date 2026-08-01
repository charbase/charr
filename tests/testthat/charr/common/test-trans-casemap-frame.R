casemap_frame_function <- function(backend, mode) {
  name <- paste0("ci_trans_to", mode)

  if (identical(backend, "base")) {
    return(get(
      name,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }

  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }

  if (identical(backend, "stringi")) {
    return(get(
      paste0("stri_trans_to", mode),
      envir = asNamespace("stringi"),
      inherits = FALSE
    ))
  }

  stop("unknown casemap backend", call. = FALSE)
}


casemap_frame_call <- function(backend, mode, value, locale) {
  fun <- casemap_frame_function(backend, mode)
  input <- if (identical(backend, "altrep")) {
    charport::as_charvec(value)
  } else {
    value
  }

  list(input = input, output = fun(input, locale))
}


expect_casemap_frame_parity <- function(mode, values, locale) {
  expected <- casemap_frame_call("stringi", mode, values, locale)$output

  for (backend in c("base", "altrep")) {
    actual <- casemap_frame_call(backend, mode, values, locale)

    if (identical(backend, "altrep")) {
      expect_false(charport::charport_info(actual$input)$is_materialized)
      expect_true(charport::is_charvec(actual$output))
      expect_false(charport::charport_info(actual$output)$is_materialized)
    }

    expect_identical(actual$output, expected)
    expect_identical(Encoding(as.character(actual$output)), Encoding(expected))
  }
}


test_that("lower and upper Frame paths preserve values and encodings", {
  utf8 <- rawToChar(as.raw(c(0xc3, 0xa9)))
  Encoding(utf8) <- "UTF-8"
  latin1 <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(latin1) <- "latin1"
  bom <- rawToChar(as.raw(c(0xef, 0xbb, 0xbf, 0x41, 0x62, 0x43)))
  Encoding(bom) <- "UTF-8"
  values <- c(
    unchanged = "123-!?",
    ascii = "Already Lower",
    utf8 = utf8,
    latin1 = latin1,
    bom = bom,
    empty = "",
    missing = NA_character_
  )

  expect_casemap_frame_parity("lower", values, "en")
  expect_casemap_frame_parity("upper", values, "en")

  expected_bom <- c(lower = "abc", upper = "ABC")
  for (mode in names(expected_bom)) {
    for (backend in c("base", "altrep")) {
      actual <- casemap_frame_call(backend, mode, bom, "en")$output
      expect_identical(actual, unname(expected_bom[[mode]]))
      expect_identical(Encoding(as.character(actual)), "unknown")
    }
  }
})


test_that("lower and upper Frame paths preserve Turkic mappings", {
  values <- c("I", "i", "\u0130", "\u0131")
  expected_lower <- c("\u0131", "i", "i", "\u0131")
  expected_upper <- c("I", "\u0130", "\u0130", "I")

  oracle_lower <- casemap_frame_call(
    "stringi", "lower", values, "tr"
  )$output
  oracle_upper <- casemap_frame_call(
    "stringi", "upper", values, "tr"
  )$output
  skip_if_not(
    identical(oracle_lower, expected_lower) &&
      identical(oracle_upper, expected_upper),
    "ICU build does not provide the expected Turkic mappings"
  )

  expect_casemap_frame_parity("lower", values, "tr")
  expect_casemap_frame_parity("upper", values, "tr")
})


test_that("lower and upper Frame paths match malformed UTF-8 behavior", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"

  for (mode in c("lower", "upper")) {
    expected <- casemap_frame_call(
      "stringi", mode, malformed, "en"
    )$output

    for (backend in c("base", "altrep")) {
      actual <- casemap_frame_call(backend, mode, malformed, "en")
      if (identical(backend, "altrep")) {
        expect_false(charport::charport_info(actual$input)$is_materialized)
        expect_false(charport::charport_info(actual$output)$is_materialized)
      }
      expect_identical(charToRaw(actual$output), charToRaw(expected))
      expect_identical(Encoding(as.character(actual$output)), Encoding(expected))
    }
  }
})


test_that("lower and upper Frame paths remain reusable after bytes errors", {
  bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(bytes) <- "bytes"
  valid <- c("AbC", "Stra\u00dfe", "", NA_character_)

  for (mode in c("lower", "upper")) {
    expected_error <- tryCatch(
      casemap_frame_call("stringi", mode, bytes, "en")$output,
      error = conditionMessage
    )
    expected_valid <- casemap_frame_call(
      "stringi", mode, valid, "en"
    )$output

    for (backend in c("base", "altrep")) {
      fun <- casemap_frame_function(backend, mode)
      input <- if (identical(backend, "altrep")) {
        charport::as_charvec(bytes)
      } else {
        bytes
      }

      expect_error(fun(input, "en"), expected_error, fixed = TRUE)
      if (identical(backend, "altrep")) {
        expect_false(charport::charport_info(input)$is_materialized)
      }

      actual <- casemap_frame_call(backend, mode, valid, "en")
      if (identical(backend, "altrep")) {
        expect_false(charport::charport_info(actual$input)$is_materialized)
        expect_false(charport::charport_info(actual$output)$is_materialized)
      }
      expect_identical(actual$output, expected_valid)
    }
  }
})
