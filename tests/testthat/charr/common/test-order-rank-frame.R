order_rank_frame_symbol <- function(backend, operation) {
  namespace <- asNamespace("charr")
  prefix <- if (identical(backend, "base")) {
    "C_charr_base_"
  } else if (identical(backend, "altrep")) {
    "C_"
  } else {
    stop("unknown order/rank backend", call. = FALSE)
  }

  get(
    paste0(prefix, "ci_", operation),
    envir = namespace,
    inherits = FALSE
  )
}


order_frame_call <- function(
    backend, string, decreasing = FALSE, na_last = TRUE,
    opts_collator = NULL
) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_order(
      string,
      decreasing = decreasing,
      na_last = na_last,
      opts_collator = opts_collator
    ))
  }

  .Call(
    order_rank_frame_symbol(backend, "order"),
    string,
    decreasing,
    na_last,
    opts_collator
  )
}


rank_frame_call <- function(backend, string, opts_collator = NULL) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_rank(
      string,
      opts_collator = opts_collator
    ))
  }

  .Call(
    order_rank_frame_symbol(backend, "rank"),
    string,
    opts_collator
  )
}


order_rank_frame_input <- function(backend, value) {
  if (identical(backend, "altrep") && is.character(value)) {
    return(charport::as_charvec(value))
  }
  value
}


order_rank_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


order_rank_frame_capture <- function(expr) {
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


expect_order_rank_frame_unmaterialized <- function(backend, input) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(input))
  expect_false(charport::charport_info(input)$is_materialized)
  invisible(NULL)
}


expect_order_rank_frame_output <- function(output) {
  expect_identical(typeof(output), "integer")
  expect_null(attributes(output))
  expect_false(charport::is_charvec(output))
  invisible(NULL)
}


test_that("order preserves stable ties, direction, and every NA mode", {
  values <- c("b", "A", "a", "B", NA_character_, NA_character_)
  options <- list(locale = "en", strength = 1L)
  expected <- list(
    increasing_first = c(5L, 6L, 2L, 3L, 1L, 4L),
    increasing_last = c(2L, 3L, 1L, 4L, 5L, 6L),
    increasing_drop = c(2L, 3L, 1L, 4L),
    decreasing_first = c(5L, 6L, 1L, 4L, 2L, 3L),
    decreasing_last = c(1L, 4L, 2L, 3L, 5L, 6L),
    decreasing_drop = c(1L, 4L, 2L, 3L)
  )

  for (decreasing in c(FALSE, TRUE)) {
    direction <- if (decreasing) "decreasing" else "increasing"
    for (na_last in list(FALSE, TRUE, NA)) {
      placement <- if (is.na(na_last)) {
        "drop"
      } else if (na_last) {
        "last"
      } else {
        "first"
      }
      case <- paste(direction, placement, sep = "_")
      oracle <- order_frame_call(
        "stringi", values,
        decreasing = decreasing,
        na_last = na_last,
        opts_collator = options
      )
      expect_identical(oracle, expected[[case]])

      for (backend in c("base", "altrep")) {
        input <- order_rank_frame_input(backend, values)
        actual <- order_frame_call(
          backend, input,
          decreasing = decreasing,
          na_last = na_last,
          opts_collator = options
        )

        expect_identical(actual, oracle, info = paste(backend, case))
        expect_order_rank_frame_output(actual)
        expect_order_rank_frame_unmaterialized(backend, input)
      }
    }
  }
})


test_that("rank uses minimum ranks with gaps", {
  values <- c("b", "A", "a", "c", NA_character_)
  options <- list(locale = "en", strength = 1L)
  oracle <- rank_frame_call("stringi", values, options)
  expect_identical(oracle, c(3L, 1L, 1L, 4L, NA_integer_))

  for (backend in c("base", "altrep")) {
    input <- order_rank_frame_input(backend, values)
    actual <- rank_frame_call(backend, input, options)

    expect_identical(actual, oracle, info = backend)
    expect_order_rank_frame_output(actual)
    expect_order_rank_frame_unmaterialized(backend, input)
  }
})


test_that("order and rank preserve encoding and malformed-input semantics", {
  latin1 <- order_rank_frame_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  bom <- order_rank_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61, 0x62, 0x63), "UTF-8"
  )
  malformed1 <- order_rank_frame_marked(c(0x61, 0xff, 0x62), "UTF-8")
  malformed2 <- order_rank_frame_marked(c(0x61, 0xfe, 0x62), "UTF-8")
  values <- c(
    latin1, "caf\u00e9", bom, "abc", malformed1, malformed2,
    NA_character_
  )
  options <- list(locale = "en", strength = 3L, normalization = TRUE)

  calls <- list(
    order = function(backend, input) {
      order_frame_call(
        backend, input, na_last = NA, opts_collator = options
      )
    },
    rank = function(backend, input) {
      rank_frame_call(backend, input, opts_collator = options)
    }
  )

  for (operation in names(calls)) {
    oracle <- calls[[operation]]("stringi", values)
    expect_order_rank_frame_output(oracle)

    for (backend in c("base", "altrep")) {
      input <- order_rank_frame_input(backend, values)
      actual <- calls[[operation]](backend, input)

      expect_identical(actual, oracle, info = paste(backend, operation))
      expect_order_rank_frame_output(actual)
      expect_order_rank_frame_unmaterialized(backend, input)
    }
  }
})


test_that("order and rank preserve coercion and drop input attributes", {
  sources <- list(
    factor(c("b", "a", "a")),
    structure(
      c("b", "a", "a"),
      names = c("first", "second", "third"),
      class = c("order_rank_input", "character"),
      source_tag = "kept only on the input"
    ),
    3:1
  )
  calls <- list(order = order_frame_call, rank = rank_frame_call)

  for (operation in names(calls)) {
    call <- calls[[operation]]
    for (source in sources) {
      oracle <- call("stringi", source)
      expect_order_rank_frame_output(oracle)

      for (backend in c("base", "altrep")) {
        actual <- call(backend, source)
        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_order_rank_frame_output(actual)
      }
    }
  }
})


test_that("order and rank preserve exact staged condition order", {
  bytes <- order_rank_frame_marked(c(0xff, 0xfe), "bytes")
  bad_options <- list(not_a_collator_option = TRUE)
  scenarios <- list(
    function(backend) order_frame_call(
      backend, new.env(parent = emptyenv()),
      decreasing = logical(), na_last = logical(),
      opts_collator = bad_options
    ),
    function(backend) order_frame_call(
      backend, new.env(parent = emptyenv()),
      decreasing = FALSE, na_last = logical(),
      opts_collator = bad_options
    ),
    function(backend) order_frame_call(
      backend, new.env(parent = emptyenv()),
      decreasing = FALSE, na_last = TRUE,
      opts_collator = bad_options
    ),
    function(backend) order_frame_call(
      backend, order_rank_frame_input(backend, bytes),
      opts_collator = bad_options
    ),
    function(backend) order_frame_call(
      backend, order_rank_frame_input(backend, character()),
      opts_collator = TRUE
    ),
    function(backend) rank_frame_call(
      backend, new.env(parent = emptyenv()),
      opts_collator = bad_options
    ),
    function(backend) rank_frame_call(
      backend, order_rank_frame_input(backend, bytes),
      opts_collator = bad_options
    ),
    function(backend) rank_frame_call(
      backend, order_rank_frame_input(backend, character()),
      opts_collator = bad_options
    )
  )

  for (scenario in scenarios) {
    oracle <- order_rank_frame_capture(scenario("stringi"))
    for (backend in c("base", "altrep")) {
      actual <- order_rank_frame_capture(scenario(backend))
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("order and rank recover after R errors at their unwind boundaries", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  bytes <- order_rank_frame_marked(c(0xff, 0xfe), "bytes")
  values <- c("b", "a", "a", NA_character_)
  successful <- list(order = c(2L, 3L, 1L, 4L), rank = c(3L, 1L, 1L, NA_integer_))
  calls <- list(
    order = function(backend, input, options = NULL) {
      order_frame_call(backend, input, opts_collator = options)
    },
    rank = function(backend, input, options = NULL) {
      rank_frame_call(backend, input, opts_collator = options)
    }
  )

  for (operation in names(calls)) {
    call <- calls[[operation]]
    for (backend in c("base", "altrep")) {
      expect_error(
        call(
          backend,
          order_rank_frame_input(backend, values),
          list(not_a_collator_option = TRUE)
        ),
        "incorrect opts_collator setting",
        fixed = TRUE,
        info = paste(backend, operation)
      )
      expect_identical(
        call(backend, order_rank_frame_input(backend, values)),
        successful[[operation]],
        info = paste(backend, operation)
      )

      failing_input <- order_rank_frame_input(backend, bytes)
      expect_error(
        call(backend, failing_input),
        "bytes encoding",
        fixed = TRUE,
        info = paste(backend, operation)
      )
      expect_order_rank_frame_unmaterialized(backend, failing_input)
      expect_identical(
        call(backend, order_rank_frame_input(backend, values)),
        successful[[operation]],
        info = paste(backend, operation)
      )
    }
  }
})
