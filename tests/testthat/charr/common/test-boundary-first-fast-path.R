test_that("direct word-boundary first results retain ICU semantics", {
  plain <- c(
    "Alpha beta", "can't stop", "Élan vital", "日本語 test",
    "A\tB", "A,B", "\uFEFFAlpha beta", "", NA_character_
  )
  input <- if (identical(charr_backend(), "altrep")) {
    charport::as_charvec(plain)
  } else {
    plain
  }
  backend <- charr:::.charr_backend_environments[[charr_backend()]]
  locate <- get(
    "stri_locate_first_boundaries", envir = backend, inherits = FALSE
  )
  extract <- get(
    "stri_extract_first_boundaries", envir = backend, inherits = FALSE
  )
  options <- list(type = "word")

  for (get_length in c(FALSE, TRUE)) {
    expect_identical(
      locate(input, opts_brkiter = options, get_length = get_length),
      stringi::stri_locate_first_boundaries(
        plain, opts_brkiter = options, get_length = get_length
      )
    )
  }
  expect_identical(
    extract(input, opts_brkiter = options),
    stringi::stri_extract_first_boundaries(
      plain, opts_brkiter = options
    )
  )

  if (identical(charr_backend(), "altrep")) {
    expect_false(charport::charport_info(input)$is_materialized)
  }
})
