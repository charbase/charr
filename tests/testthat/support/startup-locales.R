# R fixes native encoding behavior during startup. These helpers classify the
# current process and provide locale names for fresh child processes to try.

startup_locale_class <- function() {
  info <- l10n_info()
  if (isTRUE(info[["UTF-8"]])) {
    return("utf8")
  }

  if (isTRUE(info[["Latin-1"]])) {
    # ISO-8859-1 maps byte 0x80 to U+0080, while Windows-1252 maps it to
    # U+20AC. Check the conversion instead of trusting the locale name.
    value <- rawToChar(as.raw(0x80))
    Encoding(value) <- "unknown"
    converted <- suppressWarnings(
      iconv(value, from = "", to = "UTF-8", sub = NA_character_)
    )
    if (!is.na(converted) &&
        identical(charToRaw(converted), as.raw(c(0xc2, 0x80)))) {
      return("iso88591")
    }
  }

  "other"
}

startup_locale_candidates <- function() {
  list(
    utf8 = c(
      "en_US.UTF-8", "en_US.utf8", "C.UTF-8", "C.utf8",
      "English_United States.utf8", "English_United States.65001"
    ),
    iso88591 = c(
      "en_US.ISO-8859-1", "en_US.ISO8859-1", "en_US.iso88591",
      "de_DE.ISO8859-1", "de_DE.iso88591", "fr_FR.ISO8859-1",
      "English_United States.28591"
    )
  )
}

startup_locale_environment <- function(locale) {
  if (is.null(locale)) {
    return(character())
  }

  # LC_ALL would override LC_CTYPE if the parent exported it. An empty value
  # lets the child use the requested character locale without changing its
  # other locale categories.
  c("LC_ALL=", paste0("LC_CTYPE=", shQuote(locale)))
}
