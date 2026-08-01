replace_coll_frame_symbol <- function(backend, operation) {
  namespace <- asNamespace("charr")
  prefix <- if (identical(backend, "base")) {
    "C_charr_base_ci_"
  } else if (identical(backend, "altrep")) {
    "C_ci_"
  } else {
    stop("unknown collation backend", call. = FALSE)
  }
  get(
    paste0(prefix, "replace_", operation, "_coll"),
    envir = namespace,
    inherits = FALSE
  )
}


replace_coll_frame_call <- function(
    backend, operation, subject, pattern, replacement,
    vectorize_all = TRUE, opts_collator = NULL
) {
  if (identical(backend, "stringi")) {
    if (identical(operation, "first")) {
      return(stringi::stri_replace_first_coll(
        subject, pattern, replacement,
        opts_collator = opts_collator
      ))
    }
    return(stringi::stri_replace_all_coll(
      subject, pattern, replacement,
      vectorize_all = vectorize_all,
      opts_collator = opts_collator
    ))
  }

  symbol <- replace_coll_frame_symbol(backend, operation)
  if (identical(operation, "first")) {
    return(.Call(
      symbol, subject, pattern, replacement, opts_collator
    ))
  }
  .Call(
    symbol, subject, pattern, replacement,
    vectorize_all, opts_collator
  )
}


replace_coll_frame_inputs <- function(
    backend, subject, pattern, replacement
) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
    replacement <- charport::as_charvec(replacement)
  }
  list(
    subject = subject,
    pattern = pattern,
    replacement = replacement
  )
}


replace_coll_frame_invoke <- function(
    backend, operation, inputs,
    vectorize_all = TRUE, opts_collator = NULL
) {
  replace_coll_frame_call(
    backend, operation,
    inputs$subject, inputs$pattern, inputs$replacement,
    vectorize_all = vectorize_all,
    opts_collator = opts_collator
  )
}


replace_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


replace_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_replace_coll_frame_shape <- function(backend, inputs, output = NULL) {
  if (identical(backend, "base") && !is.null(output)) {
    expect_false(charport::is_charvec(output))
  }
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  for (input in inputs) {
    expect_true(charport::is_charvec(input))
    expect_false(charport::charport_info(input)$is_materialized)
  }
  if (!is.null(output)) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  }
  invisible(NULL)
}


test_that("collation replacement preserves Frame results and encodings", {
  malformed <- replace_coll_frame_marked(c(0x61, 0xff, 0x62), "UTF-8")
  latin1 <- replace_coll_frame_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  subject <- c(
    "\U0001f600\u00e4-a-A", "u\u0308x\u00dc", latin1,
    paste0("\ufeff", "abc"), malformed, "", NA_character_
  )
  pattern <- c("a", "\u00fc", "\u00e9", "a", "b", "x", "a")
  replacement <- c(
    "$1", "Z", NA_character_, "Q", "R", "S", "T"
  )
  options <- list(locale = "de", strength = 1L, normalization = TRUE)

  for (operation in c("first", "all")) {
    oracle <- replace_coll_frame_call(
      "stringi", operation, subject, pattern, replacement,
      opts_collator = options
    )
    for (backend in c("base", "altrep")) {
      inputs <- replace_coll_frame_inputs(
        backend, subject, pattern, replacement
      )
      actual <- replace_coll_frame_invoke(
        backend, operation, inputs,
        opts_collator = options
      )

      expect_identical(actual, oracle, info = paste(backend, operation))
      expect_identical(
        lapply(actual, function(value) {
          if (is.na(value)) NULL else charToRaw(value)
        }),
        lapply(oracle, function(value) {
          if (is.na(value)) NULL else charToRaw(value)
        }),
        info = paste(backend, operation)
      )
      expect_identical(
        Encoding(actual), Encoding(oracle),
        info = paste(backend, operation)
      )
      expect_replace_coll_frame_shape(backend, inputs, actual)
    }
  }
})


test_that("sequential collation replacement preserves traversal", {
  subject <- c("\u00e4-a-A", "u\u0308-\u00dc", "\u00e5-aa", "none", "", NA)
  pattern <- c("\u00e4", "a", "\u00dc")
  replacement <- c("X", "Y", "Z")
  options <- list(locale = "de", strength = 3L)

  oracle <- replace_coll_frame_call(
    "stringi", "all", subject, pattern, replacement,
    vectorize_all = FALSE, opts_collator = options
  )
  for (backend in c("base", "altrep")) {
    inputs <- replace_coll_frame_inputs(
      backend, subject, pattern, replacement
    )
    actual <- replace_coll_frame_invoke(
      backend, "all", inputs,
      vectorize_all = FALSE, opts_collator = options
    )
    expect_identical(actual, oracle, info = backend)
    expect_replace_coll_frame_shape(backend, inputs, actual)
  }

  missing_pattern <- c("z", NA_character_)
  oracle_missing <- replace_coll_frame_call(
    "stringi", "all", subject, missing_pattern, c("x", "y"),
    vectorize_all = FALSE
  )
  for (backend in c("base", "altrep")) {
    inputs <- replace_coll_frame_inputs(
      backend, subject, missing_pattern, c("x", "y")
    )
    actual <- replace_coll_frame_invoke(
      backend, "all", inputs, vectorize_all = FALSE
    )
    expect_identical(actual, oracle_missing, info = backend)
    expect_replace_coll_frame_shape(backend, inputs, actual)
  }
})


test_that("collation replacement keeps validation and warning order", {
  bytes <- replace_coll_frame_marked(c(0xff, 0xfe), "bytes")
  invalid_options <- structure(list(TRUE), names = "bogus")

  empty_oracle <- replace_coll_frame_capture(replace_coll_frame_call(
    "stringi", "all", character(), bytes, bytes,
    vectorize_all = FALSE,
    opts_collator = invalid_options
  ))
  expect_identical(empty_oracle, list(value = character(), events = character()))
  for (backend in c("base", "altrep")) {
    inputs <- replace_coll_frame_inputs(
      backend, character(), bytes, bytes
    )
    actual <- replace_coll_frame_capture(replace_coll_frame_invoke(
      backend, "all", inputs,
      vectorize_all = FALSE,
      opts_collator = invalid_options
    ))
    expect_identical(actual, empty_oracle, info = backend)
    expect_replace_coll_frame_shape(backend, inputs, actual$value)
  }

  scenarios <- list(
    list(
      subject = c("a", "b", "c"),
      pattern = c("", "a"),
      replacement = bytes,
      vectorize_all = TRUE,
      options = invalid_options
    ),
    list(
      subject = "a",
      pattern = c("", "a", "b"),
      replacement = c("x", "y"),
      vectorize_all = FALSE,
      options = invalid_options
    ),
    list(
      subject = "a",
      pattern = c("a", "b"),
      replacement = c("x", "y", "z"),
      vectorize_all = FALSE,
      options = invalid_options
    )
  )

  for (scenario in scenarios) {
    oracle <- replace_coll_frame_capture(replace_coll_frame_call(
      "stringi", "all",
      scenario$subject, scenario$pattern, scenario$replacement,
      vectorize_all = scenario$vectorize_all,
      opts_collator = scenario$options
    ))
    for (backend in c("base", "altrep")) {
      inputs <- replace_coll_frame_inputs(
        backend,
        scenario$subject,
        scenario$pattern,
        scenario$replacement
      )
      actual <- replace_coll_frame_capture(replace_coll_frame_invoke(
        backend, "all", inputs,
        vectorize_all = scenario$vectorize_all,
        opts_collator = scenario$options
      ))
      expect_identical(actual, oracle, info = backend)
      expect_replace_coll_frame_shape(backend, inputs, actual$value)
    }
  }

  scalar_empty_oracle <- replace_coll_frame_capture(
    replace_coll_frame_call(
      "stringi", "all", "a", "", "x", vectorize_all = FALSE
    )
  )
  for (backend in c("base", "altrep")) {
    inputs <- replace_coll_frame_inputs(backend, "a", "", "x")
    actual <- replace_coll_frame_capture(replace_coll_frame_invoke(
      backend, "all", inputs, vectorize_all = FALSE
    ))
    expect_identical(actual, scalar_empty_oracle, info = backend)
    expect_replace_coll_frame_shape(backend, inputs, actual$value)
  }
})


test_that("collation replacement warnings leave Frames reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    warning_inputs <- replace_coll_frame_inputs(
      backend, c("a", "b", "c"), c("a", "b"), "x"
    )
    expect_error(
      replace_coll_frame_invoke(backend, "all", warning_inputs),
      "longer object length",
      info = backend
    )
    expect_replace_coll_frame_shape(backend, warning_inputs)

    valid_inputs <- replace_coll_frame_inputs(
      backend, c("a", "ba"), "a", "x"
    )
    oracle <- replace_coll_frame_call(
      "stringi", "all", c("a", "ba"), "a", "x"
    )
    actual <- replace_coll_frame_invoke(
      backend, "all", valid_inputs
    )
    expect_identical(actual, oracle, info = backend)
    expect_replace_coll_frame_shape(backend, valid_inputs, actual)
  }
})
