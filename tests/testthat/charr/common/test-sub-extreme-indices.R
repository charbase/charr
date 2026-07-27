test_that("substring endpoints handle extreme non-missing integers", {
  skip_if(
    identical(charr_backend(), "stringi"),
    "stringi's native endpoint arithmetic overflows for these inputs"
  )

  backend <- charr:::.charr_backend_environments[[charr_backend()]]
  sub <- backend[["stri_sub"]]
  replace <- backend[["stri_sub<-"]]
  sub_all <- backend[["stri_sub_all"]]
  replace_all <- backend[["stri_sub_all<-"]]

  maximum <- .Machine$integer.max
  minimum <- -maximum
  sources <- c("abc", "aéb")

  length_from <- c(2L, minimum, minimum, -1L, maximum)
  lengths <- c(maximum, 1L, maximum, maximum, maximum)
  expected_sub <- list(
    c("bc", "", "abc", "c", ""),
    c("éb", "", "aéb", "b", "")
  )
  expected_replace <- list(
    c("aX", "Xabc", "X", "abX", "abcX"),
    c("aX", "Xaéb", "X", "aéX", "aébX")
  )
  expected_replace_all <- list(
    c("aX", "Xbc", "X", "abX", "abcX"),
    c("aX", "Xéb", "X", "aéX", "aébX")
  )

  for (i in seq_along(sources)) {
    source <- sources[[i]]
    expect_identical(
      sub(source, from = length_from, length = lengths),
      expected_sub[[i]]
    )
    expect_identical(
      sub_all(
        source, from = list(length_from), length = list(lengths)
      )[[1]],
      expected_sub[[i]]
    )
    expect_identical(
      replace(
        source, from = length_from, length = lengths, value = "X"
      ),
      expected_replace[[i]]
    )

    actual_replace_all <- vapply(
      seq_along(length_from),
      function(j) {
        replace_all(
          source, from = list(length_from[[j]]),
          length = list(lengths[[j]]), value = "X"
        )
      },
      character(1)
    )
    expect_identical(actual_replace_all, expected_replace_all[[i]])
  }

  to_from <- c(maximum, minimum, 1L, 2L, minimum)
  endpoints <- c(maximum, maximum, minimum, minimum, minimum)
  expected_sub <- list(
    c("", "abc", "", "", ""),
    c("", "aéb", "", "", "")
  )
  expected_replace <- list(
    c("abcX", "X", "Xabc", "aXbc", "Xabc"),
    c("aébX", "X", "Xaéb", "aXéb", "Xaéb")
  )

  for (i in seq_along(sources)) {
    source <- sources[[i]]
    expect_identical(
      sub(source, from = to_from, to = endpoints), expected_sub[[i]]
    )
    expect_identical(
      sub_all(source, from = list(to_from), to = list(endpoints))[[1]],
      expected_sub[[i]]
    )
    expect_identical(
      replace(source, from = to_from, to = endpoints, value = "X"),
      expected_replace[[i]]
    )

    actual_replace_all <- vapply(
      seq_along(to_from),
      function(j) {
        replace_all(
          source, from = list(to_from[[j]]),
          to = list(endpoints[[j]]), value = "X"
        )
      },
      character(1)
    )
    expect_identical(actual_replace_all, expected_replace[[i]])
  }
})
