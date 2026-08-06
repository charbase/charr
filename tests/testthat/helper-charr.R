selected_test_backend <- charr_backend()

with_backend <- function(backend, code) {
  old <- charr_backend(backend)
  on.exit(charr_backend(old), add = TRUE)
  force(code)
}

charr_altrep <- function() identical(charr_backend(), "altrep")

stringi_can_compare_native <- function() {
  if (isTRUE(l10n_info()[["UTF-8"]])) {
    return(TRUE)
  }

  # Some ICU builds hardcode the default charset to UTF-8. In that case
  # stringi cannot serve as an oracle for R-native bytes in another locale.
  !isTRUE(stringi::stri_info()[["ICU.UTF8"]])
}

skip_if_stringi_cannot_compare_native <- function() {
  skip_if_not(
    stringi_can_compare_native(),
    "installed stringi hardcodes ICU's default charset to UTF-8"
  )
}

skip_if_selected_stringi_cannot_compare_native <- function() {
  if (identical(charr_backend(), "stringi")) {
    skip_if_stringi_cannot_compare_native()
  }
}

# ICU carries no break-iterator resource for an unknown locale, so the lookup
# falls back to the root bundle and ICU reports U_USING_DEFAULT_WARNING.
# stringi only started surfacing that status in 1.8.1 (gagolews/stringi#476),
# so an older oracle stays silent where charr's ICU warns, and the stringi
# backend inherits that silence. Probe the behaviour rather than the version:
# a build's ICU data decides it too.
locale_fallback_warning_pattern <- "resource bundle lookup returned a result"

warns_on_locale_fallback <- function(backend) {
  input <- if (identical(backend, "altrep")) {
    charport::as_charvec("abc")
  } else {
    "abc"
  }

  warned <- FALSE
  withCallingHandlers(
    charr_test_leaf("ci_count_boundaries", backend)(
      input, opts_brkiter = list(type = "word", locale = "zz_ZZ")
    ),
    warning = function(condition) {
      warned <<- warned || grepl(
        locale_fallback_warning_pattern, conditionMessage(condition)
      )
      invokeRestart("muffleWarning")
    }
  )
  warned
}

stringi_warns_on_locale_fallback <- local({
  answer <- NULL
  function() {
    if (is.null(answer)) {
      answer <<- warns_on_locale_fallback("stringi")
    }
    answer
  }
})

selected_backend_warns_on_locale_fallback <- local({
  answer <- NULL
  function() {
    if (is.null(answer)) {
      answer <<- warns_on_locale_fallback(selected_test_backend)
    }
    answer
  }
})

# For assertions that compare charr against the stringi oracle.
skip_if_stringi_lacks_locale_fallback_warning <- function() {
  skip_if_not(
    stringi_warns_on_locale_fallback(),
    "installed stringi does not report ICU's locale-fallback warning"
  )
}

# For assertions about the selected backend's own warning behaviour.
skip_if_backend_lacks_locale_fallback_warning <- function() {
  skip_if_not(
    selected_backend_warns_on_locale_fallback(),
    "this backend does not report ICU's locale-fallback warning"
  )
}

# A sweep that is not itself about the fallback warning still compares the rest
# of its warning stream against an oracle that cannot raise it. Drop the
# warning from both sides in that case instead of skipping the whole sweep.
drop_unmatched_locale_fallback_warning <- function(messages) {
  if (stringi_warns_on_locale_fallback()) {
    return(messages)
  }

  messages[!grepl(locale_fallback_warning_pattern, messages)]
}

# The ci_* bindings in charr's namespace always target ALTREP. Tests use this
# selector when the same semantic assertion must reach the active backend.
charr_test_leaf <- function(name, backend = charr_backend()) {
  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }

  if (identical(backend, "base")) {
    if (identical(name, "ci_sub_replace")) {
      replacement_function <- get(
        "ci_sub<-",
        envir = charr:::.charr_base_leaf_environment,
        inherits = FALSE
      )
      return(function(..., replacement, value = replacement) {
        replacement_function(..., value = value)
      })
    }
    if (identical(name, "ci_sub_replace_all")) {
      replacement_function <- get(
        "ci_sub_all<-",
        envir = charr:::.charr_base_leaf_environment,
        inherits = FALSE
      )
      return(function(..., replacement, value = replacement) {
        replacement_function(..., value = value)
      })
    }
    return(get(
      name,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }

  if (identical(backend, "stringi")) {
    stringi_name <- sub("^ci_", "stri_", name)
    return(get(
      stringi_name, envir = asNamespace("stringi"), inherits = FALSE
    ))
  }

  stop("unknown test backend", call. = FALSE)
}

# TRUE selects the backend under test; FALSE selects the stringi oracle.
with_test_backend <- function(use_selected, code) {
  backend <- if (is.character(use_selected)) {
    use_selected
  } else if (isTRUE(use_selected)) {
    selected_test_backend
  } else {
    "stringi"
  }
  with_backend(backend, code)
}

expect_altrep_charvec <- function(value) {
  if (charr_altrep()) {
    expect_true(charport::is_charvec(value))
  }
  invisible(NULL)
}

expect_altrep_unmaterialized <- function(value) {
  if (charr_altrep()) {
    expect_altrep_charvec(value)
    expect_false(charport::charport_info(value)$is_materialized)
  }
  invisible(NULL)
}

expect_altrep_charvec_list <- function(value) {
  if (charr_altrep()) {
    expect_true(all(vapply(value, charport::is_charvec, logical(1L))))
  }
  invisible(NULL)
}

expect_altrep_unmaterialized_list <- function(value) {
  if (charr_altrep()) {
    expect_altrep_charvec_list(value)
    invisible(lapply(value, expect_altrep_unmaterialized))
  }
  invisible(NULL)
}
