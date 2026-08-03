test_that("R's Windows parser silently corrupts mixed supplementary literals", {
  windows <- identical(.Platform$OS.type, "windows")
  skip_if(
    !windows && !isTRUE(l10n_info()[["UTF-8"]]),
    "The non-Windows parser control requires a UTF-8 LC_CTYPE locale"
  )

  # This fixture deliberately uses the source spelling forbidden by
  # check-r-unicode-literals.R. Keeping it as text prevents testthat from
  # parsing it before this test can inspect the result.
  source_path <- test_path("windows-mixed-unicode-literal.txt")
  expect_silent(
    parsed <- eval(
      parse(file = source_path, encoding = "UTF-8"),
      envir = baseenv()
    )
  )
  intended <- paste0(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466",
    "e\u0301"
  )

  if (windows) {
    # Windows R releases disagree on the corrupt value. R 4.1 decodes the
    # UTF-8 source bytes through the active code page; newer releases replace
    # the raw supplementary characters with U+FFFD. Both do this before charr
    # or charport sees the string and without signaling a condition. If R
    # preserves the intended value in the future, revisit the literal lint.
    expect_false(
      identical(utf8ToInt(parsed), utf8ToInt(intended))
    )
  } else {
    expect_identical(charToRaw(parsed), charToRaw(intended))
  }
})
