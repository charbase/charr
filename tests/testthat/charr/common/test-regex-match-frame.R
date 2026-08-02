regex_match_frame_conditions <- function(expr) {
  warnings <- character()
  value <- withCallingHandlers(
    force(expr),
    warning = function(condition) {
      warnings <<- c(warnings, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = warnings)
}


test_that("regex match preserves zero-output pattern handling", {
  named_pattern <- "(?<left>a)(?<right>b)?"
  expect_identical(
    charr_test_leaf("ci_match_first_regex")(character(), named_pattern),
    stringi::stri_match_first_regex(character(), named_pattern)
  )
  expect_error(
    charr_test_leaf("ci_match_first_regex")(character(), "["),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )

  expect_identical(
    charr_test_leaf("ci_match_all_regex")(character(), "["),
    stringi::stri_match_all_regex(character(), "[")
  )
})


test_that("regex match preserves empty-pattern warning counts", {
  first_subject <- c("a", "b", NA_character_, "c")
  expect_identical(
    regex_match_frame_conditions(
      charr_test_leaf("ci_match_first_regex")(first_subject, "")
    ),
    regex_match_frame_conditions(
      stringi::stri_match_first_regex(first_subject, "")
    )
  )

  patterns <- c("", "(?<letter>a)")
  expect_identical(
    regex_match_frame_conditions(
      charr_test_leaf("ci_match_first_regex")(character(), patterns)
    ),
    regex_match_frame_conditions(
      stringi::stri_match_first_regex(character(), patterns)
    )
  )
  expect_identical(
    regex_match_frame_conditions(
      charr_test_leaf("ci_match_all_regex")(character(), patterns)
    ),
    regex_match_frame_conditions(
      stringi::stri_match_all_regex(character(), patterns)
    )
  )
})
