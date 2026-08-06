# charr-owned targeted equivalence tests for Reader/Builder ICU case mapping.

case_lower <- function(...) charr_test_leaf("ci_trans_tolower")(...)
case_upper <- function(...) charr_test_leaf("ci_trans_toupper")(...)
case_title <- function(...) charr_test_leaf("ci_trans_totitle")(...)


test_that("case mapping is locale-sensitive on CHARVEC inputs", {
  strings <- charr_test_leaf("ci_trim_both")(c(
    " I ", " İ ", " i ", " ı ", " Straße ", " ΟΣ ",
    " 𐐀𐐨 ", " 🙂É ", " ", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  lower <- case_lower(strings, "tr")
  expect_identical(
    lower,
    c("ı", "i", "i", "ı", "straße", "ος", "𐐨𐐨", "🙂é", "", NA)
  )
  expect_identical(charport::is_charvec(lower), charr_altrep())

  upper <- case_upper(strings, "tr")
  expect_identical(
    upper,
    c("I", "İ", "İ", "I", "STRASSE", "ΟΣ", "𐐀𐐀", "🙂É", "", NA)
  )
  expect_identical(charport::is_charvec(upper), charr_altrep())
  expect_identical(
    Encoding(upper),
    c("unknown", "UTF-8", "UTF-8", "unknown", "unknown", "UTF-8",
      "UTF-8", "UTF-8", "unknown", "unknown")
  )
})


test_that("titlecase preserves word and sentence break iteration", {
  strings <- charr_test_leaf("ci_trim_both")(c(
    " oNE. tWO ", " ΟΣ ΟΣΑ ", " ßtraße ", " 𐐨WORD ", " ", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  word_opts <- charr_test_leaf("ci_opts_brkiter")(type = "word", locale = "el")
  word <- case_title(strings, opts_brkiter = word_opts)
  expect_identical(
    word,
    c("One. Two", "Ος Οσα", "Sstraße", "𐐀word", "", NA)
  )
  expect_identical(charport::is_charvec(word), charr_altrep())

  sentence_opts <- charr_test_leaf("ci_opts_brkiter")(type = "sentence", locale = "el")
  expect_identical(
    case_title(strings[1:2], opts_brkiter = sentence_opts),
    c("One. two", "Ος οσα")
  )
})




casemap_bom_input <- function() {
  value <- rawToChar(as.raw(c(0xEF, 0xBB, 0xBF, 0x41, 0x62, 0x43)))
  Encoding(value) <- "UTF-8"
  # Regex replacement is UTF16-origin and preserves the leading BOM, yielding
  # a charvec only on the ALTREP route. Case mapping must then strip the BOM.
  charr_test_leaf("ci_replace_all_regex")(
    value, "NOMATCHZZZ", "", vectorize_all = TRUE, opts_regex = NULL
  )
}


test_that("case mapping strips leading BOMs and preserves malformed UTF-8", {
  bom <- casemap_bom_input()
  expect_identical(charport::is_charvec(bom), charr_altrep())
  expect_identical(case_lower(bom, "en"), "abc")
  expect_identical(
    case_title(bom, opts_brkiter = charr_test_leaf("ci_opts_brkiter")(locale = "en")),
    "Abc"
  )

  invalid <- rawToChar(as.raw(0xC3))
  Encoding(invalid) <- "UTF-8"
  malformed <- charr_test_leaf("ci_replace_all_fixed")("x", "x", invalid)
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  expect_identical(charToRaw(case_lower(malformed, "en")), as.raw(0xC3))
  expect_identical(
    charToRaw(case_title(
      malformed, opts_brkiter = charr_test_leaf("ci_opts_brkiter")(locale = "en")
    )),
    as.raw(0xC3)
  )
})


test_that("case mapping handles empty CHARVEC inputs", {
  strings <- charr_test_leaf("ci_trim_both")(character())
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(case_lower(strings, "tr"), character())
  expect_identical(case_upper(strings, "tr"), character())
  expect_identical(
    case_title(strings, opts_brkiter = charr_test_leaf("ci_opts_brkiter")(locale = "el")),
    character()
  )
})


test_that("titlecase emits one locale-fallback warning per call", {
  skip_if_not(charr:::charr_icu_bundled())
  skip_if_backend_lacks_locale_fallback_warning()
  strings <- charr_test_leaf("ci_trim_both")(rep(" ijsland ", 16L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  messages <- character()
  result <- withCallingHandlers(
    case_title(
      strings,
      opts_brkiter = charr_test_leaf("ci_opts_brkiter")(type = "word", locale = "nl")
    ),
    warning = function(w) {
      messages <<- c(messages, conditionMessage(w))
      invokeRestart("muffleWarning")
    }
  )
  expect_length(messages, 1L)
  expect_match(messages, "resource bundle")
  expect_identical(result, rep("IJsland", 16L))
})
