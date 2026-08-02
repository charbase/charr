# charr-owned tests for Reader-backed positional substring operations.

expect_sub_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

sub_bytes_value <- function() {
  value <- rawToChar(as.raw(0xff))
  Encoding(value) <- "bytes"
  value
}

sub_marked_value <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

sub_condition_events <- function(expr) {
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

test_that("positional extraction preserves recycling and negative lengths", {
  values <- c("aé🙂üz", "abcdef", NA_character_)
  subject <- charport::as_charvec(values)

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub")(
      subject,
      from = c(2L, -2L, 1L, 3L, 1L, 1L),
      length = c(3L, -1L, 0L, 2L, 1L, -2L),
      ignore_negative_length = TRUE
    )
  )
  expected <- stringi::stri_sub(
    values,
    from = c(2L, -2L, 1L, 3L, 1L, 1L),
    length = c(3L, -1L, 0L, 2L, 1L, -2L),
    ignore_negative_length = TRUE
  )

  expect_sub_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_sub_unmaterialized(subject)
})

test_that("positional extraction keeps zero and NA results Builder-backed", {
  bytes_subject <- charport::as_charvec(sub_bytes_value())

  empty <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub")(bytes_subject, integer(), integer())
  )
  empty_replacement <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_replace")(
      bytes_subject,
      integer(),
      integer(),
      replacement = "x"
    )
  )
  missing <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub")(charport::as_charvec("abc"), NA_integer_, 1L)
  )

  expect_sub_unmaterialized(empty)
  expect_identical(empty, character())
  expect_sub_unmaterialized(empty_replacement)
  expect_identical(empty_replacement, character())
  expect_sub_unmaterialized(missing)
  expect_identical(missing, NA_character_)
  expect_sub_unmaterialized(bytes_subject)
})

test_that("single positional replacement preserves copied splice rules", {
  values <- c("aé🙂üz", "abcdef", "xyz")
  replacements <- c("X", "🙂", "")
  subject <- charport::as_charvec(values)
  replacement <- charport::as_charvec(replacements)

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_replace")(
      subject,
      from = c(-2L, 2L, NA_integer_),
      to = c(-1L, 4L, 2L),
      omit_na = TRUE,
      replacement = replacement
    )
  )
  expected <- stringi::stri_sub_replace(
    values,
    from = c(-2L, 2L, NA_integer_),
    to = c(-1L, 4L, 2L),
    omit_na = TRUE,
    replacement = replacements
  )

  expect_sub_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_sub_unmaterialized(subject)
  expect_sub_unmaterialized(replacement)
})

test_that("positional forms preserve normalization and malformed UTF-8", {
  latin1 <- sub_marked_value(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- sub_marked_value(c(0x61, 0xff, 0x62, 0x63), "UTF-8")
  values <- c(latin1, "\ufeffabc", malformed)
  subject <- charport::as_charvec(values)

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub")(
      subject,
      from = c(2L, 1L, 2L),
      to = c(4L, 2L, 3L)
    )
  )
  expected <- stringi::stri_sub(
    values,
    from = c(2L, 1L, 2L),
    to = c(4L, 2L, 3L)
  )

  expect_sub_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_identical(charToRaw(actual[[3]]), as.raw(c(0xff, 0x62)))
  expect_sub_unmaterialized(subject)
})

test_that("single positional replacement supports an exact input alias", {
  values <- c("abcd", "\u00e9xyz")
  shared <- charport::as_charvec(values)

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_replace")(
      shared,
      from = c(2L, 2L),
      to = c(3L, 3L),
      replacement = shared
    )
  )
  expected <- stringi::stri_sub_replace(
    values,
    from = c(2L, 2L),
    to = c(3L, 3L),
    replacement = values
  )

  expect_sub_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_sub_unmaterialized(shared)
})

test_that("sub_all converts only source records with requested substrings", {
  bytes <- sub_bytes_value()
  values <- c(bytes, "abcdef")
  subject <- charport::as_charvec(values)
  from <- list(integer(), c(1L, 3L))
  to <- list(integer(), c(2L, 4L))

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_all")(subject, from = from, to = to)
  )
  expected <- stringi::stri_sub_all(values, from = from, to = to)

  expect_altrep_charvec_list(actual)
  invisible(lapply(actual, expect_sub_unmaterialized))
  expect_identical(actual, expected)
  expect_sub_unmaterialized(subject)
})

test_that("sub_all reuses one source borrow across many singleton records", {
  n <- 10000L
  subject <- charport::as_charvec(rep("abc", n))

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_all")(subject, from = list(2L), to = list(2L))
  )

  expect_length(actual, n)
  expect_altrep_charvec_list(actual)
  expect_identical(actual[c(1L, n)], list("b", "b"))
  expect_sub_unmaterialized(subject)
})

test_that("sub_all preserves per-element conversion and validation order", {
  bytes <- sub_bytes_value()
  bad <- matrix(1:3, nrow = 1)

  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_all")(
        charport::as_charvec(c(bytes, "abc")),
        from = list(1L, bad)
      )
    ),
    "bytes encoding is not supported",
    fixed = TRUE
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_all")(
        charport::as_charvec(c("abc", bytes)),
        from = list(bad, 1L)
      )
    ),
    "argument `from` should be a matrix with 2 columns",
    fixed = TRUE
  )
})

test_that("sub_all preserves custom integer coercion condition order", {
  state <- new.env(parent = emptyenv())
  state$calls <- character()
  method_class <- "charr_sub_all_condition_index"
  registerS3method(
    "as.integer",
    method_class,
    function(x, ...) {
      mode <- attr(x, "mode", exact = TRUE)
      state$calls <- c(state$calls, mode)
      warning(paste(mode, "index warning"), call. = FALSE)
      if (identical(mode, "error"))
        stop("late index error", call. = FALSE)
      1L
    },
    envir = baseenv()
  )

  warning_index <- structure(
    1L, class = method_class, mode = "early"
  )
  bytes <- sub_bytes_value()
  events <- sub_condition_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_all")(
        charport::as_charvec(bytes),
        from = list(warning_index),
        to = list(1L)
      )
    )
  )
  expect_identical(sub(" .*", "", events), c("warning", "error"))
  expect_match(events[[1]], "early index warning", fixed = TRUE)
  expect_match(
    events[[2]], "bytes encoding is not supported", fixed = TRUE
  )
  expect_identical(state$calls, "early")

  state$calls <- character()
  error_index <- structure(
    1L, class = method_class, mode = "error"
  )
  events <- sub_condition_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_all")(
        charport::as_charvec(c(bytes, "abc")),
        from = list(1L, error_index),
        to = list(1L, 1L)
      )
    )
  )
  expect_length(events, 1L)
  expect_match(
    events[[1]], "bytes encoding is not supported", fixed = TRUE
  )
  expect_identical(state$calls, character())
})

test_that("positional forms emit recycling warnings before bytes errors", {
  bytes <- sub_bytes_value()
  subject <- charport::as_charvec(bytes)

  expected <- sub_condition_events(
    stringi::stri_sub(bytes, from = 1:2, to = 1:3)
  )
  actual <- sub_condition_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub")(subject, from = 1:2, to = 1:3)
    )
  )
  expect_identical(actual, expected)
  expect_identical(sub(" .*", "", actual), c("warning", "error"))

  values <- c("abc", bytes)
  all_subject <- charport::as_charvec(values)
  from <- list(c(1L, 2L), 1L)
  to <- list(c(1L, 2L, 3L), 1L)
  expected_all <- sub_condition_events(
    stringi::stri_sub_all(values, from = from, to = to)
  )
  actual_all <- sub_condition_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_all")(all_subject, from = from, to = to)
    )
  )
  expect_identical(actual_all, expected_all)
  expect_identical(sub(" .*", "", actual_all), c("warning", "error"))
  expect_sub_unmaterialized(subject)
  expect_sub_unmaterialized(all_subject)
})

test_that("sub_all replacement matches sorted multibyte splices", {
  values <- c("aé🙂üz", "abcdef")
  from <- list(c(1L, 3L, 6L), c(2L, 5L))
  to <- list(c(1L, 3L, 6L), c(3L, 6L))
  replacements <- list(c("A", "B", "C"), c("X", "Y"))
  subject <- charport::as_charvec(values)
  replacement <- lapply(replacements, charport::as_charvec)

  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_sub_replace_all")(
      subject,
      from = from,
      to = to,
      replacement = replacement
    )
  )
  expected <- stringi::stri_sub_replace_all(
    values,
    from = from,
    to = to,
    replacement = replacements
  )

  expect_sub_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_sub_unmaterialized(subject)
  invisible(lapply(replacement, expect_sub_unmaterialized))
})

test_that("sub_all replacement preserves source and value error order", {
  bytes <- sub_bytes_value()
  bad <- matrix(1:3, nrow = 1)

  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_replace_all")(
        charport::as_charvec(bytes),
        from = list(bad),
        replacement = list("x")
      )
    ),
    "bytes encoding is not supported",
    fixed = TRUE
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_sub_replace_all")(
        charport::as_charvec("abc"),
        from = list(bad),
        replacement = list(charport::as_charvec(bytes))
      )
    ),
    "bytes encoding is not supported",
    fixed = TRUE
  )
})
