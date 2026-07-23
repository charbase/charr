# Charr-owned tests for length-delimited scalar option parsing.

expect_option_unmaterialized <- function(value) {
  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)
}

option_events <- function(code) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      force(code),
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

test_that("break type reads scalar record zero when type is not first", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  type <- charport::as_charvec("wo")
  opts <- list(locale = "en", skip_word_none = TRUE, type = type)
  expect_identical(
    charr:::ci_count_boundaries("alpha beta", opts),
    2L
  )
  expect_option_unmaterialized(type)

  shared <- charport::as_charvec("word")
  aliased <- structure(list(shared, shared), names = c("type", "type"))
  expect_identical(
    charr:::ci_count_boundaries("alpha beta", opts_brkiter = aliased),
    3L
  )
  expect_option_unmaterialized(shared)

  bytes <- rawToChar(as.raw(c(0x77, 0xff)))
  Encoding(bytes) <- "bytes"
  type_vector <- charport::as_charvec(c("word", bytes))
  expect_identical(
    option_events(charr:::ci_count_boundaries(
      "alpha beta", opts_brkiter = list(type = type_vector)
    )),
    option_events(stringi::stri_count_boundaries(
      "alpha beta", opts_brkiter = list(type = c("word", bytes))
    ))
  )
  expect_option_unmaterialized(type_vector)
})

test_that("warn equals two releases break-option Readers and permits recovery", {
  previous_backend <- charr_altrep(TRUE)
  on.exit(charr_altrep(previous_backend), add = TRUE)

  type <- charport::as_charvec(c("word", "character"))
  old_warn <- getOption("warn")
  on.exit(options(warn = old_warn), add = TRUE)
  options(warn = 2)

  expect_error(
    charr:::ci_count_boundaries(
      "alpha beta", opts_brkiter = list(type = type)
    ),
    "should be a single character string"
  )
  expect_identical(
    charr:::ci_count_boundaries(
      "alpha beta", opts_brkiter = list(type = "word")
    ),
    3L
  )
  expect_option_unmaterialized(type)
})
