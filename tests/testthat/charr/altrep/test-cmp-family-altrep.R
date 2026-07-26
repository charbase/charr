# charr-owned equivalence tests for the Reader-backed comparison family.
# Only ci_cmp_equiv is on the stringr dispatch map; exercise every copied
# entry point directly against installed stringi.

cmp_family_charvec <- function(x) charr:::ci_trim_both(x)
cmp_family_cmp <- function(...) charr:::ci_cmp(...)
cmp_family_lt <- function(...) charr:::ci_cmp_lt(...)
cmp_family_le <- function(...) charr:::ci_cmp_le(...)
cmp_family_gt <- function(...) charr:::ci_cmp_gt(...)
cmp_family_ge <- function(...) charr:::ci_cmp_ge(...)
cmp_family_equiv <- function(...) charr:::ci_cmp_equiv(...)
cmp_family_nequiv <- function(...) charr:::ci_cmp_nequiv(...)
cmp_family_eq <- function(...) charr:::ci_cmp_eq(...)
cmp_family_neq <- function(...) charr:::ci_cmp_neq(...)


test_that("collator comparisons preserve recycling, NA, and locale tailoring", {
  left <- cmp_family_charvec(c(
    "ä", "a", "A", "aa", "å", "2", "10", "😀", "", NA, "ü"
  ))
  right <- cmp_family_charvec(c("a", "ä", "å"))
  expect_identical(charport::is_charvec(left), charr_altrep())
  expect_identical(charport::is_charvec(right), charr_altrep())

  for (opts in list(
    list(locale = "de", strength = 1L, numeric = FALSE),
    list(locale = "de", strength = 3L, numeric = TRUE),
    list(locale = "da", strength = 1L, numeric = FALSE),
    list(locale = "da", strength = 3L, numeric = TRUE)
  )) {
    a <- as.character(left)
    b <- as.character(right)
    expect_identical(
      suppressWarnings(cmp_family_cmp(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_lt(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_lt(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_le(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_le(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_gt(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_gt(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_ge(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_ge(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_equiv(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_equiv(a, b, opts_collator = opts))
    )
    expect_identical(
      suppressWarnings(cmp_family_nequiv(left, right, opts_collator = opts)),
      suppressWarnings(stringi::stri_cmp_nequiv(a, b, opts_collator = opts))
    )
  }
})


test_that("code-point equality preserves copied UTF-8 and BOM semantics", {
  composed <- "ü"
  decomposed <- "ü"
  left <- cmp_family_charvec(c(
    "\ufeffa", composed, decomposed, "😀", "", NA, "same"
  ))
  right <- cmp_family_charvec(c(
    "a", decomposed, composed, "😀", "", "x", NA
  ))
  expect_identical(charport::is_charvec(left), charr_altrep())
  expect_identical(charport::is_charvec(right), charr_altrep())

  expect_identical(
    cmp_family_eq(left, right),
    stringi::stri_cmp_eq(as.character(left), as.character(right))
  )
  expect_identical(
    cmp_family_neq(left, right),
    stringi::stri_cmp_neq(as.character(left), as.character(right))
  )
  # Utf8Input strips the BOM but does not normalize canonical forms.
  bom_left <- cmp_family_charvec("\ufeffa")
  bom_right <- cmp_family_charvec("a")
  canonical_left <- cmp_family_charvec(composed)
  canonical_right <- cmp_family_charvec(decomposed)
  expect_identical(charport::is_charvec(bom_left), charr_altrep())
  expect_identical(charport::is_charvec(bom_right), charr_altrep())
  expect_identical(charport::is_charvec(canonical_left), charr_altrep())
  expect_identical(charport::is_charvec(canonical_right), charr_altrep())
  expect_true(cmp_family_eq(bom_left, bom_right))
  expect_false(cmp_family_eq(canonical_left, canonical_right))
  expect_true(cmp_family_equiv(
    canonical_left, canonical_right, strength = 3L
  ))
})


test_that("comparison output drops attributes and zero length skips acquisition", {
  left <- cmp_family_charvec(c("ä", "a", NA, "😀"))
  right <- cmp_family_charvec(c("a", "ä"))
  empty <- cmp_family_charvec(character())
  expect_identical(charport::is_charvec(left), charr_altrep())
  expect_identical(charport::is_charvec(right), charr_altrep())
  expect_identical(charport::is_charvec(empty), charr_altrep())
  names(left) <- paste0("n", seq_along(left))

  expect_identical(
    cmp_family_cmp(left, right),
    stringi::stri_cmp(as.character(left), as.character(right))
  )
  expect_null(attributes(suppressWarnings(cmp_family_cmp(left, right))))
  expect_identical(cmp_family_cmp(empty, left), integer())
  expect_identical(cmp_family_equiv(empty, left), logical())
  expect_identical(cmp_family_eq(empty, left), logical())

  bytes <- cmp_family_charvec("ä")
  Encoding(bytes) <- "bytes"
  expect_identical(charport::is_charvec(bytes), charr_altrep())
  # The copied n=0 container path never visits the otherwise-invalid source.
  expect_identical(cmp_family_cmp(empty, bytes), integer())
  expect_identical(cmp_family_eq(empty, bytes), logical())

  error_message <- function(expr) {
    tryCatch(expr, error = conditionMessage)
  }
  expect_identical(
    error_message(cmp_family_cmp(bytes, bytes)),
    error_message(stringi::stri_cmp(bytes, bytes))
  )
  expect_identical(
    error_message(cmp_family_eq(bytes, bytes)),
    error_message(stringi::stri_cmp_eq(bytes, bytes))
  )
})


test_that("canonical collator warnings fire once", {
  left <- cmp_family_charvec(rep(c("ä", "a", "😀", NA), 500L))
  right <- cmp_family_charvec(rep(c("a", "ä"), 1000L))
  expect_identical(charport::is_charvec(left), charr_altrep())
  expect_identical(charport::is_charvec(right), charr_altrep())
  opts <- list(locale = "de", strength = 1L, unknown_option = TRUE)

  capture <- function(fun) {
    warnings <- character()
    value <- withCallingHandlers(
      fun(),
      warning = function(w) {
        warnings <<- c(warnings, conditionMessage(w))
        invokeRestart("muffleWarning")
      }
    )
    list(value = value, warnings = warnings)
  }

  got <- capture(function() {
    cmp_family_equiv(left, right, opts_collator = opts)
  })
  want <- capture(function() {
    stringi::stri_cmp_equiv(
      as.character(left), as.character(right), opts_collator = opts
    )
  })
  expect_identical(got, want)
  expect_length(got$warnings, 1L)
})


test_that("off-map comparison family matches stringi on 800 seeded cases", {
  set.seed(7408)
  atoms <- c(
    "a", "A", "ä", "å", "aa", "ü", "ü", "😀", "2", "02", "10",
    "-", "", "\ufeffa"
  )
  make_strings <- function(n) {
    vapply(seq_len(n), function(i) {
      paste0(sample(atoms, sample(0:4, 1L), TRUE), collapse = "")
    }, character(1L))
  }
  coll_charr <- list(
    cmp = cmp_family_cmp,
    lt = cmp_family_lt,
    le = cmp_family_le,
    gt = cmp_family_gt,
    ge = cmp_family_ge,
    nequiv = cmp_family_nequiv
  )
  coll_stringi <- list(
    cmp = stringi::stri_cmp,
    lt = stringi::stri_cmp_lt,
    le = stringi::stri_cmp_le,
    gt = stringi::stri_cmp_gt,
    ge = stringi::stri_cmp_ge,
    nequiv = stringi::stri_cmp_nequiv
  )

  got <- want <- vector("list", 800L)
  for (case in seq_along(got)) {
    n1 <- sample(0:16, 1L)
    n2 <- sample(0:16, 1L)
    left <- make_strings(n1)
    right <- make_strings(n2)
    if (n1 > 0L && runif(1L) < 0.4)
      left[sample(n1, 1L)] <- NA
    if (n2 > 0L && runif(1L) < 0.4)
      right[sample(n2, 1L)] <- NA
    opts <- list(
      locale = sample(c("de", "da"), 1L),
      strength = sample(c(1L, 3L), 1L),
      numeric = sample(c(FALSE, TRUE), 1L)
    )
    left_cv <- cmp_family_charvec(left)
    right_cv <- cmp_family_charvec(right)
    expect_identical(charport::is_charvec(left_cv), charr_altrep())
    expect_identical(charport::is_charvec(right_cv), charr_altrep())

    got_coll <- lapply(coll_charr, function(fun) {
      suppressWarnings(fun(left_cv, right_cv, opts_collator = opts))
    })
    want_coll <- lapply(coll_stringi, function(fun) {
      suppressWarnings(fun(left, right, opts_collator = opts))
    })
    got[[case]] <- c(
      got_coll,
      list(
        eq = suppressWarnings(cmp_family_eq(left_cv, right_cv)),
        neq = suppressWarnings(cmp_family_neq(left_cv, right_cv))
      )
    )
    want[[case]] <- c(
      want_coll,
      list(
        eq = suppressWarnings(stringi::stri_cmp_eq(left, right)),
        neq = suppressWarnings(stringi::stri_cmp_neq(left, right))
      )
    )
  }
  expect_identical(got, want)
})
