test_that("optimized padding preserves ASCII width inside mixed strings", {
  delete <- rawToChar(as.raw(0x7f))
  strings <- c(
    "\ta",
    "a\nb",
    paste0("a", delete, "b"),
    "\U0001f1fa\U0001f1f8a",
    "x\u200da",
    NA_character_
  )
  widths <- c(5L, 6L, 4L, 7L, 5L, 3L)
  pads <- c("x\t", ".", "y\n", "-", "_", " ")
  operations <- list(
    charr:::ci_pad_left,
    charr:::ci_pad_right,
    charr:::ci_pad_both
  )

  for (operation in operations) {
    expected <- with_backend(
      "stringi",
      operation(strings, widths, pads, use_length = FALSE)
    )

    expect_identical(
      with_backend(
        "base",
        operation(
          charport::as_charvec(strings), widths,
          charport::as_charvec(pads), use_length = FALSE
        )
      ),
      expected
    )
    expect_identical(
      with_backend(
        "altrep",
        operation(
          charport::as_charvec(strings), widths,
          charport::as_charvec(pads), use_length = FALSE
        )
      ),
      expected
    )
  }
})
