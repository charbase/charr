native_locale_find <- function(candidates, predicate) {
  for (candidate in unique(candidates)) {
    locale <- suppressWarnings(Sys.setlocale("LC_CTYPE", candidate))
    if (nzchar(locale) && isTRUE(predicate())) {
      return(locale)
    }
  }

  NULL
}

native_locale_operations <- function(target) {
  list(
    utf8_container = function(value) str_detect(value, fixed(target)),
    utf16_container = function(value) {
      str_detect(value, coll(target, locale = "en", strength = 3L))
    },
    direct_length = str_length
  )
}

native_locale_expect_operations <- function(input, decoded, target) {
  operations <- native_locale_operations(target)

  if (is.na(decoded)) {
    for (operation in operations) {
      expect_error(
        operation(input),
        "failed to convert R native encoding to UTF-8"
      )
    }
    return(invisible(NULL))
  }

  Encoding(decoded) <- "UTF-8"
  for (operation in operations) {
    expect_identical(operation(input), operation(decoded))
  }

  invisible(NULL)
}

native_locale_inputs <- function(value) {
  altrep <- charport::as_charvec(value)
  expect_true(charport::is_charvec(altrep))
  expect_false(charport::charport_info(altrep)$is_materialized)

  list(base = value, altrep = altrep)
}

native_locale_check_transition <- function(
  native, target, inputs, locales
) {
  for (locale in locales) {
    selected <- suppressWarnings(Sys.setlocale("LC_CTYPE", locale))
    expect_true(nzchar(selected))

    decoded <- suppressWarnings(
      iconv(native, from = "", to = "UTF-8", sub = NA_character_)
    )

    for (backend in names(inputs)) {
      with_backend(backend, {
        native_locale_expect_operations(inputs[[backend]], decoded, target)
      })
    }
  }
}

native_locale_check_read_lines_transition <- function(
  native, path, locales
) {
  for (locale in locales) {
    selected <- suppressWarnings(Sys.setlocale("LC_CTYPE", locale))
    expect_true(nzchar(selected))
    expected <- suppressWarnings(
      iconv(native, from = "", to = "UTF-8", sub = NA_character_)
    )

    for (backend in c("base", "altrep")) {
      for (encoding in list(NULL, "")) {
        if (is.na(expected)) {
          expect_error(
            with_backend(
              backend,
              str_read_lines(path, encoding = encoding)
            ),
            "failed to convert R native encoding to UTF-8"
          )
        } else {
          Encoding(expected) <- "UTF-8"
          actual <- with_backend(
            backend,
            str_read_lines(path, encoding = encoding)
          )
          expect_identical(actual, expected)
        }
      }
    }
  }
}

native_locale_utf8_candidates <- function(original_locale) {
  c(
    original_locale, "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8",
    "English_United States.utf8"
  )
}

native_locale_single_byte_candidates <- function(original_locale) {
  c(
    original_locale,
    "en_US.ISO8859-1", "en_US.iso88591", "en_US.ISO-8859-1",
    "de_DE.ISO8859-1", "de_DE.iso88591", "fr_FR.ISO8859-1",
    "English_United States.1252", "English_United States"
  )
}

test_that("native inputs are re-resolved after an invalid locale transition", {
  original_locale <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original_locale), add = TRUE)

  utf8_locale <- native_locale_find(
    native_locale_utf8_candidates(original_locale),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(
    is.null(utf8_locale),
    "No usable UTF-8 LC_CTYPE locale is available"
  )

  Sys.setlocale("LC_CTYPE", utf8_locale)
  native <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
  Encoding(native) <- "unknown"
  target <- enc2utf8("\u00e9")
  Encoding(target) <- "UTF-8"

  invalid_locale <- native_locale_find(
    c("C", "POSIX"),
    function() {
      !isTRUE(l10n_info()[["UTF-8"]]) && is.na(suppressWarnings(
        iconv(native, from = "", to = "UTF-8", sub = NA_character_)
      ))
    }
  )
  skip_if(
    is.null(invalid_locale),
    "No non-UTF-8 LC_CTYPE locale rejects the native-marked test bytes"
  )

  Sys.setlocale("LC_CTYPE", utf8_locale)
  expect_identical(Encoding(native), "unknown")
  inputs <- native_locale_inputs(native)

  native_locale_check_transition(
    native,
    target,
    inputs,
    c(utf8_locale, invalid_locale, utf8_locale)
  )

  path <- tempfile("charr-invalid-native-lines-")
  on.exit(unlink(path), add = TRUE)
  writeBin(charToRaw(native), path)
  native_locale_check_read_lines_transition(
    native,
    path,
    c(utf8_locale, invalid_locale, utf8_locale)
  )
})

test_that("native inputs are re-decoded after a single-byte locale transition", {
  original_locale <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original_locale), add = TRUE)

  utf8_locale <- native_locale_find(
    native_locale_utf8_candidates(original_locale),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(
    is.null(utf8_locale),
    "No usable UTF-8 LC_CTYPE locale is available"
  )

  Sys.setlocale("LC_CTYPE", utf8_locale)
  native <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
  Encoding(native) <- "unknown"
  utf8_value <- suppressWarnings(
    iconv(native, from = "", to = "UTF-8", sub = NA_character_)
  )

  single_byte_locale <- native_locale_find(
    native_locale_single_byte_candidates(original_locale),
    function() {
      if (isTRUE(l10n_info()[["UTF-8"]])) {
        return(FALSE)
      }

      value <- suppressWarnings(
        iconv(native, from = "", to = "UTF-8", sub = NA_character_)
      )
      !is.na(value) && !identical(charToRaw(value), charToRaw(utf8_value))
    }
  )
  skip_if(
    is.null(single_byte_locale),
    "No usable non-UTF-8 single-byte LC_CTYPE locale is available"
  )

  Sys.setlocale("LC_CTYPE", utf8_locale)
  target <- enc2utf8("\u00e9")
  Encoding(target) <- "UTF-8"
  inputs <- native_locale_inputs(native)

  native_locale_check_transition(
    native,
    target,
    inputs,
    c(utf8_locale, single_byte_locale, utf8_locale)
  )
})

test_that("str_read_lines resolves its default encoding per operation", {
  original_locale <- Sys.getlocale("LC_CTYPE")
  on.exit(Sys.setlocale("LC_CTYPE", original_locale), add = TRUE)

  utf8_locale <- native_locale_find(
    native_locale_utf8_candidates(original_locale),
    function() isTRUE(l10n_info()[["UTF-8"]])
  )
  skip_if(
    is.null(utf8_locale),
    "No usable UTF-8 LC_CTYPE locale is available"
  )

  Sys.setlocale("LC_CTYPE", utf8_locale)
  native <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
  Encoding(native) <- "unknown"
  utf8_value <- suppressWarnings(
    iconv(native, from = "", to = "UTF-8", sub = NA_character_)
  )

  single_byte_locale <- native_locale_find(
    native_locale_single_byte_candidates(original_locale),
    function() {
      if (isTRUE(l10n_info()[["UTF-8"]])) {
        return(FALSE)
      }

      value <- suppressWarnings(
        iconv(native, from = "", to = "UTF-8", sub = NA_character_)
      )
      !is.na(value) && !identical(charToRaw(value), charToRaw(utf8_value))
    }
  )
  skip_if(
    is.null(single_byte_locale),
    "No usable non-UTF-8 single-byte LC_CTYPE locale is available"
  )

  path <- tempfile("charr-native-lines-")
  on.exit(unlink(path), add = TRUE)
  writeBin(charToRaw(native), path)

  native_locale_check_read_lines_transition(
    native,
    path,
    c(utf8_locale, single_byte_locale, utf8_locale)
  )
})
