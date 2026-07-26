# Charr-owned tests for the public whole-file line reader.

write_line_test_file <- function(bytes) {
  path <- tempfile("charr-lines-")
  writeBin(bytes, path)
  path
}

expect_read_lines_backend_equal <- function(bytes, encoding) {
  path <- write_line_test_file(bytes)
  on.exit(unlink(path), add = TRUE)

  expected <- with_backend("stringi", str_read_lines(path, encoding))
  actual <- with_backend(
    selected_test_backend,
    str_read_lines(path, encoding)
  )

  expect_false(charport::is_charvec(expected))
  expect_identical(
    charport::is_charvec(actual),
    identical(selected_test_backend, "altrep")
  )
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(actual)$is_materialized)
  }
  expect_identical(actual, expected)
  invisible(actual)
}

capture_read_lines <- function(backend, con, encoding) {
  warnings <- character()
  error <- NULL
  value <- tryCatch(
    withCallingHandlers(
      with_backend(backend, str_read_lines(con, encoding)),
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

test_that("str_read_lines preserves stringi Unicode line splitting", {
  text <- paste0(
    "aé🙂\r\n", "b\r", "c\n", "d\u0085", "e\v", "f\f",
    "g\u2028", "h\u2029", "i\n\n"
  )
  actual <- expect_read_lines_backend_equal(charToRaw(text), "UTF-8")

  expect_identical(
    actual,
    c("aé🙂", "b", "c", "d", "e", "f", "g", "h", "i", "")
  )
})

test_that("str_read_lines preserves terminal separator semantics on file paths", {
  cases <- list(
    raw(),
    charToRaw("\n"),
    charToRaw("\n\n"),
    charToRaw("a\n"),
    charToRaw("a\n\n"),
    charToRaw("\r\r\n"),
    charToRaw("a\u2028"),
    c(as.raw(c(0xef, 0xbb, 0xbf)), charToRaw("\n"))
  )

  for (bytes in cases) {
    expect_read_lines_backend_equal(bytes, "utf8")
  }
})

test_that("str_read_lines converts declared input encodings", {
  latin1 <- as.raw(c(0x63, 0x61, 0x66, 0xe9, 0x0a, 0x66, 0x69, 0x6e))
  actual <- expect_read_lines_backend_equal(latin1, "latin1")

  expect_identical(actual, c("caf\u00e9", "fin"))
  expect_read_lines_backend_equal(raw(), "UTF-8")
  expect_read_lines_backend_equal(charToRaw("\n\n"), "UTF-8")
})

test_that("str_read_lines handles BOMs and long records mechanically", {
  bom <- as.raw(c(0xef, 0xbb, 0xbf))
  expect_identical(
    expect_read_lines_backend_equal(c(bom, charToRaw("alpha\nbeta")), "UTF-8"),
    c("alpha", "beta")
  )
  expect_identical(
    expect_read_lines_backend_equal(bom, "UTF-8"),
    ""
  )

  long <- strrep("a", 200000)
  actual <- expect_read_lines_backend_equal(
    c(charToRaw(long), as.raw(0x0a), charToRaw("z")),
    "UTF-8"
  )
  expect_identical(nchar(actual, type = "bytes"), c(200000L, 1L))
})

test_that("str_read_lines preserves conversion warnings and input errors", {
  malformed_path <- write_line_test_file(
    as.raw(c(0x61, 0xc3, 0x28, 0x0a, 0x62))
  )
  on.exit(unlink(malformed_path), add = TRUE)
  expected <- capture_read_lines("stringi", malformed_path, "UTF-8")
  actual <- capture_read_lines(
    selected_test_backend, malformed_path, "UTF-8"
  )
  expect_identical(actual$value, expected$value)
  expect_identical(actual$error, expected$error)
  expect_length(actual$warnings, length(expected$warnings))
  expect_match(actual$warnings, "could not be converted")
  expect_identical(actual$value, c("a\ufffd(", "b"))

  nul_path <- write_line_test_file(
    as.raw(c(0x61, 0x00, 0x62, 0x0a, 0x63))
  )
  on.exit(unlink(nul_path), add = TRUE)
  expected_nul <- capture_read_lines("stringi", nul_path, "UTF-8")
  actual_nul <- capture_read_lines(
    selected_test_backend, nul_path, "UTF-8"
  )
  expect_identical(actual_nul$value, expected_nul$value)
  expect_identical(actual_nul$warnings, expected_nul$warnings)
  # The optimized conversion rejects the zero byte before R constructs a
  # CHARSXP, so it cannot append stringi's printable copy of the payload.
  expect_match(expected_nul$error, "embedded nul")
  expect_match(actual_nul$error, "embedded nul")

  auto_path <- write_line_test_file(charToRaw("a"))
  on.exit(unlink(auto_path), add = TRUE)
  expected_auto <- capture_read_lines("stringi", auto_path, "auto")
  actual_auto <- capture_read_lines(
    selected_test_backend, auto_path, "auto"
  )
  expect_identical(actual_auto, expected_auto)
  expect_match(actual_auto$error, "no longer supported")

  missing_path <- tempfile("charr-lines-missing-")
  expected_missing <- capture_read_lines("stringi", missing_path, "UTF-8")
  actual_missing <- capture_read_lines(
    selected_test_backend, missing_path, "UTF-8"
  )
  expect_identical(actual_missing, expected_missing)
  expect_match(actual_missing$error, "cannot open the connection")
})

test_that("str_read_lines leaves caller-owned connections open", {
  bytes <- charToRaw("a\r\nb\nc")
  expected_connection <- rawConnection(bytes, open = "rb")
  on.exit(close(expected_connection), add = TRUE)
  expected <- with_backend(
    "stringi",
    str_read_lines(expected_connection, "UTF-8")
  )

  connection <- rawConnection(bytes, open = "rb")
  on.exit(close(connection), add = TRUE)
  actual <- with_backend(
    selected_test_backend,
    str_read_lines(connection, "UTF-8")
  )

  expect_true(isOpen(connection))
  expect_identical(
    charport::is_charvec(actual),
    identical(selected_test_backend, "altrep")
  )
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(actual)$is_materialized)
  }
  expect_identical(actual, expected)
})
