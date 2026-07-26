test_that("regex detect agrees across optimized UTF-8 inputs", {
  values <- c(
    "CAFÉ", "Δέλτα", "東京", "🙂\nZ", "123", NA_character_, ""
  )
  pattern <- regex(
    "^café$|^\\p{Greek}+$|^\\p{Han}+$|^z$",
    ignore_case = TRUE,
    multiline = TRUE
  )
  expected <- c(TRUE, TRUE, TRUE, TRUE, FALSE, NA, FALSE)

  base_result <- with_backend("base", str_detect(values, pattern))

  altrep_input <- charport::as_charvec(values)
  expect_true(charport::is_charvec(altrep_input))
  expect_false(charport::charport_info(altrep_input)$is_materialized)
  altrep_result <- with_backend("altrep", str_detect(altrep_input, pattern))

  expect_identical(base_result, expected)
  expect_identical(altrep_result, base_result)
  expect_false(charport::charport_info(altrep_input)$is_materialized)
})
