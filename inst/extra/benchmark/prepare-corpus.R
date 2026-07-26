args <- commandArgs(trailingOnly = TRUE)

source_path <- if (length(args) >= 1L) {
  args[[1L]]
} else {
  "local/sentences.tar.bz2"
}
seed <- if (length(args) >= 2L) as.integer(args[[2L]]) else 20260721L
if (is.na(seed)) {
  stop("seed must be an integer")
}

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
prepare_file <- normalizePath(sub("^--file=", "", file_arg))
bench_dir <- dirname(prepare_file)
data_dir <- file.path(bench_dir, "data")
dir.create(data_dir, recursive = TRUE, showWarnings = FALSE)

ops_file <- file.path(bench_dir, "benchmark-ops.R")
source(ops_file)
source_path <- normalizePath(source_path, mustWork = TRUE)
source_md5 <- unname(tools::md5sum(source_path))
ops_md5 <- unname(tools::md5sum(ops_file))
prepare_md5 <- unname(tools::md5sum(prepare_file))
meta_path <- file.path(data_dir, "tatoeba-scaled-meta.rds")

charr_ns <- loadNamespace("charr")
if (!exists("charr_icu_info", envir = charr_ns, inherits = FALSE) ||
    !exists(".charr_backend_environments", envir = charr_ns, inherits = FALSE)) {
  stop("install the current charr package with the Makefile before preparing fixtures")
}
charr_icu <- get("charr_icu_info", envir = charr_ns, inherits = FALSE)()
base_backend <- get(
  ".charr_backend_environments", envir = charr_ns, inherits = FALSE
)[["base"]]
if (is.null(base_backend)) {
  stop("the installed charr package has no optimized base backend")
}
base_leaf <- function(name) get(name, envir = base_backend, inherits = FALSE)

stringi_info <- suppressWarnings(stringi::stri_info())
preparation_icu <- list(
  charr_mode = unname(charr_icu[["mode"]]),
  charr_runtime_version = unname(charr_icu[["runtime_version"]]),
  charr_data_version = unname(charr_icu[["data_version"]]),
  stringi_version = unname(stringi_info[["ICU.version"]]),
  stringi_system = isTRUE(stringi_info[["ICU.system"]])
)

corpus_paths <- function(scale) {
  n <- unname(bench_scale_n[[scale]])
  stem <- sprintf("tatoeba-%s-%d", scale, n)
  list(
    text = file.path(data_dir, paste0(stem, ".txt")),
    language = file.path(data_dir, paste0(stem, "-language.rds")),
    sentence_id = file.path(data_dir, paste0(stem, "-sentence-id.rds")),
    charvec = file.path(data_dir, paste0(stem, "-charvec.rds")),
    prepared_plain = file.path(data_dir, paste0(stem, "-prepared-plain.rds")),
    prepared_charvec = file.path(data_dir, paste0(stem, "-prepared-charvec.rds"))
  )
}

scales <- names(bench_scale_n)
paths <- setNames(lapply(scales, corpus_paths), scales)

hash_matches <- function(path, expected) {
  file.exists(path) && identical(unname(tools::md5sum(path)), expected)
}

cache_is_current <- function(meta) {
  if (!identical(meta$format_version, 2L) ||
      !identical(meta$seed, seed) ||
      !identical(meta$source_md5, source_md5) ||
      !identical(meta$scale_n, bench_scale_n) ||
      !identical(meta$ops_md5, ops_md5) ||
      !identical(meta$prepare_md5, prepare_md5) ||
      !identical(meta$preparation_icu, preparation_icu) ||
      !identical(meta$sampling, "streaming_reservoir_then_shuffle")) {
    return(FALSE)
  }

  all(vapply(scales, function(scale) {
    item <- meta$scales[[scale]]
    if (is.null(item)) {
      return(FALSE)
    }
    all(vapply(names(paths[[scale]]), function(field) {
      hash_matches(paths[[scale]][[field]], item[[paste0(field, "_md5")]])
    }, logical(1L)))
  }, logical(1L)))
}

if (file.exists(meta_path)) {
  old_meta <- readRDS(meta_path)
  if (cache_is_current(old_meta)) {
    cat("reusing hash-checked corpus and prepared query artifacts\n")
    quit(save = "no")
  }
}

object_md5 <- function(value) {
  path <- tempfile("charr-benchmark-hash-", fileext = ".rds")
  on.exit(unlink(path), add = TRUE)
  saveRDS(value, path, compress = FALSE, version = 3L)
  unname(tools::md5sum(path))
}

open_tatoeba <- function(path) {
  if (grepl("\\.tar\\.(bz2|gz|xz)$", path, ignore.case = TRUE)) {
    members <- utils::untar(path, list = TRUE)
    member <- members[basename(members) == "sentences.csv"]
    if (length(member) != 1L) {
      stop("the Tatoeba archive must contain exactly one sentences.csv")
    }
    tar <- Sys.getenv("TAR", unset = "tar")
    command <- paste(
      shQuote(tar), "-xOf", shQuote(path), shQuote(member[[1L]])
    )
    return(pipe(command, open = "r", encoding = "UTF-8"))
  }

  file(path, open = "r", encoding = "UTF-8")
}

max_n <- max(bench_scale_n)
reservoir_text <- character(max_n)
reservoir_language <- character(max_n)
reservoir_id <- character(max_n)
source_n <- 0L
chunk_n <- 50000L

set.seed(
  seed,
  kind = "Mersenne-Twister",
  normal.kind = "Inversion",
  sample.kind = "Rejection"
)
connection <- open_tatoeba(source_path)
repeat {
  lines <- readLines(connection, n = chunk_n, warn = FALSE)
  if (!length(lines)) {
    break
  }
  fields <- stringi::stri_split_fixed(lines, "\t", n = 3L, simplify = TRUE)
  if (ncol(fields) != 3L || anyNA(fields[, 1:2, drop = FALSE]) ||
      any(!nzchar(fields[, 1L])) || any(!nzchar(fields[, 2L]))) {
    close(connection)
    stop("malformed Tatoeba TSV record")
  }

  ids <- fields[, 1L]
  languages <- fields[, 2L]
  text <- fields[, 3L]
  m <- length(text)
  fill <- min(max_n - min(source_n, max_n), m)
  if (fill > 0L) {
    slots <- source_n + seq_len(fill)
    reservoir_id[slots] <- ids[seq_len(fill)]
    reservoir_language[slots] <- languages[seq_len(fill)]
    reservoir_text[slots] <- text[seq_len(fill)]
  }

  if (fill < m) {
    incoming <- (fill + 1L):m
    positions <- source_n + incoming
    replacement <- floor(stats::runif(length(incoming)) * positions) + 1L
    chosen <- which(replacement <= max_n)
    if (length(chosen)) {
      source_index <- incoming[chosen]
      target_index <- replacement[chosen]
      reservoir_id[target_index] <- ids[source_index]
      reservoir_language[target_index] <- languages[source_index]
      reservoir_text[target_index] <- text[source_index]
    }
  }

  source_n <- source_n + m
  if (source_n %% 1000000L < m) {
    cat(sprintf("read %s source records\n", format(source_n, big.mark = ",")))
  }
}
close(connection)

if (source_n < 1L) {
  stop("source corpus is empty")
}

if (source_n >= max_n) {
  order <- sample.int(max_n)
  sampled_text <- reservoir_text[order]
  sampled_language <- reservoir_language[order]
  sampled_id <- reservoir_id[order]
} else {
  source_text <- reservoir_text[seq_len(source_n)]
  source_language <- reservoir_language[seq_len(source_n)]
  source_id <- reservoir_id[seq_len(source_n)]
  sampled_text <- character(max_n)
  sampled_language <- character(max_n)
  sampled_id <- character(max_n)
  offset <- 0L
  while (offset < max_n) {
    order <- sample.int(source_n)
    take <- min(source_n, max_n - offset)
    target <- offset + seq_len(take)
    sampled_text[target] <- source_text[order[seq_len(take)]]
    sampled_language[target] <- source_language[order[seq_len(take)]]
    sampled_id[target] <- source_id[order[seq_len(take)]]
    offset <- offset + take
  }
}
rm(reservoir_text, reservoir_language, reservoir_id, fields, lines)
gc(FALSE)

choose_common_word <- function(tokens) {
  tokens <- tokens[!is.na(tokens) & nzchar(tokens)]
  if (!length(tokens)) {
    return(NA_character_)
  }
  tokens <- tokens[
    stringi::stri_detect_regex(tokens, "\\p{L}") &
      !stringi::stri_detect_regex(tokens, "\\p{Cc}")
  ]
  if (!length(tokens)) {
    return(NA_character_)
  }

  counts <- table(tokens, useNA = "no")
  candidates <- names(counts)
  representative <- nchar(candidates, type = "bytes") > 1L
  if (any(representative)) {
    candidates <- candidates[representative]
    counts <- counts[representative]
  }
  best_count <- max(counts)
  best <- candidates[unname(counts) == best_count]
  sort(enc2utf8(best), method = "radix")[[1L]]
}

derive_common_words <- function(text, language) {
  language_levels <- sort(unique(language), method = "radix")
  words <- setNames(character(length(language_levels)), language_levels)
  for (i in seq_along(language_levels)) {
    lang <- language_levels[[i]]
    token_lists <- stringi::stri_extract_all_words(
      text[language == lang], simplify = FALSE, omit_no_match = TRUE
    )
    words[[lang]] <- choose_common_word(unlist(token_lists, use.names = FALSE))
  }
  missing <- names(words)[is.na(words) | !nzchar(words)]
  if (length(missing)) {
    stop("no ICU word token for languages: ", paste(missing, collapse = ", "))
  }
  words
}

cat("deriving one common surface word per retained language\n")
common_words <- derive_common_words(sampled_text, sampled_language)

collation_candidates <- data.frame(
  language = c("eng", "fra", "deu", "spa", "ita", "por", "nld", "pol"),
  locale = c("en", "fr", "de", "es", "it", "pt", "nl", "pl"),
  stringsAsFactors = FALSE
)

coll_cmp <- base_leaf("stri_cmp_equiv")
coll_detect <- base_leaf("stri_detect_coll")
coll_starts <- base_leaf("stri_startswith_coll")
coll_ends <- base_leaf("stri_endswith_coll")

# The collation query is the uppercase form of a word taken from the same
# partition, so a case-insensitive collator has to match strings whose bytes
# differ. Corpus words are lowercase or titlecase, so uppercasing always
# changes them; a word that uppercases to itself is not usable here.
uppercase_query <- function(word, locale, options) {
  candidate <- stringi::stri_trans_toupper(word, locale = locale)
  if (is.na(candidate) || identical(candidate, word)) {
    return(NA_character_)
  }
  if (!isTRUE(coll_cmp(word, candidate, opts_collator = options))) {
    return(NA_character_)
  }
  candidate
}

positional_source <- function(text, locale, options, position) {
  if (identical(position, "starts")) {
    words <- stringi::stri_extract_first_words(text)
    at_edge <- stringi::stri_startswith_fixed(text, words)
    surface_values <- words
    compare <- coll_starts
  } else {
    words <- stringi::stri_extract_last_words(text)
    surface_values <- stringi::stri_extract_last_regex(
      text,
      "\\p{L}[\\p{L}\\p{M}]*(?:[\\p{P}\\p{S}\\p{Zs}]*)$"
    )
    at_edge <- !is.na(surface_values)
    compare <- coll_ends
  }
  eligible <- !is.na(words) & at_edge & nchar(words, type = "bytes") > 1L
  surface_pattern <- choose_common_word(surface_values[eligible])
  if (is.na(surface_pattern)) {
    return(NULL)
  }
  surface_word <- stringi::stri_extract_first_words(surface_pattern)
  query <- uppercase_query(surface_pattern, locale, options)
  if (is.na(query)) {
    return(NULL)
  }
  expected <- compare(text, query, opts_collator = options)
  if (!any(expected) || !any(!expected)) {
    return(NULL)
  }
  list(
    surface_word = surface_word,
    surface_pattern = surface_pattern,
    query = query,
    expected = expected
  )
}

collation_sources <- list()
for (i in seq_len(nrow(collation_candidates))) {
  lang <- collation_candidates$language[[i]]
  if (!lang %in% sampled_language) {
    next
  }
  locale <- collation_candidates$locale[[i]]
  options <- list(locale = locale, strength = 1L)
  word <- unname(common_words[[lang]])
  query <- uppercase_query(word, locale, options)
  if (is.na(query)) {
    next
  }
  pool <- sampled_text[sampled_language == lang]
  expected <- coll_detect(pool, query, opts_collator = options)
  starts <- positional_source(pool, locale, options, "starts")
  ends <- positional_source(pool, locale, options, "ends")
  if (!any(expected) || !any(!expected) || is.null(starts) || is.null(ends)) {
    next
  }
  collation_sources[[lang]] <- list(
    language = lang,
    locale = locale,
    options = options,
    surface_word = word,
    query = query,
    text = pool,
    expected = expected,
    starts = starts,
    ends = ends
  )
  if (length(collation_sources) == 4L) {
    break
  }
}
if (length(collation_sources) < 2L) {
  stop("could not prepare at least two natural collation partitions")
}

cycle_to_length <- function(value, n) {
  value[rep(seq_along(value), length.out = n)]
}

partition_sizes <- function(n, k) {
  sizes <- rep(n %/% k, k)
  sizes[seq_len(n %% k)] <- sizes[seq_len(n %% k)] + 1L
  sizes
}

make_positional_subset <- function(text, expected, n, minimum_rate = 0.05) {
  natural_index <- rep(seq_along(text), length.out = n)
  natural_rate <- mean(expected[natural_index])
  if (natural_rate >= minimum_rate) {
    return(list(text = text[natural_index], expected = expected[natural_index]))
  }

  positive_n <- max(1L, ceiling(n * minimum_rate))
  negative_n <- n - positive_n
  positive <- text[expected]
  negative <- text[!expected]
  if (!length(positive) || !length(negative)) {
    stop("a positional collation subset lacks natural matches or nonmatches")
  }
  list(
    text = c(cycle_to_length(positive, positive_n), cycle_to_length(negative, negative_n)),
    expected = c(rep(TRUE, positive_n), rep(FALSE, negative_n))
  )
}

make_collation_plain <- function(n) {
  sizes <- partition_sizes(n, length(collation_sources))
  Map(function(source, size) {
    index <- rep(seq_along(source$text), length.out = size)
    text <- source$text[index]
    expected <- source$expected[index]
    starts <- make_positional_subset(
      source$text, source$starts$expected, size
    )
    ends <- make_positional_subset(
      source$text, source$ends$expected, size
    )
    list(
      language = source$language,
      locale = source$locale,
      options = source$options,
      surface_word = source$surface_word,
      pattern = source$query,
      text = text,
      expected_detect = expected,
      startswith_text = starts$text,
      startswith_surface_word = source$starts$surface_word,
      startswith_surface_pattern = source$starts$surface_pattern,
      startswith_pattern = source$starts$query,
      expected_starts = starts$expected,
      endswith_text = ends$text,
      endswith_surface_word = source$ends$surface_word,
      endswith_surface_pattern = source$ends$surface_pattern,
      endswith_pattern = source$ends$query,
      expected_ends = ends$expected,
      positive_rate = mean(expected),
      starts_natural_rate = mean(source$starts$expected),
      starts_rate = mean(starts$expected),
      ends_natural_rate = mean(source$ends$expected),
      ends_rate = mean(ends$expected)
    )
  }, collation_sources, sizes)
}

encoding_specs <- list(
  list(
    encoding = "Windows-1252",
    languages = c("eng", "fra", "spa", "deu", "ita", "por", "nld", "cat")
  ),
  list(
    encoding = "ISO-8859-2",
    languages = c("pol", "ces", "slk", "hun", "slv", "hrv", "ron")
  ),
  list(encoding = "Shift_JIS", languages = "jpn")
)

selected_conv <- base_leaf("stri_conv")

encoded_source <- function(spec) {
  source <- sampled_text[sampled_language %in% spec$languages]
  candidate_count <- length(source)
  encoded_raw <- iconv(
    source, from = "UTF-8", to = spec$encoding,
    sub = NA_character_, mark = FALSE, toRaw = TRUE
  )
  valid <- lengths(encoded_raw) > 0L & vapply(encoded_raw, function(value) {
    !is.null(value) && !any(value == as.raw(0L)) && any(as.integer(value) > 127L)
  }, logical(1L))
  source <- source[valid]
  encoded_raw <- encoded_raw[valid]
  representable_count <- length(source)
  encoded <- vapply(encoded_raw, rawToChar, character(1L), multiple = FALSE)
  Encoding(encoded) <- "bytes"
  decoded <- iconv(
    encoded, from = spec$encoding, to = "UTF-8",
    sub = NA_character_, mark = TRUE
  )
  decoded_charr <- selected_conv(encoded, spec$encoding, "UTF-8")
  decoded_stringi <- stringi::stri_encode(
    encoded, from = spec$encoding, to = "UTF-8"
  )
  exact <- !is.na(decoded) & !is.na(decoded_charr) & !is.na(decoded_stringi) &
    decoded == source & decoded_charr == source & decoded_stringi == source
  source <- source[exact]
  encoded <- encoded[exact]
  decoded <- decoded[exact]
  decoded_charr <- decoded_charr[exact]
  decoded_stringi <- decoded_stringi[exact]
  if (!length(encoded) ||
      !identical(enc2utf8(decoded), enc2utf8(source)) ||
      !identical(enc2utf8(decoded_charr), enc2utf8(source)) ||
      !identical(enc2utf8(decoded_stringi), enc2utf8(source))) {
    stop("no exact non-ASCII round trips for ", spec$encoding)
  }
  list(
    encoding = spec$encoding,
    languages = spec$languages,
    input = encoded,
    expected = enc2utf8(source),
    candidate_count = candidate_count,
    representable_count = representable_count
  )
}

cat("encoding exact-roundtrip legacy byte sources with base iconv\n")
conversion_sources <- lapply(encoding_specs, encoded_source)

make_conversion_plain <- function(n) {
  sizes <- partition_sizes(n, length(conversion_sources))
  Map(function(source, size) {
    list(
      encoding = source$encoding,
      languages = source$languages,
      input = cycle_to_length(source$input, size),
      expected = cycle_to_length(source$expected, size),
      unique_source_records = length(source$input),
      candidate_count = source$candidate_count,
      representable_count = source$representable_count
    )
  }, conversion_sources, sizes)
}

to_charvec_partition <- function(partition, field) {
  partition[[field]] <- charport::as_charvec(partition[[field]])
  info <- charport::charport_info(partition[[field]])
  stopifnot(charport::is_charvec(partition[[field]]), !isTRUE(info$is_materialized))
  partition
}

scale_meta <- vector("list", length(scales))
names(scale_meta) <- scales
for (scale in scales) {
  n <- unname(bench_scale_n[[scale]])
  text <- sampled_text[seq_len(n)]
  language <- sampled_language[seq_len(n)]
  sentence_id <- sampled_id[seq_len(n)]
  path <- paths[[scale]]

  writeLines(text, path$text, useBytes = TRUE)
  saveRDS(language, path$language, compress = FALSE)
  saveRDS(sentence_id, path$sentence_id, compress = FALSE)
  charvec <- charport::as_charvec(text)
  info <- charport::charport_info(charvec)
  stopifnot(charport::is_charvec(charvec), !isTRUE(info$is_materialized))
  saveRDS(charvec, path$charvec, compress = FALSE)

  fixed_pattern <- unname(common_words[language])
  fixed_counts <- stringi::stri_count_fixed(text, fixed_pattern)
  fixed_plain <- list(pattern = fixed_pattern)
  fixed_charvec <- list(pattern = charport::as_charvec(fixed_pattern))
  stopifnot(
    identical(fixed_plain$pattern, fixed_charvec$pattern),
    !isTRUE(charport::charport_info(fixed_charvec$pattern)$is_materialized)
  )

  collation_plain <- if (scale %in% c("medium", "slow")) {
    make_collation_plain(n)
  } else {
    NULL
  }
  collation_charvec <- if (is.null(collation_plain)) NULL else lapply(
    collation_plain,
    function(partition) {
      partition <- to_charvec_partition(partition, "text")
      partition <- to_charvec_partition(partition, "startswith_text")
      partition <- to_charvec_partition(partition, "endswith_text")
      partition <- to_charvec_partition(partition, "pattern")
      partition <- to_charvec_partition(partition, "startswith_pattern")
      to_charvec_partition(partition, "endswith_pattern")
    }
  )

  conversion_plain <- if (identical(scale, "medium")) {
    make_conversion_plain(n)
  } else {
    NULL
  }
  conversion_charvec <- if (is.null(conversion_plain)) NULL else lapply(
    conversion_plain, to_charvec_partition, field = "input"
  )

  plain <- list(
    format_version = 2L,
    scale = scale,
    n = n,
    mode = "plain",
    seed = seed,
    source_md5 = source_md5,
    ops_md5 = ops_md5,
    prepare_md5 = prepare_md5,
    preparation_icu = preparation_icu,
    corpus_text_md5 = unname(tools::md5sum(path$text)),
    fixed_extract = fixed_plain,
    collation = collation_plain,
    conversion = conversion_plain
  )
  charvec_prepared <- list(
    format_version = 2L,
    scale = scale,
    n = n,
    mode = "charvec",
    seed = seed,
    source_md5 = source_md5,
    ops_md5 = ops_md5,
    prepare_md5 = prepare_md5,
    preparation_icu = preparation_icu,
    corpus_text_md5 = plain$corpus_text_md5,
    fixed_extract = fixed_charvec,
    collation = collation_charvec,
    conversion = conversion_charvec
  )
  saveRDS(plain, path$prepared_plain, compress = FALSE)
  saveRDS(charvec_prepared, path$prepared_charvec, compress = FALSE)

  coll_meta <- if (is.null(collation_plain)) NULL else lapply(
    collation_plain,
    function(partition) c(partition[c(
        "language", "locale", "options", "surface_word", "pattern",
        "startswith_surface_word", "startswith_pattern",
        "startswith_surface_pattern", "endswith_surface_word",
        "endswith_surface_pattern", "endswith_pattern",
        "positive_rate", "starts_natural_rate", "starts_rate",
        "ends_natural_rate", "ends_rate"
      )], list(
        n = length(partition$text),
        match_count = sum(partition$expected_detect),
        starts_match_count = sum(partition$expected_starts),
        ends_match_count = sum(partition$expected_ends),
        input_byte_estimate = sum(nchar(partition$text, type = "bytes")),
        output_byte_estimate = sum(partition$expected_detect) *
          nchar(partition$surface_word, type = "bytes")
      ))
  )
  conversion_meta <- if (is.null(conversion_plain)) NULL else lapply(
    conversion_plain,
    function(partition) list(
      encoding = partition$encoding,
      languages = partition$languages,
      n = length(partition$input),
      unique_source_records = partition$unique_source_records,
      candidate_count = partition$candidate_count,
      representable_count = partition$representable_count,
      exact_roundtrip_rate = partition$unique_source_records /
        partition$candidate_count,
      input_bytes = sum(nchar(partition$input, type = "bytes")),
      expected_bytes = sum(nchar(partition$expected, type = "bytes"))
    )
  )

  file_md5 <- vapply(path, function(file) {
    unname(tools::md5sum(file))
  }, character(1L))
  names(file_md5) <- paste0(names(file_md5), "_md5")
  scale_meta[[scale]] <- c(list(
    n = n,
    text = normalizePath(path$text, mustWork = TRUE),
    language = normalizePath(path$language, mustWork = TRUE),
    sentence_id = normalizePath(path$sentence_id, mustWork = TRUE),
    charvec = normalizePath(path$charvec, mustWork = TRUE),
    prepared_plain = normalizePath(path$prepared_plain, mustWork = TRUE),
    prepared_charvec = normalizePath(path$prepared_charvec, mustWork = TRUE),
    text_bytes = unname(file.size(path$text)),
    language_count = length(unique(language)),
    text_payload_md5 = object_md5(text),
    language_payload_md5 = object_md5(language),
    plain_prepared_payload_md5 = object_md5(plain),
    charvec_prepared_payload_md5 = object_md5(charvec_prepared),
    fixed_extract = list(
      positive_record_rate = mean(fixed_counts > 0L),
      total_matches = sum(fixed_counts),
      distinct_patterns = length(unique(fixed_pattern)),
      output_byte_estimate = sum(
        as.double(fixed_counts) * nchar(fixed_pattern, type = "bytes")
      )
    ),
    collation = coll_meta,
    conversion = conversion_meta
  ), as.list(file_md5))

  rm(
    text, language, sentence_id, charvec, plain, charvec_prepared,
    fixed_plain, fixed_charvec, collation_plain, collation_charvec,
    conversion_plain, conversion_charvec
  )
  gc(FALSE)
}

meta <- list(
  format_version = 2L,
  source = source_path,
  source_n = source_n,
  source_md5 = source_md5,
  ops_md5 = ops_md5,
  prepare_md5 = prepare_md5,
  seed = seed,
  sampling = "streaming_reservoir_then_shuffle",
  nested_prefixes = TRUE,
  scale_n = bench_scale_n,
  preparation_icu = preparation_icu,
  common_words = common_words,
  scales = scale_meta
)
saveRDS(meta, meta_path)

for (scale in scales) {
  item <- scale_meta[[scale]]
  cat(sprintf(
    "%s: %s records, %d languages, %.1f MB, %s\n",
    scale, format(item$n, big.mark = ","), item$language_count,
    item$text_bytes / 2^20, item$text
  ))
}
cat(sprintf(
  "seed: %d; charr ICU: %s (%s); metadata: %s\n",
  seed, preparation_icu$charr_runtime_version,
  preparation_icu$charr_mode, meta_path
))
