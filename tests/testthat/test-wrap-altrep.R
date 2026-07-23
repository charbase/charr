# charr-owned targeted equivalence tests for Reader/Builder wrap.

wrap_backend <- function(...) charr:::ci_wrap(...)

wrap_charvec <- function(x) {
  str_replace_all(x, fixed("\uffff"), "")
}

capture_wrap_warnings <- function(expr) {
  messages <- character()
  value <- withCallingHandlers(
    expr,
    warning = function(condition) {
      messages <<- c(messages, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, messages = messages)
}


test_that("wrap retains line breaking and emits Builder line vectors", {
  strings <- wrap_charvec(c(
    "one two three four", "日本語の文章です", "ภาษาไทยภาษาไทย",
    "👩‍👩‍👧‍👦 e\u0301 family", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- wrap_backend(
    strings, width = 5L, simplify = FALSE, normalize = FALSE,
    locale = "en_US"
  )
  want <- stringi::stri_wrap(
    as.character(strings), width = 5L, simplify = FALSE,
    normalize = FALSE, locale = "en_US"
  )
  expect_identical(got, want)
  expect_identical(
    vapply(got, charport::is_charvec, logical(1L)),
    rep(charr_altrep(), length(strings))
  )
})


test_that("wrap preserves greedy, dynamic, width, and paragraph adornments", {
  strings <- wrap_charvec(c(
    "alpha beta gamma delta epsilon", "Ａ bb ccc dddd",
    "a/b/c/d", "日本語 ภาษาไทย e\u0301"
  ))
  prefix <- wrap_charvec("λ> ")
  initial <- wrap_charvec("初> ")
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(prefix), charr_altrep())
  expect_identical(charport::is_charvec(initial), charr_altrep())

  for (cost_exponent in c(-1, 2)) {
    for (use_length in c(FALSE, TRUE)) {
      got <- suppressWarnings(wrap_backend(
        strings, width = 9L, cost_exponent = cost_exponent,
        simplify = FALSE, normalize = FALSE, indent = 2L, exdent = 1L,
        prefix = prefix, initial = initial, whitespace_only = FALSE,
        use_length = use_length, locale = "th_TH"
      ))
      want <- suppressWarnings(stringi::stri_wrap(
        as.character(strings), width = 9L,
        cost_exponent = cost_exponent, simplify = FALSE,
        normalize = FALSE, indent = 2L, exdent = 1L,
        prefix = as.character(prefix), initial = as.character(initial),
        whitespace_only = FALSE, use_length = use_length,
        locale = "th_TH"
      ))
      expect_identical(got, want)
    }
  }

  expect_identical(
    wrap_backend(strings[3L], width = 0L, cost_exponent = -1,
      normalize = FALSE, whitespace_only = TRUE),
    stringi::stri_wrap(as.character(strings[3L]), width = 0L,
      cost_exponent = -1, normalize = FALSE, whitespace_only = TRUE)
  )
})


test_that("wrap preserves NA, empty, prefix, and initial shapes", {
  strings <- wrap_charvec(c("", " ", NA, "one two"))
  prefix <- wrap_charvec("> ")
  initial <- wrap_charvec("* ")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- wrap_backend(
    strings, width = 4L, simplify = FALSE, normalize = FALSE,
    prefix = prefix, initial = initial
  )
  want <- stringi::stri_wrap(
    as.character(strings), width = 4L, simplify = FALSE,
    normalize = FALSE, prefix = as.character(prefix),
    initial = as.character(initial)
  )
  expect_identical(got, want)
  expect_identical(
    vapply(got, charport::is_charvec, logical(1L)),
    rep(charr_altrep(), length(strings))
  )

  expect_identical(
    wrap_backend(strings, width = 4L, simplify = TRUE, normalize = FALSE),
    stringi::stri_wrap(
      as.character(strings), width = 4L, simplify = TRUE,
      normalize = FALSE
    )
  )
})


test_that("wrap locale fallback warning fires once", {
  strings <- wrap_charvec(rep("one two three", 64L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- capture_wrap_warnings(wrap_backend(
    strings, width = 5L, simplify = FALSE, normalize = FALSE,
    locale = "zz_ZZ"
  ))
  want <- capture_wrap_warnings(stringi::stri_wrap(
    as.character(strings), width = 5L, simplify = FALSE,
    normalize = FALSE, locale = "zz_ZZ"
  ))
  expect_length(got$messages, 1L)
  expect_identical(got$messages, want$messages)
  expect_identical(got$value, want$value)
})


test_that("wrap retains copied malformed UTF-8 and newline errors", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- wrap_charvec(malformed)
  newline <- wrap_charvec("alpha\nbeta")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  expect_identical(charport::is_charvec(newline), charr_altrep())

  got_malformed <- tryCatch(
    wrap_backend(malformed, width = 2L, normalize = FALSE),
    error = conditionMessage
  )
  want_malformed <- tryCatch(
    stringi::stri_wrap(
      as.character(malformed), width = 2L, normalize = FALSE
    ),
    error = conditionMessage
  )
  expect_match(got_malformed, "invalid UTF-8 byte sequence", fixed = TRUE)
  expect_match(want_malformed, "invalid UTF-8 byte sequence", fixed = TRUE)

  got_newline <- tryCatch(
    wrap_backend(newline, normalize = FALSE), error = conditionMessage
  )
  want_newline <- tryCatch(
    stringi::stri_wrap(as.character(newline), normalize = FALSE),
    error = conditionMessage
  )
  expect_identical(got_newline, want_newline)
})


test_that("wrap matches stringi over 500 seeded greedy and dynamic cases", {
  set.seed(20260716)
  tokens <- c(
    "a", "bb", "é", "e\u0301", "日本語", "ภาษาไทย",
    "👩‍👩‍👧‍👦", "Ａ", "123", "x/y"
  )
  separators <- c(" ", "  ", "/", "-")
  prefixes <- c("", "> ", "λ ", "日本 ")
  initials <- c("", "* ", "初 ", "ไทย ")
  locales <- c("en_US", "th_TH", "ja_JP", "de_DE")
  got <- vector("list", 500L)
  want <- vector("list", 500L)
  case <- 0L

  for (i in seq_len(250L)) {
    raw_strings <- vapply(
      seq_len(sample.int(5L, 1L)),
      function(unused) paste(
        sample(tokens, sample.int(8L, 1L), replace = TRUE),
        collapse = sample(separators, 1L)
      ),
      character(1L)
    )
    if (runif(1L) < 0.2)
      raw_strings[sample.int(length(raw_strings), 1L)] <- ""
    if (runif(1L) < 0.2)
      raw_strings[sample.int(length(raw_strings), 1L)] <- NA_character_
    strings <- wrap_charvec(raw_strings)
    prefix <- wrap_charvec(sample(prefixes, 1L))
    initial <- wrap_charvec(sample(initials, 1L))
    width <- sample(0:18, 1L)
    indent <- sample(0:4, 1L)
    exdent <- sample(0:4, 1L)
    whitespace_only <- sample(c(FALSE, TRUE), 1L)
    use_length <- sample(c(FALSE, TRUE), 1L)
    locale <- sample(locales, 1L)

    for (cost_exponent in c(-1, 2)) {
      case <- case + 1L
      got[[case]] <- suppressWarnings(wrap_backend(
        strings, width = width, cost_exponent = cost_exponent,
        simplify = FALSE, normalize = FALSE, indent = indent,
        exdent = exdent, prefix = prefix, initial = initial,
        whitespace_only = whitespace_only, use_length = use_length,
        locale = locale
      ))
      want[[case]] <- suppressWarnings(stringi::stri_wrap(
        as.character(strings), width = width,
        cost_exponent = cost_exponent, simplify = FALSE,
        normalize = FALSE, indent = indent, exdent = exdent,
        prefix = as.character(prefix), initial = as.character(initial),
        whitespace_only = whitespace_only, use_length = use_length,
        locale = locale
      ))
    }
  }

  expect_identical(got, want)
})


test_that("normalize strips U+FEFF at field starts like per-field flatten", {
  # The upstream normalize pipeline flattens each split_lines field through
  # UTF-8 acquisition, which strips exactly one leading BOM per field. The
  # vectorized normalize must reproduce that for a BOM at the string start
  # and after EVERY separator split_lines recognizes (review finding), and
  # keep the second BOM of a doubled pair.
  bom <- "\uFEFF"
  seps <- c("\n", "\r\n", "\r", "\v", "\f", "\u0085", "\u2028", "\u2029")
  strings <- wrap_charvec(c(
    paste0("a", seps, bom, "b"),
    paste0("a\n", bom, bom, "b"),
    paste0(bom, "  x"),
    paste0(bom, bom, "z"),
    paste0("mid", bom, "dle"),
    strrep(bom, 5),                       # leading run: emergent 4-stage strip
    paste0("a\n", strrep(bom, 3), "b"),
    paste0(strrep(bom, 2), "\n", bom, "y")
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  got <- wrap_backend(strings, width = 80L)
  want <- stringi::stri_wrap(as.character(strings), width = 80L)
  expect_identical(got, want)
  expect_identical(
    got,
    c(rep("a b", length(seps)), paste0("a ", bom, "b"), "x", "z",
      paste0("mid", bom, "dle"), "",
      paste0("a ", strrep(bom, 2), "b"), "y")
  )
})
