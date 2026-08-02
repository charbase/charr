# charr-owned targeted equivalence tests for Reader/Builder coll extract.

extract_first_coll <- function(...) charr_test_leaf("ci_extract_first_coll")(...)
extract_all_coll <- function(...) charr_test_leaf("ci_extract_all_coll")(...)

extract_coll_input <- function(x) charr_test_leaf("ci_trim_both")(x)


test_that("coll extract emits exact UTF-16 match slices through Builder", {
  strings <- extract_coll_input(c(
    " 😀ä-a-A ", " üxÜ ", " å-aa-Å ", " none ", " ", NA
  ))
  patterns <- extract_coll_input(c("a", "ü", "aa", "a", "x", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  first <- extract_first_coll(strings, patterns, opts_collator = opts)
  expect_identical(first, c("ä", "ü", "aa", NA, NA, NA))
  expect_identical(charport::is_charvec(first), charr_altrep())
  expect_identical(Encoding(first), c("UTF-8", "UTF-8", "unknown",
    "unknown", "unknown", "unknown"))
})


test_that("coll extract all preserves list, omit, and simplify shapes", {
  strings <- extract_coll_input(c("😀ä-a-A", "üxÜ", "none", "", NA))
  patterns <- extract_coll_input(c("a", "ü", "a", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  all <- extract_all_coll(strings, patterns, opts_collator = opts)
  expect_identical(all, list(
    c("ä", "a", "A"), c("ü", "Ü"),
    NA_character_, NA_character_, NA_character_
  ))
  expect_identical(
    vapply(all, charport::is_charvec, logical(1L)),
    rep(charr_altrep(), 5L)
  )

  omitted <- extract_all_coll(
    strings, patterns, omit_no_match = TRUE, opts_collator = opts
  )
  expect_identical(lengths(omitted), c(3L, 2L, 0L, 0L, 1L))
  expect_identical(omitted[[5L]], NA_character_)

  padded_empty <- extract_all_coll(
    strings, patterns, simplify = TRUE, opts_collator = opts
  )
  padded_na <- extract_all_coll(
    strings, patterns, simplify = NA, opts_collator = opts
  )
  expect_identical(dim(padded_empty), c(5L, 3L))
  expect_identical(padded_empty[2L, 3L], "")
  expect_identical(padded_na[2L, 3L], NA_character_)
})


test_that("coll extract preserves Danish tailoring, recycling, and empties", {
  strings <- extract_coll_input(c("Aarhus", "Århus", "blaa", "blå", NA, ""))
  patterns <- extract_coll_input(c("Å", "aa"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "da", strength = 1L)

  expect_identical(
    extract_first_coll(strings, patterns, opts_collator = opts),
    c("Aa", "Å", "aa", "å", NA, NA)
  )

  empty <- extract_coll_input("")
  expect_warning(
    result <- extract_all_coll(strings, empty),
    "empty search patterns are not supported"
  )
  expect_identical(result, rep(list(NA_character_), length(strings)))
})


test_that("coll extract seeded differential matches stringi serially", {
  set.seed(20260717)
  atoms <- c(
    "a", "A", "ä", "u\u0308", "Ü", "å", "Å", "ß", "😀", "𐐷", "-", " "
  )
  plain_strings <- vapply(seq_len(480L), function(i) {
    if (i %% 53L == 0L)
      return(NA_character_)
    paste0(sample(atoms, sample.int(11L, 1L)-1L, replace = TRUE),
      collapse = "")
  }, character(1L))
  plain_patterns <- sample(
    c("a", "ä", "ü", "aa", "Å", "😀", "𐐷", "", NA_character_),
    length(plain_strings), replace = TRUE
  )
  strings <- charport::as_charvec(plain_strings)
  patterns <- charport::as_charvec(plain_patterns)
  expect_altrep_charvec(strings)
  expect_altrep_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  capture <- function(expr) {
    warnings <- character()
    error <- NULL
    value <- tryCatch(
      withCallingHandlers(
        force(expr),
        warning = function(cnd) {
          warnings <<- c(warnings, conditionMessage(cnd))
          invokeRestart("muffleWarning")
        }
      ),
      error = function(cnd) {
        error <<- conditionMessage(cnd)
        NULL
      }
    )
    list(value = value, warnings = warnings, error = error)
  }
  backend <- charr:::.charr_backend_environments[[charr_backend()]]
  run_backend <- function() list(
    first = backend$stri_extract_first_coll(
      strings, patterns, opts_collator = opts
    ),
    all = backend$stri_extract_all_coll(
      strings, patterns, opts_collator = opts
    ),
    omitted = backend$stri_extract_all_coll(
      strings, patterns, omit_no_match = TRUE, opts_collator = opts
    ),
    simplified = backend$stri_extract_all_coll(
      strings, patterns, simplify = NA, opts_collator = opts
    )
  )
  run_oracle <- function() list(
    first = stringi::stri_extract_first_coll(
      plain_strings, plain_patterns, opts_collator = opts
    ),
    all = stringi::stri_extract_all_coll(
      plain_strings, plain_patterns, opts_collator = opts
    ),
    omitted = stringi::stri_extract_all_coll(
      plain_strings, plain_patterns, omit_no_match = TRUE,
      opts_collator = opts
    ),
    simplified = stringi::stri_extract_all_coll(
      plain_strings, plain_patterns, simplify = NA,
      opts_collator = opts
    )
  )

  got <- capture(run_backend())
  oracle <- capture(run_oracle())
  expect_identical(got, oracle)

  bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(bytes) <- "bytes"
  backend_error <- capture(
    backend$stri_extract_all_coll(
      charport::as_charvec(bytes), "a", opts_collator = opts
    )
  )
  oracle_error <- capture(
    stringi::stri_extract_all_coll(bytes, "a", opts_collator = opts)
  )
  expect_identical(backend_error$error, oracle_error$error)
  expect_identical(backend_error$warnings, oracle_error$warnings)
})
