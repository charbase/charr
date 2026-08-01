test_that("optimized line splitting retains trailing empty fields", {
  values <- c("a\n", "\r", "x\r\n", "", NA_character_)
  routines <- getDLLRegisteredRoutines(
    getLoadedDLLs()[["charr"]]
  )$.Call
  operations <- list(
    base = function(x, omit_empty) {
      .Call(routines[["C_charr_base_ci_split_lines"]], x, omit_empty)
    },
    altrep = function(x, omit_empty) {
      .Call(routines[["C_ci_split_lines"]], x, omit_empty)
    }
  )

  for (omit_empty in list(FALSE, TRUE, NA)) {
    expected <- stringi::stri_split_lines(values, omit_empty)

    expect_identical(operations$base(values, omit_empty), expected)

    input <- charport::as_charvec(values)
    actual <- operations$altrep(input, omit_empty)
    expect_identical(actual, expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }
})
