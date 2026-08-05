# The three threading settings live in native code, not in R options. The
# accessor validates what it is given; the entry point behind it still has to
# survive a direct .Call from package code.

with_threads <- function(threads, code) {
  old <- charr_threads(threads)
  on.exit(charr_threads(old), add = TRUE)
  force(code)
}

thread_option_sample <- function() {
  c(
    "plain ascii text", "caf\u00e9 na\u00efve", "\u65e5\u672c\u8a9e",
    "\U0001f642\U0001f468", "", NA_character_
  )
}

test_that("charr_threads validates values it sets", {
  expect_identical(with_threads(2, charr_threads()), 2L)
  expect_error(charr_threads(0), "charr_threads")
  expect_error(charr_threads(-1), "charr_threads")
  expect_error(charr_threads(1.5), "charr_threads")
  expect_error(charr_threads("two"), "charr_threads")
  expect_error(charr_threads(c(2, 4)), "charr_threads")
  expect_error(charr_threads(NA_integer_), "charr_threads")
})

test_that("the chunking accessors validate values they set", {
  old <- charr_chunks_per_worker(64)
  on.exit(charr_chunks_per_worker(old), add = TRUE)
  expect_identical(charr_chunks_per_worker(), 64L)
  expect_error(charr_chunks_per_worker(0), "charr_chunks_per_worker")
  expect_error(charr_chunks_per_worker(2.5), "charr_chunks_per_worker")
  expect_error(charr_chunks_per_worker(NA), "charr_chunks_per_worker")

  previous <- charr_min_chunk(16)
  on.exit(charr_min_chunk(previous), add = TRUE)
  expect_identical(charr_min_chunk(), 16L)
  expect_error(charr_min_chunk(0), "charr_min_chunk")
  expect_error(charr_min_chunk(c(1, 2)), "charr_min_chunk")
})

test_that("a thread count above the internal limit is clamped, not refused", {
  old <- charr_threads(1000)
  on.exit(charr_threads(old), add = TRUE)
  expect_identical(charr_threads(), 256L)
})

test_that("the option of the same name is inert", {
  values <- thread_option_sample()
  # The suite runs the altrep backend at several thread counts, so the count
  # to preserve is whichever one is ambient rather than a fixed number.
  ambient <- charr_threads()
  expected <- with_threads(1, str_width(values))

  old <- options(charr_threads = ambient + 4L)
  on.exit(options(old), add = TRUE)
  expect_identical(charr_threads(), ambient)
  expect_identical(str_width(values), expected)
})

test_that("a direct .Call with an unusable value leaves the setting alone", {
  values <- thread_option_sample()
  expected <- with_threads(1, str_width(values))

  old <- charr_threads(2)
  on.exit(charr_threads(old), add = TRUE)

  invalid <- list(
    "nonsense", NA_integer_, -3L, c(2L, 4L), integer(),
    NA_real_, NaN, 0, list(2), NULL
  )
  for (value in invalid) {
    expect_identical(.Call(charr:::C_charr_threads, value), 2L)
    expect_identical(charr_threads(), 2L)
  }

  expect_identical(with_threads(1, str_width(values)), expected)
})
