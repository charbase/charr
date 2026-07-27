fixed_zero_results <- function(pattern) {
  string <- character()

  list(
    detect = str_detect(string, pattern),
    starts = str_starts(string, pattern),
    ends = str_ends(string, pattern),
    count = str_count(string, pattern),
    locate = str_locate(string, pattern),
    locate_all = str_locate_all(string, pattern),
    extract = str_extract(string, pattern),
    extract_all = str_extract_all(string, pattern),
    extract_all_matrix = str_extract_all(string, pattern, simplify = TRUE),
    replace = str_replace(string, pattern, "X"),
    replace_all = str_replace_all(string, pattern, "X"),
    split = str_split(string, pattern),
    split_matrix = str_split(string, pattern, simplify = TRUE),
    subset = str_subset(string, pattern),
    which = str_which(string, pattern),
    word = word(string, sep = pattern)
  )
}


test_that("fixed operations preserve zero-recycling output shapes", {
  expected <- list(
    detect = logical(),
    starts = logical(),
    ends = logical(),
    count = integer(),
    locate = cbind(start = integer(), end = integer()),
    locate_all = list(),
    extract = character(),
    extract_all = list(),
    extract_all_matrix = matrix(character(), 0L, 0L),
    replace = character(),
    replace_all = character(),
    split = list(),
    split_matrix = matrix(character(), 0L, 0L),
    subset = character(),
    which = integer(),
    word = character()
  )

  expect_identical(fixed_zero_results(fixed("é")), expected)
})


test_that("fixed zero recycling does not inspect inactive patterns", {
  bytes <- rawToChar(as.raw(c(0xff, 0xfe)))
  Encoding(bytes) <- "bytes"

  expect_identical(
    fixed_zero_results(fixed(bytes)),
    fixed_zero_results(fixed("é"))
  )
})
