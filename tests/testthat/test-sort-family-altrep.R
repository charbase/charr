# charr-owned Reader/Builder equivalence tests for the ICU sort family.
# Several functions are off the stringr dispatch map, so exercise the copied
# backend directly and compare it with installed stringi.

sort_family_charvec <- function(x) charr:::ci_trim_both(x)
sort_family_sort <- function(...) charr:::ci_sort(...)
sort_family_order <- function(...) charr:::ci_order(...)
sort_family_rank <- function(...) charr:::ci_rank(...)
sort_family_unique <- function(...) charr:::ci_unique(...)
sort_family_duplicated <- function(...) charr:::ci_duplicated(...)
sort_family_duplicated_any <- function(...) charr:::ci_duplicated_any(...)
sort_family_key <- function(...) charr:::ci_sort_key(...)


test_that("sort family preserves numeric order, NA placement, and stable ties", {
  strings <- sort_family_charvec(c(
    "2", "10", "1", "02", "a", "A", "ä", "😀", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  opts <- list(locale = "de", strength = 1L, numeric = TRUE)
  expect_identical(
    sort_family_sort(strings, na_last = TRUE, opts_collator = opts),
    stringi::stri_sort(
      as.character(strings), na_last = TRUE, opts_collator = opts
    )
  )
  expect_identical(
    sort_family_sort(
      strings, decreasing = TRUE, na_last = FALSE,
      opts_collator = opts
    ),
    stringi::stri_sort(
      as.character(strings), decreasing = TRUE, na_last = FALSE,
      opts_collator = opts
    )
  )
  expect_identical(
    sort_family_order(strings, na_last = NA, opts_collator = opts),
    stringi::stri_order(
      as.character(strings), na_last = NA, opts_collator = opts
    )
  )
  expect_identical(
    sort_family_rank(strings, opts_collator = opts),
    stringi::stri_rank(as.character(strings), opts_collator = opts)
  )

  sorted <- sort_family_sort(strings, opts_collator = opts)
  expect_true(charport::is_charvec(sorted))
  # "2" and "02" are numeric-collation ties and stable_sort retains order.
  expect_lt(match("2", sorted), match("02", sorted))
})


test_that("sort family preserves locale tailoring and canonical equivalence", {
  strings <- sort_family_charvec(c(
    "Aarhus", "Århus", "blaa", "blå", "ä", "a", "A", "ü", "ü",
    "😀", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  for (opts in list(
    list(locale = "da", strength = 1L),
    list(locale = "da", strength = 3L),
    list(locale = "de", strength = 1L),
    list(locale = "de", strength = 3L)
  )) {
    raw <- as.character(strings)
    expect_identical(
      sort_family_sort(strings, na_last = TRUE, opts_collator = opts),
      stringi::stri_sort(raw, na_last = TRUE, opts_collator = opts)
    )
    expect_identical(
      sort_family_order(strings, opts_collator = opts),
      stringi::stri_order(raw, opts_collator = opts)
    )
    expect_identical(
      sort_family_rank(strings, opts_collator = opts),
      stringi::stri_rank(raw, opts_collator = opts)
    )
    expect_identical(
      sort_family_unique(strings, opts_collator = opts),
      stringi::stri_unique(raw, opts_collator = opts)
    )
    expect_identical(
      sort_family_duplicated(strings, opts_collator = opts),
      stringi::stri_duplicated(raw, opts_collator = opts)
    )
    expect_identical(
      sort_family_duplicated(
        strings, from_last = TRUE, opts_collator = opts
      ),
      stringi::stri_duplicated(
        raw, from_last = TRUE, opts_collator = opts
      )
    )
    expect_identical(
      sort_family_duplicated_any(strings, opts_collator = opts),
      stringi::stri_duplicated_any(raw, opts_collator = opts)
    )
  }

  expect_true(charport::is_charvec(sort_family_unique(strings)))
})


test_that("sort keys retain bytes encoding and UTF-16 input semantics", {
  strings <- sort_family_charvec(c(
    "\ufeffb", "b", "ä", "ü", "ü", "😀", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  opts <- list(locale = "de", strength = 3L, numeric = TRUE)

  got <- sort_family_key(strings, opts_collator = opts)
  want <- stringi::stri_sort_key(
    as.character(strings), opts_collator = opts
  )
  expect_identical(got, want)
  expect_identical(Encoding(got), Encoding(want))
  expect_true(charport::is_charvec(got))
  expect_identical(
    lapply(got, function(x) if (is.na(x)) NULL else charToRaw(x)),
    lapply(want, function(x) if (is.na(x)) NULL else charToRaw(x))
  )

  # The copied UTF-8 acquisition used by sort/unique strips a leading BOM;
  # sort_key deliberately uses the copied UTF-16 acquisition instead.
  expect_identical(
    sort_family_sort(strings),
    stringi::stri_sort(as.character(strings))
  )
  expect_identical(
    sort_family_unique(strings),
    stringi::stri_unique(as.character(strings))
  )
})


test_that("off-map sort family matches stringi on 600 seeded cases", {
  set.seed(7307)
  atoms <- c(
    "a", "A", "ä", "å", "aa", "ü", "ü", "😀", "2", "02", "10",
    "-", ""
  )
  make_strings <- function(n) {
    vapply(seq_len(n), function(i) {
      paste0(sample(atoms, sample(0:5, 1L), TRUE), collapse = "")
    }, character(1L))
  }

  got_sort <- want_sort <- got_unique <- want_unique <- vector("list", 600L)
  got_any <- want_any <- got_key <- want_key <- vector("list", 600L)
  for (case in seq_along(got_sort)) {
    n <- sample(0:18, 1L)
    strings <- make_strings(n)
    if (n > 0L && runif(1L) < 0.45)
      strings[sample(n, sample.int(min(3L, n), 1L))] <- NA
    opts <- list(
      locale = sample(c("de", "da"), 1L),
      strength = sample(c(1L, 3L), 1L),
      numeric = sample(c(FALSE, TRUE), 1L)
    )
    decreasing <- sample(c(FALSE, TRUE), 1L)
    na_last <- sample(c(FALSE, TRUE, NA), 1L)
    from_last <- sample(c(FALSE, TRUE), 1L)
    strings_cv <- sort_family_charvec(strings)
    expect_identical(charport::is_charvec(strings_cv), charr_altrep())

    got_sort[[case]] <- sort_family_sort(
      strings_cv, decreasing = decreasing, na_last = na_last,
      opts_collator = opts
    )
    want_sort[[case]] <- stringi::stri_sort(
      strings, decreasing = decreasing, na_last = na_last,
      opts_collator = opts
    )
    got_unique[[case]] <- sort_family_unique(
      strings_cv, opts_collator = opts
    )
    want_unique[[case]] <- stringi::stri_unique(
      strings, opts_collator = opts
    )
    got_any[[case]] <- sort_family_duplicated_any(
      strings_cv, from_last = from_last, opts_collator = opts
    )
    want_any[[case]] <- stringi::stri_duplicated_any(
      strings, from_last = from_last, opts_collator = opts
    )
    got_key[[case]] <- sort_family_key(strings_cv, opts_collator = opts)
    want_key[[case]] <- stringi::stri_sort_key(
      strings, opts_collator = opts
    )
  }
  expect_identical(got_sort, want_sort)
  expect_identical(got_unique, want_unique)
  expect_identical(got_any, want_any)
  expect_identical(got_key, want_key)
})
