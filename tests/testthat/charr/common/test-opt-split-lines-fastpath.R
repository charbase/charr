test_that("optimized line splitting retains trailing empty fields", {
  values <- c("a\n", "\r", "x\r\n", "", NA_character_)

  for (omit_empty in list(FALSE, TRUE, NA)) {
    operation <- function(x) charr:::ci_split_lines(x, omit_empty)
    expected <- with_backend("stringi", operation(values))

    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    actual <- with_backend("altrep", operation(input))
    expect_identical(actual, expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }
})
