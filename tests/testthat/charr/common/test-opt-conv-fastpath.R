capture_conv <- function(expr) {
  warnings <- character()
  value <- withCallingHandlers(
    force(expr),
    warning = function(condition) {
      warnings <<- c(warnings, conditionMessage(condition))
      invokeRestart("muffleWarning")
    }
  )
  list(value = value, warnings = gsub("stri_", "ci_", warnings, fixed = TRUE))
}


test_that("explicit UTF-8 identity conversion matches stringi", {
  values <- c(
    "plain", "caf\u00e9", "\U0001f642", "\ufeffvalue", "", NA_character_
  )
  expected <- with_backend(
    "stringi", charr:::ci_conv(values, "UTF8", "utf-8")
  )

  actual_base <- with_backend(
    "base", charr:::ci_conv(values, "UTF8", "utf-8")
  )
  expect_identical(actual_base, expected)
  expect_identical(Encoding(actual_base), Encoding(expected))

  input <- charport::as_charvec(values)
  actual_altrep <- with_backend(
    "altrep", charr:::ci_conv(input, "UTF8", "utf-8")
  )
  expect_identical(actual_altrep, expected)
  expect_identical(Encoding(actual_altrep), Encoding(expected))
  expect_true(charport::is_charvec(actual_altrep))
  expect_false(charport::charport_info(input)$is_materialized)

  expected_marked <- with_backend(
    "stringi", charr:::ci_conv(values, NULL, "UTF-8")
  )
  expect_identical(
    with_backend("base", charr:::ci_conv(values, NULL, "UTF-8")),
    expected_marked
  )
  expect_identical(
    with_backend("altrep", charr:::ci_conv(input, NULL, "UTF-8")),
    expected_marked
  )
})


test_that("UTF-8 identity conversion falls back for malformed bytes", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  values <- c("ok", malformed, NA_character_)
  expected <- capture_conv(with_backend(
    "stringi", charr:::ci_conv(values, "UTF-8", "UTF-8")
  ))

  actual_base <- capture_conv(with_backend(
    "base", charr:::ci_conv(values, "UTF-8", "UTF-8")
  ))
  expect_identical(actual_base, expected)

  input <- charport::as_charvec(values)
  actual_altrep <- capture_conv(with_backend(
    "altrep", charr:::ci_conv(input, "UTF-8", "UTF-8")
  ))
  expect_identical(actual_altrep, expected)
  expect_false(charport::charport_info(input)$is_materialized)
})


test_that("UTF-8 identity conversion retains raw-output semantics", {
  values <- c("plain", "caf\u00e9", "\U0001f642", "", NA_character_)
  expected <- with_backend(
    "stringi",
    charr:::ci_conv(values, "UTF-8", "UTF-8", to_raw = TRUE)
  )

  expect_identical(
    with_backend(
      "base",
      charr:::ci_conv(values, "UTF-8", "UTF-8", to_raw = TRUE)
    ),
    expected
  )
  expect_identical(
    with_backend(
      "altrep",
      charr:::ci_conv(
        charport::as_charvec(values), "UTF-8", "UTF-8", to_raw = TRUE
      )
    ),
    expected
  )
})
