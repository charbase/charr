# charr-owned tests for the common ALTREP container layer. These are not
# imported from stringr.

call_container_test <- function(name, ...) {
  routine <- get(name, envir = asNamespace("charr"), inherits = FALSE)
  .Call(routine, ...)
}

encoded_raw <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_unmaterialized_charvec <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

test_that("UTF-8 and UTF-16 adapters round-trip charvec input", {
  values <- c("plain", "", NA, "caf\u00e9", "\U0001f600", "\u6c49\u5b57")
  input <- charport::as_charvec(values)

  expect_unmaterialized_charvec(input)
  out8 <- call_container_test("C_ci_test_UnicodeContainer8b", input)
  expect_unmaterialized_charvec(out8)
  expect_identical(as.character(out8), values)
  expect_unmaterialized_charvec(input)

  out16 <- call_container_test("C_ci_test_UnicodeContainer16b", input)
  expect_unmaterialized_charvec(out16)
  expect_identical(as.character(out16), values)
  expect_unmaterialized_charvec(input)
})

test_that("adapters preserve stringi encoding normalization", {
  latin1 <- encoded_raw(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c("plain", latin1)
  input <- charport::as_charvec(values)

  out8 <- call_container_test("C_ci_test_UnicodeContainer8b", input)
  out16 <- call_container_test("C_ci_test_UnicodeContainer16b", input)

  expect_identical(enc2utf8(as.character(out8)), enc2utf8(values))
  expect_identical(enc2utf8(as.character(out16)), enc2utf8(values))
  expect_identical(Encoding(as.character(out8)), c("unknown", "UTF-8"))
  expect_unmaterialized_charvec(input)
})

test_that("only the UTF-8 adapter strips a leading BOM", {
  value <- enc2utf8("\ufeffpayload")
  input <- charport::as_charvec(value)

  out8 <- call_container_test("C_ci_test_UnicodeContainer8b", input)
  out16 <- call_container_test("C_ci_test_UnicodeContainer16b", input)

  expect_identical(as.character(out8), "payload")
  expect_identical(as.character(out16), value)
  expect_unmaterialized_charvec(input)
})

test_that("non-ASCII bytes errors release the Reader", {
  input <- charport::as_charvec(encoded_raw(c(0xff, 0xfe), "bytes"))

  expect_error(
    call_container_test("C_ci_test_UnicodeContainer8b", input),
    "bytes encoding"
  )
  expect_unmaterialized_charvec(input)
  expect_error(
    call_container_test("C_ci_test_UnicodeContainer16b", input),
    "bytes encoding"
  )
  expect_unmaterialized_charvec(input)
})

test_that("exact input aliases share one Reader borrow", {
  values <- c("one", "two", "\u00e9")
  input <- charport::as_charvec(values)

  output <- call_container_test("C_ci_test_UnicodeContainer8_alias", input)

  expect_unmaterialized_charvec(output)
  expect_identical(as.character(output), values)
  expect_unmaterialized_charvec(input)
})

test_that("independent Readers may borrow the same input", {
  values <- c("one", "two", "\u00e9")
  input <- charport::as_charvec(values)

  output <- call_container_test(
    "C_ci_test_UnicodeContainer8_independent", input
  )

  expect_unmaterialized_charvec(output)
  expect_identical(as.character(output), values)
  expect_unmaterialized_charvec(input)
})

test_that("warning handlers run after Reader release", {
  pattern <- charport::as_charvec("")
  observed <- NULL

  withCallingHandlers(
    call_container_test("C_ci_test_ByteSearchContainer", pattern),
    warning = function(cnd) {
      observed <<- conditionMessage(cnd)
      expect_identical(pattern[[1]], "")
      invokeRestart("muffleWarning")
    }
  )

  expect_match(observed, "empty search pattern", ignore.case = TRUE)
})

test_that("queued warnings precede later errors after Reader release", {
  pattern <- charport::as_charvec("")
  observed <- NULL

  expect_error(
    withCallingHandlers(
      call_container_test("C_ci_test_ByteSearchContainer_error", pattern),
      warning = function(cnd) {
        observed <<- conditionMessage(cnd)
        expect_identical(pattern[[1]], "")
        invokeRestart("muffleWarning")
      }
    ),
    "error after queued warning"
  )
  expect_match(observed, "empty search pattern", ignore.case = TRUE)

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)
  warning_count <- 0L
  expect_error(
    withCallingHandlers(
      call_container_test(
        "C_ci_test_ByteSearchContainer", charport::as_charvec(c("", ""))
      ),
      warning = function(cnd) warning_count <<- warning_count + 1L
    ),
    "empty search pattern",
    ignore.case = TRUE
  )
  expect_identical(warning_count, 1L)

  expect_error(
    call_container_test("C_ci_test_ByteSearchContainer_error", pattern),
    "empty search pattern",
    ignore.case = TRUE
  )
})

test_that("fixed matchers stay within length-delimited records", {
  adjacent <- charport::as_charvec(c("ab", "cd"))

  expect_identical(
    call_container_test(
      "C_ci_test_ByteSearchMatcher", adjacent, "bc", FALSE
    ),
    c(-1L, -1L)
  )
  expect_identical(
    call_container_test(
      "C_ci_test_ByteSearchMatcher", "ab", "b", FALSE
    ),
    c(1L, 1L)
  )
  expect_identical(
    call_container_test(
      "C_ci_test_ByteSearchMatcher", "bcbc", "bc", FALSE
    ),
    c(0L, 2L)
  )
  expect_identical(
    call_container_test(
      "C_ci_test_ByteSearchMatcher", "xÉÉAyÉÉA", "ééa", TRUE
    ),
    c(1L, 7L)
  )
  expect_unmaterialized_charvec(adjacent)
})

test_that("UTF-8 record views copy bytes into output", {
  output <- call_container_test("C_ci_test_Utf8Record_views")

  expect_unmaterialized_charvec(output)
  expect_identical(
    as.character(output),
    c("owned", NA_character_, "borrowed")
  )
})

test_that("UTF-8 adapters scan only ambiguous marks and keep null-empty data", {
  expect_identical(
    call_container_test("C_ci_test_UTF8EncodingMarks"),
    c(TRUE, FALSE, TRUE, FALSE, TRUE, TRUE, TRUE)
  )
})

test_that("list containers handle aliases and empty children", {
  shared <- charport::as_charvec(c("a", "b"))

  expect_null(call_container_test("C_ci_test_ListUTF8", list(shared, shared), 2L))
  expect_unmaterialized_charvec(shared)
  expect_null(call_container_test("C_ci_test_ListUTF8", list(character()), 3L))
})
