# Charr-owned tests for Reader-backed joins and line splitting.
# These are not imported from stringr.

join_other_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_join_other_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

join_other_warnings <- function(expr) {
  messages <- character()
  value <- withCallingHandlers(
    expr,
    warning = function(condition) {
      messages <<- c(messages, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, messages = messages)
}

join_other_events <- function(expr) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      expr,
      warning = function(condition) {
        events <<- c(events, paste("warning", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste("error", conditionMessage(condition)))
    }
  )
  events
}

test_that("duplicate uses unmaterialized input and lazy output", {
  latin1 <- join_other_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c("a\u00e9", "", "x", NA_character_, latin1)
  times <- c(2L, 0L, -1L, NA_integer_, 3L)
  subject <- charport::as_charvec(values)

  actual <- with_test_backend(TRUE, charr_test_leaf("ci_dup")(subject, times))
  expect_join_other_unmaterialized(actual)
  expect_identical(actual, stringi::stri_dup(values, times))
  expect_join_other_unmaterialized(subject)

  chained <- with_test_backend(TRUE, charr_test_leaf("ci_length")(actual))
  expect_identical(chained, stringi::stri_length(stringi::stri_dup(values, times)))
})

test_that("join preserves aliases, recycling, and collapse", {
  values <- c("a", "\u00e9", NA_character_, "")
  shared <- charport::as_charvec(values)

  joined <- with_test_backend(TRUE, charr_test_leaf("ci_c")(shared, shared))
  expect_join_other_unmaterialized(joined)
  expect_identical(joined, stringi::stri_c(values, values))
  expect_join_other_unmaterialized(shared)

  right_values <- c("1", "2", "3")
  right <- charport::as_charvec(right_values)
  expected <- join_other_warnings(
    stringi::stri_c(values, right_values, sep = "|")
  )
  actual <- join_other_warnings(
    with_test_backend(TRUE, charr_test_leaf("ci_c")(shared, right, sep = "|"))
  )
  expect_identical(actual, expected)
  expect_identical(
    actual$messages,
    "longer object length is not a multiple of shorter object length"
  )

  collapsed <- suppressWarnings(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_c")(shared, right, sep = "|", collapse = ";")
    )
  )
  expect_join_other_unmaterialized(collapsed)
  expect_identical(
    collapsed,
    suppressWarnings(stringi::stri_c(
      values, right_values, sep = "|", collapse = ";"
    ))
  )
})

test_that("join inspects scalar separators without materializing them", {
  left_values <- c("a", "b")
  right_values <- c("1", "2")
  third_values <- c("x", "y")
  left <- charport::as_charvec(left_values)
  right <- charport::as_charvec(right_values)
  third <- charport::as_charvec(third_values)
  sep <- charport::as_charvec("|")
  collapse <- charport::as_charvec(";")

  joined <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_c")(left, right, third, sep = sep, collapse = collapse)
  )
  expect_identical(
    joined,
    stringi::stri_c(
      left_values, right_values, third_values,
      sep = "|", collapse = ";"
    )
  )
  expect_join_other_unmaterialized(sep)
  expect_join_other_unmaterialized(collapse)

  no_collapse_sep <- charport::as_charvec("|")
  uncollapsed <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_c")(left, right, sep = no_collapse_sep)
  )
  expect_identical(
    uncollapsed,
    stringi::stri_c(left_values, right_values, sep = "|")
  )
  expect_join_other_unmaterialized(no_collapse_sep)

  empty_sep <- charport::as_charvec("")
  fast_collapse <- charport::as_charvec(";")
  fast <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_c")(
      left, right, sep = empty_sep, collapse = fast_collapse
    )
  )
  expect_identical(
    fast,
    stringi::stri_c(
      left_values, right_values, sep = "", collapse = ";"
    )
  )
  expect_join_other_unmaterialized(empty_sep)
  expect_join_other_unmaterialized(fast_collapse)

  na_sep <- charport::as_charvec(NA_character_)
  bytes_value <- join_other_marked(c(0xff, 0xfe), "bytes")
  untouched_collapse <- charport::as_charvec(bytes_value)
  missing <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_c")(
      left, right, sep = na_sep, collapse = untouched_collapse
    )
  )
  expect_identical(
    missing,
    stringi::stri_c(
      left_values, right_values, sep = NA_character_,
      collapse = bytes_value
    )
  )
  expect_join_other_unmaterialized(na_sep)
  expect_join_other_unmaterialized(untouched_collapse)

  empty_collapse <- charport::as_charvec("")
  flattened <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_flatten")(left, empty_collapse)
  )
  expect_identical(
    flattened,
    stringi::stri_flatten(left_values, "")
  )
  expect_join_other_unmaterialized(empty_collapse)
})

test_that("join keeps copied early-return order", {
  bytes <- join_other_marked(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes)

  empty_dup <- with_test_backend(TRUE, charr_test_leaf("ci_dup")(character(), 2L))
  expect_join_other_unmaterialized(empty_dup)
  expect_identical(empty_dup, stringi::stri_dup(character(), 2L))

  empty_join <- with_test_backend(TRUE, charr_test_leaf("ci_c")(subject, character()))
  expect_join_other_unmaterialized(empty_join)
  expect_identical(
    empty_join,
    stringi::stri_c(bytes, character())
  )
  expect_join_other_unmaterialized(subject)

  ignored_sep <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_c")("x", sep = bytes, collapse = ",")
  )
  expect_join_other_unmaterialized(ignored_sep)
  expect_identical(
    ignored_sep,
    stringi::stri_c("x", sep = bytes, collapse = ",")
  )

  missing <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_flatten")(subject, NA_character_)
  )
  expect_join_other_unmaterialized(missing)
  expect_identical(
    missing,
    stringi::stri_flatten(bytes, NA_character_)
  )
  expect_join_other_unmaterialized(subject)

})

test_that("flatten preserves NA and empty controls", {
  values <- c(NA_character_, "", "A", "", "B", NA_character_, "\u00e9")
  subject <- charport::as_charvec(values)

  cases <- list(
    list(na_empty = FALSE, omit_empty = FALSE),
    list(na_empty = TRUE, omit_empty = FALSE),
    list(na_empty = NA, omit_empty = FALSE),
    list(na_empty = TRUE, omit_empty = TRUE)
  )
  for (case in cases) {
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_flatten")(
        subject, ",", case$na_empty, case$omit_empty
      )
    )
    expect_join_other_unmaterialized(actual)
    expect_identical(
      actual,
      stringi::stri_flatten(
        values, ",", case$na_empty, case$omit_empty
      )
    )
  }
  expect_join_other_unmaterialized(subject)
})

test_that("line splitting recognizes every copied separator", {
  separators <- c("\r\n", "\r", "\n", "\u0085", "\v", "\f", "\u2028", "\u2029")
  values <- c(
    paste0("a", paste(separators, collapse = ""), "b"),
    "\r",
    "",
    NA_character_
  )
  subject <- charport::as_charvec(values)

  actual <- charr_test_leaf("ci_split_lines")(subject, FALSE)
  expect_identical(actual, stringi::stri_split_lines(values, FALSE))
  expect_altrep_unmaterialized_list(actual)
  expect_join_other_unmaterialized(subject)
})

test_that("line splitting preserves omit-empty recycling and encodings", {
  latin1 <- join_other_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x0d, 0x66, 0x69, 0x6e),
    "latin1"
  )
  values <- c("\ufeffa\n\nb", latin1, "\n", "x\r\n")
  omit <- c(FALSE, TRUE, NA)
  subject <- charport::as_charvec(values)

  expected <- join_other_warnings(stringi::stri_split_lines(values, omit))
  actual <- join_other_warnings(charr_test_leaf("ci_split_lines")(subject, omit))
  expect_identical(actual, expected)
  expect_join_other_unmaterialized(subject)
})

test_that("join and line splitting preserve malformed and bytes handling", {
  malformed <- join_other_marked(c(0x61, 0xff, 0x62), "UTF-8")
  malformed_subject <- charport::as_charvec(malformed)
  joined <- with_test_backend(TRUE, charr_test_leaf("ci_c")(malformed_subject, "x"))
  expect_identical(
    charToRaw(joined),
    charToRaw(stringi::stri_c(malformed, "x"))
  )
  split <- charr_test_leaf("ci_split_lines")(malformed_subject, FALSE)
  expect_identical(split, stringi::stri_split_lines(malformed, FALSE))

  bytes <- join_other_marked(c(0xff, 0xfe), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_dup")(bytes_subject, 2L)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_c")(bytes_subject, "x")),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_flatten")(bytes_subject, ",")),
    "bytes encoding"
  )
  expect_error(charr_test_leaf("ci_split_lines")(bytes_subject), "bytes encoding")
})

test_that("join emits list recycling warning before a bytes error", {
  bytes <- join_other_marked(c(0xff, 0xfe), "bytes")
  values2 <- c("a", "b")
  values3 <- c("x", "y", "z")
  bytes_subject <- charport::as_charvec(bytes)
  subject2 <- charport::as_charvec(values2)
  subject3 <- charport::as_charvec(values3)

  expected <- join_other_events(
    stringi::stri_c(bytes, values2, values3, sep = "-")
  )
  actual <- join_other_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_c")(bytes_subject, subject2, subject3, sep = "-")
    )
  )
  expect_identical(actual, expected)
  expect_identical(
    sub(" .*", "", actual),
    c("warning", "error")
  )
  expect_join_other_unmaterialized(bytes_subject)
  expect_join_other_unmaterialized(subject2)
  expect_join_other_unmaterialized(subject3)
})
