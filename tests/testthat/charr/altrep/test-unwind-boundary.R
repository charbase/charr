erroring_altrep_message <- "charr test ALTREP access failure"

new_erroring_altrep <- function(n = 2L) {
  .Call(charr:::C_ci_test_erroring_altrep, n)
}

expect_original_altrep_error <- function(operation) {
  errors <- lapply(c(FALSE, TRUE), function(route) {
    x <- new_erroring_altrep()
    expect_error(
      with_altrep(route, operation(x)),
      erroring_altrep_message,
      fixed = TRUE
    )
  })

  messages <- vapply(errors, conditionMessage, character(1))
  expect_identical(messages, rep(erroring_altrep_message, 2L))
  expect_identical(class(errors[[2L]]), class(errors[[1L]]))
}

test_that("Reader construction continues foreign ALTREP errors", {
  expect_original_altrep_error(function(x) charr:::ci_length(x))
})

test_that("char-output operations continue foreign ALTREP errors", {
  expect_original_altrep_error(function(x) charr:::ci_trim_both(x))
})
