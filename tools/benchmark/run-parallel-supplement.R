# Measure work-order shapes that are absent from the public benchmark registry.
# The controller and fresh-process worker live in one file so this supplement
# does not change the corpus cache key or the recorded 67-operation harness.

supplement_reps <- 5L
supplement_threads <- c(1L, 2L, 4L, 8L)
supplement_scale_n <- c(medium = 100000L, slow = 10000L)

supplement_restore_input <- function(values, source) {
  if (charport::is_charvec(source)) {
    charport::as_charvec(values)
  } else {
    values
  }
}

supplement_text <- function(x) {
  if (charport::is_charvec(x)) as.character(x) else x
}

supplement_affix_whitespace <- function(x) {
  values <- supplement_text(x)
  present <- !is.na(values)
  values[present] <- paste0("  ", values[present], "\t ")
  supplement_restore_input(values, x)
}

supplement_decompose <- function(x) {
  values <- stringi::stri_trans_nfd(supplement_text(x))
  supplement_restore_input(values, x)
}

supplement_add_lines <- function(x) {
  values <- supplement_text(x)
  present <- !is.na(values)
  values[present] <- paste0(values[present], "\r\n", values[present])
  supplement_restore_input(values, x)
}

supplement_op <- function(
  entrypoint, family, scale, prep, call, oracle, base_call = NULL,
  wrapper = entrypoint
) {
  list(
    entrypoint = entrypoint,
    wrapper = wrapper,
    family = family,
    scale = scale,
    prep = prep,
    call = call,
    oracle = oracle,
    base_call = base_call
  )
}

supplement_ops <- list(
  ci_replace_all_charclass = supplement_op(
    "ci_replace_all_charclass", "character-class", "medium",
    supplement_affix_whitespace,
    function(leaf, x) {
      leaf(x, "\\p{WHITE_SPACE}", " ", merge = TRUE)
    },
    function(x) {
      stringi::stri_replace_all_charclass(
        x, "\\p{WHITE_SPACE}", " ", merge = TRUE
      )
    }
  ),
  ci_trans_nfc = supplement_op(
    "ci_trans_nfc", "normalization", "medium",
    supplement_decompose,
    function(leaf, x) leaf(x),
    function(x) stringi::stri_trans_nfc(x)
  ),
  ci_split_lines = supplement_op(
    "ci_split_lines", "line-split", "medium",
    supplement_add_lines,
    function(leaf, x) leaf(x, omit_empty = FALSE),
    function(x) stringi::stri_split_lines(x, omit_empty = FALSE)
  ),
  ci_encode_raw = supplement_op(
    "ci_encode_raw", "encoding", "medium", identity,
    function(leaf, x) leaf(x, "UTF-8", "UTF-16LE", to_raw = TRUE),
    function(x) {
      stringi::stri_encode(x, "UTF-8", "UTF-16LE", to_raw = TRUE)
    },
    # The current ci_encode R wrapper selects one of two ALTREP natives. Call
    # the unchanged combined base routine directly so this remains a noise
    # control instead of measuring the dispatcher change.
    function(namespace, x) {
      native <- get(
        "C_charr_base_ci_encode", envir = namespace, inherits = FALSE
      )
      .Call(native, x, "UTF-8", "UTF-16LE", TRUE)
    },
    wrapper = "ci_encode"
  ),
  ci_wrap_list = supplement_op(
    "ci_wrap", "wrap", "slow", identity,
    function(leaf, x) {
      leaf(x, width = 40L, simplify = FALSE, .output_mode = 0L)
    },
    function(x) stringi::stri_wrap(x, width = 40L, simplify = FALSE)
  ),
  ci_wrap_joined = supplement_op(
    "ci_wrap", "wrap", "slow", identity,
    function(leaf, x) {
      leaf(x, width = 40L, simplify = TRUE, .output_mode = 2L)
    },
    function(x) {
      lines <- stringi::stri_wrap(x, width = 40L, simplify = FALSE)
      vapply(
        lines,
        function(value) stringi::stri_join(value, collapse = "\n"),
        character(1L)
      )
    }
  )
)

supplement_conditions <- data.frame(
  condition = c(
    "main-base", "main-altrep-1", "current-base",
    paste0("current-altrep-", supplement_threads)
  ),
  condition_label = c(
    "main base", "main ALTREP, 1 thread", "current base",
    paste0("current ALTREP, ", supplement_threads, " thread",
           ifelse(supplement_threads == 1L, "", "s"))
  ),
  package_role = c(
    "main", "main", "current", rep("current", length(supplement_threads))
  ),
  backend = c(
    "base", "altrep", "base", rep("altrep", length(supplement_threads))
  ),
  input_mode = c(
    "plain", "charvec", "plain",
    rep("charvec", length(supplement_threads))
  ),
  nthreads = c(1L, 1L, 1L, supplement_threads),
  stringsAsFactors = FALSE
)

stopifnot(
  length(supplement_ops) == 6L,
  !anyDuplicated(names(supplement_ops)),
  identical(
    unname(supplement_scale_n[vapply(
      supplement_ops, `[[`, character(1L), "scale"
    )]),
    c(100000L, 100000L, 100000L, 100000L, 10000L, 10000L)
  ),
  nrow(supplement_conditions) == 7L,
  !anyDuplicated(supplement_conditions$condition)
)

supplement_normalize_attributes <- function(value) {
  value_attributes <- attributes(value)
  if (is.null(value_attributes)) {
    return(NULL)
  }
  lapply(value_attributes, supplement_normalize_result)
}

supplement_normalize_result <- function(value) {
  if (is.character(value)) {
    missing <- is.na(value)
    payload <- lapply(seq_along(value), function(index) {
      if (missing[[index]]) NULL else charToRaw(value[[index]])
    })
    return(list(
      type = "character",
      length = length(value),
      missing = missing,
      encoding = Encoding(value),
      payload = payload,
      attributes = supplement_normalize_attributes(value)
    ))
  }
  if (is.list(value)) {
    return(list(
      type = typeof(value),
      values = lapply(value, supplement_normalize_result),
      attributes = supplement_normalize_attributes(value)
    ))
  }

  value_attributes <- supplement_normalize_attributes(value)
  attributes(value) <- NULL
  list(
    type = typeof(value),
    value = value,
    attributes = value_attributes
  )
}

supplement_package_metadata <- function(namespace, loaded_package) {
  if (exists("charr_icu_info", envir = namespace, inherits = FALSE)) {
    info <- get("charr_icu_info", envir = namespace, inherits = FALSE)()
    charr_icu_version <- info[["runtime_version"]]
    charr_icu_mode <- info[["mode"]]
  } else {
    info <- suppressWarnings(
      get("ci_info", envir = namespace, inherits = FALSE)(FALSE)
    )
    charr_icu_version <- info[["ICU.version"]]
    bundled <- get(
      "charr_icu_bundled", envir = namespace, inherits = FALSE
    )()
    charr_icu_mode <- if (isTRUE(bundled)) "bundle" else "system"
  }
  stringi_info <- suppressWarnings(stringi::stri_info())
  list(
    package_path = loaded_package,
    package_version = as.character(utils::packageVersion("charr")),
    charr_icu_version = charr_icu_version,
    charr_icu_mode = charr_icu_mode,
    stringi_icu_version = stringi_info[["ICU.version"]],
    r_version = R.version.string
  )
}

supplement_emit_metadata <- function(key, value) {
  value <- gsub("[\t\r\n]", " ", as.character(value))
  cat(sprintf("META\t%s\t%s\n", key, value))
}

supplement_resolve_leaf <- function(namespace, backend, entrypoint) {
  environment <- if (identical(backend, "base")) {
    get(".charr_base_leaf_environment", envir = namespace, inherits = FALSE)
  } else {
    namespace
  }
  if (!exists(entrypoint, envir = environment, inherits = FALSE)) {
    stop("installed package lacks internal entrypoint: ", entrypoint)
  }
  get(entrypoint, envir = environment, inherits = FALSE)
}

supplement_evaluate <- function(op, backend, namespace, x) {
  if (identical(backend, "stringi")) {
    return(op$oracle(x))
  }
  if (identical(backend, "base") && !is.null(op$base_call)) {
    return(op$base_call(namespace, x))
  }
  leaf <- supplement_resolve_leaf(namespace, backend, op$wrapper)
  op$call(leaf, x)
}

supplement_worker <- function(args) {
  if (length(args) < 2L) {
    stop("worker usage: <metadata|preflight|time> <package-lib> ...")
  }
  mode <- args[[1L]]
  package_lib <- normalizePath(args[[2L]], mustWork = TRUE)
  stopifnot(mode %in% c("metadata", "preflight", "time"))

  .libPaths(c(package_lib, .libPaths()))
  suppressMessages(library(charr, lib.loc = package_lib))
  loaded_package <- normalizePath(system.file(package = "charr"), mustWork = TRUE)
  expected_package <- normalizePath(
    file.path(package_lib, "charr"), mustWork = TRUE
  )
  stopifnot(identical(loaded_package, expected_package))
  namespace <- asNamespace("charr")

  if (identical(mode, "metadata")) {
    stopifnot(length(args) == 2L)
    metadata <- supplement_package_metadata(namespace, loaded_package)
    for (key in names(metadata)) {
      supplement_emit_metadata(key, metadata[[key]])
    }
    quit(save = "no")
  }

  if (length(args) != 10L) {
    stop(
      "preflight/time worker requires: <package-lib> <operation> <backend> ",
      "<input-mode> <text> <charvec-rds> <input-n> <nthreads> <output|rep>"
    )
  }
  operation <- args[[3L]]
  backend <- args[[4L]]
  input_mode <- args[[5L]]
  corpus_text <- normalizePath(args[[6L]], mustWork = TRUE)
  corpus_charvec <- normalizePath(args[[7L]], mustWork = TRUE)
  input_n <- as.integer(args[[8L]])
  nthreads <- as.integer(args[[9L]])
  stopifnot(
    backend %in% c("stringi", "base", "altrep"),
    input_mode %in% c("plain", "charvec"),
    !is.na(input_n), input_n > 0L,
    nthreads %in% supplement_threads
  )
  op <- supplement_ops[[operation]]
  if (is.null(op)) {
    stop("unknown supplementary operation: ", operation)
  }

  options(charr_backend = backend); charr_threads(nthreads)
  if (identical(input_mode, "charvec")) {
    x <- readRDS(corpus_charvec)
    info <- charport::charport_info(x)
    stopifnot(
      charport::is_charvec(x),
      !isTRUE(info$is_materialized),
      length(x) == input_n
    )
  } else {
    x <- readLines(corpus_text, encoding = "UTF-8", warn = FALSE)
    stopifnot(is.character(x), length(x) == input_n)
  }
  x <- op$prep(x)
  if (identical(input_mode, "charvec")) {
    info <- charport::charport_info(x)
    stopifnot(
      charport::is_charvec(x),
      !isTRUE(info$is_materialized),
      length(x) == input_n
    )
    input_class <- "charvec"
  } else {
    stopifnot(is.character(x), length(x) == input_n)
    input_class <- "character"
  }
  gc(FALSE)

  if (identical(mode, "preflight")) {
    output_path <- args[[10L]]
    result <- supplement_evaluate(op, backend, namespace, x)
    saveRDS(
      supplement_normalize_result(result), output_path,
      compress = FALSE
    )
    cat(sprintf(
      "CHECK\t%s\t%s\t%s\t%d\n",
      operation, backend, input_class, nthreads
    ))
    quit(save = "no")
  }

  rep <- as.integer(args[[10L]])
  stopifnot(!is.na(rep), rep >= 1L)
  started <- proc.time()[["elapsed"]]
  result <- supplement_evaluate(op, backend, namespace, x)
  seconds <- proc.time()[["elapsed"]] - started
  cat(sprintf(
    "TIME\t%.9f\t%s\t%s\t%d\n",
    seconds, backend, input_class, nthreads
  ))
  rm(result)
}

supplement_run_process <- function(rscript, run_file, arguments) {
  output <- suppressWarnings(system2(
    rscript,
    c("--vanilla", shQuote(run_file), arguments),
    stdout = TRUE,
    stderr = TRUE
  ))
  list(output = output, status = attr(output, "status"))
}

supplement_parse_metadata <- function(rscript, run_file, package_lib, role) {
  process <- supplement_run_process(
    rscript, run_file,
    c("--worker", "metadata", shQuote(package_lib))
  )
  lines <- grep("^META\t", process$output, value = TRUE)
  if (!is.null(process$status) || !length(lines)) {
    stop(
      "metadata worker failed for ", role, ":\n",
      paste(process$output, collapse = "\n")
    )
  }
  fields <- strsplit(lines, "\t", fixed = TRUE)
  values <- vapply(fields, function(field) field[[3L]], character(1L))
  names(values) <- vapply(fields, function(field) field[[2L]], character(1L))
  values
}

supplement_controller <- function(args, run_file) {
  if (length(args) >= 1L && identical(args[[1L]], "--dry-run")) {
    if (length(args) > 2L) {
      stop("dry-run accepts at most one operation regex")
    }
    ops <- names(supplement_ops)
    if (length(args) == 2L) {
      ops <- grep(args[[2L]], ops, value = TRUE)
    }
    if (!length(ops)) {
      stop("no supplementary operations match the filter")
    }
    counts <- table(factor(
      vapply(supplement_ops[ops], `[[`, character(1L), "scale"),
      levels = names(supplement_scale_n)
    ))
    cat(sprintf(
      "%d operations x 7 conditions x 5 reps = %d timed workers\n",
      length(ops), length(ops) * 7L * supplement_reps
    ))
    cat(sprintf(
      "%d semantic preflight workers, including the stringi oracle\n",
      length(ops) * 8L
    ))
    for (scale in names(supplement_scale_n)) {
      cat(sprintf(
        "%-6s %2d operations at %s records\n",
        scale, counts[[scale]],
        format(supplement_scale_n[[scale]], big.mark = ",")
      ))
    }
    cat("conditions: main base/ALTREP-1; current base/ALTREP-1/2/4/8\n")
    quit(save = "no")
  }

  if (length(args) < 5L) {
    stop(
      "usage: run-parallel-supplement.R <label> <main-lib> <main-commit> ",
      "<current-lib> <current-commit> [operation-regex] [--resume]\n",
      "or: run-parallel-supplement.R --dry-run [operation-regex]"
    )
  }
  label <- args[[1L]]
  main_lib <- normalizePath(args[[2L]], mustWork = TRUE)
  main_commit <- args[[3L]]
  current_lib <- normalizePath(args[[4L]], mustWork = TRUE)
  current_commit <- args[[5L]]
  extra <- if (length(args) > 5L) args[6L:length(args)] else character()
  resume <- "--resume" %in% extra
  filters <- extra[extra != "--resume"]
  if (length(filters) > 1L) {
    stop("at most one operation regex may be supplied")
  }
  ops <- names(supplement_ops)
  if (length(filters)) {
    ops <- grep(filters[[1L]], ops, value = TRUE)
  }
  if (!length(ops)) {
    stop("no supplementary operations match the filter")
  }

  bench_dir <- dirname(run_file)
  data_dir <- file.path(bench_dir, "data")
  results_dir <- file.path(bench_dir, "results")
  prepare_file <- file.path(bench_dir, "prepare-corpus.R")
  ops_file <- file.path(bench_dir, "benchmark-ops.R")
  dir.create(results_dir, recursive = TRUE, showWarnings = FALSE)
  script_md5 <- unname(tools::md5sum(run_file))
  prepare_md5 <- unname(tools::md5sum(prepare_file))
  public_ops_md5 <- unname(tools::md5sum(ops_file))

  meta_path <- file.path(data_dir, "tatoeba-scaled-meta.rds")
  if (!file.exists(meta_path)) {
    stop("run prepare-corpus.R first")
  }
  corpus_meta <- readRDS(meta_path)
  stopifnot(
    identical(corpus_meta$format_version, 2L),
    identical(corpus_meta$seed, 20260721L),
    identical(corpus_meta$ops_md5, public_ops_md5),
    identical(corpus_meta$prepare_md5, prepare_md5),
    isTRUE(corpus_meta$nested_prefixes)
  )
  for (scale in unique(vapply(
    supplement_ops[ops], `[[`, character(1L), "scale"
  ))) {
    corpus <- corpus_meta$scales[[scale]]
    stopifnot(
      identical(corpus$n, unname(supplement_scale_n[[scale]])),
      file.exists(corpus$text),
      file.exists(corpus$charvec),
      identical(unname(tools::md5sum(corpus$text)), corpus$text_md5),
      identical(unname(tools::md5sum(corpus$charvec)), corpus$charvec_md5)
    )
  }

  rscript <- file.path(R.home("bin"), "Rscript")
  metadata <- list(
    main = supplement_parse_metadata(
      rscript, run_file, main_lib, "main"
    ),
    current = supplement_parse_metadata(
      rscript, run_file, current_lib, "current"
    )
  )
  package_libs <- list(main = main_lib, current = current_lib)
  package_commits <- list(main = main_commit, current = current_commit)

  worker_arguments <- function(
    mode, op, condition, final_argument, oracle = FALSE
  ) {
    operation <- supplement_ops[[op]]
    scale <- operation$scale
    input_n <- unname(supplement_scale_n[[scale]])
    corpus <- corpus_meta$scales[[scale]]
    if (oracle) {
      role <- "current"
      backend <- "stringi"
      input_mode <- "plain"
      nthreads <- 1L
    } else {
      role <- condition$package_role[[1L]]
      backend <- condition$backend[[1L]]
      input_mode <- condition$input_mode[[1L]]
      nthreads <- condition$nthreads[[1L]]
    }
    c(
      "--worker", mode, shQuote(package_libs[[role]]), shQuote(op),
      backend, input_mode, shQuote(corpus$text), shQuote(corpus$charvec),
      input_n, nthreads, shQuote(final_argument)
    )
  }

  preflight_output <- function(op, condition = NULL, oracle = FALSE) {
    path <- tempfile(sprintf("charr-supplement-%s-", op), fileext = ".rds")
    process <- supplement_run_process(
      rscript, run_file,
      worker_arguments("preflight", op, condition, path, oracle)
    )
    line <- grep("^CHECK\t", process$output, value = TRUE)
    if (!is.null(process$status) || length(line) != 1L || !file.exists(path)) {
      unlink(path)
      description <- if (oracle) "stringi oracle" else condition$condition[[1L]]
      stop(
        "semantic preflight failed for ", op, " / ", description, ":\n",
        paste(process$output, collapse = "\n")
      )
    }
    path
  }

  preflight_operation <- function(op) {
    oracle_path <- preflight_output(op, oracle = TRUE)
    on.exit(unlink(oracle_path), add = TRUE)
    reference <- readRDS(oracle_path)
    for (index in seq_len(nrow(supplement_conditions))) {
      condition <- supplement_conditions[index, , drop = FALSE]
      path <- preflight_output(op, condition = condition)
      actual <- readRDS(path)
      unlink(path)
      if (!identical(actual, reference)) {
        difference <- capture.output(all.equal(actual, reference))
        stop(
          "semantic output differs for ", op, " / ",
          condition$condition[[1L]], ":\n",
          paste(difference, collapse = "\n")
        )
      }
    }
    invisible(TRUE)
  }

  safe_label <- gsub("[^A-Za-z0-9_.-]", "_", label)
  times_path <- file.path(
    results_dir, paste0(safe_label, "-supplement-times.csv")
  )
  summary_path <- file.path(
    results_dir, paste0(safe_label, "-supplement-summary.csv")
  )
  if (file.exists(times_path) && !resume) {
    stop("result already exists; use a new label or pass --resume: ", times_path)
  }

  expected_input_md5 <- function(op, input_mode) {
    scale <- supplement_ops[[op]]$scale
    corpus <- corpus_meta$scales[[scale]]
    if (identical(input_mode, "charvec")) {
      corpus$charvec_md5
    } else {
      corpus$text_md5
    }
  }
  if (resume && file.exists(times_path)) {
    times <- read.csv(times_path, check.names = FALSE, stringsAsFactors = FALSE)
    expected_md5 <- mapply(
      expected_input_md5, times$op, times$input_mode,
      USE.NAMES = FALSE
    )
    expected_commit <- ifelse(
      times$package_role == "main", main_commit, current_commit
    )
    expected_path <- vapply(times$package_role, function(role) {
      unname(metadata[[role]][["package_path"]])
    }, character(1L))
    stopifnot(
      all(times$label == label),
      all(times$reps == supplement_reps),
      all(times$op %in% names(supplement_ops)),
      all(times$condition %in% supplement_conditions$condition),
      all(times$package_commit == expected_commit),
      all(times$package_path == expected_path),
      all(times$seed == corpus_meta$seed),
      all(times$input_md5 == expected_md5),
      all(times$script_md5 == script_md5),
      all(times$prepare_md5 == prepare_md5),
      all(times$public_ops_md5 == public_ops_md5),
      all(times$fixture_format_version == corpus_meta$format_version)
    )
  } else {
    times <- NULL
  }

  summarize_times <- function(data) {
    if (is.null(data) || !nrow(data)) {
      return(NULL)
    }
    groups <- split(
      data,
      interaction(data$op, data$condition, drop = TRUE, lex.order = TRUE)
    )
    complete <- groups[vapply(groups, nrow, integer(1L)) == supplement_reps]
    if (!length(complete)) {
      return(NULL)
    }
    summary <- do.call(rbind, lapply(complete, function(group) {
      first <- group[1L, c(
        "label", "op", "entrypoint", "R_wrapper", "family", "condition",
        "condition_label", "package_role", "backend", "nthreads",
        "input_n", "scale", "input_mode", "input_class", "call_shape",
        "package_commit", "package_path", "package_version", "seed",
        "corpus_md5", "input_md5", "icu_version", "icu_mode", "r_version",
        "script_md5", "prepare_md5", "public_ops_md5",
        "fixture_format_version"
      )]
      first$reps <- nrow(group)
      first$median_ms <- median(group$milliseconds)
      first$min_ms <- min(group$milliseconds)
      first$max_ms <- max(group$milliseconds)
      first
    }))
    rownames(summary) <- NULL
    value_for <- function(op, condition) {
      rows <- summary$op == op & summary$condition == condition
      if (sum(rows) == 1L) summary$median_ms[rows] else NA_real_
    }
    summary$speedup_vs_current_altrep_1 <- mapply(
      function(op, condition, backend, role, milliseconds) {
        if (!identical(backend, "altrep") || !identical(role, "current")) {
          return(NA_real_)
        }
        value_for(op, "current-altrep-1") / milliseconds
      },
      summary$op, summary$condition, summary$backend,
      summary$package_role, summary$median_ms,
      USE.NAMES = FALSE
    )
    summary$serial_speedup_vs_main <- mapply(
      function(op, condition, milliseconds) {
        if (!identical(condition, "current-altrep-1")) {
          return(NA_real_)
        }
        value_for(op, "main-altrep-1") / milliseconds
      },
      summary$op, summary$condition, summary$median_ms,
      USE.NAMES = FALSE
    )
    summary$base_ratio_vs_main <- mapply(
      function(op, condition, milliseconds) {
        if (!identical(condition, "current-base")) {
          return(NA_real_)
        }
        value_for(op, "main-base") / milliseconds
      },
      summary$op, summary$condition, summary$median_ms,
      USE.NAMES = FALSE
    )
    summary
  }

  write_progress <- function() {
    write.csv(times, times_path, row.names = FALSE)
    summary <- summarize_times(times)
    if (!is.null(summary)) {
      write.csv(summary, summary_path, row.names = FALSE)
    }
  }

  completed_key <- function(op, condition, rep) {
    paste(op, condition, rep, sep = "\r")
  }
  completed <- if (is.null(times)) character() else completed_key(
    times$op, times$condition, times$rep
  )

  measure <- function(op, condition, rep) {
    role <- condition$package_role[[1L]]
    backend <- condition$backend[[1L]]
    input_mode <- condition$input_mode[[1L]]
    nthreads <- condition$nthreads[[1L]]
    scale <- supplement_ops[[op]]$scale
    input_n <- unname(supplement_scale_n[[scale]])
    corpus <- corpus_meta$scales[[scale]]
    process <- supplement_run_process(
      rscript, run_file,
      worker_arguments("time", op, condition, rep)
    )
    line <- grep("^TIME\t", process$output, value = TRUE)
    if (!is.null(process$status) || length(line) != 1L) {
      stop(
        "timing worker failed for ", op, " / ",
        condition$condition[[1L]], " / rep ", rep, ":\n",
        paste(process$output, collapse = "\n")
      )
    }
    fields <- strsplit(line, "\t", fixed = TRUE)[[1L]]
    seconds <- as.numeric(fields[[2L]])
    actual_backend <- fields[[3L]]
    input_class <- fields[[4L]]
    actual_threads <- as.integer(fields[[5L]])
    stopifnot(
      !is.na(seconds),
      identical(actual_backend, backend),
      identical(actual_threads, nthreads)
    )
    package_metadata <- metadata[[role]]
    call_shape <- if (
      identical(op, "ci_encode_raw") && identical(backend, "base")
    ) {
      "native-noise-control"
    } else {
      "R-wrapper"
    }
    data.frame(
      label = label,
      op = op,
      entrypoint = supplement_ops[[op]]$entrypoint,
      R_wrapper = supplement_ops[[op]]$wrapper,
      family = supplement_ops[[op]]$family,
      condition = condition$condition[[1L]],
      condition_label = condition$condition_label[[1L]],
      package_role = role,
      backend = backend,
      nthreads = nthreads,
      rep = rep,
      reps = supplement_reps,
      seconds = seconds,
      milliseconds = seconds * 1000,
      input_n = input_n,
      scale = scale,
      input_mode = input_mode,
      input_class = input_class,
      call_shape = call_shape,
      package_commit = package_commits[[role]],
      package_path = unname(package_metadata[["package_path"]]),
      package_version = unname(package_metadata[["package_version"]]),
      seed = corpus_meta$seed,
      corpus_md5 = corpus$text_md5,
      input_md5 = expected_input_md5(op, input_mode),
      icu_version = unname(package_metadata[["charr_icu_version"]]),
      icu_mode = unname(package_metadata[["charr_icu_mode"]]),
      r_version = unname(package_metadata[["r_version"]]),
      script_md5 = script_md5,
      prepare_md5 = prepare_md5,
      public_ops_md5 = public_ops_md5,
      fixture_format_version = corpus_meta$format_version,
      stringsAsFactors = FALSE
    )
  }

  cat(sprintf(
    "%s: %d supplementary operations, seven conditions, five fresh reps\n",
    label, length(ops)
  ))
  cat(sprintf("main package:    %s at %s\n", main_commit, main_lib))
  cat(sprintf("current package: %s at %s\n\n", current_commit, current_lib))

  cat("checking full-input equivalence against stringi\n")
  failures <- list()
  for (op in ops) {
    agreement <- tryCatch(preflight_operation(op), error = identity)
    if (inherits(agreement, "error")) {
      failures[[op]] <- conditionMessage(agreement)
      cat(sprintf("%-28s FAILED\n", op))
    } else {
      cat(sprintf("%-28s outputs agree\n", op))
    }
  }
  if (length(failures)) {
    details <- paste(
      sprintf(
        "[%s]\n%s", names(failures), unlist(failures, use.names = FALSE)
      ),
      collapse = "\n\n"
    )
    stop(length(failures), " semantic preflight operation(s) failed:\n", details)
  }
  cat("\n")

  for (op in ops) {
    for (rep in seq_len(supplement_reps)) {
      for (index in seq_len(nrow(supplement_conditions))) {
        condition <- supplement_conditions[index, , drop = FALSE]
        key <- completed_key(op, condition$condition[[1L]], rep)
        if (key %in% completed) {
          next
        }
        row <- measure(op, condition, rep)
        times <- rbind(times, row)
        completed <- c(completed, key)
        write_progress()
        cat(sprintf(
          "%-28s %-18s rep %d  %9.3f ms  n=%s\n",
          op, condition$condition[[1L]], rep, row$milliseconds,
          format(row$input_n, big.mark = ",")
        ))
      }
    }
  }

  selected <- times[times$op %in% ops, , drop = FALSE]
  counts <- table(
    factor(selected$op, levels = ops),
    factor(selected$condition, levels = supplement_conditions$condition)
  )
  stopifnot(all(counts == supplement_reps))
  write_progress()
  cat(sprintf(
    "\nmeasured %d operations x seven conditions x five reps\n",
    length(ops)
  ))
  cat("raw times: ", times_path, "\n", sep = "")
  cat("summary:   ", summary_path, "\n", sep = "")
}

all_args <- commandArgs(trailingOnly = TRUE)
file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
run_file <- normalizePath(sub("^--file=", "", file_arg))
if (length(all_args) && identical(all_args[[1L]], "--worker")) {
  supplement_worker(all_args[-1L])
} else {
  supplement_controller(all_args, run_file)
}
