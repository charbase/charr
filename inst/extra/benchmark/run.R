# Three plotted conditions only: current stringi (plain), current base
# (plain), and current ALTREP (charvec). The frozen Claude ALTREP snapshot was
# retired as a comparison once the optimized backends passed it everywhere; its
# last recorded numbers stay in the archived result files.

args <- commandArgs(trailingOnly = TRUE)
reps_arg <- grep("^--reps=", args, value = TRUE)
if (length(reps_arg) > 1L) {
  stop("at most one --reps=N option may be supplied")
}
reps <- if (length(reps_arg)) {
  as.integer(sub("^--reps=", "", reps_arg[[1L]]))
} else {
  3L
}
if (is.na(reps) || reps < 1L) {
  stop("--reps must be a positive integer")
}
args <- args[!grepl("^--reps=", args)]

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
run_file <- normalizePath(sub("^--file=", "", file_arg))
bench_dir <- dirname(run_file)
ops_file <- file.path(bench_dir, "benchmark-ops.R")
worker <- file.path(bench_dir, "benchmark-worker.R")
prepare_file <- file.path(bench_dir, "prepare-corpus.R")
results_dir <- file.path(bench_dir, "results")
data_dir <- file.path(bench_dir, "data")
dir.create(results_dir, recursive = TRUE, showWarnings = FALSE)
source(ops_file)
harness_md5 <- c(
  ops = unname(tools::md5sum(ops_file)),
  worker = unname(tools::md5sum(worker)),
  run = unname(tools::md5sum(run_file)),
  prepare = unname(tools::md5sum(prepare_file))
)

filter_ops <- function(pattern = NULL) {
  ops <- names(bench_ops)
  if (!is.null(pattern)) {
    ops <- grep(pattern, ops, value = TRUE)
  }
  if (!length(ops)) {
    stop("no operations match the filter")
  }
  ops
}

if (length(args) >= 1L && identical(args[[1L]], "--dry-run")) {
  op_filter <- if (length(args) >= 2L) args[[2L]] else NULL
  ops <- filter_ops(op_filter)
  counts <- table(factor(
    bench_scale_class[ops], levels = names(bench_scale_n)
  ))
  cat(sprintf(
    "%d operations x 3 conditions x %d reps = %d timed workers\n",
    length(ops), reps, length(ops) * 3L * reps
  ))
  for (scale in names(bench_scale_n)) {
    cat(sprintf(
      "%-6s %3d operations at %s records\n",
      scale, counts[[scale]], format(bench_scale_n[[scale]], big.mark = ",")
    ))
  }
  cat("conditions: stringi, base, altrep\n")
  quit(save = "no")
}

if (length(args) < 3L) {
  stop(
    "usage: run.R <label> <main-lib> <main-commit> ",
    "[operation-regex] [--resume] [--reps=N]\n",
    "or: run.R --dry-run [operation-regex] [--reps=N]"
  )
}

label <- args[[1L]]
main_lib <- normalizePath(args[[2L]], mustWork = TRUE)
main_commit <- args[[3L]]
extra <- if (length(args) > 3L) args[4L:length(args)] else character()
resume <- "--resume" %in% extra
filters <- extra[extra != "--resume"]
if (length(filters) > 1L) {
  stop("at most one operation regex may be supplied")
}
op_filter <- if (length(filters)) filters[[1L]] else NULL
ops <- filter_ops(op_filter)

meta_path <- file.path(data_dir, "tatoeba-scaled-meta.rds")
if (!file.exists(meta_path)) {
  stop("run prepare-corpus.R first")
}
corpus_meta <- readRDS(meta_path)
stopifnot(
  identical(corpus_meta$format_version, 2L),
  identical(corpus_meta$seed, 20260721L),
  identical(corpus_meta$scale_n, bench_scale_n),
  identical(corpus_meta$ops_md5, unname(harness_md5[["ops"]])),
  identical(corpus_meta$prepare_md5, unname(harness_md5[["prepare"]])),
  isTRUE(corpus_meta$nested_prefixes)
)
for (scale in names(bench_scale_n)) {
  item <- corpus_meta$scales[[scale]]
  artifact_fields <- c(
    "text", "language", "sentence_id", "charvec",
    "prepared_plain", "prepared_charvec"
  )
  stopifnot(
    identical(item$n, unname(bench_scale_n[[scale]])),
    all(vapply(artifact_fields, function(field) {
      path <- item[[field]]
      expected <- item[[paste0(field, "_md5")]]
      file.exists(path) && identical(unname(tools::md5sum(path)), expected)
    }, logical(1L)))
  )
}

conditions <- data.frame(
  condition = c("stringi", "base", "altrep"),
  condition_label = c("stringi", "Main base", "Main ALTREP"),
  branch = c("main", "main", "main"),
  backend = c("stringi", "base", "altrep"),
  input_mode = c("plain", "plain", "charvec"),
  stringsAsFactors = FALSE
)

rscript <- file.path(R.home("bin"), "Rscript")
run_process <- function(arguments) {
  output <- suppressWarnings(system2(
    rscript,
    c("--vanilla", arguments),
    stdout = TRUE,
    stderr = TRUE
  ))
  list(output = output, status = attr(output, "status"))
}

package_metadata <- function(package_lib, branch_kind) {
  process <- run_process(c(
    shQuote(worker), "metadata", shQuote(package_lib), branch_kind
  ))
  lines <- grep("^META\\t", process$output, value = TRUE)
  if (!is.null(process$status) || !length(lines)) {
    stop(
      "metadata worker failed for ", branch_kind, ":\n",
      paste(process$output, collapse = "\n")
    )
  }
  fields <- strsplit(lines, "\t", fixed = TRUE)
  values <- vapply(fields, function(field) field[[3L]], character(1L))
  names(values) <- vapply(fields, function(field) field[[2L]], character(1L))
  values
}

metadata <- list(
  main = package_metadata(main_lib, "main")
)

prepared_path <- function(corpus, input_mode) {
  corpus[[paste0("prepared_", input_mode)]]
}

preflight_worker <- function(op, condition_row, output_path) {
  condition <- condition_row$condition[[1L]]
  branch <- condition_row$branch[[1L]]
  input_mode <- condition_row$input_mode[[1L]]
  scale_class <- unname(bench_scale_class[[op]])
  input_n <- unname(bench_scale_n[[scale_class]])
  corpus <- corpus_meta$scales[[scale_class]]
  package_lib <- main_lib

  process <- run_process(c(
    shQuote(worker),
    "preflight",
    shQuote(package_lib),
    branch,
    shQuote(ops_file),
    shQuote(op),
    condition,
    shQuote(corpus$text),
    shQuote(corpus$charvec),
    shQuote(prepared_path(corpus, input_mode)),
    input_n,
    shQuote(output_path)
  ))
  line <- grep("^CHECK\\t", process$output, value = TRUE)
  if (!is.null(process$status) || length(line) != 1L || !file.exists(output_path)) {
    stop(
      "semantic preflight failed for ", op, " / ", condition, ":\n",
      paste(process$output, collapse = "\n")
    )
  }
}

preflight_operation <- function(op) {
  paths <- setNames(
    lapply(conditions$condition, function(condition) {
      tempfile(sprintf("charr-%s-%s-", op, condition), fileext = ".rds")
    }),
    conditions$condition
  )
  on.exit(unlink(unlist(paths, use.names = FALSE)), add = TRUE)

  outputs <- setNames(vector("list", nrow(conditions)), conditions$condition)
  for (index in seq_len(nrow(conditions))) {
    condition <- conditions$condition[[index]]
    preflight_worker(op, conditions[index, , drop = FALSE], paths[[condition]])
    outputs[[condition]] <- readRDS(paths[[condition]])
  }

  stringi_reference <- outputs[["stringi"]]
  exact <- vapply(outputs, identical, logical(1L), stringi_reference)
  if (all(exact)) {
    return(invisible("exact"))
  }

  # Default collation order and Unicode title-casing data can change between
  # ICU releases. When stringi and charr use different releases, require the
  # two backends that share charr's ICU to agree instead of treating a
  # data-table update as a backend error. Every other operation must still
  # agree across all three conditions.
  version_sensitive <- op %in% c(
    "ci_order", "ci_rank", "ci_trans_totitle",
    "ci_count_boundaries", "ci_locate_first_boundaries",
    "ci_locate_all_boundaries", "ci_extract_first_boundaries",
    "ci_extract_all_boundaries", "ci_split_boundaries"
  )
  different_icu <- !identical(
    unname(metadata$main[["charr_icu_version"]]),
    unname(metadata$main[["stringi_icu_version"]])
  )
  cohort_agreement <- identical(outputs[["base"]], outputs[["altrep"]])
  if (version_sensitive && different_icu && cohort_agreement) {
    return(invisible("ICU cohorts"))
  }

  failed <- names(exact)[!exact]
  difference <- capture.output(
    all.equal(outputs[[failed[[1L]]]], stringi_reference)
  )
  stop(
    "semantic preflight output differs for ", op, " / ",
    paste(failed, collapse = ", "), ":\n",
    paste(difference, collapse = "\n")
  )
}

safe_label <- gsub("[^A-Za-z0-9_.-]", "_", label)
times_path <- file.path(results_dir, paste0(safe_label, "-times.csv"))
summary_path <- file.path(results_dir, paste0(safe_label, "-summary.csv"))
if (file.exists(times_path) && !resume) {
  stop("result already exists; use a new label or pass --resume: ", times_path)
}

if (resume && file.exists(times_path)) {
  times <- read.csv(times_path, check.names = FALSE, stringsAsFactors = FALSE)
  expected_fixture_md5 <- mapply(function(op, condition) {
    scale <- unname(bench_scale_class[[op]])
    input_mode <- conditions$input_mode[match(condition, conditions$condition)]
    path <- prepared_path(corpus_meta$scales[[scale]], input_mode)
    unname(tools::md5sum(path))
  }, times$op, times$condition, USE.NAMES = FALSE)
  stopifnot(
    all(times$label == label),
    all(times$reps == reps),
    all(times$seed == corpus_meta$seed),
    all(times$branch_commit[times$branch == "main"] == main_commit),
    all(times$ops_md5 == unname(harness_md5[["ops"]])),
    all(times$worker_md5 == unname(harness_md5[["worker"]])),
    all(times$run_md5 == unname(harness_md5[["run"]])),
    all(times$prepare_md5 == corpus_meta$prepare_md5),
    all(times$fixture_format_version == corpus_meta$format_version),
    all(times$fixture_md5 == expected_fixture_md5)
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
  complete <- groups[vapply(groups, nrow, integer(1L)) == reps]
  if (!length(complete)) {
    return(NULL)
  }
  do.call(rbind, lapply(complete, function(group) {
    first <- group[1L, c(
      "label", "op", "family", "condition", "condition_label",
      "branch", "branch_commit", "backend", "input_n", "scale_class",
      "input_class", "seed", "corpus_md5", "icu_owner", "icu_version",
      "icu_mode", "r_version", "package_path", "ops_md5", "worker_md5",
      "run_md5", "prepare_md5", "fixture_format_version", "fixture_md5"
    )]
    first$reps <- nrow(group)
    first$median_ms <- median(group$milliseconds)
    first$min_ms <- min(group$milliseconds)
    first$max_ms <- max(group$milliseconds)
    first
  }))
}

write_progress <- function() {
  write.csv(times, times_path, row.names = FALSE)
  summary <- summarize_times(times)
  if (!is.null(summary)) {
    write.csv(summary, summary_path, row.names = FALSE)
  }
}

completed_key <- function(op, condition, rep) paste(op, condition, rep, sep = "\r")
completed <- if (is.null(times)) character() else completed_key(
  times$op, times$condition, times$rep
)

measure <- function(op, condition_row, rep) {
  condition <- condition_row$condition[[1L]]
  branch <- condition_row$branch[[1L]]
  scale_class <- unname(bench_scale_class[[op]])
  input_n <- unname(bench_scale_n[[scale_class]])
  corpus <- corpus_meta$scales[[scale_class]]
  package_lib <- main_lib
  fixture <- prepared_path(corpus, condition_row$input_mode[[1L]])

  process <- run_process(c(
    shQuote(worker),
    "time",
    shQuote(package_lib),
    branch,
    shQuote(ops_file),
    shQuote(op),
    condition,
    shQuote(corpus$text),
    shQuote(corpus$charvec),
    shQuote(fixture),
    input_n,
    rep
  ))
  line <- grep("^TIME\\t", process$output, value = TRUE)
  if (!is.null(process$status) || length(line) != 1L) {
    stop(
      "timing worker failed for ", op, " / ", condition,
      " / rep ", rep, ":\n", paste(process$output, collapse = "\n")
    )
  }
  fields <- strsplit(line, "\t", fixed = TRUE)[[1L]]
  seconds <- as.numeric(fields[[2L]])
  input_class <- fields[[3L]]
  if (is.na(seconds)) {
    stop("worker returned a non-numeric time")
  }

  package_meta <- metadata[[branch]]
  use_stringi_icu <- identical(condition, "stringi")
  icu_prefix <- if (use_stringi_icu) "stringi" else "charr"
  branch_commit <- main_commit

  data.frame(
    label = label,
    op = op,
    family = bench_ops[[op]]$family,
    condition = condition,
    condition_label = condition_row$condition_label[[1L]],
    branch = branch,
    branch_commit = branch_commit,
    backend = condition_row$backend[[1L]],
    rep = rep,
    reps = reps,
    seconds = seconds,
    milliseconds = seconds * 1000,
    input_n = input_n,
    scale_class = scale_class,
    input_class = input_class,
    seed = corpus_meta$seed,
    corpus_md5 = corpus$text_md5,
    icu_owner = icu_prefix,
    icu_version = unname(package_meta[[paste0(icu_prefix, "_icu_version")]]),
    icu_mode = unname(package_meta[[paste0(icu_prefix, "_icu_mode")]]),
    r_version = unname(package_meta[["r_version"]]),
    package_path = unname(package_meta[["package_path"]]),
    ops_md5 = unname(harness_md5[["ops"]]),
    worker_md5 = unname(harness_md5[["worker"]]),
    run_md5 = unname(harness_md5[["run"]]),
    prepare_md5 = corpus_meta$prepare_md5,
    fixture_format_version = corpus_meta$format_version,
    fixture_md5 = unname(tools::md5sum(fixture)),
    stringsAsFactors = FALSE
  )
}

cat(sprintf(
  "%s: %d operations, three plotted conditions, %d fresh-process reps\n",
  label, length(ops), reps
))
cat(sprintf("main %s: %s\n\n", main_commit, metadata$main[["package_path"]]))

cat("checking full-input semantic equivalence before timing\n")
preflight_failures <- list()
for (op in ops) {
  agreement <- tryCatch(preflight_operation(op), error = identity)
  if (inherits(agreement, "error")) {
    preflight_failures[[op]] <- conditionMessage(agreement)
    cat(sprintf("%-28s FAILED\n", op))
  } else {
    cat(sprintf("%-28s outputs agree (%s)\n", op, agreement))
  }
}
if (length(preflight_failures)) {
  details <- paste(
    sprintf(
      "[%s]\n%s", names(preflight_failures),
      unlist(preflight_failures, use.names = FALSE)
    ),
    collapse = "\n\n"
  )
  stop(
    length(preflight_failures), " semantic preflight operation(s) failed:\n",
    details
  )
}
cat("\n")

for (op in ops) {
  scale_class <- unname(bench_scale_class[[op]])
  for (rep in seq_len(reps)) {
    for (condition_index in seq_len(nrow(conditions))) {
      condition_row <- conditions[condition_index, , drop = FALSE]
      key <- completed_key(op, condition_row$condition, rep)
      if (key %in% completed) {
        next
      }
      row <- measure(op, condition_row, rep)
      times <- rbind(times, row)
      completed <- c(completed, key)
      write_progress()
      cat(sprintf(
        "%-28s %-14s rep %d  %9.3f ms  n=%s (%s)\n",
        op, condition_row$condition, rep, row$milliseconds,
        format(row$input_n, big.mark = ","), scale_class
      ))
    }
  }
}

selected <- times[times$op %in% ops, , drop = FALSE]
counts <- table(selected$op, selected$condition)
stopifnot(
  identical(sort(unique(selected$condition)), sort(conditions$condition)),
  all(counts == reps)
)
write_progress()

cat(sprintf(
  "\nmeasured %d operations x three conditions x %d reps\n",
  length(ops), reps
))
cat("raw times: ", times_path, "\n", sep = "")
cat("summary:   ", summary_path, "\n", sep = "")
cat(sprintf(
  "plot with: Rscript %s %s\n",
  shQuote(file.path(bench_dir, "plot-relative-performance.R")), shQuote(label)
))
