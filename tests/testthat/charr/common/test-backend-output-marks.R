expect_output_marks <- function(actual, expected) {
  value <- actual
  expect_altrep_unmaterialized(value)
  expect_identical(Encoding(as.character(value)), Encoding(expected))
}

test_that("generated and sliced output marks match stringi", {
  values <- c("abc", "caf\u00e9", "")
  normalized <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_trans_nfc")(charport::as_charvec(values))
  )
  expect_output_marks(normalized, stringi::stri_trans_nfc(values))

  escaped <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_escape_unicode")(charport::as_charvec(values))
  )
  expect_output_marks(escaped, stringi::stri_escape_unicode(values))

  sliced <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_extract_all_fixed")(
      charport::as_charvec(c("abc", "caf\u00e9")),
      charport::as_charvec(c("b", "\u00e9"))
    )
  )
  expected_sliced <- stringi::stri_extract_all_fixed(
    c("abc", "caf\u00e9"), c("b", "\u00e9")
  )
  for (i in seq_along(sliced))
    expect_output_marks(sliced[[i]], expected_sliced[[i]])
})
