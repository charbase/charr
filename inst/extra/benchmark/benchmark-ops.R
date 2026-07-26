# Four-backend benchmark registry. The names and order match
# charr:::.charr_leaf_map on the optimized branch.
#
# Each timed expression receives the backend leaf as `leaf`. Current charr
# resolves that leaf from .charr_backend_environments; the Claude snapshot
# resolves its copied ci_* wrapper directly after enabling charr_altrep().

bench_args <- list(
  fixed_pattern = " ",
  regex_pattern = "(?<=\\s)(\\p{L}[\\p{L}\\p{M}]*)",
  regex_match_pattern = "\\s+(\\p{L}[\\p{L}\\p{M}]*)",
  split_fixed_pattern = " ",
  split_regex_pattern = "\\s+",
  replacement = "X",
  collator = NULL,
  brk_word = list(type = "word"),
  sub_from = 2L,
  sub_to = 8L,
  sub_replace_to = 5L,
  pad_width = 60L,
  wrap_width = 40L,
  dup_times = 2L,
  flatten_collapse = " ",
  join_suffix = "!"
)

bench_op <- function(family, thunk, prep = NULL, fixture = NULL) {
  list(family = family, thunk = thunk, prep = prep, fixture = fixture)
}

bench_reverse_prep <- function(resolve, x, ctx) {
  resolve("ci_reverse")(x)
}

bench_restore_input_mode <- function(values, source) {
  if (charport::is_charvec(source)) {
    charport::as_charvec(values)
  } else {
    values
  }
}

bench_trim_prep <- function(resolve, x, ctx) {
  bench_restore_input_mode(paste0(" ", x, " "), x)
}

bench_replace_na_prep <- function(resolve, x, ctx) {
  values <- as.character(x)
  values[seq.int(1L, length(values), by = 8L)] <- NA_character_
  bench_restore_input_mode(values, x)
}

bench_fixed_extract_prep <- function(resolve, x, ctx) {
  ctx$prepared$fixed_extract$pattern
}

bench_collation_prep <- function(resolve, x, ctx) {
  ctx$prepared$collation
}

bench_conversion_prep <- function(resolve, x, ctx) {
  ctx$prepared$conversion
}

bench_partition_call <- function(partitions, callback) {
  lapply(partitions, callback)
}

bench_character_identical <- function(x, y) {
  if (length(x) != length(y) ||
      !identical(is.na(x), is.na(y)) ||
      !identical(Encoding(x), Encoding(y))) {
    return(FALSE)
  }
  present <- which(!is.na(x))
  all(vapply(present, function(i) {
    identical(charToRaw(x[[i]]), charToRaw(y[[i]]))
  }, logical(1L)))
}

bench_validate_fixture <- function(operation, prepared, input_n, input_mode) {
  op <- bench_ops[[operation]]
  if (is.null(op$fixture)) {
    return(invisible(TRUE))
  }
  stopifnot(
    identical(prepared$format_version, 2L),
    identical(prepared$n, input_n),
    identical(prepared$mode, input_mode)
  )

  if (identical(op$fixture, "fixed_extract")) {
    stopifnot(length(prepared$fixed_extract$pattern) == input_n)
  } else if (identical(op$fixture, "collation")) {
    stopifnot(
      length(prepared$collation) >= 2L,
      sum(vapply(prepared$collation, function(partition) {
        length(partition$text)
      }, integer(1L))) == input_n,
      all(vapply(prepared$collation, function(partition) {
        length(partition$pattern) == 1L &&
          length(partition$expected_detect) == length(partition$text) &&
          length(partition$expected_starts) == length(partition$startswith_text) &&
          length(partition$expected_ends) == length(partition$endswith_text) &&
          mean(partition$expected_starts) >= 0.05 &&
          mean(partition$expected_ends) >= 0.05 &&
          !identical(partition$pattern, partition$surface_word) &&
          !identical(
            partition$startswith_pattern, partition$startswith_surface_pattern
          ) &&
          !identical(
            partition$endswith_pattern, partition$endswith_surface_pattern
          )
      }, logical(1L)))
    )
  } else if (identical(op$fixture, "conversion")) {
    stopifnot(
      identical(
        vapply(prepared$conversion, `[[`, character(1L), "encoding"),
        c("Windows-1252", "ISO-8859-2", "Shift_JIS")
      ),
      sum(vapply(prepared$conversion, function(partition) {
        length(partition$input)
      }, integer(1L))) == input_n,
      all(vapply(prepared$conversion, function(partition) {
        length(partition$input) == length(partition$expected)
      }, logical(1L)))
    )
  } else {
    stop("unknown benchmark fixture: ", op$fixture)
  }

  if (identical(input_mode, "charvec")) {
    vectors <- if (identical(op$fixture, "fixed_extract")) {
      list(prepared$fixed_extract$pattern)
    } else if (identical(op$fixture, "collation")) {
      unlist(lapply(prepared$collation, function(partition) {
        partition[c(
          "text", "startswith_text", "endswith_text", "pattern",
          "startswith_pattern", "endswith_pattern"
        )]
      }), recursive = FALSE)
    } else {
      lapply(prepared$conversion, `[[`, "input")
    }
    stopifnot(all(vapply(vectors, function(value) {
      charport::is_charvec(value) &&
        !isTRUE(charport::charport_info(value)$is_materialized)
    }, logical(1L))))
  }

  invisible(TRUE)
}

bench_validate_result <- function(operation, result, aux) {
  if (identical(operation, "ci_detect_coll")) {
    stopifnot(
      length(result) == length(aux),
      all(unlist(
        Map(identical, result, lapply(aux, `[[`, "expected_detect")),
        use.names = FALSE
      ))
    )
  }
  if (identical(operation, "ci_startswith_coll")) {
    stopifnot(
      length(result) == length(aux),
      all(unlist(
        Map(identical, result, lapply(aux, `[[`, "expected_starts")),
        use.names = FALSE
      ))
    )
  }
  if (identical(operation, "ci_endswith_coll")) {
    stopifnot(
      length(result) == length(aux),
      all(unlist(
        Map(identical, result, lapply(aux, `[[`, "expected_ends")),
        use.names = FALSE
      ))
    )
  }
  if (identical(operation, "ci_conv")) {
    stopifnot(
      length(result) == length(aux),
      all(unlist(Map(function(actual, partition) {
        bench_character_identical(actual, partition$expected)
      }, result, aux), use.names = FALSE))
    )
  }
  invisible(TRUE)
}

bench_ops <- list(
  ci_detect_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_startswith_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_endswith_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_count_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_locate_first_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_locate_all_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$fixed_pattern)
  ),
  ci_extract_first_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, aux),
    prep = bench_fixed_extract_prep,
    fixture = "fixed_extract"
  ),
  ci_extract_all_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) {
      leaf(x, aux, omit_no_match = TRUE)
    },
    prep = bench_fixed_extract_prep,
    fixture = "fixed_extract"
  ),
  ci_replace_first_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$fixed_pattern, bench_args$replacement)
    }
  ),
  ci_replace_all_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$fixed_pattern, bench_args$replacement)
    }
  ),
  ci_split_fixed = bench_op(
    "fixed",
    function(leaf, x, ctx, aux) leaf(x, bench_args$split_fixed_pattern)
  ),

  ci_sub = bench_op(
    "substring",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$sub_from, bench_args$sub_to)
    }
  ),
  `ci_sub<-` = bench_op(
    "substring",
    function(leaf, x, ctx, aux) {
      leaf(
        x, bench_args$sub_from, bench_args$sub_replace_to,
        value = bench_args$replacement
      )
    }
  ),
  ci_sub_all = bench_op(
    "substring",
    function(leaf, x, ctx, aux) {
      leaf(x, list(bench_args$sub_from), list(bench_args$sub_to))
    }
  ),
  `ci_sub_all<-` = bench_op(
    "substring",
    function(leaf, x, ctx, aux) {
      leaf(
        x, list(bench_args$sub_from), list(bench_args$sub_replace_to),
        value = bench_args$replacement
      )
    }
  ),

  ci_length = bench_op(
    "other",
    function(leaf, x, ctx, aux) leaf(x)
  ),
  ci_c = bench_op(
    "core",
    function(leaf, x, ctx, aux) leaf(x, bench_args$join_suffix)
  ),
  ci_flatten = bench_op(
    "core",
    function(leaf, x, ctx, aux) {
      leaf(x, collapse = bench_args$flatten_collapse)
    }
  ),
  ci_dup = bench_op(
    "core",
    function(leaf, x, ctx, aux) leaf(x, bench_args$dup_times)
  ),
  ci_reverse = bench_op(
    "core",
    function(leaf, x, ctx, aux) leaf(x)
  ),
  ci_trim_left = bench_op(
    "other",
    function(leaf, x, ctx, aux) leaf(aux),
    prep = bench_trim_prep
  ),
  ci_trim_right = bench_op(
    "other",
    function(leaf, x, ctx, aux) leaf(aux),
    prep = bench_trim_prep
  ),
  ci_trim_both = bench_op(
    "other",
    function(leaf, x, ctx, aux) leaf(aux),
    prep = bench_trim_prep
  ),
  ci_replace_na = bench_op(
    "core",
    function(leaf, x, ctx, aux) leaf(aux, bench_args$replacement),
    prep = bench_replace_na_prep
  ),

  ci_detect_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_pattern)
  ),
  ci_count_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_pattern)
  ),
  ci_locate_first_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_pattern)
  ),
  ci_locate_all_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_pattern)
  ),
  ci_extract_first_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_pattern)
  ),
  ci_extract_all_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$regex_pattern, omit_no_match = TRUE)
    }
  ),
  ci_replace_first_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$regex_pattern, bench_args$replacement)
    }
  ),
  ci_replace_all_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$regex_pattern, bench_args$replacement)
    }
  ),
  ci_split_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$split_regex_pattern)
  ),
  ci_match_first_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) leaf(x, bench_args$regex_match_pattern)
  ),
  ci_match_all_regex = bench_op(
    "regex",
    function(leaf, x, ctx, aux) {
      leaf(x, bench_args$regex_match_pattern, omit_no_match = TRUE)
    }
  ),

  ci_detect_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_startswith_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$startswith_text, partition$startswith_pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_endswith_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$endswith_text, partition$endswith_pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_count_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_locate_first_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_locate_all_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_extract_first_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_extract_all_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern, omit_no_match = TRUE,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_replace_first_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern, bench_args$replacement,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_replace_all_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern, bench_args$replacement,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),
  ci_split_coll = bench_op(
    "collation",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(
          partition$text, partition$pattern,
          opts_collator = partition$options
        )
      })
    },
    prep = bench_collation_prep,
    fixture = "collation"
  ),

  ci_order = bench_op(
    "ordering",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_collator = bench_args$collator)
    }
  ),
  ci_rank = bench_op(
    "ordering",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_collator = bench_args$collator)
    }
  ),
  ci_cmp_equiv = bench_op(
    "ordering",
    function(leaf, x, ctx, aux) {
      leaf(x, aux, opts_collator = bench_args$collator)
    },
    prep = bench_reverse_prep
  ),
  ci_duplicated = bench_op(
    "ordering",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_collator = bench_args$collator)
    }
  ),

  ci_trans_tolower = bench_op(
    "case-map",
    function(leaf, x, ctx, aux) leaf(x)
  ),
  ci_trans_toupper = bench_op(
    "case-map",
    function(leaf, x, ctx, aux) leaf(x)
  ),
  ci_trans_totitle = bench_op(
    "case-map",
    function(leaf, x, ctx, aux) leaf(x)
  ),

  ci_count_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_brkiter = bench_args$brk_word)
    }
  ),
  ci_locate_first_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_brkiter = bench_args$brk_word)
    }
  ),
  ci_locate_all_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_brkiter = bench_args$brk_word)
    }
  ),
  ci_extract_first_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_brkiter = bench_args$brk_word)
    }
  ),
  ci_extract_all_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(
        x, omit_no_match = TRUE,
        opts_brkiter = bench_args$brk_word
      )
    }
  ),
  ci_split_boundaries = bench_op(
    "boundary",
    function(leaf, x, ctx, aux) {
      leaf(x, opts_brkiter = bench_args$brk_word)
    }
  ),

  ci_wrap = bench_op(
    "layout",
    function(leaf, x, ctx, aux) leaf(x, width = bench_args$wrap_width)
  ),
  ci_pad_left = bench_op(
    "layout",
    function(leaf, x, ctx, aux) leaf(x, bench_args$pad_width)
  ),
  ci_pad_right = bench_op(
    "layout",
    function(leaf, x, ctx, aux) leaf(x, bench_args$pad_width)
  ),
  ci_pad_both = bench_op(
    "layout",
    function(leaf, x, ctx, aux) leaf(x, bench_args$pad_width)
  ),
  ci_width = bench_op(
    "layout",
    function(leaf, x, ctx, aux) leaf(x)
  ),

  ci_escape_unicode = bench_op(
    "encoding-files",
    function(leaf, x, ctx, aux) leaf(x)
  ),
  ci_conv = bench_op(
    "encoding-files",
    function(leaf, x, ctx, aux) {
      bench_partition_call(aux, function(partition) {
        leaf(partition$input, partition$encoding, "UTF-8")
      })
    },
    prep = bench_conversion_prep,
    fixture = "conversion"
  ),
  ci_read_lines = bench_op(
    "encoding-files",
    function(leaf, x, ctx, aux) leaf(ctx$corpus, encoding = "UTF-8")
  )
)

bench_fast_ops <- c(
  "ci_count_fixed",
  "ci_cmp_equiv",
  "ci_detect_fixed",
  "ci_endswith_fixed",
  "ci_extract_first_fixed",
  "ci_flatten",
  "ci_length",
  "ci_locate_first_fixed",
  "ci_read_lines",
  "ci_replace_na",
  "ci_startswith_fixed",
  "ci_trim_both",
  "ci_trim_left",
  "ci_trim_right"
)

bench_slow_ops <- c(
  "ci_count_coll",
  "ci_extract_all_coll",
  "ci_locate_all_coll",
  "ci_replace_all_coll",
  "ci_split_coll",
  "ci_wrap"
)

bench_scale_n <- c(fast = 1000000L, medium = 100000L, slow = 10000L)
bench_scale_class <- setNames(rep("medium", length(bench_ops)), names(bench_ops))
bench_scale_class[bench_fast_ops] <- "fast"
bench_scale_class[bench_slow_ops] <- "slow"

stopifnot(
  length(bench_ops) == 67L,
  !anyDuplicated(names(bench_ops)),
  !length(setdiff(c(bench_fast_ops, bench_slow_ops), names(bench_ops))),
  !length(intersect(bench_fast_ops, bench_slow_ops)),
  identical(unname(bench_scale_n[bench_scale_class]), unname(vapply(
    bench_scale_class,
    function(scale) unname(bench_scale_n[[scale]]),
    integer(1L)
  )))
)
