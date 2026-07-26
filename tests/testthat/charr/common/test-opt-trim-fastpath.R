test_that("optimized trim preserves no-op and empty-recycling semantics", {
  operations <- list(
    charr:::ci_trim_left,
    charr:::ci_trim_right,
    charr:::ci_trim_both
  )
  values <- c("alpha", "βeta", "中", "", NA_character_)

  bytes <- rawToChar(as.raw(0xe9))
  Encoding(bytes) <- "bytes"

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))

    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend("altrep", operation(charport::as_charvec(values))),
      expected
    )

    expect_identical(
      with_backend("base", operation(bytes, character())),
      character()
    )
    expect_identical(
      with_backend(
        "altrep",
        operation(charport::as_charvec(bytes), character())
      ),
      character()
    )
  }
})


test_that("optimized trim keeps conversion, BOM, and output-mark semantics", {
  latin1 <- iconv(" élan ", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  bom <- enc2utf8("\ufeff surrounded ")
  values <- c(latin1, bom, " éascii", "plainé ", "  中  ", NA_character_)

  cases <- list(
    list(operation = charr:::ci_trim_left),
    list(operation = charr:::ci_trim_right),
    list(operation = charr:::ci_trim_both),
    list(operation = charr:::ci_trim_left, pattern = "[a-z]"),
    list(operation = charr:::ci_trim_both, pattern = c("[a-z]", "\\P{Wspace}"))
  )

  for (case in cases) {
    args <- c(list(str = values), case[setdiff(names(case), "operation")])
    expected <- with_backend(
      "stringi",
      do.call(case$operation, args)
    )

    expect_identical(
      with_backend("base", do.call(case$operation, args)),
      expected
    )
    args$str <- charport::as_charvec(values)
    actual <- with_backend("altrep", do.call(case$operation, args))
    expect_identical(actual, expected)
    expect_identical(Encoding(actual), Encoding(expected))
  }
})


test_that("optimized trim rejects bytes even under a missing pattern", {
  bytes <- rawToChar(as.raw(0xe9))
  Encoding(bytes) <- "bytes"

  for (operation in list(
    charr:::ci_trim_left,
    charr:::ci_trim_right,
    charr:::ci_trim_both
  )) {
    expected <- tryCatch(
      with_backend("stringi", operation(bytes, NA_character_)),
      error = conditionMessage
    )
    expect_match(expected, "bytes")

    expect_error(
      with_backend("base", operation(bytes, NA_character_)),
      "bytes"
    )
    expect_error(
      with_backend(
        "altrep",
        operation(charport::as_charvec(bytes), NA_character_)
      ),
      "bytes"
    )
  }
})


test_that("optimized trim keeps malformed UTF-8 scan boundaries", {
  malformed_left <- rawToChar(as.raw(c(0xc3, 0x28, 0x20)))
  malformed_right <- rawToChar(as.raw(c(0x20, 0xc3)))
  malformed_inside <- rawToChar(
    as.raw(c(0x20, 0x61, 0xc3, 0x28, 0x62, 0x20))
  )
  Encoding(malformed_left) <- "UTF-8"
  Encoding(malformed_right) <- "UTF-8"
  Encoding(malformed_inside) <- "UTF-8"

  cases <- list(
    list(charr:::ci_trim_left, malformed_left),
    list(charr:::ci_trim_right, malformed_right),
    list(charr:::ci_trim_both, malformed_left)
  )
  for (case in cases) {
    operation <- case[[1L]]
    value <- case[[2L]]
    expect_error(with_backend("base", operation(value)), "invalid UTF-8")
    expect_error(
      with_backend(
        "altrep",
        operation(charport::as_charvec(value))
      ),
      "invalid UTF-8"
    )
  }

  for (operation in list(
    charr:::ci_trim_left,
    charr:::ci_trim_right,
    charr:::ci_trim_both
  )) {
    expected <- with_backend("stringi", operation(malformed_inside))
    expect_identical(with_backend("base", operation(malformed_inside)), expected)
    expect_identical(
      with_backend(
        "altrep",
        operation(charport::as_charvec(malformed_inside))
      ),
      expected
    )
  }
})


test_that("optimized trim preserves converted source recycling", {
  latin1 <- iconv(c(" é ", " à "), from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  pattern <- rep("\\P{Wspace}", 4L)

  for (operation in list(
    charr:::ci_trim_left,
    charr:::ci_trim_right,
    charr:::ci_trim_both
  )) {
    expected <- with_backend("stringi", operation(latin1, pattern))
    expect_identical(with_backend("base", operation(latin1, pattern)), expected)
    actual <- with_backend(
      "altrep",
      operation(charport::as_charvec(latin1), pattern)
    )
    expect_identical(actual, expected)
    expect_identical(Encoding(actual), Encoding(expected))
  }
})
