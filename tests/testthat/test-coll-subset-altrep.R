# ci_subset_coll and its replacement form are off the stringr dispatch map.
# Validate the Reader backend directly against installed stringi on both routes.

subset_coll <- function(...) charr:::ci_subset_coll(...)
`subset_coll<-` <- function(...) charr:::`ci_subset_coll<-`(...)
subset_coll_charvec <- function(x) charr:::ci_trim_both(x)


test_that("coll subset preserves locale, omit-NA, and negate selection", {
  strings <- subset_coll_charvec(c(
    "😀ä-a-A", "üxÜ", "å-aa-Å", "none", "", NA
  ))
  patterns <- subset_coll_charvec(c("a", "ü", "aa", "a", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  selected <- subset_coll(strings, patterns, opts_collator = opts)
  expect_identical(selected, c("😀ä-a-A", "üxÜ", "å-aa-Å", NA))
  expect_true(charport::is_charvec(selected))
  expect_identical(
    subset_coll(
      strings, patterns, omit_na = TRUE, opts_collator = opts
    ),
    c("😀ä-a-A", "üxÜ", "å-aa-Å")
  )
  expect_identical(
    subset_coll(strings, patterns, negate = TRUE, opts_collator = opts),
    c("none", "", NA)
  )
  expect_identical(
    subset_coll(
      strings, patterns, omit_na = TRUE, negate = TRUE,
      opts_collator = opts
    ),
    c("none", "")
  )
})


test_that("coll subset preserves Danish tailoring and strength", {
  danish <- subset_coll_charvec(c("Aarhus", "Århus", "blaa", "blå"))
  danish_patterns <- subset_coll_charvec(c("Å", "aa"))
  expect_identical(charport::is_charvec(danish), charr_altrep())
  expect_identical(charport::is_charvec(danish_patterns), charr_altrep())
  expect_identical(
    subset_coll(
      danish, danish_patterns,
      opts_collator = list(locale = "da", strength = 1L)
    ),
    c("Aarhus", "Århus", "blaa", "blå")
  )

  german <- subset_coll_charvec(c("äaA", "AAA"))
  pattern <- subset_coll_charvec("a")
  expect_identical(
    subset_coll(
      german, pattern,
      opts_collator = list(locale = "de", strength = 1L)
    ),
    c("äaA", "AAA")
  )
  expect_identical(
    subset_coll(
      german, pattern,
      opts_collator = list(locale = "de", strength = 3L)
    ),
    "äaA"
  )
  expect_error(
    subset_coll(
      german, pattern, opts_collator = list(numeric = TRUE)
    ),
    "U_UNSUPPORTED_ERROR"
  )
})


test_that("coll subset replacement preserves indexing semantics", {
  strings <- subset_coll_charvec(c(
    "😀ä-a-A", "üxÜ", "å-aa-Å", "none", "", NA
  ))
  patterns <- subset_coll_charvec(c("a", "ü", "aa", "a", "x", "a"))
  values <- subset_coll_charvec(c("X", "Y"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  expect_identical(charport::is_charvec(values), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  replaced <- strings
  suppressWarnings(
    subset_coll(replaced, patterns, opts_collator = opts) <- values
  )
  expect_identical(as.character(replaced), c("X", "Y", "X", "none", "", NA))
  expect_true(charport::is_charvec(replaced))

  negated <- strings
  subset_coll(
    negated, patterns, negate = TRUE, opts_collator = opts
  ) <- values
  expect_identical(
    as.character(negated),
    c("😀ä-a-A", "üxÜ", "å-aa-Å", "X", "Y", NA)
  )

  na_pattern <- subset_coll_charvec(c(NA, "a"))
  na_value <- subset_coll_charvec(NA_character_)
  preserved <- strings
  subset_coll(preserved, na_pattern, opts_collator = opts) <- na_value
  expect_identical(
    as.character(preserved),
    c("😀ä-a-A", "üxÜ", "å-aa-Å", "none", "", NA)
  )
})


test_that("coll subset retains subject BOM but strips replacement BOM", {
  make_charvec <- function(x) {
    charr:::ci_replace_all_coll(x, "never-present", "x")
  }
  strings <- make_charvec(c("\ufeffapple", "apple"))
  miss <- make_charvec("z")
  hit <- make_charvec("a")
  value <- make_charvec("\ufeffVALUE")
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(miss), charr_altrep())
  expect_identical(charport::is_charvec(hit), charr_altrep())
  expect_identical(charport::is_charvec(value), charr_altrep())

  kept <- strings
  subset_coll(kept, miss) <- value
  expect_identical(
    lapply(as.character(kept), charToRaw),
    lapply(c("\ufeffapple", "apple"), charToRaw)
  )

  replaced <- strings
  subset_coll(replaced, hit) <- value
  expect_identical(as.character(replaced), c("VALUE", "VALUE"))
})


test_that("coll subset empty patterns warn with copied NA behavior", {
  strings <- subset_coll_charvec(c("apple", "", NA))
  empty <- subset_coll_charvec("")
  value <- subset_coll_charvec("X")
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(empty), charr_altrep())
  expect_identical(charport::is_charvec(value), charr_altrep())

  expect_warning(
    selected <- subset_coll(strings, empty),
    "empty search patterns are not supported"
  )
  expect_identical(selected, rep(NA_character_, 3L))
  expect_warning(
    omitted <- subset_coll(strings, empty, omit_na = TRUE),
    "empty search patterns are not supported"
  )
  expect_identical(omitted, character())

  replaced <- strings
  expect_warning(
    subset_coll(replaced, empty) <- value,
    "empty search patterns are not supported"
  )
  expect_identical(as.character(replaced), rep(NA_character_, 3L))
})


test_that("coll subset pair matches stringi on 600 seeded cases", {
  set.seed(7206)
  atoms <- c("a", "A", "ä", "å", "aa", "ü", "ü", "😀", "-", "")
  make_strings <- function(n) {
    vapply(seq_len(n), function(i) {
      paste0(sample(atoms, sample(0:6, 1L), TRUE), collapse = "")
    }, character(1L))
  }

  got <- want <- got_replacement <- want_replacement <- vector("list", 600L)
  for (case in seq_along(got)) {
    ns <- sample(1:15, 1L)
    np <- sample(1:ns, 1L)
    nv <- sample(1:6, 1L)
    strings <- make_strings(ns)
    patterns <- make_strings(np)
    patterns[patterns == ""] <- "a"
    values <- make_strings(nv)
    if (runif(1L) < 0.3)
      strings[sample(ns, 1L)] <- NA
    if (runif(1L) < 0.2)
      patterns[sample(np, 1L)] <- NA
    if (runif(1L) < 0.2)
      values[sample(nv, 1L)] <- NA
    opts <- list(
      locale = sample(c("de", "da"), 1L),
      strength = sample(c(1L, 3L), 1L)
    )
    omit_na <- sample(c(FALSE, TRUE), 1L)
    negate <- sample(c(FALSE, TRUE), 1L)

    strings_cv <- subset_coll_charvec(strings)
    patterns_cv <- subset_coll_charvec(patterns)
    values_cv <- subset_coll_charvec(values)
    got[[case]] <- suppressWarnings(as.character(subset_coll(
      strings_cv, patterns_cv, omit_na = omit_na, negate = negate,
      opts_collator = opts
    )))
    want[[case]] <- suppressWarnings(stringi::stri_subset_coll(
      strings, patterns, omit_na = omit_na, negate = negate,
      opts_collator = opts
    ))

    current <- strings_cv
    suppressWarnings(
      subset_coll(
        current, patterns_cv, negate = negate, opts_collator = opts
      ) <- values_cv
    )
    got_replacement[[case]] <- as.character(current)
    current <- strings
    suppressWarnings(
      stringi::stri_subset_coll(
        current, patterns, negate = negate, opts_collator = opts
      ) <- values
    )
    want_replacement[[case]] <- current
  }
  expect_identical(got, want)
  expect_identical(got_replacement, want_replacement)
})
