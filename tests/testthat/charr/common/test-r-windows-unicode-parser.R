test_that("R's Windows parser silently replaces mixed supplementary literals", {
  # This fixture deliberately uses the source spelling forbidden by
  # check-r-unicode-literals.R. Keeping it as text prevents testthat from
  # parsing it before this test can inspect the result.
  source_path <- test_path("windows-mixed-unicode-literal.txt")
  parsed <- eval(
    parse(file = source_path, encoding = "UTF-8"),
    envir = baseenv()
  )
  intended <- paste0(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466",
    "e\u0301"
  )

  if (identical(.Platform$OS.type, "windows")) {
    # Windows R currently replaces each raw supplementary character with
    # U+FFFD when the same literal contains a Unicode escape. The parser does
    # this silently, before charr or charport receives the value. The exact
    # assertion is intentional: parse() does not signal an error or warning.
    # If R changes this behavior, revisit the source-literal lint as well.
    expect_identical(
      utf8ToInt(parsed),
      c(
        0xfffdL, 0x200dL, 0xfffdL, 0x200dL, 0xfffdL,
        0x200dL, 0xfffdL, 0x0065L, 0x0301L
      )
    )
  } else {
    expect_identical(charToRaw(parsed), charToRaw(intended))
  }
})
