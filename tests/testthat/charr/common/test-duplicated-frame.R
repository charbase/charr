duplicated_frame_symbol <- function(backend) {
  namespace <- asNamespace("charr")
  name <- if (identical(backend, "base")) {
    "C_charr_base_ci_duplicated"
  } else if (identical(backend, "altrep")) {
    "C_ci_duplicated"
  } else {
    stop("unknown duplicated backend", call. = FALSE)
  }

  get(name, envir = namespace, inherits = FALSE)
}


duplicated_frame_call <- function(
    backend, string, from_last = FALSE, opts_collator = NULL
) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_duplicated(
      string,
      from_last = from_last,
      opts_collator = opts_collator
    ))
  }

  .Call(
    duplicated_frame_symbol(backend),
    string,
    from_last,
    opts_collator
  )
}


duplicated_frame_input <- function(backend, value) {
  if (identical(backend, "altrep") && is.character(value)) {
    return(charport::as_charvec(value))
  }
  value
}


duplicated_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


duplicated_frame_capture <- function(expr) {
  events <- character()
  normalized_message <- function(condition) {
    sub(
      "?ci_opts_collator", "?stri_opts_collator",
      conditionMessage(condition), fixed = TRUE
    )
  }
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", normalized_message(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", normalized_message(condition)))
      NULL
    }
  )
  list(value = value, events = events)
}


expect_duplicated_frame_unmaterialized <- function(backend, input) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(input))
  expect_false(charport::charport_info(input)$is_materialized)
  invisible(NULL)
}


test_that("duplicated preserves traversal and collation semantics", {
  traversal <- c("a", "b", "a", NA_character_, "a", NA_character_)
  expected <- list(
    forward = c(FALSE, FALSE, TRUE, FALSE, TRUE, TRUE),
    reverse = c(TRUE, FALSE, TRUE, TRUE, FALSE, FALSE)
  )

  for (from_last in c(FALSE, TRUE)) {
    direction <- if (from_last) "reverse" else "forward"
    oracle <- duplicated_frame_call(
      "stringi", traversal, from_last = from_last
    )
    expect_identical(oracle, expected[[direction]])

    for (backend in c("base", "altrep")) {
      input <- duplicated_frame_input(backend, traversal)
      actual <- duplicated_frame_call(
        backend, input, from_last = from_last
      )

      expect_identical(actual, oracle, info = paste(backend, direction))
      expect_identical(typeof(actual), "logical")
      expect_null(attributes(actual))
      expect_false(charport::is_charvec(actual))
      expect_duplicated_frame_unmaterialized(backend, input)
    }
  }

  cases <- list(
    list(
      values = c(
        "\u00e4", "a\u0308", "A", "a", "2", "02", "10", "010",
        NA_character_, NA_character_
      ),
      options = list(locale = "de", strength = 1L, numeric = TRUE)
    ),
    list(
      values = c(
        duplicated_frame_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1"),
        "caf\u00e9",
        duplicated_frame_marked(
          c(0xef, 0xbb, 0xbf, 0x61, 0x62, 0x63), "UTF-8"
        ),
        "abc",
        duplicated_frame_marked(c(0x61, 0xff, 0x62), "UTF-8"),
        duplicated_frame_marked(c(0x61, 0xff, 0x62), "UTF-8"),
        duplicated_frame_marked(c(0x61, 0xfe, 0x62), "UTF-8"),
        NA_character_, NA_character_
      ),
      options = list(locale = "en", strength = 3L, normalization = TRUE)
    )
  )

  for (case in cases) {
    for (from_last in c(FALSE, TRUE)) {
      oracle <- duplicated_frame_call(
        "stringi",
        case$values,
        from_last = from_last,
        opts_collator = case$options
      )

      for (backend in c("base", "altrep")) {
        input <- duplicated_frame_input(backend, case$values)
        actual <- duplicated_frame_call(
          backend,
          input,
          from_last = from_last,
          opts_collator = case$options
        )

        expect_identical(actual, oracle, info = backend)
        expect_identical(typeof(actual), "logical")
        expect_null(attributes(actual))
        expect_duplicated_frame_unmaterialized(backend, input)
      }
    }
  }
})


test_that("duplicated preserves coercion and drops input attributes", {
  sources <- list(
    factor(c("a", "a", "b")),
    structure(
      c("a", "a", "b"),
      names = c("first", "second", "third"),
      class = c("duplicated_input", "character"),
      source_tag = "kept only on the input"
    ),
    1:4
  )

  for (source in sources) {
    oracle <- duplicated_frame_call("stringi", source)
    expect_null(attributes(oracle))

    for (backend in c("base", "altrep")) {
      actual <- duplicated_frame_call(backend, source)
      expect_identical(actual, oracle, info = backend)
      expect_null(attributes(actual))
    }
  }

  nested <- list(c("a", "a"))
  oracle <- duplicated_frame_capture(
    duplicated_frame_call("stringi", nested)
  )
  for (backend in c("base", "altrep")) {
    actual <- duplicated_frame_capture(
      duplicated_frame_call(backend, nested)
    )
    expect_identical(actual, oracle, info = backend)
  }
})


test_that("duplicated preserves staged condition order", {
  bytes <- duplicated_frame_marked(c(0xff, 0xfe), "bytes")
  bad_options <- list(not_a_collator_option = TRUE)
  scenarios <- list(
    function(backend) duplicated_frame_call(
      backend, new.env(parent = emptyenv()),
      from_last = logical(), opts_collator = bad_options
    ),
    function(backend) duplicated_frame_call(
      backend, duplicated_frame_input(backend, bytes),
      from_last = logical(), opts_collator = bad_options
    ),
    function(backend) duplicated_frame_call(
      backend, duplicated_frame_input(backend, bytes),
      from_last = c(FALSE, TRUE), opts_collator = bad_options
    ),
    function(backend) duplicated_frame_call(
      backend, duplicated_frame_input(backend, bytes),
      from_last = FALSE, opts_collator = TRUE
    ),
    function(backend) duplicated_frame_call(
      backend, duplicated_frame_input(backend, character()),
      from_last = FALSE, opts_collator = bad_options
    )
  )

  for (scenario in scenarios) {
    oracle <- duplicated_frame_capture(scenario("stringi"))
    for (backend in c("base", "altrep")) {
      actual <- duplicated_frame_capture(scenario(backend))
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("duplicated recovers after R errors at its unwind boundary", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  bytes <- duplicated_frame_marked(c(0xff, 0xfe), "bytes")
  for (backend in c("base", "altrep")) {
    expect_error(
      duplicated_frame_call(
        backend,
        duplicated_frame_input(backend, c("a", "a")),
        opts_collator = list(not_a_collator_option = TRUE)
      ),
      "incorrect opts_collator setting",
      fixed = TRUE,
      info = backend
    )
    expect_identical(
      duplicated_frame_call(
        backend,
        duplicated_frame_input(backend, c("a", "a"))
      ),
      c(FALSE, TRUE),
      info = backend
    )

    failing_input <- duplicated_frame_input(backend, bytes)
    expect_error(
      duplicated_frame_call(
        backend,
        failing_input,
        opts_collator = list(locale = "en")
      ),
      "bytes encoding",
      fixed = TRUE,
      info = backend
    )
    expect_duplicated_frame_unmaterialized(backend, failing_input)
    expect_identical(
      duplicated_frame_call(
        backend,
        duplicated_frame_input(backend, c("a", "a"))
      ),
      c(FALSE, TRUE),
      info = backend
    )
  }
})
