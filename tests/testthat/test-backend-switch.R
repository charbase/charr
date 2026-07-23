test_that("the R switch covers the complete stringr-facing inventory", {
  map <- charr:::.charr_backend_map

  expect_length(map, 69)
  expect_length(unique(names(map)), 69)
  expect_length(unique(unname(map)), 69)
  expect_setequal(
    names(map),
    sub("^stri_", "ci_", unname(map))
  )

  copied <- charr:::.altrep_backend
  expect_true(all(vapply(
    names(map), exists, logical(1), envir = copied, inherits = FALSE
  )))
  expect_true(all(vapply(
    names(charr:::.charr_replacement_map), exists, logical(1),
    envir = copied, inherits = FALSE
  )))

  all_routes <- c(map, charr:::.charr_replacement_map)
  namespace <- asNamespace("charr")
  for (ci_name in names(all_routes)) {
    dispatch <- get(ci_name, envir = namespace, inherits = FALSE)
    altrep_call <- get(
      "altrep_call", envir = environment(dispatch), inherits = FALSE
    )
    bound_target <- get(
      all_routes[[ci_name]], envir = environment(altrep_call),
      inherits = FALSE
    )
    expect_identical(bound_target, copied[[ci_name]])
  }
})

test_that("the ALTREP backend is selected as one complete backend", {
  probes <- list(
    fixed = function() str_detect(c("abc", NA), fixed("b")),
    regex = function() str_extract_all("a1 b2", "[[:alpha:]]")[[1]],
    coll = function() str_sort(c("z", "ä", "a"), locale = "de"),
    case = function() str_to_upper("Straße", locale = "de"),
    boundary = function() str_count("one two", boundary("word")),
    wrap = function() str_wrap("one two three", width = 7),
    conversion = function() str_conv("façade", "UTF-8")
  )

  for (probe in probes) {
    reference <- with_altrep(FALSE, probe())
    altrep <- with_altrep(TRUE, {
      expect_gt(altrep_backend_calls(value <- probe()), 0)
      value
    })
    expect_identical(altrep, reference)
  }
})

test_that("charr's own ICU supports the ALTREP backend", {
  if (charr:::charr_icu_bundled()) {
    expect_identical(.Call(charr:::C_charr_icu_version), "74.1")
  } else {
    # configure accepted a system ICU4C; its version floats (floor 61)
    icu_major <- as.integer(sub("\\..*$", "", .Call(charr:::C_charr_icu_version)))
    expect_gte(icu_major, 61L)
  }
  expect_true(charr:::charr_icu_ok())
  expect_true(.Call(charr:::C_charr_icu_smoke))

  with_altrep(TRUE, {
    expect_identical(str_detect("abc", coll("b")), TRUE)
    expect_identical(str_wrap("one two", width = 3), "one\ntwo")
  })
})
