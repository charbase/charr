output_mark_read <- function(value) {
  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)

  marks <- .Call(charr:::C_ci_enc_mark, value)

  expect_false(charport::charport_info(value)$is_materialized)
  expect_true(charport::is_charvec(marks))
  expect_false(charport::charport_info(marks)$is_materialized)
  marks
}

expect_output_marks <- function(actual, expected) {
  expect_identical(
    output_mark_read(actual),
    stringi::stri_enc_mark(expected)
  )
}

test_that("whole, generated, and sliced output marks match stringi", {
  owned <- .Call(charr:::C_ci_test_Utf8Record_views)
  expect_output_marks(owned, c("owned", NA_character_, "borrowed"))

  values <- c("abc", "caf\u00e9", "")
  normalized <- with_altrep(
    TRUE,
    charr:::ci_trans_nfc(charport::as_charvec(values))
  )
  expect_output_marks(normalized, stringi::stri_trans_nfc(values))

  escaped <- with_altrep(
    TRUE,
    charr:::ci_escape_unicode(charport::as_charvec(values))
  )
  expect_output_marks(escaped, stringi::stri_escape_unicode(values))

  sliced <- with_altrep(
    TRUE,
    charr:::ci_extract_all_fixed(
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
