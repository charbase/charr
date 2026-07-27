test_that("the backend option defaults, queries, and sets", {
  old <- options(charr_backend = NULL)
  on.exit(options(old), add = TRUE)

  expect_identical(charr:::charr_backend(), "altrep")
  expect_invisible(charr:::charr_backend("stringi"))
  expect_identical(charr:::charr_backend(), "stringi")
  expect_identical(getOption("charr_backend"), "stringi")

  previous <- charr:::charr_backend("altrep")
  expect_identical(previous, "stringi")
  expect_identical(charr:::charr_backend(), "altrep")
})

test_that("setter and direct option writes are validated", {
  old <- options(charr_backend = NULL)
  on.exit(options(old), add = TRUE)

  bad <- list(
    TRUE,
    NA_character_,
    character(),
    c("altrep", "stringi"),
    factor("altrep"),
    "ALTREP",
    "unknown"
  )
  for (value in bad) {
    expect_error(charr:::charr_backend(value), "charr_backend")
  }

  options(charr_backend = "unknown")
  expect_error(str_length("abc"), "charr_backend")
  expect_identical(charr:::charr_backend("base"), "unknown")
  expect_identical(charr:::charr_backend(), "base")
})

test_that("the public backend registry is complete and exact-formal", {
  namespace <- asNamespace("charr")
  roots <- charr:::.charr_backend_roots
  helpers <- charr:::.charr_backend_helpers
  templates <- charr:::.charr_backend_templates

  expect_length(roots, 57L)
  expect_length(unique(roots), 57L)
  expect_length(helpers, 12L)
  expect_length(unique(helpers), 12L)
  expect_setequal(names(templates), c(roots, helpers))

  for (name in roots) {
    dispatch <- get(name, envir = namespace, inherits = FALSE)
    expect_identical(
      formals(dispatch),
      formals(templates[[name]])
    )
    expect_false(any(c("match.call", "eval", "do.call") %in% all.names(body(dispatch))))
  }

  expect_identical(
    formals(get("str_sub<-", envir = namespace, inherits = FALSE)),
    as.pairlist(alist(
      string = ,
      start = 1L,
      end = -1L,
      omit_na = FALSE,
      value =
    ))
  )
})

test_that("each backend owns a pinned closure graph", {
  namespace <- asNamespace("charr")
  environments <- charr:::.charr_backend_environments
  graph <- c(charr:::.charr_backend_roots, charr:::.charr_backend_helpers)

  expect_named(environments, c("stringi", "base", "altrep"))
  for (backend in environments) {
    expect_identical(parent.env(backend), namespace)
    expect_true(all(vapply(
      graph,
      exists,
      logical(1),
      envir = backend,
      inherits = FALSE
    )))
    expect_true(all(vapply(
      graph,
      function(name) identical(environment(backend[[name]]), backend),
      logical(1)
    )))
  }

  for (name in graph) {
    expect_false(identical(
      environments$stringi[[name]],
      environments$base[[name]]
    ))
    expect_false(identical(
      environments$stringi[[name]],
      environments$altrep[[name]]
    ))
    expect_false(identical(
      environments$base[[name]],
      environments$altrep[[name]]
    ))
  }

  for (name in names(charr:::.charr_leaf_map)) {
    expect_identical(
      environments$stringi[[name]],
      getExportedValue("stringi", name)
    )
    expect_identical(
      environments$altrep[[name]],
      get(
        charr:::.charr_leaf_map[[name]],
        envir = namespace,
        inherits = FALSE
      )
    )
  }
})

test_that("the base leaf graph is closed over base native aliases", {
  leaf_environment <- charr:::.charr_base_leaf_environment
  graph <- charr:::.charr_base_wrapper_graph

  expect_setequal(
    ls(charr:::.charr_base_leaf_bindings, all.names = TRUE),
    names(charr:::.charr_leaf_map)
  )
  expect_identical(parent.env(leaf_environment), baseenv())
  expect_true(all(vapply(
    graph,
    function(name) identical(environment(leaf_environment[[name]]), leaf_environment),
    logical(1)
  )))
  expect_true(all(vapply(
    names(charr:::.charr_base_native_aliases),
    function(name) inherits(leaf_environment[[name]], "NativeSymbolInfo"),
    logical(1)
  )))
})

test_that("nested calls stay on the backend selected at entry", {
  old_option <- options(charr_backend = "altrep")
  on.exit(options(old_option), add = TRUE)

  environments <- charr:::.charr_backend_environments
  old_altrep <- environments$altrep$str_detect
  old_stringi <- environments$stringi$str_detect
  on.exit(assign("str_detect", old_altrep, environments$altrep), add = TRUE)
  on.exit(assign("str_detect", old_stringi, environments$stringi), add = TRUE)

  environments$altrep$str_detect <- function(string, ...) {
    rep(TRUE, length(string))
  }
  environments$stringi$str_detect <- function(string, ...) {
    rep(FALSE, length(string))
  }

  out <- str_subset(
    c("a", "b"),
    {
      options(charr_backend = "stringi")
      fixed("a")
    }
  )
  expect_identical(out, c("a", "b"))
})

test_that("pattern objects can cross backend boundaries", {
  old <- options(charr_backend = NULL)
  on.exit(options(old), add = TRUE)

  charr:::charr_backend("stringi")
  stringi_pattern <- fixed("a")
  charr:::charr_backend("altrep")
  expect_identical(
    str_detect(c("a", "b"), stringi_pattern),
    c(TRUE, FALSE)
  )

  altrep_pattern <- regex("^a", ignore_case = TRUE)
  charr:::charr_backend("stringi")
  expect_identical(
    str_detect(c("A", "b"), altrep_pattern),
    c(TRUE, FALSE)
  )
})

test_that("str_interp defaults to its public caller's environment", {
  old <- options(charr_backend = "stringi")
  on.exit(options(old), add = TRUE)

  interpolate_here <- function() {
    caller_value <- "found in caller"
    str_interp("${caller_value}")
  }
  expect_identical(interpolate_here(), "found in caller")
})
