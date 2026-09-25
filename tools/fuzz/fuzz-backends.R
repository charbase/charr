#!/usr/bin/env Rscript
# Compare random stringr calls across charr backends.
# Use `make fuzz`, or `make fuzz FUZZ_REPLAY=local/fuzz/<case>.rds`
# (add FUZZ_REPLAY_LOCALE=latin1 for a case saved by the Latin-1 pass).

args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 2L && identical(args[[1L]], "--replay")) {
  replay <- args[[2L]]
} else if (length(args) == 6L &&
           identical(args[c(1L, 3L, 5L)], c("--seed", "--iters", "--output"))) {
  seed <- as.integer(args[[2L]])
  iters <- as.integer(args[[4L]])
  output <- args[[6L]]
  if (is.na(seed) || is.na(iters) || iters < 1L) {
    stop("seed and iterations must be positive integers")
  }
} else {
  stop("usage: --seed N --iters N --output DIR | --replay CASE.rds")
}
suppressPackageStartupMessages(library(charr))
# str_view_all() uses a session-wide, rate-limited deprecation warning.
# Silence that lifecycle warning so call order cannot select a backend.
options(lifecycle_verbosity = "quiet")

atoms <- c(
  "a", "b", "A", "z", " ", "\t", "\n", "\r\n", ".", "*", "\\", "aa", "ab",
  "\u00e9", "e\u0301", "\u00df", "\u0130", "\u03a3", "\u03c2", "\u4e2d",
  "\U0001f600", "\U0001f468\u200d\U0001f469", "\ufeff", "\u00a0",
  "\u2028", "\u0645", "\u05d0", "1", "22", "x", "-", "_", "Ab", "iI",
  "\u1e9e"
)

rand_string <- function() {
  r <- runif(1L)
  if (r < 0.05) return(NA_character_)
  if (r < 0.10) return("")
  paste(sample(atoms, sample.int(12L, 1L), replace = TRUE), collapse = "")
}

rand_vec <- function(max_n = 40L, long = FALSE, force_n = NULL) {
  n <- if (is.null(force_n)) {
    sample(c(0L, 1L, 2L, 3L, sample.int(max_n, 1L),
             sample(c(32L, 64L, 128L), 1L)), 1L)
  } else {
    force_n
  }
  x <- vapply(seq_len(n), function(i) rand_string(), "")
  if (n > 0L) {
    i <- sample.int(n, 1L)
    kind <- sample(c("plain", "latin1", "unknown", "invalid_utf8",
                     "invalid_unknown", "bytes"), 1L,
                   prob = c(70, 8, 8, 5, 5, 4))
    if (kind == "latin1") {
      x[[i]] <- iconv("caf\u00e9", "UTF-8", "latin1")
      Encoding(x[[i]]) <- "latin1"
    } else if (kind == "unknown") {
      x[[i]] <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xc3, 0xa9)))
      Encoding(x[[i]]) <- "unknown"
    } else if (kind %in% c("invalid_utf8", "invalid_unknown", "bytes")) {
      x[[i]] <- rawToChar(as.raw(sample(list(c(0xc3, 0x28), c(0xff),
                                           c(0xe2, 0x82)), 1L)[[1L]]))
      Encoding(x[[i]]) <- switch(kind, invalid_utf8 = "UTF-8",
                                invalid_unknown = "unknown", bytes = "bytes")
    }
    if (long) {
      repeats <- sample(c(500L, 2000L, 10000L, 15000L), 1L,
                        prob = c(55, 30, 14, 1))
      x[[sample.int(n, 1L)]] <- paste(rep("ab 2 \u00e9", repeats), collapse = "")
    }
    if (runif(1L) < 0.15) names(x) <- paste0("n", seq_along(x))
    if (runif(1L) < 0.02) dim(x) <- c(n, 1L)
  }
  x
}

rand_pattern <- function() {
  sample(c("a", "b", "ab", ".", "[a-z]+", "\\s", "\u00e9", "e\u0301",
           "x*", "(a)(b)?", "\\b", "^", "$", "\U0001f600",
           "[[:alpha:]]+", "a|b", "(?i)a", "\\d+", "", NA_character_), 1L)
}

rand_fixed <- function() {
  sample(c("a", "b", "ab", ".", "\u00e9", "e\u0301", " ",
           "\U0001f600", "aa", "\n", "x", "", NA_character_), 1L)
}

rand_int <- function(allow_na = TRUE) {
  sample(c(-3L, -1L, 0L, 1L, 2L, 3L, 5L, 100L,
           if (allow_na) NA_integer_), 1L)
}

rand_replacement <- function() {
  sample(c("X", "\\0\\0", "", "\u00e9", NA_character_), 1L)
}

rand_patterns <- function() {
  sample(c("a", "b", "", NA_character_), sample(c(2L, 3L), 1L),
         replace = TRUE)
}

one <- function(fn, ...) list(fn = fn, args = list(...))
# Collation compares the first bytes of each sort key before the strings, so
# add copies behind a long common prefix, where only the tails decide.
shared_prefix <- function(x) c(x, paste0("https://example.com/a/long/path/", x))
bool <- function() sample(c(TRUE, FALSE), 1L)
# xx_YY has no ICU data, so ICU falls back to root and warns.
locale <- function() sample(c("en", "tr", "de", "fr", "xx_YY"), 1L)
na_last <- function() sample(c(TRUE, FALSE, NA), 1L)

operations <- list(
  length = function(x) one("str_length", x),
  width = function(x) one("str_width", x),
  upper = function(x) one("str_to_upper", x, locale = locale()),
  lower = function(x) one("str_to_lower", x, locale = locale()),
  title = function(x) one("str_to_title", x, locale = locale()),
  sentence = function(x) one("str_to_sentence", x, locale = locale()),
  trim = function(x) one("str_trim", x, side = sample(c("both", "left", "right"), 1L)),
  squish = function(x) one("str_squish", x),
  reverse = function(x) one("str_reverse", x),
  dup = function(x) one("str_dup", x, rand_int()),
  pad = function(x) one("str_pad", x, rand_int(FALSE) + 3L,
                        sample(c("left", "right", "both"), 1L),
                        sample(c(" ", "x", "\u00e9"), 1L), use_width = bool()),
  trunc = function(x) one("str_trunc", x, max(3L, rand_int(FALSE)),
                          side = sample(c("left", "right", "center"), 1L)),
  sub = function(x) one("str_sub", x, rand_int(), rand_int()),
  sub_replace = function(x) one("str_sub<-", x, rand_int(), rand_int(),
                                value = rand_replacement()),
  sub_all = function(x) one("str_sub_all", x, rand_int(FALSE), rand_int(FALSE)),
  c = function(x) one("str_c", x, rand_vec(3L), sep = sample(c("", "-"), 1L)),
  c_collapse = function(x) one("str_c", x, collapse = "|"),
  flatten = function(x) one("str_flatten", x, "-", na.rm = bool()),
  flatten_comma = function(x) one("str_flatten_comma", x, last = ", and ", na.rm = bool()),
  replace_na = function(x) one("str_replace_na", x, rand_replacement()),
  detect_re = function(x) one("str_detect", x, rand_pattern(), negate = bool()),
  detect_regex = function(x) one("str_detect", x, regex(rand_pattern(),
                              ignore_case = bool(), multiline = TRUE)),
  detect_fx = function(x) one("str_detect", x, fixed(rand_fixed(), bool())),
  detect_co = function(x) one("str_detect", x, coll(rand_fixed(), bool(), locale())),
  detect_vec = function(x) one("str_detect", x, rand_patterns()),
  count_re = function(x) one("str_count", x, rand_pattern()),
  count_fx = function(x) one("str_count", x, fixed(rand_fixed())),
  count_co = function(x) one("str_count", x, coll(rand_fixed(), locale = locale())),
  count_bd = function(x) one("str_count", x, boundary(sample(
    c("character", "word", "sentence", "line_break"), 1L), locale = locale())),
  starts_fx = function(x) one("str_starts", x, fixed(rand_fixed(), bool())),
  ends_co = function(x) one("str_ends", x, coll(rand_fixed(), bool(), locale())),
  locate_re = function(x) one("str_locate", x, rand_pattern()),
  locate_fx = function(x) one("str_locate", x, fixed(rand_fixed())),
  locate_vec = function(x) one("str_locate", x, rand_patterns()),
  locate_all_re = function(x) one("str_locate_all", x, rand_pattern()),
  locate_all_co = function(x) one("str_locate_all", x, coll(rand_fixed())),
  locate_all_bd = function(x) one("str_locate_all", x, boundary("word")),
  extract_re = function(x) one("str_extract", x, rand_pattern()),
  extract_vec = function(x) one("str_extract", x, rand_patterns()),
  extract_all_re = function(x) one("str_extract_all", x, rand_pattern(),
                                   simplify = bool()),
  extract_all_fx = function(x) one("str_extract_all", x, fixed(rand_fixed())),
  extract_all_co = function(x) one("str_extract_all", x, coll(rand_fixed())),
  extract_all_bd = function(x) one("str_extract_all", x, boundary("word")),
  match = function(x) one("str_match", x, "(a)(b)?"),
  match_all = function(x) one("str_match_all", x, "([a-z])(\\d)?"),
  replace_re = function(x) one("str_replace", x, rand_pattern(), rand_replacement()),
  replace_vec = function(x) one("str_replace", x, rand_patterns(), rand_replacement()),
  replace_all_re = function(x) one("str_replace_all", x, rand_pattern(),
                                   rand_replacement()),
  replace_all_fx = function(x) one("str_replace_all", x, fixed(rand_fixed()), "Z"),
  replace_all_co = function(x) one("str_replace_all", x, coll(rand_fixed()), "Z"),
  remove = function(x) one("str_remove", x, rand_pattern()),
  remove_all = function(x) one("str_remove_all", x, rand_pattern()),
  split_re = function(x) one("str_split", x, rand_pattern(),
                             n = sample(c(Inf, 1, 2, 3), 1L), simplify = bool()),
  split_fx = function(x) one("str_split", x, fixed(rand_fixed())),
  split_co = function(x) one("str_split", x, coll(rand_fixed())),
  split_bd = function(x) one("str_split", x, boundary("word", locale = locale())),
  split_1 = function(x) one("str_split_1", if (length(x)) x[[1L]] else "",
                           fixed(" ")),
  split_fixed = function(x) one("str_split_fixed", x, fixed(" "), 3L),
  split_i = function(x) one("str_split_i", x, " ",
                            sample(c(1L, 2L, -1L), 1L)),
  subset = function(x) one("str_subset", x, rand_pattern(), negate = bool()),
  which = function(x) one("str_which", x, rand_pattern(), negate = bool()),
  order = function(x) one("str_order", x, na_last = na_last(),
                          locale = locale(), numeric = bool()),
  sort = function(x) one("str_sort", x, decreasing = bool(),
                         na_last = na_last(), locale = locale(), numeric = bool()),
  rank = function(x) one("str_rank", x, locale = locale(), numeric = bool()),
  unique = function(x) one("str_unique", x, locale = locale(), ignore_case = bool()),
  order_opts = function(x) one("str_order", shared_prefix(x),
                               decreasing = bool(), na_last = na_last(),
                               locale = locale(), strength = sample(1:4, 1L),
                               case_level = bool(),
                               uppercase_first = sample(c(NA, TRUE, FALSE), 1L)),
  rank_opts = function(x) one("str_rank", shared_prefix(x), locale = locale(),
                              strength = sample(1:3, 1L)),
  unique_opts = function(x) one("str_unique", shared_prefix(x),
                                locale = locale(), strength = sample(1:3, 1L)),
  equal = function(x) one("str_equal", x, rev(x), ignore_case = bool()),
  wrap = function(x) one("str_wrap", x, width = sample(c(1L, 5L, 20L), 1L)),
  conv = function(x) one("str_conv", x, "UTF-8"),
  escape = function(x) one("str_escape", x),
  like = function(x) one("str_like", x, sample(c("a%", "%b_", "_"), 1L)),
  ilike = function(x) one("str_ilike", x, sample(c("a%", "%b_", "_"), 1L)),
  to_snake = function(x) one("str_to_snake", x),
  to_camel = function(x) one("str_to_camel", x, first_upper = bool()),
  to_kebab = function(x) one("str_to_kebab", x),
  word = function(x) one("word", x, 1L),
  glue = function(x) one("str_glue", "{value}", value = x),
  glue_data = function(x) one("str_glue_data", list(value = x), "{value}"),
  interp = function(x) one("str_interp", "${value}", list(value = x)),
  view = function(x) one("str_view", x, rand_pattern(), match = NA),
  view_all = function(x) one("str_view_all", x, rand_pattern()),
  read_lines = function(x) one("str_read_lines", x, encoding = "UTF-8")
)

normalize <- function(x) {
  if (is.null(x)) return(NULL)
  attrs <- attributes(x)
  if (is.list(x)) {
    value <- lapply(x, normalize)
  } else if (is.character(x)) {
    marks <- Encoding(x)
    value <- lapply(seq_along(x), function(i) {
      if (is.na(x[[i]])) return(list(kind = "NA"))
      if (identical(marks[[i]], "bytes")) {
        return(list(kind = "bytes", value = as.raw(charToRaw(x[[i]]))))
      }
      # Marks are compared as text. Base may return an input CHARSXP as is
      # (a native string stays "unknown"), while ALTREP stores UTF-8 and
      # marks the same text "UTF-8". Translating to UTF-8 makes those equal
      # but keeps a mark that changes the meaning (latin1 bytes marked
      # UTF-8, say) a mismatch. "bytes" is its own class, compared raw.
      translated <- tryCatch(enc2utf8(x[[i]]), error = function(e) NULL)
      if (is.null(translated)) {
        return(list(kind = paste0("invalid-", marks[[i]]),
                    value = as.raw(charToRaw(x[[i]]))))
      }
      list(kind = "text", value = as.raw(charToRaw(translated)))
    })
  } else {
    value <- as.vector(x)
  }
  list(type = typeof(x), value = value,
       attributes = if (is.null(attrs)) NULL else lapply(attrs, normalize))
}

run_case <- function(call, backend, threads) {
  charr_backend(backend)
  charr_threads(threads)
  charr_min_chunk(1L)
  charr_chunks_per_worker(1000L)
  warned <- FALSE
  error_text <- NULL
  value <- tryCatch(
    withCallingHandlers({
      if (identical(call$fn, "str_read_lines")) {
        path <- tempfile("charr-fuzz-lines-")
        on.exit(unlink(path), add = TRUE)
        con <- file(path, "wb")
        tryCatch(
          writeBin(charToRaw(paste(c(call$args[[1L]], ""), collapse = "\n")), con),
          finally = close(con)
        )
        call$args[[1L]] <- path
      }
      do.call(get(call$fn, envir = asNamespace("charr")), call$args)
    }, warning = function(w) {
      warned <<- TRUE
      invokeRestart("muffleWarning")
    }),
    error = function(e) {
      error_text <<- conditionMessage(e)
      NULL
    }
  )
  list(error = !is.null(error_text), warning = warned,
       result = if (is.null(error_text)) normalize(value) else NULL,
       message = error_text)
}

agree <- function(a, b) {
  identical(a$error, b$error) && identical(a$warning, b$warning) &&
    (a$error || identical(a$result, b$result))
}

# Names of the ways two runs differ, prefixed by the pair compared.
diff_kinds <- function(prefix, a, b) {
  kinds <- c(error = !identical(a$error, b$error),
             warning = !identical(a$warning, b$warning),
             value = !a$error && !b$error && !identical(a$result, b$result))
  paste0(prefix, names(kinds)[kinds])[any(kinds)]
}

compare_case <- function(call) {
  results <- list(
    base = run_case(call, "base", 1L),
    altrep_serial = run_case(call, "altrep", 1L),
    altrep_threaded = run_case(call, "altrep", 4L),
    stringi = run_case(call, "stringi", 1L)
  )
  mismatches <- c(
    diff_kinds("base_vs_altrep_", results$base, results$altrep_serial),
    diff_kinds("serial_vs_threaded_", results$altrep_serial,
               results$altrep_threaded)
  )
  list(results = results, mismatches = mismatches,
       stringi_diff = !agree(results$base, results$stringi))
}

native_class <- function() {
  info <- l10n_info()
  if (isTRUE(info[["UTF-8"]])) "UTF-8"
  else if (isTRUE(info[["Latin-1"]])) "Latin-1"
  else "other"
}

if (exists("replay")) {
  artifact <- readRDS(replay)
  if (!is.null(artifact$native) && !identical(artifact$native, native_class())) {
    cat(sprintf("note: case was saved in a %s session, replaying in %s\n",
                artifact$native, native_class()))
  }
  comparison <- compare_case(artifact$call)
  cat(sprintf("replay seed %d iteration %d operation %s: %s\n",
              artifact$seed, artifact$iteration, artifact$operation,
              if (length(comparison$mismatches)) "MISMATCH" else "agree"))
  if (length(comparison$mismatches)) {
    cat(paste0("  ", comparison$mismatches, "\n"), sep = "")
    quit(status = 1L)
  }
  quit(status = 0L)
}

dir.create(output, showWarnings = FALSE, recursive = TRUE)
set.seed(seed)
replay_locale <- if (identical(native_class(), "Latin-1")) {
  " FUZZ_REPLAY_LOCALE=latin1"
} else {
  ""
}
stringi_diffs <- 0L
calls <- 0L
long_calls <- 0L
reported <- new.env(parent = emptyenv())
for (iteration in seq_len(iters)) {
  for (operation in names(operations)) {
    # Mostly short, with occasional long inputs and rare 100k-character cases.
    long <- runif(1L) < 0.05
    x <- rand_vec(long = long,
                  force_n = if (iteration == 1L && operation == "length") 128L)
    call <- operations[[operation]](x)
    comparison <- compare_case(call)
    calls <- calls + 1L
    long_calls <- long_calls + as.integer(long && length(x) > 0L)
    stringi_diffs <- stringi_diffs + as.integer(comparison$stringi_diff)
    if (length(comparison$mismatches)) {
      for (kind in comparison$mismatches) {
        key <- paste(operation, kind, sep = ":")
        if (exists(key, envir = reported, inherits = FALSE)) next
        assign(key, TRUE, envir = reported)
        artifact <- list(seed = seed, iteration = iteration,
                         native = native_class(),
                         operation = operation, call = call,
                         results = comparison$results,
                         mismatches = comparison$mismatches)
        path <- file.path(output, sprintf("case-%d-%d-%s-%s.rds", seed,
                                          iteration, operation, kind))
        saveRDS(artifact, path)
        cat(sprintf("MISMATCH seed %d iteration %d call %d operation %s (%s)\n",
                    seed, iteration, calls, operation, kind))
        cat(sprintf("artifact: %s\n", path))
        cat(sprintf("replay: make fuzz FUZZ_REPLAY='%s'%s\n", path,
                    replay_locale))
      }
    }
  }
}
cat(sprintf(paste("seed %d (%s) summary: %d calls (%d with a long string),",
                  "%d distinct mismatches, %d informational stringi differences\n"),
            seed, native_class(), calls, long_calls, length(ls(reported)),
            stringi_diffs))
if (length(ls(reported))) quit(status = 1L)
