test_that("break iterator type is read from its scalar option value", {
  skip_if_not(identical(charr_backend(), "base"))

  pattern <- boundary("word")
  attr(pattern, "options") <- list(locale = "en", type = "word")

  expect_identical(str_count("abc 123", pattern), 3L)
})


test_that("scalar substring replacement bounds malformed marked UTF-8", {
  malformed <- rawToChar(as.raw(0xf0))
  Encoding(malformed) <- "UTF-8"
  environments <- charr:::.charr_backend_environments
  replace_all <- environments[[charr_backend()]][["stri_sub_all<-"]]

  actual <- replace_all(
    malformed, list(1L), list(1L), value = "XXXX"
  )
  expect_identical(charToRaw(actual), charToRaw("XXXX"))

  multiple <- replace_all(
    malformed, list(c(1L, 2L)), list(c(1L, 2L)),
    value = list(c("X", "Y"))
  )
  expect_identical(charToRaw(multiple), charToRaw("XY"))
})
