# charr-owned file: tools/import-upstream.R must not rename here.
#
# Fixed-search encoding, recycling, and option equivalence. The oracle uses
# installed stringi; the selected route uses the current test backend. Every
# literal must hold identically on both routes. Fixed byte
# search matches raw declared-UTF-8 bytes without validation, so malformed
# UTF-8 passes through instead of being rejected. Only bytes-marked input
# errors.

mkenc_fixed <- function(bytes, enc) {
  s <- if (is.character(bytes)) bytes else rawToChar(as.raw(bytes))
  Encoding(s) <- enc
  s
}

detect_fixed <- function(...) charr_test_leaf("ci_detect_fixed")(...)
count_fixed <- function(...) charr_test_leaf("ci_count_fixed")(...)
starts_fixed <- function(...) charr_test_leaf("ci_startswith_fixed")(...)
ends_fixed <- function(...) charr_test_leaf("ci_endswith_fixed")(...)


test_that("fixed search handles the str encoding matrix", {
  utf8_e <- mkenc_fixed(c(0xc3, 0xa9), "UTF-8")
  latin1_e <- mkenc_fixed(0xe9, "latin1")
  native_e <- mkenc_fixed(c(0xc3, 0xa9), "unknown")
  strings <- c(
    "plain", paste0("caf", utf8_e), paste0("caf", latin1_e),
    paste0("caf", native_e), "ü", "ü", "x😀y"
  )
  patterns <- c("ain", utf8_e, latin1_e, native_e, "̈", "ü", "😀")

  expect_identical(
    detect_fixed(strings, patterns),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE)
  )
  expect_identical(count_fixed(strings, patterns), rep(1L, 7L))
  expect_identical(
    starts_fixed(strings, patterns),
    c(FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE)
  )
  expect_identical(
    ends_fixed(strings, patterns),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE)
  )
})


test_that("fixed search handles the pattern encoding matrix", {
  utf8_e <- mkenc_fixed(c(0xc3, 0xa9), "UTF-8")
  latin1_e <- mkenc_fixed(0xe9, "latin1")
  patterns <- c("ain", utf8_e, latin1_e, "̈", "ü", "😀")

  expect_identical(
    detect_fixed("é😀ü", patterns),
    c(FALSE, TRUE, TRUE, TRUE, FALSE, TRUE)
  )
  expect_identical(
    count_fixed("é😀ü", patterns),
    c(0L, 1L, 1L, 1L, 0L, 1L)
  )
})


test_that("fixed search accepts native UTF-8 patterns", {
  skip_if_not(l10n_info()[["UTF-8"]])
  native_e <- mkenc_fixed(c(0xc3, 0xa9), "unknown")

  expect_identical(detect_fixed("é😀ü", native_e), TRUE)
  expect_identical(count_fixed("é😀ü", native_e), 1L)
})


test_that("fixed count keeps its direct prefix across mixed encodings", {
  latin1 <- mkenc_fixed(c(0x61, 0xe9, 0x61), "latin1")
  malformed <- mkenc_fixed(c(0x61, 0xff, 0x61), "UTF-8")

  expect_identical(
    count_fixed(c("aaaa", latin1, malformed, NA_character_, ""), "a"),
    c(4L, 2L, 2L, NA_integer_, 0L)
  )
})


test_that("fixed starts and ends keep direct prefixes across encodings", {
  latin1 <- mkenc_fixed(c(0x61, 0xe9, 0x61), "latin1")
  malformed <- mkenc_fixed(c(0x61, 0xff, 0x61), "UTF-8")
  strings <- c("aba", latin1, malformed, NA_character_, "")
  expected <- c(TRUE, TRUE, TRUE, NA, FALSE)

  expect_identical(starts_fixed(strings, "a"), expected)
  expect_identical(ends_fixed(strings, "a"), expected)
})


test_that("fixed search strips exactly one leading UTF-8 BOM", {
  one_bom <- mkenc_fixed(c(0xef, 0xbb, 0xbf, 0x61), "UTF-8")
  two_boms <- mkenc_fixed(
    c(0xef, 0xbb, 0xbf, 0xef, 0xbb, 0xbf, 0x61),
    "UTF-8"
  )

  expect_identical(starts_fixed(one_bom, "a"), TRUE)
  expect_identical(count_fixed(one_bom, "a"), 1L)
  expect_identical(ends_fixed(one_bom, "a"), TRUE)

  # killbom removes one prefix only; a second BOM remains before the 'a'.
  expect_identical(starts_fixed(two_boms, "a"), FALSE)
  expect_identical(count_fixed(two_boms, "a"), 1L)
  expect_identical(ends_fixed(two_boms, "a"), TRUE)
})


test_that("fixed search strips a native BOM in a UTF-8 locale", {
  skip_if_not(l10n_info()[["UTF-8"]])
  native_bom <- mkenc_fixed(c(0xef, 0xbb, 0xbf, 0x61), "unknown")

  expect_identical(starts_fixed(native_bom, "a"), TRUE)
})


test_that("fixed search preserves two- and three-input recycling", {
  expect_identical(
    detect_fixed("abcabc", c("a", "bc", "z")),
    c(TRUE, TRUE, FALSE)
  )
  expect_identical(
    count_fixed(c("aaaa", "baaa", ""), "aa"),
    c(2L, 1L, 0L)
  )
  expect_identical(
    detect_fixed(c("abc", "xyz"), c("a", "y", "z", "q")),
    c(TRUE, TRUE, FALSE, FALSE)
  )
  expect_identical(
    starts_fixed(
      c("abc", "xyz"), c("a", "y", "c", "z"), c(1L, 2L)
    ),
    c(TRUE, TRUE, FALSE, FALSE)
  )
  expect_identical(
    ends_fixed(
      c("abc", "xyz"), c("a", "y", "c", "z"), c(1L, 2L)
    ),
    c(TRUE, TRUE, FALSE, FALSE)
  )
})


test_that("fixed search preserves NA and empty string branches", {
  expect_identical(
    detect_fixed(c(NA, "", "abc"), c("a", "a", NA)),
    c(NA, FALSE, NA)
  )
  expect_identical(
    count_fixed(c(NA, "", "abc"), c("a", "a", NA)),
    c(NA_integer_, 0L, NA_integer_)
  )
  expect_identical(
    starts_fixed(
      c(NA, "", "abc", "abc"), c("a", "a", NA, "a"),
      c(1L, NA, 1L, NA)
    ),
    c(NA, FALSE, NA, NA)
  )
  expect_identical(
    ends_fixed(
      c(NA, "", "abc", "abc"), c("a", "a", NA, "c"),
      c(-1L, NA, -1L, NA)
    ),
    c(NA, FALSE, NA, NA)
  )
})


test_that("empty fixed patterns warn and produce NA", {
  expect_warning(
    detected <- detect_fixed(c("abc", ""), ""),
    "empty search patterns are not supported"
  )
  expect_identical(detected, c(NA, NA))

  expect_warning(
    counted <- count_fixed(c("abc", ""), ""),
    "empty search patterns are not supported"
  )
  expect_identical(counted, c(NA_integer_, NA_integer_))

  expect_warning(
    started <- starts_fixed(c("abc", ""), ""),
    "empty search patterns are not supported"
  )
  expect_identical(started, c(NA, NA))

  expect_warning(
    ended <- ends_fixed(c("abc", ""), ""),
    "empty search patterns are not supported"
  )
  expect_identical(ended, c(NA, NA))
})


test_that("fixed search preserves negate, case, and overlap options", {
  expect_identical(
    detect_fixed(c("abc", "xyz", NA), "a", negate = TRUE),
    c(FALSE, TRUE, NA)
  )
  expect_identical(
    starts_fixed(c("abc", "xyz", NA), "a", negate = TRUE),
    c(FALSE, TRUE, NA)
  )
  expect_identical(
    ends_fixed(c("abc", "xyz", NA), "c", negate = TRUE),
    c(FALSE, TRUE, NA)
  )

  case_opts <- list(case_insensitive = TRUE)
  expect_identical(
    detect_fixed(c("Alpha", "BETA", "ß"), "a", opts_fixed = case_opts),
    c(TRUE, TRUE, FALSE)
  )
  expect_identical(
    starts_fixed(c("Alpha", "beta"), "A", opts_fixed = case_opts),
    c(TRUE, FALSE)
  )
  expect_identical(
    ends_fixed(c("Alpha", "BETA"), "A", opts_fixed = case_opts),
    c(TRUE, TRUE)
  )

  expect_identical(
    count_fixed(
      c("aaaa", "AaAa", "abababa"), c("aa", "aa", "aba"),
      opts_fixed = list(case_insensitive = TRUE, overlap = TRUE)
    ),
    c(3L, 3L, 3L)
  )
})


test_that("detect fixed preserves max_count traversal order", {
  strings <- c("hit", "miss", "hit", "miss", "hit", "miss")
  patterns <- c("hit", "x")

  expect_identical(
    detect_fixed(strings, patterns, max_count = 0L),
    rep(NA, 6L)
  )
  expect_identical(
    detect_fixed(strings, patterns, max_count = 1L),
    c(TRUE, NA, NA, NA, NA, NA)
  )
  expect_identical(
    detect_fixed(strings, patterns, max_count = 3L),
    c(TRUE, NA, TRUE, NA, TRUE, NA)
  )
  expect_identical(
    detect_fixed(strings, patterns, max_count = -1L),
    c(TRUE, FALSE, TRUE, FALSE, TRUE, FALSE)
  )
  expect_identical(
    detect_fixed(
      c("hit", "", "miss", "hit"), c("hit", "x"),
      negate = TRUE, max_count = 2L
    ),
    c(FALSE, TRUE, TRUE, NA)
  )

  latin1_e <- mkenc_fixed(0xe9, "latin1")
  expect_identical(
    detect_fixed(c("miss", paste0("caf", latin1_e), "a"), "a",
                 max_count = 1L),
    c(FALSE, TRUE, NA)
  )
})


test_that("fixed starts/ends preserve positive and negative indices", {
  string <- "aé😀bcé"
  expect_identical(
    starts_fixed(
      string, c("a", "é", "é", "b", "z", "a"),
      from = c(1L, -1L, 2L, -2L, 99L, 0L)
    ),
    c(TRUE, TRUE, TRUE, FALSE, FALSE, TRUE)
  )
  expect_identical(
    ends_fixed(
      string, c("é", "a", "é", "c", "é", "z"),
      to = c(-1L, 1L, 2L, -2L, 99L, 0L)
    ),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, FALSE)
  )
})


test_that("fixed search rejects bytes-marked input and patterns", {
  bytes <- mkenc_fixed(c(0xff, 0xfe), "bytes")
  calls <- list(
    function(x, p) detect_fixed(x, p),
    function(x, p) count_fixed(x, p),
    function(x, p) starts_fixed(x, p),
    function(x, p) ends_fixed(x, p)
  )
  for (call in calls) {
    expect_error(call(bytes, "a"), "bytes encoding")
    expect_error(call("abc", bytes), "bytes encoding")
  }

  # Input preparation scans every str element before detect's max_count
  # traversal, so bytes errors are not hidden by max_count == 0 or exhaustion.
  expect_error(
    detect_fixed(bytes, "a", max_count = 0L),
    "bytes encoding"
  )
  expect_error(
    detect_fixed(c("hit", bytes), "hit", max_count = 1L),
    "bytes encoding"
  )

  # Str acquisition also precedes pattern construction and its empty warning.
  expect_error(
    expect_no_warning(detect_fixed(bytes, "", max_count = 0L)),
    "bytes encoding"
  )
})


test_that("fixed search passes malformed UTF-8 through verbatim (no validation)", {
  # Fixed byte-search does not validate UTF-8: it matches raw declared-UTF-8
  # bytes and passes them through after BOM handling. These therefore hold
  # identically OFF and ON -- a regression to validation or replacement on
  # either route fails here.
  bad <- mkenc_fixed(c(0x61, 0xff, 0x62), "UTF-8")   # a <ff> b
  expect_identical(detect_fixed(bad, "b"), TRUE)
  expect_identical(detect_fixed(bad, "a"), TRUE)
  expect_identical(count_fixed(bad, "b"), 1L)
  expect_identical(starts_fixed(bad, "a"), TRUE)
  expect_identical(ends_fixed(bad, "b"), TRUE)
  # Lenient U8_FWD_1/U8_BACK_1 indexing counts the malformed byte as one
  # position, so the trailing 'b' is code-point position 3.
  expect_identical(starts_fixed(bad, "b", from = 3L), TRUE)
  expect_identical(starts_fixed(bad, "b", from = 2L), FALSE)
  expect_identical(ends_fixed(bad, "a", to = 1L), TRUE)
  expect_identical(ends_fixed(bad, "a", to = -3L), TRUE)
  # the bad byte is passed through, not replaced with U+FFFD
  expect_identical(count_fixed(bad, "�"), 0L)
  bad_pattern <- mkenc_fixed(0xff, "UTF-8")
  expect_identical(detect_fixed(bad, bad_pattern), TRUE)
  expect_identical(count_fixed(bad, bad_pattern), 1L)
  # no error and no warning on either route
  expect_silent(detect_fixed(bad, "a"))

  # a truncated lead byte is likewise passed through, not rejected
  trunc <- mkenc_fixed(c(0x61, 0xc3), "UTF-8")
  expect_identical(detect_fixed(trunc, "a"), TRUE)
  expect_identical(count_fixed(trunc, "a"), 1L)

  expect_identical(
    detect_fixed(c("a", bad), "a", max_count = 1L),
    c(TRUE, NA)
  )
})


test_that("fixed predicates borrow native bytes on UTF-8 locales", {
  skip_if_not(l10n_info()[["UTF-8"]])
  bad_native <- mkenc_fixed(c(0x61, 0xff, 0x62), "unknown")
  bad_pattern <- mkenc_fixed(0xff, "unknown")

  expect_identical(
    detect_fixed(c("hit", bad_native), "hit", max_count = 1L),
    c(TRUE, NA)
  )
  expect_identical(
    count_fixed(c("hit", bad_native), "i"),
    c(1L, 0L)
  )
  expect_identical(
    starts_fixed(c("hit", bad_native), "h"),
    c(TRUE, FALSE)
  )
  expect_identical(
    ends_fixed(c("hit", bad_native), "t"),
    c(TRUE, FALSE)
  )
  expect_identical(detect_fixed(bad_native, bad_pattern), TRUE)
  expect_identical(count_fixed(bad_native, bad_pattern), 1L)
})


test_that("fixed matchers stay within Reader-backed record lengths", {
  # The ALTREP route makes this a charvec whose records are length-delimited,
  # not C strings. Neighbouring records must never satisfy a search in the
  # preceding record. The stringi oracle asserts the same observable result.
  strings <- charr_test_leaf("ci_trim_both")(c("aaa", "z", "aaaa", "xyz", "abcx"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    detect_fixed(strings, c("z", "z", "xy", "xy", "x")),
    c(FALSE, TRUE, FALSE, TRUE, TRUE)
  )
  expect_identical(
    count_fixed(strings, c("z", "z", "xy", "xy", "x")),
    c(0L, 1L, 0L, 1L, 1L)
  )
})

test_that("short fixed matchers skip false first-byte candidates", {
  strings <- charr_test_leaf("ci_trim_both")(c(
    "ttttttthe", "ttttttthx", "abababababa", "abcabcabc", "short"
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(
    detect_fixed(strings, c("the", "the", "aba", "cab", "shorter")),
    c(TRUE, FALSE, TRUE, TRUE, FALSE)
  )
  expect_identical(
    count_fixed(strings, c("the", "the", "aba", "cab", "shorter")),
    c(1L, 0L, 3L, 2L, 0L)
  )
})


test_that("fixed search preserves large-corpus traversal", {
  corpus <- rep(c("abc", "café", "😀a", "汉字", "", "üa"), 5000)

  run <- function() {
    list(
      detect = detect_fixed(corpus, "a"),
      count = count_fixed(corpus, "a"),
      starts = starts_fixed(corpus, "a"),
      ends = ends_fixed(corpus, "a"),
      limited = detect_fixed(corpus, "a", max_count = 3L)
    )
  }

  result <- run()
  expect_identical(
    result,
    list(
      detect = stringi::stri_detect_fixed(corpus, "a"),
      count = stringi::stri_count_fixed(corpus, "a"),
      starts = stringi::stri_startswith_fixed(corpus, "a"),
      ends = stringi::stri_endswith_fixed(corpus, "a"),
      limited = stringi::stri_detect_fixed(corpus, "a", max_count = 3L)
    )
  )
  expect_identical(
    result$detect,
    rep(c(TRUE, TRUE, TRUE, FALSE, FALSE, TRUE), 5000)
  )
  expect_identical(
    result$count,
    rep(c(1L, 1L, 1L, 0L, 0L, 1L), 5000)
  )
})


test_that("buried bytes errors are independent of position", {
  make <- function(pos, n = 12000L) {
    x <- rep("abc", n)
    x[pos] <- mkenc_fixed(0xff, "bytes")
    x
  }
  calls <- list(
    function(x) detect_fixed(x, "a"),
    function(x) count_fixed(x, "a"),
    function(x) starts_fixed(x, "a"),
    function(x) ends_fixed(x, "a")
  )
  for (call in calls) {
    expect_error(call(make(1L)), "bytes encoding")
    expect_error(call(make(6123L)), "bytes encoding")
    expect_error(call(make(12000L)), "bytes encoding")
  }
})


test_that("buried malformed UTF-8 is passed through at any position", {
  # The malformed element still contains 'a' and 'b'; it must byte-match like
  # any other element at every position, with no error on either route.
  make <- function(pos, n = 12000L) {
    x <- rep("abc", n)
    x[pos] <- mkenc_fixed(c(0x61, 0xff, 0x62), "UTF-8")
    x
  }
  for (pos in c(1L, 6123L, 12000L)) {
    x <- make(pos)
    expect_identical(detect_fixed(x, "a"), rep(TRUE, 12000L))
    expect_identical(count_fixed(x, "a"), rep(1L, 12000L))
  }
})
