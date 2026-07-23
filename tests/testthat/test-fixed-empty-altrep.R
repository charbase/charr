# charr-owned file: zero-recycling returns for Reader-backed fixed search.

fixed_bytes <- function() {
  value <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(value) <- "bytes"
  value
}


test_that("fixed operations return exact empty shapes before acquisition", {
  bytes <- fixed_bytes()

  expect_identical(
    charr:::ci_detect_fixed(bytes, character()),
    logical()
  )
  expect_identical(
    charr:::ci_count_fixed(character(), "a"),
    integer()
  )
  expect_identical(
    charr:::ci_startswith_fixed(bytes, "a", from = integer()),
    logical()
  )
  expect_identical(
    charr:::ci_endswith_fixed(bytes, character()),
    logical()
  )

  expect_identical(
    charr:::ci_locate_first_fixed(bytes, character()),
    cbind(start = integer(), end = integer())
  )
  expect_identical(
    charr:::ci_locate_first_fixed(
      bytes, character(), get_length = TRUE
    ),
    cbind(start = integer(), length = integer())
  )
  expect_identical(
    charr:::ci_locate_all_fixed(bytes, character()),
    list()
  )

  extracted <- charr:::ci_extract_first_fixed(bytes, character())
  expect_identical(extracted, character())
  expect_identical(charport::is_charvec(extracted), charr_altrep())
  expect_identical(
    charr:::ci_extract_all_fixed(bytes, character()),
    list()
  )
  expect_identical(
    charr:::ci_extract_all_fixed(bytes, character(), simplify = TRUE),
    matrix(character(), 0L, 0L)
  )

  replaced <- charr:::ci_replace_all_fixed(bytes, "a", character())
  expect_identical(replaced, character())
  expect_identical(charport::is_charvec(replaced), charr_altrep())

  expect_identical(
    charr:::ci_split_fixed(bytes, "_", n = integer()),
    list()
  )
  expect_identical(
    charr:::ci_split_fixed(
      bytes, character(), simplify = NA
    ),
    matrix(character(), 0L, 0L)
  )
})


test_that("fixed zero recycling skips large Reader-backed subjects", {
  strings <- charr:::ci_trim_both(rep(c("café", "🙂x", "ü"), 2000L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(charr:::ci_detect_fixed(strings, character()), logical())
  expect_identical(
    charr:::ci_locate_all_fixed(strings, character()),
    list()
  )
  expect_identical(
    charr:::ci_extract_first_fixed(strings, character()),
    character()
  )
  expect_identical(
    charr:::ci_split_fixed(strings, character()),
    list()
  )
})
