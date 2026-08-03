# Covers NativeToUtf8::native_is_utf8(), the single facility deciding whether
# R's native encoding is UTF-8. It replaced a per-backend locale-name scan that
# could disagree with the Riconv converter actually used to transcode.

native_probe_locale <- function(candidates, predicate) {
  for (candidate in unique(candidates)) {
    locale <- suppressWarnings(Sys.setlocale("LC_CTYPE", candidate))
    if (nzchar(locale) && isTRUE(predicate())) {
      return(locale)
    }
  }

  NULL
}

# "café" as native bytes: valid ISO-8859-1, and NOT valid UTF-8. A backend that
# wrongly believes the native encoding is UTF-8 reads these bytes directly and
# raises an invalid-UTF-8 condition instead of transcoding them.
native_probe_bytes <- function() {
  value <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(value) <- "unknown"
  value
}


test_that("the native-encoding probe agrees with iconv in a UTF-8 locale", {
  original <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original), add = TRUE)

  utf8_locale <- native_probe_locale(
    c(original, "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(is.null(utf8_locale), "No usable UTF-8 LC_CTYPE locale is available")

  Sys.setlocale("LC_CTYPE", utf8_locale)
  # Native bytes that ARE valid UTF-8 here, so no transcoding is needed and the
  # probe must say so by leaving the payload untouched.
  value <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
  Encoding(value) <- "unknown"

  expected <- with_backend("stringi", str_length(value))
  expect_identical(expected, 4L)
  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_length(value)), expected)
  }
})


test_that("the native-encoding probe transcodes in a single-byte locale", {
  original <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original), add = TRUE)

  value <- native_probe_bytes()
  single_byte <- native_probe_locale(
    c(
      "en_US.ISO8859-1", "en_US.iso88591", "en_US.ISO-8859-1",
      "de_DE.ISO8859-1", "de_DE.iso88591", "fr_FR.ISO8859-1",
      "English_United States.1252", "English_United States"
    ),
    function() {
      !isTRUE(l10n_info()[["UTF-8"]]) &&
        !is.na(suppressWarnings(
          iconv(value, from = "", to = "UTF-8", sub = NA_character_)
        ))
    }
  )
  skip_if(
    is.null(single_byte),
    "No usable non-UTF-8 single-byte LC_CTYPE locale is available"
  )

  # iconv is the oracle for what the native bytes decode to. The probe must
  # route through conversion rather than reading the bytes as UTF-8.
  decoded <- iconv(value, from = "", to = "UTF-8")
  Encoding(decoded) <- "UTF-8"
  expect_identical(nchar(decoded), 4L)

  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_length(value)), 4L)
  }
})


test_that("a BOM is stripped only where stringi strips it", {
  # stringi is inconsistent here by design and charr reproduces it: str_length
  # counts a leading BOM as a code point, while the operations that build a
  # UTF-8 container strip it. Pin both halves so neither drifts.
  bom <- "﻿abc"

  expect_identical(with_backend("stringi", str_length(bom)), 4L)
  for (backend in c("base", "altrep")) {
    expect_identical(with_backend(backend, str_length(bom)), 4L)
    expect_identical(with_backend(backend, str_sub(bom, 1L, 1L)), "a")
    expect_identical(with_backend(backend, str_length(str_reverse(bom))), 3L)
    expect_identical(
      with_backend(backend, str_length(str_to_upper(bom))), 3L
    )
    expect_identical(with_backend(backend, str_count(bom, fixed("a"))), 1L)
  }
})


test_that("BOM handling matches stringi for native-marked input", {
  original <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original), add = TRUE)

  utf8_locale <- native_probe_locale(
    c(original, "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(is.null(utf8_locale), "No usable UTF-8 LC_CTYPE locale is available")

  Sys.setlocale("LC_CTYPE", utf8_locale)
  # A BOM carried in native bytes. Because the native encoding is UTF-8 here,
  # stringi strips it in container-building operations; native_is_utf8()
  # supplies the locale fact needed by the input normalization path.
  value <- rawToChar(as.raw(c(0xef, 0xbb, 0xbf, 0x61, 0x62, 0x63)))
  Encoding(value) <- "unknown"

  for (operation in list(
    function(x) str_sub(x, 1L, 1L),
    function(x) str_length(str_reverse(x)),
    function(x) str_count(x, fixed("a"))
  )) {
    expected <- with_backend("stringi", operation(value))
    for (backend in c("base", "altrep")) {
      expect_identical(with_backend(backend, operation(value)), expected)
    }
  }
})


test_that("UTF-8 native input leaves validation to the consumer", {
  original <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original), add = TRUE)

  utf8_locale <- native_probe_locale(
    c(original, "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(is.null(utf8_locale), "No usable UTF-8 LC_CTYPE locale is available")
  Sys.setlocale("LC_CTYPE", utf8_locale)

  native <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(native) <- "unknown"
  declared <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(declared) <- "UTF-8"

  for (backend in c("stringi", "base", "altrep")) {
    expect_identical(
      with_backend(backend, str_detect(native, fixed("b"))),
      TRUE,
      info = backend
    )
    expect_identical(
      with_backend(backend, str_detect(native, regex("b"))),
      TRUE,
      info = backend
    )
    expect_error(
      with_backend(
        backend,
        charr_test_leaf("ci_wrap")(
          native, normalize = FALSE, simplify = FALSE
        )
      ),
      "invalid UTF-8 byte sequence",
      fixed = TRUE,
      info = backend
    )

    expect_identical(
      with_backend(backend, str_detect(native, fixed("b"))),
      with_backend(backend, str_detect(declared, fixed("b"))),
      info = backend
    )
  }
})
