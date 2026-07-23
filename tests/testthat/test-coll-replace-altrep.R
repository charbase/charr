# charr-owned targeted equivalence tests for Reader/Builder coll replace.

replace_all_coll <- function(...) charr:::ci_replace_all_coll(...)
replace_first_coll <- function(...) charr:::ci_replace_first_coll(...)
replace_last_coll <- function(...) charr:::ci_replace_last_coll(...)
replace_coll_charvec <- function(x) charr:::ci_trim_both(x)


test_that("coll replace splices literal UTF-16 ranges", {
  strings <- replace_coll_charvec(c(
    "😀ä-a-A", "üxÜ", "å-aa-Å", "none", "", NA
  ))
  patterns <- replace_coll_charvec(c("a", "ü", "aa", "a", "x", "a"))
  replacements <- replace_coll_charvec(c("$1", "Z", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  expect_identical(charport::is_charvec(replacements), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  all <- replace_all_coll(
    strings, patterns, replacements, opts_collator = opts
  )
  first <- replace_first_coll(
    strings, patterns, replacements, opts_collator = opts
  )
  last <- replace_last_coll(
    strings, patterns, replacements, opts_collator = opts
  )
  expect_identical(
    all, c("😀$1-$1-$1", "ZxZ", NA, "none", "", NA)
  )
  expect_identical(
    first, c("😀$1-a-A", "ZxÜ", NA, "none", "", NA)
  )
  expect_identical(
    last, c("😀ä-a-$1", "üxZ", NA, "none", "", NA)
  )
  expect_identical(charport::is_charvec(all), charr_altrep())
  expect_identical(charport::is_charvec(first), charr_altrep())
  # replace_last_coll is off the dispatch map and always runs charr.
  expect_true(charport::is_charvec(last))
  expect_identical(
    Encoding(all),
    c("UTF-8", "unknown", "unknown", "unknown", "unknown", "unknown")
  )
})


test_that("coll replace preserves locale tailoring and strength", {
  danish <- replace_coll_charvec(c("Aarhus", "Århus", "blaa", "blå"))
  danish_patterns <- replace_coll_charvec(c("Å", "aa"))
  replacement <- replace_coll_charvec("X")
  expect_identical(charport::is_charvec(danish), charr_altrep())
  expect_identical(charport::is_charvec(danish_patterns), charr_altrep())
  expect_identical(charport::is_charvec(replacement), charr_altrep())
  expect_identical(
    replace_all_coll(
      danish, danish_patterns, replacement,
      opts_collator = list(locale = "da", strength = 1L)
    ),
    c("Xrhus", "Xrhus", "blX", "blX")
  )

  german <- replace_coll_charvec("äaA")
  german_pattern <- replace_coll_charvec("a")
  expect_identical(
    replace_all_coll(
      german, german_pattern, replacement,
      opts_collator = list(locale = "de", strength = 1L)
    ),
    "XXX"
  )
  expect_identical(
    replace_all_coll(
      german, german_pattern, replacement,
      opts_collator = list(locale = "de", strength = 3L)
    ),
    "äXA"
  )
  expect_error(
    replace_all_coll(
      german, german_pattern, replacement,
      opts_collator = list(numeric = TRUE)
    ),
    "U_UNSUPPORTED_ERROR"
  )
})


test_that("coll replace preserves NA and empty-pattern timing", {
  strings <- replace_coll_charvec(c("hit", "none"))
  hit_patterns <- replace_coll_charvec(c("h", "i"))
  miss_patterns <- replace_coll_charvec(c("z", "i"))
  replacements <- replace_coll_charvec(c(NA, "X"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(hit_patterns), charr_altrep())
  expect_identical(charport::is_charvec(miss_patterns), charr_altrep())
  expect_identical(charport::is_charvec(replacements), charr_altrep())

  expect_identical(
    replace_all_coll(
      strings, hit_patterns, replacements, vectorize_all = FALSE
    ),
    c(NA, "none")
  )
  expect_identical(
    replace_all_coll(
      strings, miss_patterns, replacements, vectorize_all = FALSE
    ),
    c("hXt", "none")
  )

  empty_pattern <- replace_coll_charvec("")
  replacement <- replace_coll_charvec("X")
  expect_warning(
    vectorized <- replace_all_coll(strings, empty_pattern, replacement),
    "empty search patterns are not supported"
  )
  expect_identical(vectorized, c(NA_character_, NA_character_))

  sequential_patterns <- replace_coll_charvec(c("z", ""))
  warnings <- character()
  sequential <- withCallingHandlers(
    replace_all_coll(
      strings, sequential_patterns, replacement, vectorize_all = FALSE
    ),
    warning = function(w) {
      warnings <<- c(warnings, conditionMessage(w))
      invokeRestart("muffleWarning")
    }
  )
  expect_identical(sequential, c(NA_character_, NA_character_))
  expect_identical(
    warnings,
    rep("empty search patterns are not supported", 2L)
  )
})


test_that("coll replace sequential mode applies patterns in order", {
  strings <- replace_coll_charvec(c(
    "ä-a-A", "ü-Ü", "å-aa", "none", "", NA
  ))
  patterns <- replace_coll_charvec(c("ä", "a", "Ü"))
  replacements <- replace_coll_charvec(c("X", "Y", "Z"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  expect_identical(charport::is_charvec(replacements), charr_altrep())

  result <- replace_all_coll(
    strings, patterns, replacements, vectorize_all = FALSE,
    opts_collator = list(locale = "de", strength = 3L)
  )
  expect_identical(result, c("X-Y-A", "ü-Z", "å-YY", "none", "", NA))
  expect_identical(charport::is_charvec(result), charr_altrep())
})


test_that("off-map coll replace last matches stringi randomized", {
  set.seed(7105)
  atoms <- c("a", "A", "ä", "å", "aa", "ü", "ü", "😀", "-", "")
  make_strings <- function(n) {
    vapply(seq_len(n), function(i) {
      paste0(sample(atoms, sample(0:6, 1L), TRUE), collapse = "")
    }, character(1L))
  }

  got <- want <- vector("list", 300L)
  for (case in seq_along(got)) {
    ns <- sample(1:12, 1L)
    np <- sample(1:6, 1L)
    nr <- sample(1:6, 1L)
    strings <- make_strings(ns)
    patterns <- make_strings(np)
    patterns[patterns == ""] <- "a"
    replacements <- make_strings(nr)
    if (runif(1L) < 0.3)
      strings[sample(ns, 1L)] <- NA
    if (runif(1L) < 0.2)
      patterns[sample(np, 1L)] <- NA
    if (runif(1L) < 0.2)
      replacements[sample(nr, 1L)] <- NA
    opts <- list(
      locale = sample(c("de", "da"), 1L),
      strength = sample(c(1L, 3L), 1L)
    )

    strings_cv <- replace_coll_charvec(strings)
    patterns_cv <- replace_coll_charvec(patterns)
    replacements_cv <- replace_coll_charvec(replacements)
    got[[case]] <- suppressWarnings(as.character(replace_last_coll(
      strings_cv, patterns_cv, replacements_cv, opts_collator = opts
    )))
    want[[case]] <- suppressWarnings(stringi::stri_replace_last_coll(
      strings, patterns, replacements, opts_collator = opts
    ))
  }
  expect_identical(got, want)
})
