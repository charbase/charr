test_that("optimized fixed locate keeps scalar byte positions and shapes", {
  malformed <- rawToChar(as.raw(c(
    0x61, 0xc3, 0x28, 0x20, 0x62, 0x20, 0x63
  )))
  Encoding(malformed) <- "UTF-8"

  values <- c(
    "é a b",
    paste0("\ufeff", " a "),
    malformed,
    "no-match",
    "",
    NA_character_
  )

  expected_first <- with_backend(
    "stringi",
    charr:::ci_locate_first_fixed(values, " ")
  )
  expected_first_length <- with_backend(
    "stringi",
    charr:::ci_locate_first_fixed(values, " ", get_length = TRUE)
  )
  expected_all <- with_backend(
    "stringi",
    charr:::ci_locate_all_fixed(values, " ")
  )
  expected_all_omit <- with_backend(
    "stringi",
    charr:::ci_locate_all_fixed(
      values, " ", omit_no_match = TRUE, get_length = TRUE
    )
  )

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }

    expect_identical(
      with_backend(backend, charr:::ci_locate_first_fixed(input, " ")),
      expected_first
    )
    expect_identical(
      with_backend(
        backend,
        charr:::ci_locate_first_fixed(input, " ", get_length = TRUE)
      ),
      expected_first_length
    )
    expect_identical(
      with_backend(backend, charr:::ci_locate_all_fixed(input, " ")),
      expected_all
    )
    expect_identical(
      with_backend(
        backend,
        charr:::ci_locate_all_fixed(
          input, " ", omit_no_match = TRUE, get_length = TRUE
        )
      ),
      expected_all_omit
    )
  }
})


test_that("optimized fixed locate falls back for options and conversion", {
  latin1 <- iconv("café ici", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "A a", NA_character_)

  expected <- with_backend(
    "stringi",
    charr:::ci_locate_first_fixed(
      values, "a", case_insensitive = TRUE
    )
  )
  expected_all <- with_backend(
    "stringi",
    charr:::ci_locate_all_fixed(
      values, "a", overlap = TRUE, get_length = TRUE
    )
  )

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }

    expect_identical(
      with_backend(
        backend,
        charr:::ci_locate_first_fixed(
          input, "a", case_insensitive = TRUE
        )
      ),
      expected
    )
    expect_identical(
      with_backend(
        backend,
        charr:::ci_locate_all_fixed(
          input, "a", overlap = TRUE, get_length = TRUE
        )
      ),
      expected_all
    )
  }
})


test_that("optimized fixed locate keeps prefix outputs on conversion fallback", {
  latin1 <- iconv("café a", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x61)))
  Encoding(malformed) <- "UTF-8"
  values <- c("éa a", latin1, malformed, NA_character_, "")

  expected_first <- with_backend(
    "stringi", charr:::ci_locate_first_fixed(values, "a")
  )
  expected_all <- with_backend(
    "stringi", charr:::ci_locate_all_fixed(values, "a", get_length = TRUE)
  )

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }

    expect_identical(
      with_backend(backend, charr:::ci_locate_first_fixed(input, "a")),
      expected_first
    )
    expect_identical(
      with_backend(
        backend,
        charr:::ci_locate_all_fixed(input, "a", get_length = TRUE)
      ),
      expected_all
    )
  }
})


test_that("optimized fixed locate validates incompatible tails", {
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"

  for (backend in c("base", "altrep")) {
    values <- c("a", bytes)
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(values)
    } else {
      values
    }
    expect_error(
      with_backend(backend, charr:::ci_locate_first_fixed(input, "a")),
      "bytes encoding"
    )
    expect_error(
      with_backend(backend, charr:::ci_locate_all_fixed(input, "a")),
      "bytes encoding"
    )
  }

  if (l10n_info()[["UTF-8"]]) {
    native <- rawToChar(as.raw(0xff))
    Encoding(native) <- "unknown"
    for (backend in c("base", "altrep")) {
      values <- c("a", native)
      input <- if (identical(backend, "altrep")) {
        charport::as_charvec(values)
      } else {
        values
      }
      expect_error(
        with_backend(backend, charr:::ci_locate_first_fixed(input, "a"))
      )
      expect_error(
        with_backend(backend, charr:::ci_locate_all_fixed(input, "a"))
      )
    }
  }
})
