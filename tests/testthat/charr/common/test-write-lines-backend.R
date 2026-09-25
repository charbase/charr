# Charr-owned tests for the public line writer.

write_lines_path <- function(prefix = "charr-write-lines-") {
  tempfile(prefix)
}

write_lines_bytes <- function(path) {
  connection <- file(path, open = "rb")
  on.exit(close(connection), add = TRUE)
  readBin(connection, what = "raw", n = file.info(path)$size)
}

write_lines_utf8 <- function(bytes) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- "UTF-8"
  value
}

write_lines_bytes_value <- function() {
  value <- rawToChar(as.raw(0xff))
  Encoding(value) <- "bytes"
  value
}

# Write through the native path and through a binary connection, which takes
# the backend's join-and-encode route, and return both files' bytes.
write_lines_both_routes <- function(values, ...) {
  path <- write_lines_path()
  connection_path <- write_lines_path("charr-write-lines-connection-")
  on.exit(unlink(c(path, connection_path)), add = TRUE)

  str_write_lines(values, path, ...)
  connection <- file(connection_path, open = "wb")
  str_write_lines(values, connection, ...)
  close(connection)

  list(
    path = write_lines_bytes(path),
    connection = write_lines_bytes(connection_path)
  )
}

write_lines_oracle <- function(values, sep = "\n", ...) {
  path <- write_lines_path("charr-write-lines-oracle-")
  on.exit(unlink(path), add = TRUE)
  stringi::stri_write_lines(values, path, sep = sep, ...)
  write_lines_bytes(path)
}

test_that("str_write_lines writes the same encoded records across backends", {
  values <- c("alpha", "caf\u00e9", "\U0001f642", "", "last")
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)

  result <- withVisible(str_write_lines(values, path))

  expect_identical(result$value, values)
  expect_false(result$visible)
  expect_identical(write_lines_bytes(path), write_lines_oracle(values))
  expect_identical(str_read_lines(path, encoding = "UTF-8"), values)

  routes <- write_lines_both_routes(values)
  expect_identical(routes$path, routes$connection)
})

test_that("str_write_lines follows stringi for BOMs and malformed UTF-8", {
  values <- c(
    "\ufeffbom",
    "inner\ufeffbom",
    write_lines_utf8(c(0x61, 0xff, 0x62)),
    write_lines_utf8(c(0x61, 0xe2, 0x82, 0x62)),
    write_lines_utf8(c(0x61, 0xed, 0xa0, 0x80, 0x62)),
    write_lines_utf8(c(0xc0, 0xaf, 0xf4, 0x90, 0x80, 0x80)),
    write_lines_utf8(c(0x61, 0xf0, 0x9f, 0x99))
  )

  routes <- write_lines_both_routes(values)
  expect_identical(routes$path, write_lines_oracle(values))
  expect_identical(routes$connection, routes$path)

  routes <- write_lines_both_routes("x", sep = "\ufeff|")
  expect_identical(routes$path, charToRaw("x|"))
  expect_identical(routes$connection, routes$path)
})

test_that("str_write_lines handles empty input, empty strings, and separators", {
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)

  str_write_lines(character(), path)
  expect_true(file.exists(path))
  expect_length(write_lines_bytes(path), 0L)

  routes <- write_lines_both_routes(character(), encoding = "latin1")
  expect_length(routes$path, 0L)
  expect_length(routes$connection, 0L)

  str_write_lines(c("", "x"), path, sep = "")
  expect_identical(write_lines_bytes(path), charToRaw("x"))

  str_write_lines(c("", ""), path)
  expect_identical(write_lines_bytes(path), charToRaw("\n\n"))

  str_write_lines(c("a", "b"), path, sep = "\r\n")
  expect_identical(write_lines_bytes(path), charToRaw("a\r\nb\r\n"))

  routes <- write_lines_both_routes(c("a", "b"), sep = "\u2028")
  expect_identical(
    routes$path,
    as.raw(c(0x61, 0xe2, 0x80, 0xa8, 0x62, 0xe2, 0x80, 0xa8))
  )
  expect_identical(routes$connection, routes$path)
})

test_that("str_write_lines converts marked inputs and output encodings", {
  latin1 <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(latin1) <- "latin1"
  ascii <- "plain"
  Encoding(ascii) <- "unknown"
  if (isTRUE(l10n_info()[["UTF-8"]])) {
    native <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
  } else {
    native <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  }
  Encoding(native) <- "unknown"
  values <- c("caf\u00e9", latin1, native, ascii)
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)

  routes <- write_lines_both_routes(values)
  expect_identical(routes$path, write_lines_oracle(values))
  expect_identical(routes$connection, routes$path)

  str_write_lines(values, path, encoding = "latin1")
  expect_identical(
    write_lines_bytes(path),
    as.raw(c(0x63, 0x61, 0x66, 0xe9, 0x0a,
             0x63, 0x61, 0x66, 0xe9, 0x0a,
             charToRaw(iconv(native, from = "", to = "latin1")),
             0x0a, 0x70, 0x6c, 0x61, 0x69, 0x6e, 0x0a))
  )
  expect_identical(
    write_lines_bytes(path),
    write_lines_oracle(values, encoding = "latin1")
  )

  routes <- write_lines_both_routes(
    c("caf\u00e9", latin1), encoding = "UTF-16LE", sep = "\u00e9"
  )
  expect_identical(
    routes$path,
    write_lines_oracle(
      c("caf\u00e9", latin1), encoding = "UTF-16LE", sep = "\u00e9"
    )
  )
  expect_identical(routes$connection, routes$path)

  null_path <- write_lines_path("charr-write-lines-null-encoding-")
  on.exit(unlink(null_path), add = TRUE)
  str_write_lines(values, null_path, encoding = NULL)
  native_path <- write_lines_path("charr-write-lines-native-")
  on.exit(unlink(native_path), add = TRUE)
  str_write_lines(values, native_path, encoding = "")
  expect_identical(
    write_lines_bytes(native_path),
    write_lines_bytes(null_path)
  )

  # Characters the output encoding cannot represent become its substitution
  # character, with a warning, as in stringi.
  unrepresentable <- "a\U0001f642b"
  expect_warning(
    str_write_lines(unrepresentable, path, encoding = "latin1"),
    "cannot be converted"
  )
  expect_identical(write_lines_bytes(path), charToRaw("a\032b\n"))
  expect_identical(
    write_lines_bytes(path),
    suppressWarnings(write_lines_oracle(unrepresentable, encoding = "latin1"))
  )
})

test_that("str_write_lines supports binary connections and path translation", {
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)
  connection <- file(path, open = "wb")
  str_write_lines(c("a", "b"), connection, sep = "")
  str_write_lines("c", connection)
  expect_true(isOpen(connection))
  close(connection)
  expect_identical(write_lines_bytes(path), charToRaw("abc\n"))

  # A vector longer than one write chunk of the encode route.
  many <- rep(c("a", "b"), 40000L)
  connection <- file(path, open = "wb")
  str_write_lines(many, connection, encoding = "latin1")
  close(connection)
  expect_identical(str_read_lines(path, encoding = "latin1"), many)

  unopened <- file(path)
  expect_error(str_write_lines("x", unopened), "binary mode")
  expect_false(isOpen(unopened))
  close(unopened)
  text_connection <- file(path, open = "w")
  expect_error(str_write_lines("x", text_connection))
  close(text_connection)

  old_home <- Sys.getenv("HOME")
  on.exit(Sys.setenv(HOME = old_home), add = TRUE)
  Sys.setenv(HOME = tempdir())
  home_path <- path.expand("~/charr-write-lines-test.txt")
  on.exit(unlink(home_path), add = TRUE)
  str_write_lines("tilde", "~/charr-write-lines-test.txt")
  expect_identical(write_lines_bytes(home_path), charToRaw("tilde\n"))
  str_write_lines("tilde", "~/charr-write-lines-test.txt", encoding = "latin1")
  expect_identical(write_lines_bytes(home_path), charToRaw("tilde\n"))

  unicode_path <- tempfile(paste0("charr-\u00e9-"))
  on.exit(unlink(unicode_path), add = TRUE)
  str_write_lines("unicode path", unicode_path)
  expect_identical(str_read_lines(unicode_path), "unicode path")
})

test_that("str_write_lines errors on missing values before writing", {
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)

  expect_error(str_write_lines(c("ok", NA_character_), path))
  expect_false(file.exists(path))
  expect_error(str_write_lines(NA_character_, path, encoding = "latin1"))
  expect_false(file.exists(path))

  connection <- file(path, open = "wb")
  expect_error(str_write_lines(c("ok", NA_character_), connection))
  close(connection)
  expect_length(write_lines_bytes(path), 0L)
})

test_that("str_write_lines rejects bytes-encoded strings", {
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)
  bytes <- write_lines_bytes_value()

  expect_error(str_write_lines(bytes, path), "bytes")
  expect_error(str_write_lines(c("a", bytes), path, encoding = "latin1"), "bytes")
  expect_error(str_write_lines("a", path, sep = bytes), "bytes")

  connection <- file(path, open = "wb")
  on.exit(close(connection), add = TRUE)
  expect_error(str_write_lines(bytes, connection), "bytes")
})

test_that("str_write_lines validates its arguments", {
  path <- write_lines_path()
  on.exit(unlink(path), add = TRUE)

  expect_error(str_write_lines(1, path))
  expect_error(str_write_lines(list("x"), path))
  expect_error(str_write_lines(NULL, path))
  expect_error(str_write_lines("x", character()))
  expect_error(str_write_lines("x", c(path, path)))
  expect_error(str_write_lines("x", NA_character_))
  expect_error(str_write_lines("x", 1))
  expect_error(str_write_lines("x", path, encoding = NA_character_))
  expect_error(str_write_lines("x", path, encoding = c("UTF-8", "latin1")))
  expect_error(str_write_lines("x", path, encoding = 1))
  expect_error(str_write_lines("x", path, sep = c("a", "b")))
  expect_error(str_write_lines("x", path, sep = NA_character_))
  expect_error(str_write_lines("x", path, sep = NULL))
  expect_error(str_write_lines("x", path, sep = 1))
  expect_false(file.exists(path))
})

test_that("str_write_lines errors cleanly for unwritable paths", {
  directory <- write_lines_path("charr-write-lines-directory-")
  on.exit(unlink(directory, recursive = TRUE), add = TRUE)
  missing <- file.path(directory, "missing", "file")

  suppressWarnings(expect_error(str_write_lines("x", missing)))
  suppressWarnings(expect_error(
    str_write_lines("x", missing, encoding = "latin1")
  ))
  expect_true(dir.create(directory))
  suppressWarnings(expect_error(str_write_lines("x", directory)))
})

test_that("str_write_lines streams ALTREP input without materializing it", {
  skip_if_not(identical(selected_test_backend, "altrep"))
  input_path <- write_lines_path("charr-write-lines-input-")
  output_path <- write_lines_path("charr-write-lines-output-")
  on.exit(unlink(c(input_path, output_path)), add = TRUE)
  writeLines(rep(c("stream", "caf\u00e9"), 100L), input_path, useBytes = TRUE)

  values <- str_read_lines(input_path, encoding = "UTF-8")
  expect_altrep_unmaterialized(values)
  str_write_lines(values, output_path)
  expect_altrep_unmaterialized(values)
  expect_identical(write_lines_bytes(output_path), write_lines_bytes(input_path))

  upper <- str_to_upper(values)
  expect_altrep_unmaterialized(upper)
  str_write_lines(upper, output_path)
  expect_altrep_unmaterialized(upper)
  expect_identical(
    str_read_lines(output_path, encoding = "UTF-8"),
    rep(c("STREAM", "CAF\u00c9"), 100L)
  )

  str_write_lines(upper, output_path, encoding = "latin1")
  expect_altrep_unmaterialized(upper)
  expect_identical(
    str_read_lines(output_path, encoding = "latin1"),
    rep(c("STREAM", "CAF\u00c9"), 100L)
  )
})
