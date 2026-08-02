# charr-owned targeted equivalence tests for Reader/Builder coll replace.

replace_all_coll <- function(...) charr_test_leaf("ci_replace_all_coll")(...)
replace_first_coll <- function(...) charr_test_leaf("ci_replace_first_coll")(...)
replace_coll_input <- function(x) charr_test_leaf("ci_trim_both")(x)


test_that("coll replace splices literal UTF-16 ranges", {
  strings <- replace_coll_input(c(
    "😀ä-a-A", "üxÜ", "å-aa-Å", "none", "", NA
  ))
  patterns <- replace_coll_input(c("a", "ü", "aa", "a", "x", "a"))
  replacements <- replace_coll_input(c("$1", "Z", NA))
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
  expect_identical(
    all, c("😀$1-$1-$1", "ZxZ", NA, "none", "", NA)
  )
  expect_identical(
    first, c("😀$1-a-A", "ZxÜ", NA, "none", "", NA)
  )
  expect_identical(charport::is_charvec(all), charr_altrep())
  expect_identical(charport::is_charvec(first), charr_altrep())
  expect_identical(
    Encoding(all),
    c("UTF-8", "unknown", "unknown", "unknown", "unknown", "unknown")
  )
})


test_that("coll replace preserves locale tailoring and strength", {
  danish <- replace_coll_input(c("Aarhus", "Århus", "blaa", "blå"))
  danish_patterns <- replace_coll_input(c("Å", "aa"))
  replacement <- replace_coll_input("X")
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

  german <- replace_coll_input("äaA")
  german_pattern <- replace_coll_input("a")
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
  strings <- replace_coll_input(c("hit", "none"))
  hit_patterns <- replace_coll_input(c("h", "i"))
  miss_patterns <- replace_coll_input(c("z", "i"))
  replacements <- replace_coll_input(c(NA, "X"))
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

  empty_pattern <- replace_coll_input("")
  replacement <- replace_coll_input("X")
  expect_warning(
    vectorized <- replace_all_coll(strings, empty_pattern, replacement),
    "empty search patterns are not supported"
  )
  expect_identical(vectorized, c(NA_character_, NA_character_))

  sequential_patterns <- replace_coll_input(c("z", ""))
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
  strings <- replace_coll_input(c(
    "ä-a-A", "ü-Ü", "å-aa", "none", "", NA
  ))
  patterns <- replace_coll_input(c("ä", "a", "Ü"))
  replacements <- replace_coll_input(c("X", "Y", "Z"))
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
