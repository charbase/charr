base_native_leaves <- function() {
  charr:::.charr_backend_environments$base
}

test_that("base warning errors unwind search owners and permit recovery", {
  leaves <- base_native_leaves()
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  expect_error(
    leaves$stri_detect_fixed("alpha", ""),
    "empty search patterns are not supported",
    fixed = TRUE
  )
  expect_identical(leaves$stri_detect_fixed("alpha", "alpha"), TRUE)
})

test_that("base converter warnings are emitted after ICU owners close", {
  leaves <- base_native_leaves()
  subject <- "\U0001f642"
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  expect_error(
    leaves$stri_conv(subject, "UTF-8", "ISO-8859-1", FALSE),
    "Unicode code point",
    fixed = TRUE
  )
  expect_identical(
    leaves$stri_conv(subject, "UTF-8", "UTF-8", FALSE),
    subject
  )
})

test_that("base entry boundary catches warning errors before operation setup", {
  leaves <- base_native_leaves()
  options_brkiter <- list(type = "word", locale = "en_US")
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  expect_error(
    leaves$stri_trans_totitle(
      list(c("alpha", "beta")), opts_brkiter = options_brkiter
    ),
    "argument is not an atomic vector; coercing",
    fixed = TRUE
  )
  expect_identical(
    leaves$stri_trans_totitle("alpha beta", opts_brkiter = options_brkiter),
    "Alpha Beta"
  )
})

test_that("base collator fallback warnings close the opened ICU owner", {
  leaves <- base_native_leaves()
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  expect_error(
    leaves$stri_order(
      c("b", "a"), opts_collator = list(locale = "zz_ZZ")
    ),
    "resource bundle",
    fixed = TRUE
  )
  expect_identical(
    leaves$stri_order(
      c("b", "a"), opts_collator = list(locale = "en")
    ),
    c(2L, 1L)
  )
})
