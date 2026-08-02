compare_frame_events <- function(expr) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      force(expr),
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

compare_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    charport::as_charvec(value)
  } else {
    value
  }
}

test_that("optimized comparison preserves warnings before a native error", {
  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"
  left <- c(bytes, "a", "b")
  right <- c("a", "b")
  opts <- list(bogus = TRUE)

  expected <- compare_frame_events(
    stringi::stri_cmp_equiv(left, right, opts_collator = opts)
  )

  for (backend in c("base", "altrep")) {
    actual <- compare_frame_events(
      with_backend(
        backend,
        charr_test_leaf("ci_cmp_equiv")(
          compare_frame_input(backend, left),
          compare_frame_input(backend, right),
          opts_collator = opts
        )
      )
    )
    expect_identical(actual, expected, info = backend)
  }
})

test_that("collator option warning errors leave comparison reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    expect_error(
      with_backend(
        backend,
        charr_test_leaf("ci_cmp_equiv")("a", "a", opts_collator = list(bogus = TRUE))
      ),
      "incorrect opts_collator setting",
      fixed = TRUE,
      info = backend
    )
    expect_identical(
      with_backend(backend, charr_test_leaf("ci_cmp_equiv")("a", "a")),
      TRUE,
      info = backend
    )
  }
})
