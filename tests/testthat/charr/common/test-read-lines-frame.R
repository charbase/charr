read_lines_frame_symbol <- function(backend) {
  namespace <- asNamespace("charr")
  name <- if (identical(backend, "base")) {
    "C_charr_base_ci_read_lines"
  } else if (identical(backend, "altrep")) {
    "C_ci_read_lines"
  } else {
    stop("unknown read-lines backend", call. = FALSE)
  }

  get(name, envir = namespace, inherits = FALSE)
}


read_lines_frame_call <- function(backend, path, encoding = "UTF-8") {
  if (identical(backend, "stringi")) {
    return(stringi::stri_read_lines(path, encoding = encoding))
  }

  .Call(read_lines_frame_symbol(backend), path, encoding)
}


read_lines_frame_file <- function(bytes) {
  path <- tempfile("charr-read-lines-frame-")
  writeBin(bytes, path)
  path
}


read_lines_frame_capture <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
      NULL
    }
  )
  list(value = value, events = events)
}


expect_read_lines_frame_shape <- function(backend, value) {
  expect_true(is.character(value))
  expect_null(attributes(value))

  if (identical(backend, "base")) {
    expect_false(charport::is_charvec(value))
  } else if (identical(backend, "altrep")) {
    expect_true(charport::is_charvec(value))
    expect_false(charport::charport_info(value)$is_materialized)
  }
  invisible(NULL)
}


as.character.read_lines_frame_path <- function(x, ...) {
  paste0(unclass(x))
}


test_that("read lines preserves whole-file Unicode and terminal semantics", {
  cases <- list(
    empty = raw(),
    bom_only = as.raw(c(0xef, 0xbb, 0xbf)),
    separators = c(
      as.raw(c(0xef, 0xbb, 0xbf)),
      charToRaw(paste0(
        "a\u00e9\U0001f642\r\n", "b\r", "c\n", "d\u0085",
        "e\v", "f\f", "g\u2028", "h\u2029", "i\n\n"
      ))
    ),
    terminal = charToRaw("first\nsecond\n")
  )

  for (case in names(cases)) {
    path <- read_lines_frame_file(cases[[case]])
    on.exit(unlink(path), add = TRUE)
    oracle <- read_lines_frame_call("stringi", path)

    for (backend in c("base", "altrep")) {
      actual <- read_lines_frame_call(backend, path)
      expect_read_lines_frame_shape(backend, actual)
      expect_identical(actual, oracle, info = paste(backend, case))
    }
  }
})


test_that("read lines preserves missing-file condition order", {
  path <- tempfile("charr-read-lines-frame-missing-")
  unlink(path)
  oracle <- read_lines_frame_capture(
    read_lines_frame_call("stringi", path)
  )

  expect_identical(
    sub(":.*", "", oracle$events),
    c("warning", "error")
  )
  expect_match(oracle$events[[1L]], "cannot open file")
  expect_match(oracle$events[[2L]], "cannot open the connection")

  for (backend in c("base", "altrep")) {
    actual <- read_lines_frame_capture(
      read_lines_frame_call(backend, path)
    )
    expect_identical(actual, oracle, info = backend)
  }
})


test_that("read lines preserves directory condition order", {
  skip_on_os("windows")
  path <- tempfile("charr-read-lines-frame-directory-")
  expect_true(dir.create(path))
  on.exit(unlink(path, recursive = TRUE), add = TRUE)
  oracle <- read_lines_frame_capture(
    read_lines_frame_call("stringi", path)
  )

  expect_identical(
    sub(":.*", "", oracle$events),
    c("warning", "warning", "error")
  )
  expect_match(oracle$events[[1L]], "not a regular file")
  expect_match(oracle$events[[2L]], "it is a directory")
  expect_match(oracle$events[[3L]], "cannot open the connection")

  for (backend in c("base", "altrep")) {
    actual <- read_lines_frame_capture(
      read_lines_frame_call(backend, path)
    )
    expect_identical(actual, oracle, info = backend)
  }
})


test_that("read lines repairs each malformed UTF-8 sequence", {
  malformed <- read_lines_frame_file(
    as.raw(c(0x61, 0xff, 0x62, 0xfe, 0x63, 0x0a, 0x64, 0xc3, 0x28))
  )
  on.exit(unlink(malformed), add = TRUE)
  oracle <- read_lines_frame_capture(
    read_lines_frame_call("stringi", malformed)
  )

  expect_false(any(grepl("^error:", oracle$events)))
  expect_gt(sum(grepl("^warning:", oracle$events)), 1L)

  for (backend in c("base", "altrep")) {
    actual <- read_lines_frame_capture(
      read_lines_frame_call(backend, malformed)
    )
    expect_identical(actual$value, oracle$value, info = backend)
    expect_identical(
      sum(grepl("^warning:", actual$events)),
      sum(grepl("^warning:", oracle$events)),
      info = backend
    )
    expect_false(any(grepl("^error:", actual$events)), info = backend)
    expect_true(all(grepl(
      "could not be converted",
      actual$events[grepl("^warning:", actual$events)]
    )))
  }
})


test_that("read lines recovers after an embedded NUL error", {
  invalid <- read_lines_frame_file(
    as.raw(c(0x61, 0x00, 0x62, 0x0a, 0x63))
  )
  valid <- read_lines_frame_file(charToRaw("after\nrecovery"))
  on.exit(unlink(c(invalid, valid)), add = TRUE)

  for (backend in c("base", "altrep")) {
    failed <- read_lines_frame_capture(
      read_lines_frame_call(backend, invalid)
    )
    expect_identical(sub(":.*", "", failed$events), "error", info = backend)
    expect_match(failed$events, "embedded nul", info = backend)

    recovered <- read_lines_frame_call(backend, valid)
    expect_read_lines_frame_shape(backend, recovered)
    expect_identical(recovered, c("after", "recovery"), info = backend)
  }
})


test_that("read lines recovers when malformed warnings become errors", {
  malformed <- read_lines_frame_file(as.raw(c(0x61, 0xff, 0x62)))
  valid <- read_lines_frame_file(charToRaw("still\nworks"))
  on.exit(unlink(c(malformed, valid)), add = TRUE)
  old_options <- options(warn = 2)
  on.exit(options(old_options), add = TRUE)

  for (backend in c("base", "altrep")) {
    error <- tryCatch(
      {
        read_lines_frame_call(backend, malformed)
        NULL
      },
      error = identity
    )
    expect_true(inherits(error, "error"), info = backend)
    expect_match(conditionMessage(error), "could not be converted", info = backend)

    recovered <- read_lines_frame_call(backend, valid)
    expect_read_lines_frame_shape(backend, recovered)
    expect_identical(recovered, c("still", "works"), info = backend)
  }
})


test_that("read lines validates path before encoding", {
  valid <- read_lines_frame_file(charToRaw("ok"))
  on.exit(unlink(valid), add = TRUE)
  cases <- list(
    both_missing = list(
      path = NA_character_,
      encoding = NA_character_,
      error = "invalid 'description' argument"
    ),
    encoding_missing = list(
      path = valid,
      encoding = NA_character_,
      error = "invalid 'encoding' value"
    )
  )

  for (case in names(cases)) {
    args <- cases[[case]]
    for (backend in c("base", "altrep")) {
      actual <- read_lines_frame_capture(
        read_lines_frame_call(backend, args$path, args$encoding)
      )
      expect_identical(
        sub(":.*", "", actual$events),
        "error",
        info = paste(backend, case)
      )
      expect_match(actual$events, args$error, info = paste(backend, case))
    }
  }
})


test_that("read lines dispatches coercion and drops attributes", {
  path <- read_lines_frame_file(charToRaw("coerced\npath"))
  on.exit(unlink(path), add = TRUE)
  decorated_path <- structure(
    path,
    class = "read_lines_frame_path",
    names = "fixture",
    note = "not propagated"
  )
  decorated_encoding <- structure(
    "UTF-8",
    class = "read_lines_frame_path",
    names = "encoding",
    note = "not propagated"
  )
  oracle <- read_lines_frame_call(
    "stringi", decorated_path, decorated_encoding
  )

  for (backend in c("base", "altrep")) {
    actual <- read_lines_frame_call(
      backend, decorated_path, decorated_encoding
    )
    expect_read_lines_frame_shape(backend, actual)
    expect_identical(actual, oracle, info = backend)
  }
})
