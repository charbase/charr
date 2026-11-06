# Short ALTREP-backend results are charpacks, longer ones charvecs. These
# checks run before anything reads a result from R, because an element read
# materializes a whole charpack (by design), and compare against the base
# backend, which returns ordinary character vectors.

result_class <- function(x) {
  if (charport::is_charpack(x)) "charpack"
  else if (charport::is_charvec(x)) "charvec"
  else "plain"
}

# A fresh result: its class, and it is still unmaterialized.
fresh <- function(x) {
  list(class = result_class(x),
       materialized = charport::charport_info(x)$is_materialized)
}

expect_fresh_charpack <- function(x) {
  expect_identical(fresh(x), list(class = "charpack", materialized = FALSE))
}

same_text <- function(actual, expected) {
  identical(as.character(actual), as.character(expected)) &&
    identical(Encoding(as.character(actual)), Encoding(as.character(expected)))
}

test_that("each ALTREP output path returns a fresh charpack for short results", {
  skip_if_not(charr_altrep())
  x <- c("alpha beta", "café", NA, "", "gamma")

  # OutputBuilder (serial) and ParallelOutputBuilder (threaded)
  for (threads in c(1L, 2L)) {
    old <- charr_threads(threads)
    expect_fresh_charpack(str_to_upper(x))
    expect_fresh_charpack(str_replace_na(x))       # direct charport Builder
    charr_threads(old)
  }
  # finalize() on one store per row
  rows <- str_split(x, " ")
  for (row in rows) expect_fresh_charpack(row)
  # GrowableOutputBuilder
  expect_fresh_charpack(str_wrap("one two three four", width = 5))
  for (row in str_extract_all(x, boundary("word"))) expect_fresh_charpack(row)
  # Scalar results
  expect_fresh_charpack(str_flatten(c("a", "b")))
  info <- get("ci_enc_info", envir = asNamespace("charr"))("UTF-8")
  characters <- Filter(is.character, info)
  expect_true(length(characters) > 0L)
  for (value in characters) expect_identical(result_class(value), "charpack")
})

test_that("the limits pick charpack or charvec at their boundaries", {
  skip_if_not(charr_altrep())
  expect_identical(result_class(str_to_upper(rep("a", 64))), "charpack")
  expect_identical(result_class(str_to_upper(rep("a", 65))), "charvec")
  expect_identical(result_class(str_to_upper(character(0))), "charpack")
  expect_identical(result_class(str_to_upper(strrep("a", 65536))), "charpack")
  expect_identical(result_class(str_to_upper(strrep("a", 65537))), "charvec")
  # NA strings count as no bytes
  expect_identical(
    result_class(str_to_upper(c(strrep("a", 65536), NA))), "charpack"
  )

})

test_that("charpack results match the base backend's values and marks", {
  skip_if_not(charr_altrep())
  latin1 <- "fa\xe7ade"
  Encoding(latin1) <- "latin1"
  x <- c("Ab c", "café ü", NA, "", latin1, "plain")
  cases <- list(
    upper = function(v) str_to_upper(v),
    split = function(v) str_split(v, " "),
    sub = function(v) str_sub(v, 2, 3),
    replace = function(v) str_replace_all(v, "[aé]", "x"),
    na = function(v) str_replace_na(v),
    trim = function(v) str_trim(v),
    flatten = function(v) str_flatten(v, "-", na.rm = TRUE),
    # UTF-8 input only: reading the Latin-1 sample as UTF-8 warns, rightly
    conv = function(v) str_conv(c("abc", "café", NA), "UTF-8")
  )
  for (name in names(cases)) {
    altrep <- cases[[name]](x)
    base <- with_backend("base", cases[[name]](x))
    if (is.list(altrep)) {
      expect_identical(length(altrep), length(base), label = name)
      for (i in seq_along(altrep)) {
        expect_true(same_text(altrep[[i]], base[[i]]), label = name)
      }
    } else {
      expect_true(same_text(altrep, base), label = name)
    }
  }
})

test_that("a charpack result chains, serializes and reads without surprises", {
  skip_if_not(charr_altrep())
  x <- str_to_upper(c("ab", NA, "café", ""))
  # charr reads it through the Reader without materializing it
  y <- str_to_lower(x)
  expect_fresh_charpack(x)
  expect_fresh_charpack(y)
  # serialization keeps it packed
  z <- unserialize(serialize(x, NULL))
  expect_identical(result_class(z), "charpack")
  expect_true(same_text(z, c("AB", NA, "CAFÉ", "")))
  # duplicating and modifying leaves the original untouched
  w <- x
  w[1] <- "changed"
  expect_identical(w[[1]], "changed")
  expect_identical(as.character(x), c("AB", NA, "CAFÉ", ""))
})

test_that("operations that subset or test for NA in R keep charpack input packed", {
  skip_if_not(charr_altrep())
  values <- c("delta", NA, "alpha", "", "alpha", "charlie")
  packed <- function() charport::as_charpack(values)
  cases <- list(
    trunc_none = function(v) str_trunc(v, 20),
    trunc_some = function(v) str_trunc(v, 4),
    trunc_left = function(v) str_trunc(v, 4, side = "left"),
    flatten_na_rm = function(v) str_flatten(v, ",", na.rm = TRUE),
    sort = function(v) str_sort(v),
    unique = function(v) str_unique(v),
    subset = function(v) str_subset(v, "a")
  )
  for (name in names(cases)) {
    x <- packed()
    result <- cases[[name]](x)
    expect_false(charport::charport_info(x)$is_materialized, label = name)
    base <- with_backend("base", cases[[name]](values))
    expect_true(same_text(result, base), label = name)
  }
  # these come straight from a packed subset, unread
  for (op in list(str_sort, str_unique, function(v) str_subset(v, "a"))) {
    expect_fresh_charpack(op(packed()))
  }
  expect_fresh_charpack(str_trunc(packed(), 20))
})

test_that("short results survive gctorture", {
  skip_if_not(charr_altrep())
  skip_on_cran()
  gctorture(TRUE)
  rows <- str_split(c("a b", "c d e", NA), " ")
  upper <- str_to_upper(c("x", NA, "é"))
  gctorture(FALSE)
  expect_identical(rows, list(c("a", "b"), c("c", "d", "e"), NA_character_))
  expect_identical(upper, c("X", NA, "É"))
})

# Most tests produce short results, so these few keep the charvec path covered.
# A charvec keeps the kernel's store and reads it later, unlike a charpack,
# which copies it at once; reading after gc() catches records that point into
# memory the store does not own.
test_that("results above the limits are charvecs that chain and stay valid", {
  skip_if_not(charr_altrep())
  x <- rep(c("alpha beta", "caf\u00e9", NA, "", "gamma"), 20)  # 100 strings

  # OutputBuilder (serial) and ParallelOutputBuilder (threaded)
  for (threads in c(1L, charr_test_threads())) {
    old <- charr_threads(threads)
    upper <- str_to_upper(x)
    lower <- str_to_lower(upper)
    expect_identical(fresh(upper), list(class = "charvec", materialized = FALSE))
    expect_identical(fresh(lower), list(class = "charvec", materialized = FALSE))
    gc()
    expect_true(same_text(lower, with_backend("base", str_to_lower(str_to_upper(x)))))
    charr_threads(old)
  }
  # Direct charport Builder
  replaced <- str_replace_na(x)
  expect_identical(result_class(replaced), "charvec")
  gc()
  expect_true(same_text(replaced, with_backend("base", str_replace_na(x))))
  # One store per row: a long row is a charvec, a short one a charpack
  rows_input <- c(str_flatten(rep(c("w", "caf\u00e9"), 40), " "), "a b")
  rows <- str_split(rows_input, " ")
  expect_identical(vapply(rows, result_class, ""), c("charvec", "charpack"))
  counted <- str_count(rows[[1]], "\u00e9")
  gc()
  expect_identical(counted, with_backend("base", str_count(str_split(rows_input, " ")[[1]], "\u00e9")))
  expect_true(same_text(rows[[1]], with_backend("base", str_split(rows_input, " ")[[1]])))
  # GrowableOutputBuilder, and a scalar above the byte limit
  words <- str_extract_all(str_flatten(rep("word", 100), " "), boundary("word"))[[1]]
  expect_identical(result_class(words), "charvec")
  long <- str_flatten(rep("abcdefgh", 9000))
  expect_identical(result_class(long), "charvec")
  gc()
  expect_identical(as.character(words), rep("word", 100))
  expect_identical(nchar(long), 72000L)
})

test_that("trunc and flatten find missing values as stringr does", {
  # stringr tests is.na() before coercion: a numeric NaN and a list's NA are
  # missing there, although coercion turns them into "NaN" and "NA".
  inputs <- list(
    numeric = c(1, NaN, NA, 22),
    list = list("a", NA, "bcd"),
    factor = factor(c("abc", NA, "d")),
    character = c("abc", NA, "", "d")
  )
  cases <- list(
    trunc = function(v) suppressWarnings(str_trunc(v, 1, ellipsis = "")),
    flatten = function(v) str_flatten(v, ",", na.rm = TRUE)
  )
  for (case in names(cases)) {
    for (input in names(inputs)) {
      expect_identical(
        as.list(cases[[case]](inputs[[input]])),
        as.list(with_backend("reference", cases[[case]](inputs[[input]]))),
        label = paste(case, input)
      )
    }
  }
})
