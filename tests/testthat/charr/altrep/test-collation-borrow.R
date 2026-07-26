# Regression for the audit-E collation-container borrow: stable ASCII/UTF-8
# views are borrowed from the input SEXP, latin1 (transient conversion-buffer)
# views are still copied. Interleaving latin1 with ASCII/UTF-8 exercises both
# paths in one container without any locale manipulation. Collation results are
# checked against charr's base backend, which is built against the same ICU.

collation_borrow_base <- get(
  ".charr_backend_environments",
  envir = asNamespace("charr"),
  inherits = FALSE
)$base

collation_borrow_base_call <- function(name, ...) {
  get(name, envir = collation_borrow_base, inherits = FALSE)(...)
}

collation_borrow_utf8 <- function(x) enc2utf8(as.character(x))

collation_borrow_base_sort <- function(
  x,
  decreasing = FALSE,
  na_last = NA,
  opts_collator = NULL
) {
  x <- collation_borrow_utf8(x)
  order <- collation_borrow_base_call(
    "stri_order",
    x,
    decreasing = decreasing,
    na_last = na_last,
    opts_collator = opts_collator
  )
  x[order]
}

collation_borrow_base_unique <- function(x, opts_collator = NULL) {
  x <- collation_borrow_utf8(x)
  duplicated <- collation_borrow_base_call(
    "stri_duplicated",
    x,
    opts_collator = opts_collator
  )
  x[!duplicated]
}

collation_borrow_base_compare <- function(e1, e2, opts_collator = NULL) {
  stopifnot(length(e1) == length(e2))
  if (length(e1) == 0L) {
    return(integer())
  }

  vapply(seq_along(e1), function(i) {
    if (is.na(e1[[i]]) || is.na(e2[[i]])) {
      return(NA_integer_)
    }
    equivalent <- collation_borrow_base_call(
      "stri_cmp_equiv",
      e1[[i]],
      e2[[i]],
      opts_collator = opts_collator
    )
    if (equivalent) {
      return(0L)
    }
    order <- collation_borrow_base_call(
      "stri_order",
      c(e1[[i]], e2[[i]]),
      opts_collator = opts_collator
    )
    if (order[[1L]] == 1L) -1L else 1L
  }, integer(1L))
}

test_that("collation ops borrow stable views on mixed encodings", {
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
        same(charr:::ci_sort(x), collation_borrow_base_sort(s))
        same(charr:::ci_sort(x, decreasing = TRUE),
             collation_borrow_base_sort(s, decreasing = TRUE))
        same(charr:::ci_unique(x), collation_borrow_base_unique(s))
        same(charr:::ci_sort_key(x), charr:::ci_sort_key(s))
        expect_identical(
          charr:::ci_order(x),
          collation_borrow_base_call("stri_order", s)
        )
        expect_identical(
          charr:::ci_rank(x),
          collation_borrow_base_call("stri_rank", s)
        )
        expect_identical(
          charr:::ci_duplicated(x),
          collation_borrow_base_call("stri_duplicated", s)
        )

        # equal length: cmp recycling parity is covered by the scratch
        # differential; here the goal is the borrow, so avoid the noisy
        # (and equivalent) recycling warnings from both backends.
        s2 <- sample(pool, n, replace = TRUE)
        x2 <- if (on) charport::as_charvec(s2) else s2
        base_cmp <- collation_borrow_base_compare(s, s2)
        expect_identical(charr:::ci_cmp(x, x2), base_cmp)
        expect_identical(charr:::ci_cmp_eq(x, x2), charr:::ci_cmp_eq(s, s2))
        expect_identical(charr:::ci_cmp_neq(x, x2), charr:::ci_cmp_neq(s, s2))
        expect_identical(charr:::ci_cmp_lt(x, x2), base_cmp < 0L)
      }
    })
  }
})

test_that("collation borrow preserves the eager bytes-encoding error", {
  by <- rawToChar(as.raw(0xE9)); Encoding(by) <- "bytes"
  s <- c("a", by, "c")
  for (on in c(FALSE, TRUE)) {
    with_altrep(on, {
      x <- if (on) charport::as_charvec(s) else s
      expect_error(charr:::ci_sort(x), "bytes encoding")
      expect_error(collation_borrow_base_sort(s), "bytes encoding")
    })
  }
})
