write_lines_frame_symbol <- function(backend) {
  namespace <- asNamespace("charr")
  name <- if (identical(backend, "base")) {
    "C_charr_base_ci_write_lines"
  } else if (identical(backend, "altrep")) {
    "C_ci_write_lines"
  } else {
    stop("unknown write-lines backend", call. = FALSE)
  }

  get(name, envir = namespace, inherits = FALSE)
}

test_that("write lines closes files and recovers after native errors", {
  path <- tempfile("charr-write-lines-frame-")
  missing <- file.path(path, "missing", "output")
  on.exit(unlink(path), add = TRUE)

  for (backend in c("base", "altrep")) {
    native <- write_lines_frame_symbol(backend)
    expect_error(
      .Call(native, "first", missing, "\n"),
      "cannot write file"
    )
    expect_error(
      .Call(native, c("first", NA_character_), path, "\n"),
      "missing strings"
    )
    expect_error(.Call(native, "first", NA_character_, "\n"), "'con'")
    expect_error(.Call(native, "first", path, NA_character_), "'sep'")
    expect_false(file.exists(path))

    expect_identical(
      .Call(native, c("first", "second"), path, "\n"),
      c("first", "second")
    )
    expect_identical(
      readBin(path, what = "raw", n = file.info(path)$size),
      charToRaw("first\nsecond\n")
    )
    unlink(path)
  }
})


test_that("write lines reports buffered write and close failures", {
  skip_if_not(file.exists("/dev/full") && dir.exists("/proc/self/fd"))

  # A short record fails when fclose flushes the buffer; a long one fails in
  # fwrite. The descriptor count shows the Frame closed the handle on the
  # error path.
  open_descriptors <- function() length(list.files("/proc/self/fd"))
  short <- "x"
  long <- strrep("x", 70000L)
  for (backend in c("base", "altrep")) {
    native <- write_lines_frame_symbol(backend)
    for (value in list(short, long)) {
      before <- open_descriptors()
      for (i in seq_len(20L)) {
        expect_error(
          .Call(native, value, "/dev/full", "\n"),
          "cannot write file '/dev/full'"
        )
      }
      expect_identical(open_descriptors(), before)
    }

    path <- tempfile("charr-write-lines-frame-recovered-")
    expect_identical(.Call(native, "ok", path, "\n"), "ok")
    expect_identical(
      readBin(path, what = "raw", n = file.info(path)$size),
      charToRaw("ok\n")
    )
    unlink(path)
  }
})
