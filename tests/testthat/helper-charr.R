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
