test_that("str_wrap joins lines in the selected backend", {
  input <- c(text = "one two", missing = NA_character_, empty = "")
  output <- str_wrap(input, width = 3)

  expect_identical(
    is_charr_result(output),
    identical(charr_backend(), "altrep")
  )
  if (identical(charr_backend(), "altrep")) {
    expect_lazy_result(output)
  }
  expect_identical(
    output,
    c(text = "one\ntwo", missing = NA_character_, empty = "")
  )
})

test_that("dynamic wrapping rejects an overflowing work matrix", {
  skip_if(
    identical(charr_backend(), "reference"),
    "The overflow guard is specific to charr's optimized backends"
  )
  skip_if(.Machine$sizeof.pointer != 4L)

  many_words <- paste(rep.int("a", 65536L), collapse = " ")
  expect_error(
    charr_test_leaf("ci_wrap")(
      many_words,
      width = 1L,
      cost_exponent = 2,
      simplify = FALSE,
      normalize = FALSE
    ),
    "word-wrap matrix is too large"
  )
})
