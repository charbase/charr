fixed_replace_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("fixed replace keeps completed rows across mixed encodings", {
  latin1 <- fixed_replace_marked(c(0x61, 0xe9, 0x61), "latin1")
  native <- fixed_replace_marked(c(0x61, 0xc3, 0xa9, 0x61), "unknown")
  malformed <- fixed_replace_marked(c(0x61, 0xff, 0x61), "UTF-8")
  values <- c(
    "a-a", paste0("\ufeff", "a-a"), latin1, native,
    malformed, "none", "", NA_character_
  )

  operations <- list(
    first = function(x) charr_test_leaf("ci_replace_first_fixed")(x, "a", "X"),
    all_byte = function(x) charr_test_leaf("ci_replace_all_fixed")(x, "a", "X"),
    all_long = function(x) charr_test_leaf("ci_replace_all_fixed")(x, "a", "word"),
    all_delete = function(x) charr_test_leaf("ci_replace_all_fixed")(x, "a", ""),
    all_missing = function(x) {
      charr_test_leaf("ci_replace_all_fixed")(x, "a", NA_character_)
    }
  )

  for (operation in operations) {
    expected <- with_backend("stringi", operation(values))
    expect_identical(with_backend("base", operation(values)), expected)

    actual <- with_backend(
      "altrep", operation(charport::as_charvec(values))
    )
    expect_true(charport::is_charvec(actual))
    expect_identical(actual, expected)
    expect_identical(Encoding(actual), Encoding(expected))
  }
})


test_that("fixed replace preserves general and sequential output shapes", {
  values <- c("ababa", "none", "Xab", NA_character_, "")
  operations <- list(
    recycled = function(x) {
      charr_test_leaf("ci_replace_all_fixed")(x, c("a", "b"), c("X", "Y"))
    },
    insensitive = function(x) {
      charr_test_leaf("ci_replace_all_fixed")(x, "A", "x", case_insensitive = TRUE)
    },
    sequential = function(x) {
      charr_test_leaf("ci_replace_all_fixed")(
        x, c("ab", "X"), c("X", "!"), vectorize_all = FALSE
      )
    }
  )

  for (operation in operations) {
    expected <- suppressWarnings(
      with_backend("stringi", operation(values))
    )
    expect_identical(
      suppressWarnings(with_backend("base", operation(values))),
      expected
    )
    expect_identical(
      suppressWarnings(with_backend(
        "altrep", operation(charport::as_charvec(values))
      )),
      expected
    )
  }
})


test_that("fixed replace still rejects buried bytes inputs", {
  bytes <- fixed_replace_marked(0xff, "bytes")
  subjects <- c("a-a", "none", bytes)
  calls <- list(
    function(x) charr_test_leaf("ci_replace_first_fixed")(x, "a", "X"),
    function(x) charr_test_leaf("ci_replace_all_fixed")(x, "a", "X"),
    function(x) charr_test_leaf("ci_replace_all_fixed")(x, "a", "X", vectorize_all = FALSE)
  )

  for (call in calls) {
    expect_error(with_backend("base", call(subjects)), "bytes encoding")
    expect_error(
      with_backend("altrep", call(charport::as_charvec(subjects))),
      "bytes encoding"
    )
  }

  for (backend in c("base", "altrep")) {
    input <- if (identical(backend, "altrep")) {
      charport::as_charvec("a-a")
    } else {
      "a-a"
    }
    expect_error(
      with_backend(
        backend,
        charr_test_leaf("ci_replace_first_fixed")(input, bytes, "X")
      ),
      "bytes encoding"
    )
    expect_error(
      with_backend(
        backend,
        charr_test_leaf("ci_replace_first_fixed")(input, "a", bytes)
      ),
      "bytes encoding"
    )
  }
})
