title_frame_function <- function(backend) {
  if (identical(backend, "base")) {
    return(get(
      "ci_trans_totitle",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }

  if (identical(backend, "altrep")) {
    return(get(
      "ci_trans_totitle",
      envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }

  if (identical(backend, "stringi")) {
    return(stringi::stri_trans_totitle)
  }

  stop("unknown titlecase backend", call. = FALSE)
}


title_frame_call <- function(backend, value, options) {
  input <- if (identical(backend, "altrep")) {
    charport::as_charvec(value)
  } else {
    value
  }

  output <- title_frame_function(backend)(
    input, opts_brkiter = options
  )
  list(input = input, output = output)
}


title_frame_capture <- function(expr) {
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


title_frame_normalize_events <- function(events) {
  sub("stri_opts_brkiter", "ci_opts_brkiter", events, fixed = TRUE)
}


test_that("titlecase supports built-in prefixes and custom rules", {
  values <- c("oNE. tWO", "abc 123", "\u00e942", "", NA_character_)
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )

  for (type in list("word", "wo", rules)) {
    options <- list(type = type, locale = "en")
    expected <- title_frame_call("stringi", values, options)$output

    for (backend in c("base", "altrep")) {
      actual <- title_frame_call(backend, values, options)

      if (identical(backend, "altrep")) {
        expect_false(charport::charport_info(actual$input)$is_materialized)
        expect_true(charport::is_charvec(actual$output))
        expect_false(charport::charport_info(actual$output)$is_materialized)
      }

      expect_identical(actual$output, expected)
      expect_identical(
        Encoding(as.character(actual$output)),
        Encoding(expected)
      )
    }
  }
})


test_that("titlecase accepts Latin-1 custom rules", {
  rules <- iconv(
    "$letters = [[:L:]\u00e9]; $letters+; .;",
    from = "UTF-8", to = "latin1"
  )
  skip_if(is.na(rules), "Latin-1 conversion is unavailable")
  expect_identical(Encoding(rules), "latin1")

  values <- c("\u00e9CLAIR test", "abc \u00e9DEF")
  options <- list(type = rules, locale = "en")
  expected <- title_frame_call("stringi", values, options)$output

  for (backend in c("base", "altrep")) {
    actual <- title_frame_call(backend, values, options)
    if (identical(backend, "altrep")) {
      expect_false(charport::charport_info(actual$input)$is_materialized)
      expect_false(charport::charport_info(actual$output)$is_materialized)
    }
    expect_identical(actual$output, expected)
  }
})


test_that("titlecase validates the full type vector before scalar selection", {
  bytes <- rawToChar(as.raw(c(0x77, 0xff)))
  Encoding(bytes) <- "bytes"
  bytes_options <- list(type = c("word", bytes), locale = "en")
  expected_bytes <- title_frame_capture(
    title_frame_function("stringi")(
      "oNE. tWO", opts_brkiter = bytes_options
    )
  )

  expect_identical(
    sub(":.*$", "", expected_bytes$events),
    "error"
  )
  expect_match(expected_bytes$events, "bytes encoding", fixed = TRUE)

  vector_options <- list(type = c("word", "sentence"), locale = "en")
  expected_vector <- title_frame_capture(
    title_frame_function("stringi")(
      "oNE. tWO", opts_brkiter = vector_options
    )
  )
  expect_identical(sub(":.*$", "", expected_vector$events), "warning")
  expect_identical(expected_vector$value, "One. Two")

  missing_options <- list(type = c(NA_character_, "word"), locale = "en")
  expected_missing <- title_frame_capture(
    title_frame_function("stringi")(
      "oNE. tWO", opts_brkiter = missing_options
    )
  )
  expect_identical(
    sub(":.*$", "", expected_missing$events),
    c("warning", "error")
  )

  for (backend in c("base", "altrep")) {
    fun <- title_frame_function(backend)
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec("oNE. tWO")
    } else {
      "oNE. tWO"
    }

    actual_bytes <- title_frame_capture(
      fun(input, opts_brkiter = bytes_options)
    )
    expect_identical(actual_bytes, expected_bytes)

    actual_vector <- title_frame_capture(
      fun(input, opts_brkiter = vector_options)
    )
    expect_identical(actual_vector, expected_vector)

    actual_missing <- title_frame_capture(
      fun(input, opts_brkiter = missing_options)
    )
    expect_identical(actual_missing, expected_missing)

    if (identical(backend, "altrep")) {
      expect_false(charport::charport_info(input)$is_materialized)
      expect_false(charport::charport_info(actual_vector$value)$is_materialized)
    }
  }
})


test_that("titlecase rejects malformed break iterator options", {
  cases <- list(
    not_a_list = 1L,
    unnamed = list("word"),
    missing_name = structure(list("word"), names = NA_character_),
    empty_type = list(type = character()),
    missing_type = list(type = NA_character_)
  )

  for (options in cases) {
    expected <- title_frame_capture(
      title_frame_function("stringi")(
        "alpha beta", opts_brkiter = options
      )
    )
    expected$events <- title_frame_normalize_events(expected$events)

    for (backend in c("base", "altrep")) {
      actual <- title_frame_capture(
        title_frame_function(backend)(
          "alpha beta", opts_brkiter = options
        )
      )
      expect_identical(actual, expected)
    }
  }
})


test_that("titlecase validates every recognized skip option", {
  skip_options <- c(
    "skip_word_none", "skip_word_number", "skip_word_letter",
    "skip_word_kana", "skip_word_ideo", "skip_line_soft",
    "skip_line_hard", "skip_sentence_term", "skip_sentence_sep"
  )

  for (option in skip_options) {
    options <- list(type = "word")
    options[[option]] <- NA
    expected <- title_frame_capture(
      title_frame_function("stringi")(
        "alpha beta", opts_brkiter = options
      )
    )

    for (backend in c("base", "altrep")) {
      actual <- title_frame_capture(
        title_frame_function(backend)(
          "alpha beta", opts_brkiter = options
        )
      )
      expect_identical(actual, expected)
    }
  }

  options <- list(type = "word", skip_word_none = c(TRUE, FALSE))
  expected <- title_frame_capture(
    title_frame_function("stringi")(
      "alpha beta", opts_brkiter = options
    )
  )
  expect_identical(sub(":.*$", "", expected$events), "warning")

  for (backend in c("base", "altrep")) {
    actual <- title_frame_capture(
      title_frame_function(backend)(
        "alpha beta", opts_brkiter = options
      )
    )
    expect_identical(actual, expected)
  }
})


test_that("titlecase recovers after fallback warnings and subject errors", {
  skip_if_not(charr:::charr_icu_bundled())
  bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(bytes) <- "bytes"
  failing_options <- list(type = "word", locale = "nl")
  recovery_options <- list(type = "word", locale = "en")
  expected_recovery <- title_frame_function("stringi")(
    "alpha beta", opts_brkiter = recovery_options
  )

  for (backend in c("base", "altrep")) {
    fun <- title_frame_function(backend)
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(bytes)
    } else {
      bytes
    }
    failed <- title_frame_capture(
      fun(input, opts_brkiter = failing_options)
    )

    expect_identical(
      sub(":.*$", "", failed$events),
      c("warning", "error")
    )
    expect_match(failed$events[[1L]], "resource bundle", ignore.case = TRUE)
    expect_match(failed$events[[2L]], "bytes encoding", fixed = TRUE)

    recovery_input <- if (identical(backend, "altrep")) {
      charport::as_charvec("alpha beta")
    } else {
      "alpha beta"
    }
    recovery <- fun(recovery_input, opts_brkiter = recovery_options)

    if (identical(backend, "altrep")) {
      expect_false(charport::charport_info(input)$is_materialized)
      expect_false(charport::charport_info(recovery_input)$is_materialized)
      expect_false(charport::charport_info(recovery)$is_materialized)
    }
    expect_identical(recovery, expected_recovery)
  }
})
