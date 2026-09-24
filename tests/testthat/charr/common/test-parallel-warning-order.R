# A failing threaded run must raise the warnings a serial run raises before the
# same error: those of the tasks below the failing one, and none after it.
# Workers claim chunks in whatever order the scheduler allows, so these runs
# repeat each call with one task per chunk and compare every repetition with
# the serial answer of the same backend.

parallel_warning_events <- function(fun) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      fun(),
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
  events
}

expect_parallel_warnings_match_serial <- function(fun, repeats = 100L) {
  old_threads <- charr_threads(1)
  old_chunks <- charr_chunks_per_worker()
  old_minimum <- charr_min_chunk()
  on.exit({
    charr_threads(old_threads)
    charr_chunks_per_worker(old_chunks)
    charr_min_chunk(old_minimum)
  }, add = TRUE)

  serial <- parallel_warning_events(fun)
  expect_true(any(startsWith(serial, "error:")))

  charr_threads(4)
  charr_chunks_per_worker(1000)
  charr_min_chunk(1)
  mismatches <- 0L
  for (i in seq_len(repeats)) {
    if (!identical(parallel_warning_events(fun), serial))
      mismatches <- mismatches + 1L
  }
  expect_identical(mismatches, 0L)
}

test_that("regex locate and match warn in serial order before an error", {
  subjects <- rep("abc", 256L)
  # Every lane before the invalid one warns; a serial run stops at it.
  before <- c(rep("", 255L), "(")
  # The first lane fails; no lane after it may add a warning.
  after <- c("(", rep("", 255L))

  leaves <- c(
    "ci_locate_all_regex", "ci_locate_first_regex",
    "ci_match_all_regex", "ci_match_first_regex"
  )
  for (leaf in leaves) {
    fun <- charr_test_leaf(leaf)
    expect_parallel_warnings_match_serial(function() fun(subjects, before))
    expect_parallel_warnings_match_serial(function() fun(subjects, after))
  }
})

test_that("boundary split raises the fallback warning in serial order", {
  split <- charr_test_leaf("ci_split_boundaries")
  fallback <- list(type = "word", locale = "xx_YY")
  big <- .Machine$integer.max

  # The iterator opens, and warns, on the first element; the second fails.
  expect_parallel_warnings_match_serial(function() {
    split(c("alpha", "beta"), n = c(1L, big), opts_brkiter = fallback)
  })
  # The first element fails before any iterator opens, so a serial run
  # raises no warning even though later elements would have opened one.
  expect_parallel_warnings_match_serial(function() {
    split(
      rep(c("alpha", "beta"), 8L), n = c(big, rep(1L, 15L)),
      opts_brkiter = fallback
    )
  })
})

test_that("sequential regex replacement drops worker warnings on error", {
  replace <- charr_test_leaf("ci_replace_all_regex")
  # With vectorize_all = FALSE each chunk applies the whole pattern list, so a
  # chunk that reaches the empty pattern counts it again. The group reference
  # fails only on a subject that matches, which a serial run reaches while
  # applying the first pattern, before the empty one.
  subjects <- c(rep("zz", 255L), "a")
  for (values in list(subjects, rev(subjects))) {
    expect_parallel_warnings_match_serial(function() {
      replace(values, c("a", ""), c("$1", "y"), vectorize_all = FALSE)
    })
  }
})
