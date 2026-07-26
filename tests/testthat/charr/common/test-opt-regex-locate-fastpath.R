test_that("optimized regex locate matches stringi on direct UTF-8 input", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x20, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c(
    "one two three", "caf\u00e9 noir", "\u6f22\u5b57 \u304b\u306a",
    "\ufeff alpha", "no-space", malformed, "", NA_character_
  )

  operations <- list(
    first = function(x) {
      charr:::ci_locate_first_regex(x, "(?<=\\s)\\p{L}+")
    },
    first_length = function(x) {
      charr:::ci_locate_first_regex(
        x, "(?<=\\s)\\p{L}+", get_length = TRUE
      )
    },
    all = function(x) {
      charr:::ci_locate_all_regex(x, "(?<=\\s)\\p{L}+")
    },
    all_omit = function(x) {
      charr:::ci_locate_all_regex(
        x, "(?<=\\s)\\p{L}+", omit_no_match = TRUE
      )
    },
    all_length = function(x) {
      charr:::ci_locate_all_regex(
        x, "(?<=\\s)\\p{L}+", get_length = TRUE
      )
    },
    zero_width = function(x) {
      charr:::ci_locate_all_regex(x, "(?=a)", omit_no_match = TRUE)
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    input <- charport::as_charvec(values)
    expect_identical(with_backend("altrep", operation(input)), expected)
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("regex locate keeps capture metadata and recycled patterns", {
  values <- c("one two", "caf\u00e9 noir", "none", NA_character_)
  patterns <- c("(?<word>\\p{L}+)", "(?<space>\\s+)")

  operations <- list(
    first_capture = function(x) {
      charr:::ci_locate_first_regex(
        x, patterns[[1L]], capture_groups = TRUE
      )
    },
    all_capture = function(x) {
      charr:::ci_locate_all_regex(
        x, patterns[[1L]], capture_groups = TRUE,
        omit_no_match = TRUE, get_length = TRUE
      )
    },
    recycled = function(x) charr:::ci_locate_all_regex(x, patterns)
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend(
        "altrep", operation(charport::as_charvec(values))
      ),
      expected
    )
  }
})


test_that("regex locate retains encoding fallbacks and bytes errors", {
  latin1 <- iconv("caf\u00e9 noir", from = "UTF-8", to = "latin1")
  Encoding(latin1) <- "latin1"
  values <- c(latin1, "plain text", NA_character_)

  operations <- list(
    first = function(x) charr:::ci_locate_first_regex(x, "\\p{L}+"),
    all = function(x) charr:::ci_locate_all_regex(x, "\\p{L}+")
  )
  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)
    expect_identical(
      with_backend(
        "altrep", operation(charport::as_charvec(values))
      ),
      expected
    )
  }

  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"
  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec(bytes)
    } else {
      bytes
    }
    expect_error(
      with_backend(
        backend, charr:::ci_locate_first_regex(input, "x")
      ),
      "bytes encoding"
    )
  }
})


test_that("regex locate accepts zero-length subjects with scalar patterns", {
  operations <- list(
    first = function(x) charr:::ci_locate_first_regex(x, "x"),
    all = function(x) charr:::ci_locate_all_regex(x, "x")
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(character()))
    expect_identical(
      with_backend("base", operation(character())),
      expected
    )
    expect_identical(
      with_backend(
        "altrep", operation(charport::as_charvec(character()))
      ),
      expected
    )
  }
})
