wrap_selected_leaf <- function(...) {
  backend <- charr_backend()
  leaf <- get(
    "stri_wrap",
    envir = charr:::.charr_backend_environments[[backend]],
    inherits = FALSE
  )
  leaf(...)
}

wrap_stringi_joined <- function(...) {
  value <- stringi::stri_wrap(..., simplify = FALSE)
  vapply(
    value,
    function(x) if (anyNA(x)) NA_character_ else paste(x, collapse = "\n"),
    character(1L)
  )
}

# ICU carries no line-break resource for th_TH, so the lookup falls back to the
# root bundle and stringi reports it (gagolews/stringi#476, added in 1.8.1).
# charr reproduces stringi rather than hiding the condition, so the warning is
# part of what these tests check.
root_fallback_warning <- "resource bundle lookup returned a result"

# Returns the value and every warning raised, so a comparison can cover both.
# Muffling here also keeps the expected warnings out of the test summary, where
# dozens of copies would bury a real one.
wrap_with_warnings <- function(expr) {
  warnings <- character()
  value <- withCallingHandlers(
    expr,
    warning = function(condition) {
      warnings <<- c(warnings, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = warnings)
}

test_that("wrap preserves supplementary graphemes", {
  family_text <- paste0(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466",
    " e\u0301 family"
  )
  actual <- wrap_selected_leaf(
    family_text, width = 5L, simplify = FALSE, normalize = FALSE,
    locale = "en_US"
  )
  expected <- stringi::stri_wrap(
    family_text, width = 5L, simplify = FALSE, normalize = FALSE,
    locale = "en_US"
  )
  expect_identical(actual, expected)
  expect_identical(paste(actual[[1L]], collapse = " "), family_text)
})


test_that("native wrap normalization matches stringi's complete preprocessing", {
  bom <- "\uFEFF"
  line_separators <- c(
    "\n", "\r\n", "\r", "\v", "\f", "\u0085", "\u2028", "\u2029"
  )
  unicode_whitespace <- intToUtf8(c(
    0x0009:0x000d, 0x0020, 0x0085, 0x00a0, 0x1680,
    0x2000:0x200a, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000
  ))
  strings <- c(
    setNames(paste0("left", line_separators, "right"), paste0("sep", seq_along(line_separators))),
    setNames(paste0("left", line_separators, bom, "right"), paste0("sep_bom", seq_along(line_separators))),
    setNames(vapply(0:8, function(n) paste0(strrep(bom, n), "head"), character(1L)), paste0("bom", 0:8)),
    paste0("\n", strrep(bom, 5), "after-leading-separator"),
    paste0(" ", strrep(bom, 5), "after-leading-space"),
    paste0("left\n", strrep(bom, 5), "right"),
    paste0("left\n ", strrep(bom, 5), "right"),
    paste0(unicode_whitespace, "trimmed", unicode_whitespace),
    "left \u00a0 \t right",
    "Cafe\u0301 and A\u030a",
    "", NA_character_
  )

  for (simplify in c(FALSE, TRUE)) {
    actual <- wrap_selected_leaf(
      strings, width = 13L, cost_exponent = 2,
      simplify = simplify, normalize = TRUE, indent = 1L, exdent = 2L,
      prefix = "p> ", initial = "i> ", whitespace_only = FALSE,
      use_length = FALSE, locale = "en_US"
    )
    expected <- stringi::stri_wrap(
      strings, width = 13L, cost_exponent = 2,
      simplify = simplify, normalize = TRUE, indent = 1L, exdent = 2L,
      prefix = "p> ", initial = "i> ", whitespace_only = FALSE,
      use_length = FALSE, locale = "en_US"
    )
    expect_identical(actual, expected)
  }
})

test_that("wrapping a locale without break data warns like stringi", {
  # Asserted here so the option sweep below can compare warnings between the
  # two implementations without that comparison passing vacuously if ICU ever
  # stops falling back. A failure here means the expectation moved, not that
  # charr broke: check whether stringi still warns before changing anything.
  expect_warning(
    stringi::stri_wrap("ภาษาไทย", width = 8L, locale = "th_TH"),
    root_fallback_warning
  )
  expect_warning(
    wrap_selected_leaf("ภาษาไทย", width = 8L, locale = "th_TH"),
    root_fallback_warning
  )
})

test_that("native normalization feeds every wrap mode and option path", {
  strings <- c(
    first = "  alpha\r\nbeta  gamma\t delta  ",
    second = "日本語\u2028ภาษาไทย e\u0301 zeta",
    missing = NA_character_, empty = ""
  )

  for (cost_exponent in c(-1, 2)) {
    for (whitespace_only in c(FALSE, TRUE)) {
      for (use_length in c(FALSE, TRUE)) {
        arguments <- list(
          str = strings, width = 8L, cost_exponent = cost_exponent,
          normalize = TRUE, indent = 2L, exdent = 1L,
          prefix = "λ> ", initial = "初> ",
          whitespace_only = whitespace_only, use_length = use_length,
          locale = "th_TH"
        )
        actual_list <- wrap_with_warnings(do.call(
          wrap_selected_leaf, c(arguments, list(simplify = FALSE))
        ))
        expected_list <- wrap_with_warnings(do.call(
          stringi::stri_wrap, c(arguments, list(simplify = FALSE))
        ))
        expect_identical(actual_list$value, expected_list$value)
        expect_identical(actual_list$warnings, expected_list$warnings)

        actual_flat <- wrap_with_warnings(do.call(
          wrap_selected_leaf, c(arguments, list(simplify = TRUE))
        ))
        expected_flat <- wrap_with_warnings(do.call(
          stringi::stri_wrap, c(arguments, list(simplify = TRUE))
        ))
        expect_identical(actual_flat$value, expected_flat$value)
        expect_identical(actual_flat$warnings, expected_flat$warnings)

        if (!identical(charr_backend(), "stringi")) {
          actual_joined <- wrap_with_warnings(do.call(
            wrap_selected_leaf,
            c(arguments, list(simplify = TRUE, .output_mode = 2L))
          ))
          expected_joined <- wrap_with_warnings(
            do.call(wrap_stringi_joined, arguments)
          )
          expect_identical(actual_joined$value, expected_joined$value)
          expect_identical(
            actual_joined$warnings, expected_joined$warnings
          )
        }
      }
    }
  }
})

test_that("native wrap keeps normalize aliases and invalid inputs compatible", {
  expect_identical(
    wrap_selected_leaf("a\nb", width = 80L, normalize = FALSE, normalise = TRUE),
    stringi::stri_wrap("a\nb", width = 80L, normalize = FALSE, normalise = TRUE)
  )
  expect_error(
    wrap_selected_leaf("a", normalize = logical()),
    "argument is of length zero"
  )
  expect_error(
    wrap_selected_leaf("a", normalize = NA),
    "missing value where TRUE/FALSE needed"
  )
  expect_error(
    wrap_selected_leaf("a", normalize = c(TRUE, FALSE)),
    "condition has length > 1"
  )

  bytes <- rawToChar(as.raw(c(0x61, 0x20, 0xff)))
  Encoding(bytes) <- "bytes"
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  expect_error(wrap_selected_leaf(bytes, normalize = TRUE), "bytes")
  expect_error(
    wrap_selected_leaf(malformed, normalize = TRUE),
    "invalid UTF-8 byte sequence"
  )
})
