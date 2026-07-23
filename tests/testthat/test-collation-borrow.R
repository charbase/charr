# Regression for the audit-E collation-container borrow: stable ASCII/UTF-8
# views are borrowed from the input SEXP, latin1 (transient conversion-buffer)
# views are still copied. Interleaving latin1 with ASCII/UTF-8 exercises both
# paths in one container without any locale manipulation. Oracle: stringi.

test_that("collation ops borrow stable views and match stringi on mixed encodings", {
  skip_if_not_installed("stringi")
  latin1 <- iconv(c("café", "naïve", "Über"), "UTF-8", "latin1")
  expect_true(any(Encoding(latin1) == "latin1"))
  utf8 <- c("日本語", "café", "x\U0001F600y")
  ascii <- c("alpha", "beta", "ALPHA", "10", "2")
  pool <- c(ascii, utf8, latin1, "", NA)

  same <- function(a, b) {
    expect_identical(as.vector(as.character(a)), as.vector(as.character(b)))
    expect_identical(Encoding(as.character(a)), Encoding(as.character(b)))
  }

  set.seed(11)
  for (on in c(FALSE, TRUE)) {
    with_altrep(on, {
      for (rep in 1:15) {
        n <- sample(4:10, 1L)
        s <- sample(pool, n, replace = TRUE)
        x <- if (on) charport::as_charvec(s) else s
        expect_identical(charport::is_charvec(x), on)
        same(charr:::ci_sort(x), stringi::stri_sort(s))
        same(charr:::ci_sort(x, decreasing = TRUE),
             stringi::stri_sort(s, decreasing = TRUE))
        same(charr:::ci_unique(x), stringi::stri_unique(s))
        same(charr:::ci_sort_key(x), stringi::stri_sort_key(s))
        expect_identical(charr:::ci_order(x), stringi::stri_order(s))
        expect_identical(charr:::ci_rank(x), stringi::stri_rank(s))
        expect_identical(charr:::ci_duplicated(x), stringi::stri_duplicated(s))

        # equal length: cmp recycling parity is covered by the scratch
        # differential; here the goal is the borrow, so avoid the noisy
        # (and equivalent) recycling warnings from both backends.
        s2 <- sample(pool, n, replace = TRUE)
        x2 <- if (on) charport::as_charvec(s2) else s2
        expect_identical(charr:::ci_cmp(x, x2), stringi::stri_cmp(s, s2))
        expect_identical(charr:::ci_cmp_eq(x, x2), stringi::stri_cmp_eq(s, s2))
        expect_identical(charr:::ci_cmp_neq(x, x2), stringi::stri_cmp_neq(s, s2))
        expect_identical(charr:::ci_cmp_lt(x, x2), stringi::stri_cmp_lt(s, s2))
      }
    })
  }
})

test_that("collation borrow preserves the eager bytes-encoding error", {
  skip_if_not_installed("stringi")
  by <- rawToChar(as.raw(0xE9)); Encoding(by) <- "bytes"
  s <- c("a", by, "c")
  for (on in c(FALSE, TRUE)) {
    with_altrep(on, {
      x <- if (on) charport::as_charvec(s) else s
      ci <- tryCatch(charr:::ci_sort(x), error = conditionMessage)
      st <- tryCatch(stringi::stri_sort(s), error = conditionMessage)
      expect_identical(ci, st)
    })
  }
})
