locate_fixed_frame_function <- function(backend, all = FALSE) {
  name <- if (all) "ci_locate_all_fixed" else "ci_locate_first_fixed"
  if (identical(backend, "stringi")) {
    return(if (all) {
      stringi::stri_locate_all_fixed
    } else {
      stringi::stri_locate_first_fixed
    })
  }
  if (identical(backend, "base")) {
    return(get(
      name,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown fixed-locate backend", call. = FALSE)
}


locate_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


locate_fixed_frame_invoke <- function(
    backend, inputs, all = FALSE, omit_no_match = FALSE,
    get_length = FALSE, opts_fixed = NULL
) {
  fun <- locate_fixed_frame_function(backend, all = all)
  if (!all) {
    return(fun(
      inputs$subject, inputs$pattern,
      opts_fixed = opts_fixed,
      get_length = get_length
    ))
  }
  fun(
    inputs$subject, inputs$pattern,
    omit_no_match = omit_no_match,
    opts_fixed = opts_fixed,
    get_length = get_length
  )
}


locate_fixed_frame_capture <- function(expr, warning_handler = NULL) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        if (!is.null(warning_handler)) {
          warning_handler(condition)
        }
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


locate_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_locate_fixed_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


test_that("fixed location preserves positions and result shapes", {
  latin1 <- locate_fixed_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x20, 0xe9), "latin1"
  )
  malformed <- locate_fixed_frame_marked(
    c(0x61, 0xff, 0x62, 0x61), "UTF-8"
  )
  bom_a <- locate_fixed_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61, 0x61), "UTF-8"
  )
  cases <- list(
    list(
      subject = c(
        "aaaa", "ababa", "ééé", latin1, malformed,
        bom_a, "", NA_character_
      ),
      pattern = c("aa", "aba", "éé", "é", "b", "a", "x", "a"),
      options = list(overlap = TRUE)
    ),
    list(
      subject = c("AaAa", "ÉéÉ", "0123456789abcdef0123456789abcdef"),
      pattern = c("aa", "éé", "0123456789abcdef"),
      options = list(case_insensitive = TRUE, overlap = TRUE)
    )
  )

  calls <- list(
    list(all = FALSE, omit = FALSE, length = FALSE),
    list(all = FALSE, omit = FALSE, length = TRUE),
    list(all = TRUE, omit = FALSE, length = FALSE),
    list(all = TRUE, omit = TRUE, length = TRUE)
  )
  for (case in cases) {
    for (call in calls) {
      options <- case$options
      if (!call$all) {
        options$overlap <- NULL
      }
      oracle_inputs <- locate_fixed_frame_inputs(
        "stringi", case$subject, case$pattern
      )
      oracle <- locate_fixed_frame_invoke(
        "stringi", oracle_inputs,
        all = call$all,
        omit_no_match = call$omit,
        get_length = call$length,
        opts_fixed = options
      )

      for (backend in c("base", "altrep")) {
        inputs <- locate_fixed_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- locate_fixed_frame_invoke(
          backend, inputs,
          all = call$all,
          omit_no_match = call$omit,
          get_length = call$length,
          opts_fixed = options
        )

        expect_identical(actual, oracle, info = backend)
        expect_locate_fixed_frame_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("fixed location preserves condition order", {
  bytes <- locate_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = c(bytes, "a", "b"), pattern = c("a", "")),
    list(subject = "abc", pattern = bytes),
    list(
      subject = bytes, pattern = "a",
      options = list(unknown_option = TRUE)
    )
  )

  for (scenario in scenarios) {
    for (all in c(FALSE, TRUE)) {
      oracle_inputs <- locate_fixed_frame_inputs(
        "stringi", scenario$subject, scenario$pattern
      )
      oracle <- locate_fixed_frame_capture(locate_fixed_frame_invoke(
        "stringi", oracle_inputs, all = all,
        opts_fixed = scenario$options
      ))

      for (backend in c("base", "altrep")) {
        inputs <- locate_fixed_frame_inputs(
          backend, scenario$subject, scenario$pattern
        )
        actual <- locate_fixed_frame_capture(locate_fixed_frame_invoke(
          backend, inputs, all = all,
          opts_fixed = scenario$options
        ))

        expect_identical(actual$events, oracle$events, info = backend)
        expect_identical(actual$value, oracle$value, info = backend)
        expect_locate_fixed_frame_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("fixed location does not inspect zero-recycled inputs", {
  bytes <- locate_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character()),
    list(subject = character(), pattern = "")
  )

  for (case in cases) {
    for (all in c(FALSE, TRUE)) {
      for (backend in c("stringi", "base", "altrep")) {
        inputs <- locate_fixed_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- locate_fixed_frame_capture(locate_fixed_frame_invoke(
          backend, inputs, all = all
        ))

        expect_identical(actual$events, character(), info = backend)
        expect_length(actual$value, 0)
        expect_locate_fixed_frame_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("fixed location warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- locate_fixed_frame_inputs(backend, c("abc", "def"), "")
    expect_error(
      locate_fixed_frame_invoke(backend, invalid, all = TRUE),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_locate_fixed_frame_unmaterialized(backend, invalid)

    valid <- locate_fixed_frame_inputs(
      backend, c("ééé", "none"), "éé"
    )
    oracle <- locate_fixed_frame_invoke(
      "stringi",
      locate_fixed_frame_inputs(
        "stringi", c("ééé", "none"), "éé"
      ),
      all = TRUE,
      opts_fixed = list(overlap = TRUE)
    )
    expect_identical(
      locate_fixed_frame_invoke(
        backend, valid, all = TRUE,
        opts_fixed = list(overlap = TRUE)
      ),
      oracle,
      info = backend
    )
    expect_locate_fixed_frame_unmaterialized(backend, valid)
  }
})


test_that("fixed location warnings permit ALTREP reentry", {
  inputs <- locate_fixed_frame_inputs(
    "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
  )
  scalar_pattern <- charport::as_charvec("a")
  reentered <- NULL

  actual <- locate_fixed_frame_capture(
    locate_fixed_frame_invoke("altrep", inputs, all = TRUE),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_locate_first_fixed(
        inputs$subject, scalar_pattern
      )
    }
  )
  expected <- locate_fixed_frame_capture(
    locate_fixed_frame_invoke(
      "stringi",
      locate_fixed_frame_inputs(
        "stringi", c("aaaa", "bbbb", "cccc"), c("a", "b")
      ),
      all = TRUE
    )
  )

  expect_length(actual$events, 1)
  expect_length(expected$events, 1)
  expect_identical(actual$value, expected$value)
  expect_identical(
    reentered,
    locate_fixed_frame_invoke(
      "stringi",
      locate_fixed_frame_inputs(
        "stringi", c("aaaa", "bbbb", "cccc"), "a"
      )
    )
  )
  expect_locate_fixed_frame_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})
