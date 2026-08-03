source("testthat/support/startup-locales.R")

rscript <- file.path(R.home("bin"), "Rscript")
locale_support <- normalizePath(
  "testthat/support/startup-locales.R",
  mustWork = TRUE
)
test_worker <- normalizePath(
  "testthat/support/run-backend-tests.R",
  mustWork = TRUE
)

r_literal <- function(value) {
  paste(deparse(value), collapse = "")
}

probe_startup_locale <- function(locale, expected_class) {
  probe <- paste0(
    "source(", r_literal(locale_support), "); ",
    "quit(save = 'no', status = if (identical(startup_locale_class(), ",
    r_literal(expected_class), ")) 0L else 1L)"
  )
  status <- suppressWarnings(system2(
    rscript,
    c("--vanilla", "-e", shQuote(probe)),
    stdout = FALSE,
    stderr = FALSE,
    env = startup_locale_environment(locale)
  ))
  identical(status, 0L)
}

select_startup_locales <- function() {
  candidates <- startup_locale_candidates()
  runs <- list()
  covered <- character()
  current_class <- startup_locale_class()

  if (current_class %in% names(candidates)) {
    runs[[length(runs) + 1L]] <- list(
      label = current_class,
      locale = NULL
    )
    covered <- current_class
  } else {
    runs[[length(runs) + 1L]] <- list(
      label = paste0("current:", Sys.getlocale("LC_CTYPE")),
      locale = NULL
    )
  }

  for (target in setdiff(names(candidates), covered)) {
    selected <- NULL
    for (candidate in candidates[[target]]) {
      if (probe_startup_locale(candidate, target)) {
        selected <- candidate
        break
      }
    }

    if (is.null(selected)) {
      message("Skipping unavailable startup locale class: ", target)
    } else {
      runs[[length(runs) + 1L]] <- list(
        label = target,
        locale = selected
      )
    }
  }

  runs
}

backends <- c("stringi", "base", "altrep")
startup_locales <- select_startup_locales()
statuses <- integer(length(startup_locales) * length(backends))
names(statuses) <- character(length(statuses))

index <- 0L
for (startup_locale in startup_locales) {
  selected_locale <- if (is.null(startup_locale$locale)) {
    Sys.getlocale("LC_CTYPE")
  } else {
    startup_locale$locale
  }

  for (backend in backends) {
    index <- index + 1L
    label <- paste(startup_locale$label, backend, sep = "/")
    names(statuses)[[index]] <- label
    message(
      "== startup locale: ", startup_locale$label, " (", selected_locale,
      "); charr backend: ", backend, " =="
    )
    statuses[[index]] <- system2(
      rscript,
      c("--vanilla", shQuote(test_worker), backend),
      env = startup_locale_environment(startup_locale$locale)
    )
  }
}

failed <- names(statuses)[statuses != 0L]
if (length(failed) > 0L) {
  stop(
    "test failures for startup locale/backend: ",
    paste(failed, collapse = ", "),
    call. = FALSE
  )
}
