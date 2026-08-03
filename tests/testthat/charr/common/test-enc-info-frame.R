enc_info_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_enc_info)
  }

  if (identical(backend, "base")) {
    return(get(
      "ci_enc_info",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }

  if (identical(backend, "altrep")) {
    return(get(
      "ci_enc_info",
      envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }

  stop("unknown encoding-info backend", call. = FALSE)
}


enc_info_frame_capture <- function(expr) {
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


expect_enc_info_frame_result <- function(actual, expected, backend) {
  expect_type(actual, "list")
  expect_identical(names(actual), names(expected))
  expect_identical(
    vapply(actual, typeof, character(1)),
    vapply(expected, typeof, character(1))
  )
  expect_identical(
    vapply(actual, length, integer(1)),
    vapply(expected, length, integer(1))
  )

  if (identical(backend, "altrep")) {
    character_children <- which(vapply(actual, is.character, logical(1)))
    expect_gt(length(character_children), 0L)
    for (i in character_children) {
      expect_true(charport::is_charvec(actual[[i]]))
      expect_false(charport::charport_info(actual[[i]])$is_materialized)
    }
  }

  expect_identical(actual, expected)
}


test_that("encoding info preserves default and empty-name behavior", {
  backend <- charr_backend()
  fun <- enc_info_frame_function(backend)
  expected <- fun()

  expect_enc_info_frame_result(expected, expected, backend)
  expect_enc_info_frame_result(fun(NULL), expected, backend)
  expect_enc_info_frame_result(fun(""), expected, backend)
})


test_that("encoding info preserves aliases and converter metadata", {
  aliases <- c(
    "UTF-8", "utf8", "latin1", "ISO-8859-1", "Shift_JIS"
  )

  for (encoding in aliases) {
    expected <- enc_info_frame_function("stringi")(encoding)

    for (backend in c("base", "altrep")) {
      actual <- enc_info_frame_function(backend)(encoding)
      expect_enc_info_frame_result(actual, expected, backend)
    }
  }

  for (backend in c("stringi", "base", "altrep")) {
    fun <- enc_info_frame_function(backend)
    expect_identical(fun("UTF-8"), fun("utf8"))
    expect_identical(fun("latin1"), fun("ISO-8859-1"))
  }
})


test_that("encoding info validates scalar encoding names", {
  cases <- list(
    multiple = c("UTF-8", "latin1"),
    empty = character(),
    missing = NA_character_,
    coercible = list("UTF-8")
  )

  for (encoding in cases) {
    expected <- enc_info_frame_capture(
      enc_info_frame_function("stringi")(encoding)
    )

    for (backend in c("base", "altrep")) {
      actual <- enc_info_frame_capture(
        enc_info_frame_function(backend)(encoding)
      )
      expect_identical(actual$events, expected$events)

      if (is.null(expected$value)) {
        expect_null(actual$value)
      } else {
        expect_enc_info_frame_result(actual$value, expected$value, backend)
      }
    }
  }

  multiple <- enc_info_frame_capture(
    enc_info_frame_function("stringi")(c("UTF-8", "latin1"))
  )
  expect_identical(sub(":.*$", "", multiple$events), "warning")
  expect_identical(
    multiple$value,
    enc_info_frame_function("stringi")("UTF-8")
  )
})


test_that("encoding info recovers after unknown encoding errors", {
  unknown <- "charr-encoding-that-does-not-exist"
  expected_error <- enc_info_frame_capture(
    enc_info_frame_function("stringi")(unknown)
  )
  expect_null(expected_error$value)
  expect_identical(sub(":.*$", "", expected_error$events), "error")

  expected_recovery <- enc_info_frame_function("stringi")("UTF-8")
  for (backend in c("base", "altrep")) {
    fun <- enc_info_frame_function(backend)
    actual_error <- enc_info_frame_capture(fun(unknown))
    expect_identical(actual_error, expected_error)

    recovery <- fun("UTF-8")
    expect_enc_info_frame_result(recovery, expected_recovery, backend)
  }
})
