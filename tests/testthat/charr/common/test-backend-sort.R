# Charr-owned tests for stringr's Reader-backed collation helpers.
# These are not imported from stringr.

sort_backend_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_sort_source_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

sort_backend_events <- function(expr) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
    }
  )
  events
}

test_that("order and rank preserve stable collation and NA placement", {
  values <- c(
    "2", "02", "10", "1", "a", "A", "\u00e4", "", NA_character_,
    NA_character_
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 1L, numeric = TRUE)

  for (decreasing in c(FALSE, TRUE)) {
    for (na_last in c(FALSE, TRUE, NA)) {
      expect_identical(
        with_test_backend(
          TRUE,
          charr_test_leaf("ci_order")(
            subject, decreasing = decreasing, na_last = na_last,
            opts_collator = opts
          )
        ),
        stringi::stri_order(
          values, decreasing = decreasing, na_last = na_last,
          opts_collator = opts
        )
      )
    }
  }

  actual_rank <- with_test_backend(
    TRUE, charr_test_leaf("ci_rank")(subject, opts_collator = opts)
  )
  expect_identical(
    actual_rank,
    stringi::stri_rank(values, opts_collator = opts)
  )
  expect_identical(actual_rank[1L], actual_rank[2L])

  all_na <- charport::as_charvec(rep(NA_character_, 3L))
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_order")(all_na, na_last = NA, opts_collator = opts)
    ),
    stringi::stri_order(
      rep(NA_character_, 3L), na_last = NA, opts_collator = opts
    )
  )
  expect_sort_source_unmaterialized(all_na)
  expect_sort_source_unmaterialized(subject)
})

test_that("duplicated preserves collation ties and missing values", {
  values <- c(
    "\u00e4", "a\u0308", "A", "a", NA_character_, "z", NA_character_,
    "Z", "\u00e4"
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 1L)

  for (from_last in c(FALSE, TRUE)) {
    expect_identical(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_duplicated")(
          subject, from_last = from_last, opts_collator = opts
        )
      ),
      stringi::stri_duplicated(
        values, from_last = from_last, opts_collator = opts
      )
    )
  }

  empty <- charport::as_charvec(character())
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_order")(empty)),
    stringi::stri_order(character())
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_rank")(empty)),
    stringi::stri_rank(character())
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(empty)),
    stringi::stri_duplicated(character())
  )
  expect_sort_source_unmaterialized(empty)
  expect_sort_source_unmaterialized(subject)
})

test_that("mapped collation helpers preserve marked and malformed input", {
  latin1 <- sort_backend_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed1 <- sort_backend_marked(c(0x61, 0xff, 0x62), "UTF-8")
  malformed2 <- sort_backend_marked(c(0x61, 0xfe, 0x62), "UTF-8")
  values <- c(
    latin1, "\ufeffabc", malformed1, malformed2, "abc", NA_character_
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "en", strength = 3L)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_order")(subject, na_last = NA, opts_collator = opts)
    ),
    stringi::stri_order(values, na_last = NA, opts_collator = opts)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_rank")(subject, opts_collator = opts)),
    stringi::stri_rank(values, opts_collator = opts)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(subject, opts_collator = opts)),
    stringi::stri_duplicated(values, opts_collator = opts)
  )
  expect_sort_source_unmaterialized(subject)
})

test_that("mapped collation helpers retain validation order", {
  bytes <- sort_backend_marked(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes)

  expected <- list(
    order = function() stringi::stri_order(bytes),
    rank = function() stringi::stri_rank(bytes),
    duplicated = function() stringi::stri_duplicated(bytes)
  )
  actual <- list(
    order = function() with_test_backend(TRUE, charr_test_leaf("ci_order")(subject)),
    rank = function() with_test_backend(TRUE, charr_test_leaf("ci_rank")(subject)),
    duplicated = function() with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(subject))
  )

  for (name in names(expected)) {
    expect_identical(
      sort_backend_events(expected[[name]]()),
      sort_backend_events(actual[[name]]()),
      info = name
    )
  }

  bad_opts <- list(not_a_collator_option = TRUE)
  expect_identical(
    sort_backend_events(
      stringi::stri_order(bytes, opts_collator = bad_opts)
    ),
    sort_backend_events(
      with_test_backend(
        TRUE, charr_test_leaf("ci_order")(subject, opts_collator = bad_opts)
      )
    )
  )
  expect_sort_source_unmaterialized(subject)
})

# Ordering, ranking and deduplication compare the first 8 bytes of each ICU
# sort key and fall back to full collation only when those bytes tie. The
# cases below sit on that boundary: with the default "en" collator the keys of
# "abc", "abcd" and "abcde" are 7, 8 and 9 bytes long, so only "abc" has a
# complete prefix, and "ab\u00e9" ties its canonical equivalent in 8 bytes.
sort_prefix_boundary_values <- function() {
  c(
    "abcde", "abcd", "abc", "abcd", "ABC", "abc", "abcde", "abcD",
    "ab\u00e9", "abe\u0301", NA_character_
  )
}

# Collation-equal neighbours of an order keep increasing source indices, and
# ranks rise exactly where neighbours differ. str_equal() collates each pair
# directly, so it checks the ties independently of the sorting code.
expect_sort_ties_consistent <- function(values, ...) {
  present <- which(!is.na(values))
  for (decreasing in c(FALSE, TRUE)) {
    order <- str_order(values, decreasing = decreasing, na_last = NA, ...)
    expect_setequal(order, present)
    if (length(order) > 1L) {
      first <- order[-length(order)]
      second <- order[-1L]
      equal <- str_equal(values[first], values[second], ...)
      expect_true(all(first[equal] < second[equal]))
    }
  }

  order <- str_order(values, na_last = NA, ...)
  rank <- str_rank(values, ...)
  expect_true(all(is.na(rank[is.na(values)])))
  if (length(order) > 1L) {
    equal <- str_equal(values[order[-length(order)]], values[order[-1L]], ...)
    steps <- diff(rank[order])
    expect_true(all(steps[equal] == 0L))
    expect_identical(
      rank[order][-1L][!equal], (seq_along(order)[-1L])[!equal]
    )
  }

  n <- length(values)
  pairs <- str_equal(rep(values, each = n), rep(values, times = n), ...)
  equal <- matrix(pairs, n, n, byrow = TRUE)
  equal[is.na(equal)] <- FALSE
  both_na <- outer(is.na(values), is.na(values), "&")
  equal <- equal | both_na
  earlier <- vapply(seq_len(n), function(i) any(equal[i, seq_len(i - 1L)]),
                    logical(1))
  later <- vapply(seq_len(n), function(i) any(equal[i, -seq_len(i)]),
                  logical(1))
  expect_identical(str_unique(values, ...), values[!earlier])
  duplicated_leaf <- charr_test_leaf("ci_duplicated")
  expect_identical(
    with_test_backend(
      TRUE,
      duplicated_leaf(values, from_last = TRUE, opts_collator = list(...))
    ),
    later
  )
}

test_that("order, rank, and unique agree across the 8-byte key boundary", {
  values <- sort_prefix_boundary_values()

  expect_identical(
    str_order(values, locale = "en"),
    c(3L, 6L, 5L, 2L, 4L, 8L, 1L, 7L, 9L, 10L, 11L)
  )
  expect_identical(
    str_order(values, decreasing = TRUE, locale = "en"),
    c(9L, 10L, 1L, 7L, 8L, 2L, 4L, 5L, 3L, 6L, 11L)
  )
  expect_identical(
    str_rank(values, locale = "en"),
    c(7L, 4L, 1L, 4L, 3L, 1L, 7L, 6L, 9L, 9L, NA)
  )
  expect_identical(
    str_unique(values, locale = "en"),
    c("abcde", "abcd", "abc", "ABC", "abcD", "ab\u00e9", NA)
  )
  expect_identical(
    str_rank(values, locale = "en", strength = 1L),
    c(7L, 4L, 1L, 4L, 1L, 1L, 7L, 4L, 9L, 9L, NA)
  )
  expect_identical(
    str_unique(values, locale = "en", strength = 2L),
    c("abcde", "abcd", "abc", "ab\u00e9", NA)
  )

  for (strength in 1:3) {
    expect_sort_ties_consistent(values, locale = "en", strength = strength)
  }
})

test_that("complete and incomplete key prefixes mix without false ties", {
  units <- c("a", "A", "b", "\u00e9", "e\u0301", "E\u0301")
  grid <- as.vector(outer(units, units, paste0))
  grid <- c(grid, as.vector(outer(grid, units[1:3], paste0)))
  set.seed(4127)
  values <- c(sample(grid, 90L, replace = TRUE), NA_character_, "", "")

  for (strength in 1:3) {
    expect_sort_ties_consistent(values, locale = "en", strength = strength)
  }
  expect_sort_ties_consistent(values, locale = "sv", strength = 2L)
})

test_that("strings sharing a long prefix order by their whole content", {
  url <- "https://example.com/some/long/path/"
  tails <- c("b", "a", "index.html", "Index.html", "e\u0301", "\u00e9", "a")
  values <- c(paste0(url, tails), url, NA_character_)

  expect_identical(
    str_order(values, locale = "en"),
    c(8L, 2L, 7L, 1L, 5L, 6L, 3L, 4L, 9L)
  )
  expect_identical(
    str_order(values, decreasing = TRUE, locale = "en"),
    c(4L, 3L, 5L, 6L, 1L, 2L, 7L, 8L, 9L)
  )
  expect_identical(
    str_unique(values, locale = "en"),
    values[c(1:5, 8:9)]
  )
  expect_sort_ties_consistent(values, locale = "en")
  expect_sort_ties_consistent(values, locale = "en", strength = 1L)

  same <- rep(paste0(url, "index.html"), 50L)
  expect_identical(str_order(same, locale = "en"), seq_along(same))
  expect_identical(
    str_order(same, decreasing = TRUE, locale = "en"), seq_along(same)
  )
  expect_identical(str_rank(same, locale = "en"), rep(1L, 50L))
  expect_identical(str_unique(same, locale = "en"), same[1L])
})

test_that("deduplication survives strings that differ only mid-string", {
  # Equal length, key prefix, middle and tail: every such string lands on
  # the same deduplication hash and must still be told apart by collation.
  base <- strrep("x", 60L)
  variants <- c(letters, LETTERS, "\u00e9", "e\u0301")
  values <- vapply(variants, function(ch) {
    paste0(substr(base, 1L, 40L), ch, substr(base, 42L, 60L))
  }, "", USE.NAMES = FALSE)
  set.seed(5501)
  values <- sample(rep(values, 3L))

  for (strength in 1:3) {
    expect_sort_ties_consistent(values, locale = "en", strength = strength)
  }
  expect_length(str_unique(values, locale = "en", strength = 1L), 26L)
  expect_length(str_unique(values, locale = "en"), 53L)
})

test_that("the case level orders by collation, not by ICU sort keys", {
  # With the case level on and uppercase first, ICU's sort keys order
  # ".../index.html" before ".../Index.html" while collation puts it after,
  # so no key prefix may be used; every comparison must collate.
  url <- "https://example.com/some/long/path/"
  values <- c(
    paste0(url, c(
      "index.html", "Index.html", "index.html", "INDEX.html", "Index.html",
      "index.htm", "\u00cdndex.html"
    )),
    NA_character_, "Index.html", "index.html"
  )
  opts <- list(
    locale = "en", strength = 1L, case_level = TRUE, uppercase_first = TRUE
  )
  with_opts <- function(fun, ...) do.call(fun, c(list(...), opts))

  expect_identical(
    with_opts(str_order, paste0(url, c("index.html", "Index.html"))),
    c(2L, 1L)
  )
  expect_identical(
    with_opts(str_order, values), c(6L, 4L, 2L, 5L, 7L, 1L, 3L, 9L, 10L, 8L)
  )
  expect_identical(
    with_opts(str_order, values, decreasing = TRUE),
    c(10L, 9L, 1L, 3L, 2L, 5L, 7L, 4L, 6L, 8L)
  )
  expect_identical(
    with_opts(str_rank, values), c(6L, 3L, 6L, 2L, 3L, 1L, 3L, NA, 8L, 9L)
  )
  expect_identical(with_opts(str_unique, values), values[c(1:2, 4L, 6L, 8:10)])

  duplicated_leaf <- charr_test_leaf("ci_duplicated")
  expect_identical(
    with_test_backend(
      TRUE, duplicated_leaf(values, from_last = FALSE, opts_collator = opts)
    ),
    c(FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE)
  )
  expect_identical(
    with_test_backend(
      TRUE, duplicated_leaf(values, from_last = TRUE, opts_collator = opts)
    ),
    c(TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE)
  )
  do.call(expect_sort_ties_consistent, c(list(values), opts))
})

test_that("threaded prefix computation matches the serial result", {
  original_threads <- charr_threads()
  original_min_chunk <- charr_min_chunk()
  on.exit(charr_threads(original_threads), add = TRUE)
  on.exit(charr_min_chunk(original_min_chunk), add = TRUE)
  charr_min_chunk(1L)

  set.seed(9321)
  url <- "https://example.com/some/long/path/"
  values <- c(
    rep(sort_prefix_boundary_values(), 20L),
    paste0(url, sample(c("a", "A", "b", "\u00e9", "e\u0301"), 100L, TRUE)),
    rep(c("b", "A", "a", "B", "z"), 40L)
  )

  results <- lapply(c(1L, charr_test_threads()), function(threads) {
    charr_threads(threads)
    list(
      order = str_order(values, locale = "en", strength = 2L),
      decreasing = str_order(
        values, decreasing = TRUE, na_last = FALSE, locale = "en"
      ),
      sort = str_sort(values, locale = "en"),
      rank = str_rank(values, locale = "en", strength = 1L),
      unique = str_unique(values, locale = "en", strength = 1L),
      duplicated = with_test_backend(
        TRUE,
        charr_test_leaf("ci_duplicated")(
          values, from_last = TRUE, opts_collator = list(locale = "en")
        )
      )
    )
  })

  expect_identical(results[[1L]], results[[2L]])
  expect_identical(results[[1L]]$order[1:4], c(322L, 323L, 327L, 328L))
})
