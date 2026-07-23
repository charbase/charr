# charr-owned tests for Reader-backed padding. These are not imported from
# stringr.

pad_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_pad_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

test_that("pad keeps all sides on unmaterialized inputs and outputs", {
  latin1 <- pad_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  latin1_pad <- pad_marked_string(0xe9, "latin1")
  bom <- enc2utf8("\ufeffa")
  bom_pad <- enc2utf8("\ufeff.")
  values <- c("a", "\u00e9", "\u4e2d", "", NA_character_, latin1, bom)
  pads <- c(".", "-", "x\u0301", ".", ".", latin1_pad, bom_pad)
  widths <- c(4L, 3L, 6L, 2L, 3L, 7L, 4L)
  subject <- charport::as_charvec(values)
  padding <- charport::as_charvec(pads)
  operations <- list(
    left = charr:::ci_pad_left,
    right = charr:::ci_pad_right,
    both = charr:::ci_pad_both
  )

  for (operation in operations) {
    expected <- with_altrep(
      FALSE,
      operation(values, widths, pads, use_length = FALSE)
    )
    actual <- with_altrep(
      TRUE,
      operation(subject, widths, padding, use_length = FALSE)
    )

    expect_pad_unmaterialized(actual)
    expect_identical(actual, expected)
  }

  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(padding)
})

test_that("pad output remains lazy when consumed by another pad call", {
  values <- c("a", "\u00e9", "\U0001f642", "", NA_character_)
  subject <- charport::as_charvec(values)
  first_pad <- charport::as_charvec(".")
  second_pad <- charport::as_charvec("-")

  first <- with_altrep(
    TRUE,
    charr:::ci_pad_left(subject, 3L, first_pad, use_length = TRUE)
  )
  expect_pad_unmaterialized(first)
  second <- with_altrep(
    TRUE,
    charr:::ci_pad_right(first, 5L, second_pad, use_length = TRUE)
  )
  expect_pad_unmaterialized(second)

  expected <- with_altrep(
    FALSE,
    charr:::ci_pad_right(
      charr:::ci_pad_left(values, 3L, ".", use_length = TRUE),
      5L, "-", use_length = TRUE
    )
  )
  expect_identical(second, expected)
  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(first_pad)
  expect_pad_unmaterialized(second_pad)
})

test_that("pad shares exact input aliases and preserves width semantics", {
  values <- c("x", "\u00e9", "\u4e2d")
  shared <- charport::as_charvec(values)
  actual <- with_altrep(
    TRUE,
    charr:::ci_pad_both(shared, 3L, shared, use_length = TRUE)
  )
  expect_pad_unmaterialized(actual)
  expect_identical(
    actual,
    with_altrep(
      FALSE,
      charr:::ci_pad_both(values, 3L, values, use_length = TRUE)
    )
  )
  expect_pad_unmaterialized(shared)

  subject <- charport::as_charvec("a")
  combining_pad <- charport::as_charvec("x\u0301")
  accepted <- with_altrep(
    TRUE,
    charr:::ci_pad_left(
      subject, 3L, combining_pad, use_length = FALSE
    )
  )
  expect_pad_unmaterialized(accepted)
  expect_identical(
    accepted,
    with_altrep(
      FALSE,
      charr:::ci_pad_left(
        "a", 3L, "x\u0301", use_length = FALSE
      )
    )
  )
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        subject, 3L, combining_pad, use_length = TRUE
      )
    ),
    "exactly 1 code points"
  )

  wide_pad <- charport::as_charvec("\u4e2d")
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(subject, 3L, wide_pad, use_length = FALSE)
    ),
    "total width 1"
  )
  wide <- with_altrep(
    TRUE,
    charr:::ci_pad_left(subject, 3L, wide_pad, use_length = TRUE)
  )
  expect_pad_unmaterialized(wide)
  expect_identical(wide, "\u4e2d\u4e2da")
  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(combining_pad)
  expect_pad_unmaterialized(wide_pad)
})

test_that("pad preserves NA, empty, and native recycling behavior", {
  values <- c("a", "", NA_character_, "x")
  widths <- c(NA_integer_, 3L, 3L, 3L)
  pads <- c(".", ".", ".", NA_character_)
  subject <- charport::as_charvec(values)
  padding <- charport::as_charvec(pads)

  actual <- with_altrep(
    TRUE,
    charr:::ci_pad_right(
      subject, widths, padding, use_length = TRUE
    )
  )
  expect_pad_unmaterialized(actual)
  expect_identical(
    actual,
    with_altrep(
      FALSE,
      charr:::ci_pad_right(
        values, widths, pads, use_length = TRUE
      )
    )
  )

  missing_subject <- charport::as_charvec(NA_character_)
  empty_pad <- charport::as_charvec("")
  missing <- with_altrep(
    TRUE,
    charr:::ci_pad_left(
      missing_subject, 3L, empty_pad, use_length = TRUE
    )
  )
  expect_pad_unmaterialized(missing)
  expect_identical(missing, NA_character_)

  recycle_values <- c("a", "bb", "c")
  recycle_widths <- c(3L, 4L)
  recycle_pads <- c(".", "-")
  recycle_subject <- charport::as_charvec(recycle_values)
  recycle_padding <- charport::as_charvec(recycle_pads)
  warning <- "^longer object length is not a multiple of shorter object length$"
  expected <- NULL
  actual <- NULL
  expect_warning(
    expected <- with_altrep(
      FALSE,
      charr:::ci_pad_right(
        recycle_values, recycle_widths, recycle_pads,
        use_length = TRUE
      )
    ),
    warning
  )
  expect_warning(
    actual <- with_altrep(
      TRUE,
      charr:::ci_pad_right(
        recycle_subject, recycle_widths, recycle_padding,
        use_length = TRUE
      )
    ),
    warning
  )
  expect_pad_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(padding)
  expect_pad_unmaterialized(missing_subject)
  expect_pad_unmaterialized(empty_pad)
  expect_pad_unmaterialized(recycle_subject)
  expect_pad_unmaterialized(recycle_padding)
})

test_that("zero recycling avoids pad readers", {
  bytes_value <- pad_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  padding <- charport::as_charvec("x")
  empty_padding <- charport::as_charvec(character())

  by_width <- expect_no_warning(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        subject, integer(), padding, use_length = TRUE
      )
    )
  )
  by_pad <- expect_no_warning(
    with_altrep(
      TRUE,
      charr:::ci_pad_right(
        subject, 3L, empty_padding, use_length = TRUE
      )
    )
  )
  expect_pad_unmaterialized(by_width)
  expect_pad_unmaterialized(by_pad)
  expect_identical(by_width, character())
  expect_identical(by_pad, character())
  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(padding)
  expect_pad_unmaterialized(empty_padding)
})

test_that("pad preserves malformed UTF-8 and bytes errors", {
  malformed <- pad_marked_string(c(0x61, 0xc3, 0x28, 0x62), "UTF-8")
  malformed_pad <- pad_marked_string(0xff, "UTF-8")
  bytes_value <- pad_marked_string(c(0xff, 0xfe), "bytes")
  malformed_subject <- charport::as_charvec(malformed)
  valid_padding <- charport::as_charvec("x")

  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        malformed_subject, 0L, valid_padding, use_length = TRUE
      )
    ),
    "invalid UTF-8 byte sequence"
  )
  expect_pad_unmaterialized(malformed_subject)
  expect_pad_unmaterialized(valid_padding)

  subject <- charport::as_charvec("a")
  malformed_padding <- charport::as_charvec(malformed_pad)
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        subject, 0L, malformed_padding, use_length = TRUE
      )
    ),
    "exactly 1 code points"
  )
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        subject, 0L, malformed_padding, use_length = FALSE
      )
    ),
    "invalid UTF-8 byte sequence"
  )
  expect_pad_unmaterialized(malformed_padding)

  bytes <- charport::as_charvec(bytes_value)
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(bytes, 3L, valid_padding, use_length = TRUE)
    ),
    "bytes encoding"
  )
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(subject, 3L, bytes, use_length = TRUE)
    ),
    "bytes encoding"
  )

  invalid_padding <- charport::as_charvec("")
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_pad_left(
        bytes, 3L, invalid_padding, use_length = TRUE
      )
    ),
    "bytes encoding"
  )
  expect_pad_unmaterialized(subject)
  expect_pad_unmaterialized(valid_padding)
  expect_pad_unmaterialized(bytes)
  expect_pad_unmaterialized(invalid_padding)
})
