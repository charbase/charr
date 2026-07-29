# charr-owned targeted equivalence tests for Reader-backed regex locate.

locate_first_regex <- function(...) charr:::ci_locate_first_regex(...)
locate_all_regex <- function(...) charr:::ci_locate_all_regex(...)

regex_locate_charvec <- function(x) {
  charr:::ci_replace_all_regex(x, "\uffff", "")
}

regex_locate_capture_result <- function(fun) {
  warnings <- character()
  value <- withCallingHandlers(
    fun(),
    warning = function(cnd) {
      warnings <<- c(warnings, conditionMessage(cnd))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = warnings)
}


test_that("regex locate converts UTF-16 offsets to code-point positions", {
  strings <- charr:::ci_trim_both(c(
    " 😀a😀 ", " 𐐷Z ", " é😀b ", " üa ", NA_character_
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    locate_first_regex(strings, c("a", "Z", "b", "̈", "x")),
    cbind(
      start = c(2L, 2L, 3L, 2L, NA_integer_),
      end = c(2L, 2L, 3L, 2L, NA_integer_)
    )
  )
  expect_identical(
    locate_first_regex(
      strings, c("😀a", "𐐷Z", "é😀", "ü", "x"), get_length = TRUE
    ),
    cbind(
      start = c(1L, 1L, 1L, 1L, NA_integer_),
      length = c(2L, 2L, 2L, 2L, NA_integer_)
    )
  )
})
test_that("regex locate all handles astral and zero-length matches", {
  strings <- charr:::ci_trim_both(c(" 😀a😀 ", " 𐐷𐐷 ", " é😀é ", "", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    locate_all_regex(strings, c("😀", "𐐷", "é", "^", "(?=a)")),
    list(
      cbind(start = c(1L, 3L), end = c(1L, 3L)),
      cbind(start = c(1L, 2L), end = c(1L, 2L)),
      cbind(start = c(1L, 3L), end = c(1L, 3L)),
      cbind(start = 1L, end = 0L),
      cbind(start = 1L, end = 0L)
    )
  )

  zeroes <- charr:::ci_trim_both(c("", "a", "😀a", "😀"))
  expect_identical(charport::is_charvec(zeroes), charr_altrep())
  expect_identical(
    locate_all_regex(zeroes, ".*?", get_length = TRUE),
    list(
      cbind(start = 1L, length = 0L),
      cbind(start = c(1L, 2L), length = c(0L, 0L)),
      cbind(start = c(1L, 2L, 3L), length = c(0L, 0L, 0L)),
      cbind(start = c(1L, 2L), length = c(0L, 0L))
    )
  )
})

test_that("regex locate preserves no-match, omit, NA, and empty shapes", {
  strings <- charr:::ci_trim_both(c(" abc ", "", NA_character_, " 😀 "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    locate_first_regex(strings, "x", get_length = TRUE),
    cbind(
      start = c(-1L, -1L, NA_integer_, -1L),
      length = c(-1L, -1L, NA_integer_, -1L)
    )
  )
  expect_identical(
    locate_all_regex(strings, "x", get_length = TRUE),
    list(
      cbind(start = -1L, length = -1L),
      cbind(start = -1L, length = -1L),
      cbind(start = NA_integer_, length = NA_integer_),
      cbind(start = -1L, length = -1L)
    )
  )
  omitted <- locate_all_regex(
    strings, "x", omit_no_match = TRUE, get_length = TRUE
  )
  expect_identical(omitted[[1]], cbind(start = integer(), length = integer()))
  expect_identical(omitted[[2]], cbind(start = integer(), length = integer()))
  expect_identical(
    omitted[[3]], cbind(start = NA_integer_, length = NA_integer_)
  )

  warnings <- character()
  empty <- withCallingHandlers(
    locate_all_regex(strings, "", omit_no_match = TRUE, get_length = TRUE),
    warning = function(cnd) {
      warnings <<- c(warnings, conditionMessage(cnd))
      invokeRestart("muffleWarning")
    }
  )
  expect_identical(
    warnings, rep("empty search patterns are not supported", 5L)
  )
  expect_identical(
    empty,
    rep(list(cbind(start = NA_integer_, length = NA_integer_)), 4L)
  )
})


test_that("regex locate preserves named capture-group attributes", {
  strings <- charr:::ci_trim_both(c(" 😀a ", " x ", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  pattern <- "(?<emoji>😀)?(?<letter>a)?"

  expected_first <- matrix(
    c(1L, 1L, NA_integer_, 2L, 0L, NA_integer_), ncol = 2L
  )
  attr(expected_first, "capture_groups") <- list(
    emoji = cbind(
      start = c(1L, NA_integer_, NA_integer_),
      end = c(1L, NA_integer_, NA_integer_)
    ),
    letter = cbind(
      start = c(2L, NA_integer_, NA_integer_),
      end = c(2L, NA_integer_, NA_integer_)
    )
  )
  colnames(expected_first) <- c("start", "end")
  expect_identical(
    locate_first_regex(strings, pattern, capture_groups = TRUE),
    expected_first
  )

  one <- charr:::ci_trim_both(" 😀a ")
  expect_identical(charport::is_charvec(one), charr_altrep())
  whole <- matrix(c(1L, 3L, 2L, 2L), ncol = 2L)
  attr(whole, "capture_groups") <- list(
    emoji = cbind(start = c(1L, NA_integer_), end = c(1L, NA_integer_)),
    letter = cbind(start = c(2L, NA_integer_), end = c(2L, NA_integer_))
  )
  colnames(whole) <- c("start", "end")
  expect_identical(
    locate_all_regex(one, pattern, capture_groups = TRUE),
    list(whole)
  )
})






test_that("regex locate compiles before NA subjects and honors inline modes", {
  missing <- charr:::ci_trim_both(NA_character_)
  present <- charr:::ci_trim_both(c(" WORD ", " a ", " z "))
  expect_identical(charport::is_charvec(missing), charr_altrep())
  expect_identical(charport::is_charvec(present), charr_altrep())

  expect_error(
    locate_first_regex(missing, "["),
    "Missing closing bracket.*U_REGEX_MISSING_CLOSE_BRACKET.*context=`\\[`")
  expect_error(
    locate_all_regex(missing, "["),
    "Missing closing bracket.*U_REGEX_MISSING_CLOSE_BRACKET.*context=`\\[`")
  expect_identical(
    locate_first_regex(present, c("(?wi)word", "az", "az")),
    cbind(
      start = c(1L, NA_integer_, NA_integer_),
      end = c(4L, NA_integer_, NA_integer_)
    )
  )
  expect_warning(
    recycled <- locate_first_regex(present, c("(?wi)word", "a")),
    "longer object length is not a multiple"
  )
  expect_identical(
    recycled,
    cbind(
      start = c(1L, 1L, NA_integer_),
      end = c(4L, 1L, NA_integer_)
    )
  )
})
