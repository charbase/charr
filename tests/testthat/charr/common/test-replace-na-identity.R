# str_replace_na on an input with no NAs and no attributes is the identity.
# The ALTREP backend used to take that shortcut only for an unmaterialized
# charvec source and rebuilt every other input element by element, copying the
# whole payload to produce a value identical to the input.

test_that("str_replace_na returns a no-NA input unchanged", {
  values <- c("plain", "", "ASCII only", "café", "\U0001f469 family")

  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_replace_na(values)), values)
  }

  altrep_input <- charport::as_charvec(values)
  expect_identical(
    with_backend("altrep", str_replace_na(altrep_input)), altrep_input
  )
})


test_that("str_replace_na still rebuilds when there is anything to change", {
  values <- c("plain", NA_character_, "café", NA_character_)
  expected <- with_backend("stringi", str_replace_na(values))

  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_replace_na(values)), expected)
  }

  expect_identical(
    with_backend("altrep", str_replace_na(charport::as_charvec(values))),
    expected
  )
})


test_that("str_replace_na preserves names, replaced or not", {
  # The C-level identity shortcut is gated on NO_ATTRIB, but the stringr
  # wrapper restores attributes around the leaf, so names survive either way.
  # Both branches must agree with stringi.
  unchanged <- c(a = "one", b = "two")
  replaced <- c(a = "one", b = NA_character_)

  for (values in list(unchanged, replaced)) {
    expected <- with_backend("stringi", str_replace_na(values))
    expect_identical(names(expected), c("a", "b"))
    for (backend in c("base", "altrep")) {
      expect_identical(with_backend(backend, str_replace_na(values)), expected)
    }
  }
})


test_that("str_replace_na honours a custom replacement and validates it", {
  values <- c("plain", NA_character_)

  for (backend in c("base", "altrep")) {
    expect_identical(
      with_backend(backend, str_replace_na(values, "gone")),
      c("plain", "gone")
    )
    # An invalid replacement must signal even when the input has no NAs, so the
    # identity shortcut cannot skip replacement validation.
    expect_error(
      with_backend(backend, str_replace_na("no missing values", c("a", "b")))
    )
  }
})
