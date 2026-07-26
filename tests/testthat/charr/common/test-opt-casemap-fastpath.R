test_that("case mapping matches stringi across ASCII and Unicode paths", {
  latin1 <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(latin1) <- "latin1"
  values <- c(
    plain = "The QUICK brown fox 123",
    sharp_s = "Straße",
    dotted_i = "İstanbul",
    greek = "ΟΣ ΟΣΑ",
    deseret = "𐐀𐐨",
    latin1 = latin1,
    empty = "",
    missing = NA_character_
  )

  for (locale in c("en", "tr", "az", "lt")) {
    expected_lower <- stringi::stri_trans_tolower(values, locale)
    expected_upper <- stringi::stri_trans_toupper(values, locale)
    names(expected_lower) <- names(values)
    names(expected_upper) <- names(values)

    expect_identical(str_to_lower(values, locale), expected_lower)
    expect_identical(str_to_upper(values, locale), expected_upper)
  }
})


test_that("case mapping handles output growth and shrinkage", {
  ascii_i <- strrep("i", 128L)
  dotted_i <- strrep("İ", 128L)
  sharp_s <- strrep("ß", 128L)

  expect_identical(
    str_to_upper(ascii_i, "tr"),
    stringi::stri_trans_toupper(ascii_i, "tr")
  )
  expect_identical(
    str_to_lower(dotted_i, "tr"),
    stringi::stri_trans_tolower(dotted_i, "tr")
  )
  expect_identical(
    str_to_upper(sharp_s, "de"),
    stringi::stri_trans_toupper(sharp_s, "de")
  )
})


test_that("title and sentence mapping keep locale boundary behavior", {
  values <- c(first = "ijsland. tweede", second = "oNE. tWO", missing = NA)

  word_opts <- stringi::stri_opts_brkiter(type = "word", locale = "nl")
  word_expected <- suppressWarnings(stringi::stri_trans_totitle(
    values, opts_brkiter = word_opts
  ))
  names(word_expected) <- names(values)
  expect_identical(suppressWarnings(str_to_title(values, "nl")), word_expected)

  sentence_opts <- stringi::stri_opts_brkiter(
    type = "sentence", locale = "nl"
  )
  sentence_expected <- suppressWarnings(stringi::stri_trans_totitle(
    values, opts_brkiter = sentence_opts
  ))
  names(sentence_expected) <- names(values)
  expect_identical(
    suppressWarnings(str_to_sentence(values, "nl")),
    sentence_expected
  )
})


test_that("case mapping preserves malformed UTF-8 and rejects bytes", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"

  actual <- str_to_lower(malformed, "en")
  expected <- stringi::stri_trans_tolower(malformed, "en")
  expect_identical(charToRaw(actual), charToRaw(expected))

  Encoding(malformed) <- "bytes"
  expect_error(str_to_lower(malformed, "en"), "bytes encoding")
  expect_error(str_to_upper(malformed, "en"), "bytes encoding")
  expect_error(str_to_title(malformed, "en"), "bytes encoding")
  expect_error(str_to_sentence(malformed, "en"), "bytes encoding")
})
