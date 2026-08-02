# Charr-owned tests for the mapped case operations and wrap's NFC helper.

transformation_call <- function(name, ...) {
  with_test_backend(
    TRUE,
    do.call(charr_test_leaf(name), list(...))
  )
}

transformation_oracle <- function(name, ...) {
  stri_name <- sub("^ci_", "stri_", name)
  do.call(get(stri_name, envir = asNamespace("stringi")), list(...))
}

transformation_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_transformation_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

transformation_events <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
      NULL
    }
  )
  list(value = value, events = events)
}

test_that("case transformations preserve the copied ICU mappings", {
  latin1 <- transformation_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c(
    "I", "\u0130", "i", "\u0131", "Stra\u00dfe", "\u039f\u03a3",
    "\U00010400\U00010428", "\U0001f642\u00c9", "\ufeffAbC", latin1,
    "", NA_character_
  )
  subject <- charport::as_charvec(values)

  lower <- transformation_call("ci_trans_tolower", subject, "tr")
  expect_transformation_unmaterialized(lower)
  expect_identical(lower, transformation_oracle("ci_trans_tolower", values, "tr"))

  upper <- transformation_call("ci_trans_toupper", subject, "tr")
  expect_transformation_unmaterialized(upper)
  expect_identical(upper, transformation_oracle("ci_trans_toupper", values, "tr"))

  word_opts <- stringi::stri_opts_brkiter(type = "word", locale = "el")
  word <- transformation_call(
    "ci_trans_totitle", subject, opts_brkiter = word_opts
  )
  expect_transformation_unmaterialized(word)
  expect_identical(
    word,
    transformation_oracle(
      "ci_trans_totitle", values, opts_brkiter = word_opts
    )
  )

  sentence_values <- c(
    "oNE. tWO", "\u039f\u03a3 \u039f\u03a3\u0391", "", NA_character_
  )
  sentence_subject <- charport::as_charvec(sentence_values)
  sentence_opts <- stringi::stri_opts_brkiter(
    type = "sentence", locale = "el"
  )
  sentence <- transformation_call(
    "ci_trans_totitle", sentence_subject, opts_brkiter = sentence_opts
  )
  expect_transformation_unmaterialized(sentence)
  expect_identical(
    sentence,
    transformation_oracle(
      "ci_trans_totitle", sentence_values, opts_brkiter = sentence_opts
    )
  )

  expect_transformation_unmaterialized(subject)
  expect_transformation_unmaterialized(sentence_subject)
})

test_that("case transformations preserve malformed and bytes behavior", {
  malformed <- transformation_marked(0xc3, "UTF-8")
  malformed_subject <- charport::as_charvec(malformed)
  word_opts <- stringi::stri_opts_brkiter(type = "word", locale = "en")

  calls <- list(
    lower = function(x) transformation_call("ci_trans_tolower", x, "en"),
    upper = function(x) transformation_call("ci_trans_toupper", x, "en"),
    title = function(x) transformation_call(
      "ci_trans_totitle", x, opts_brkiter = word_opts
    )
  )
  oracles <- list(
    lower = function(x) stringi::stri_trans_tolower(x, "en"),
    upper = function(x) stringi::stri_trans_toupper(x, "en"),
    title = function(x) stringi::stri_trans_totitle(
      x, opts_brkiter = word_opts
    )
  )

  for (name in names(calls)) {
    actual <- calls[[name]](malformed_subject)
    expect_transformation_unmaterialized(actual)
    expect_identical(charToRaw(actual[[1L]]), charToRaw(oracles[[name]](malformed)))
  }

  bytes <- malformed
  Encoding(bytes) <- "bytes"
  bytes_subject <- charport::as_charvec(bytes)
  for (call in calls) {
    expect_error(call(bytes_subject), "bytes encoding")
  }

  expect_transformation_unmaterialized(malformed_subject)
  expect_transformation_unmaterialized(bytes_subject)
})

test_that("wrap's NFC helper accepts charvec input", {
  latin1 <- transformation_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- transformation_marked(c(0x61, 0xc3, 0x28), "UTF-8")
  values <- c(
    "\u00e5", "a\u030a", "\ufeffAbC", latin1, malformed, "",
    NA_character_
  )
  subject <- charport::as_charvec(values)

  actual <- transformation_call("ci_trans_nfc", subject)
  expect_transformation_unmaterialized(actual)
  expect_identical(actual, transformation_oracle("ci_trans_nfc", values))

  bytes <- transformation_marked(c(0x61, 0xff), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  expect_error(transformation_call("ci_trans_nfc", bytes_subject), "bytes encoding")
  expect_transformation_unmaterialized(subject)
  expect_transformation_unmaterialized(bytes_subject)
})

test_that("mapped transformation output stays lazy through a chain", {
  values <- c("Stra\u00dfe", "\u00c9cole", "a\u030a", "", NA_character_)
  subject <- charport::as_charvec(values)

  lower <- transformation_call("ci_trans_tolower", subject, "de")
  normalized <- transformation_call("ci_trans_nfc", lower)
  upper <- transformation_call("ci_trans_toupper", normalized, "de")

  expected <- stringi::stri_trans_tolower(values, "de")
  expected <- stringi::stri_trans_nfc(expected)
  expected <- stringi::stri_trans_toupper(expected, "de")

  expect_identical(upper, expected)
  for (value in list(subject, lower, normalized, upper)) {
    expect_transformation_unmaterialized(value)
  }
})

test_that("mapped transformation entry points return lazy empty vectors", {
  empty <- charport::as_charvec(character())
  word_opts <- stringi::stri_opts_brkiter(type = "word", locale = "en")
  calls <- list(
    function() transformation_call("ci_trans_tolower", empty, "en"),
    function() transformation_call("ci_trans_toupper", empty, "en"),
    function() transformation_call(
      "ci_trans_totitle", empty, opts_brkiter = word_opts
    ),
    function() transformation_call("ci_trans_nfc", empty)
  )

  for (call in calls) {
    actual <- call()
    expect_transformation_unmaterialized(actual)
    expect_identical(actual, character())
  }
  expect_transformation_unmaterialized(empty)
})

test_that("titlecase releases fallback state before warning and input error", {
  skip_if_not(charr:::charr_icu_bundled())
  bytes <- transformation_marked(c(0x61, 0xff), "bytes")
  subject <- charport::as_charvec(bytes)
  opts <- stringi::stri_opts_brkiter(type = "word", locale = "nl")

  result <- transformation_events(
    transformation_call("ci_trans_totitle", subject, opts_brkiter = opts)
  )
  expect_identical(sub(":.*$", "", result$events), c("warning", "error"))
  expect_match(result$events[[1L]], "resource bundle", ignore.case = TRUE)
  expect_match(result$events[[2L]], "bytes encoding", fixed = TRUE)
  expect_transformation_unmaterialized(subject)
})

test_that("titlecase releases option state before warn=2 coercion exits", {
  opts <- stringi::stri_opts_brkiter(type = "word", locale = "en_US")
  old_warn <- getOption("warn")
  on.exit(options(warn = old_warn), add = TRUE)
  options(warn = 2)

  expect_error(
    transformation_call(
      "ci_trans_totitle", list(c("alpha", "beta")),
      opts_brkiter = opts
    ),
    "argument is not an atomic vector; coercing",
    fixed = TRUE
  )
  expect_error(
    transformation_call(
      "ci_trans_totitle", "alpha beta",
      opts_brkiter = list(
        type = "word", locale = c("en_US", "en_GB")
      )
    ),
    "argument `locale` should be a single character string",
    fixed = TRUE
  )

  result <- transformation_call(
    "ci_trans_totitle", charport::as_charvec("alpha beta"),
    opts_brkiter = opts
  )
  expect_transformation_unmaterialized(result)
  expect_identical(result, "Alpha Beta")
})
