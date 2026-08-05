args <- commandArgs(trailingOnly = TRUE)
label <- if (length(args) >= 1L) args[[1L]] else stop("label is required")

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
bench_dir <- dirname(normalizePath(sub("^--file=", "", file_arg)))
results_dir <- file.path(bench_dir, "results")
safe_label <- gsub("[^A-Za-z0-9_.-]", "_", label)
times_path <- file.path(results_dir, paste0(safe_label, "-times.csv"))
targets_path <- file.path(results_dir, paste0(safe_label, "-targets.csv"))

times <- read.csv(times_path, check.names = FALSE, stringsAsFactors = FALSE)
stopifnot(all(c("op", "condition", "rep", "milliseconds") %in% names(times)))

compare_pair <- function(candidate, reference, prefix) {
  candidate_rows <- times[times$condition == candidate, c(
    "op", "rep", "milliseconds"
  )]
  reference_rows <- times[times$condition == reference, c(
    "op", "rep", "milliseconds"
  )]
  names(candidate_rows)[[3L]] <- "candidate_ms"
  names(reference_rows)[[3L]] <- "reference_ms"
  paired <- merge(candidate_rows, reference_rows, by = c("op", "rep"))
  groups <- split(paired, paired$op)

  result <- do.call(rbind, lapply(groups, function(group) {
    speedup <- group$reference_ms / group$candidate_ms
    wins <- sum(group$candidate_ms < group$reference_ms)
    losses <- sum(group$candidate_ms > group$reference_ms)
    ties <- nrow(group) - wins - losses
    untied <- wins + losses
    sign_p <- if (untied) {
      stats::pbinom(wins - 1L, untied, 0.5, lower.tail = FALSE)
    } else {
      1
    }
    data.frame(
      op = group$op[[1L]],
      reps = nrow(group),
      candidate_median_ms = median(group$candidate_ms),
      reference_median_ms = median(group$reference_ms),
      speedup_median = median(speedup),
      wins = wins,
      losses = losses,
      ties = ties,
      sign_p = sign_p,
      pass = median(speedup) > 1 && sign_p < 0.05,
      stringsAsFactors = FALSE
    )
  }))
  names(result)[-1L] <- paste0(prefix, "_", names(result)[-1L])
  result
}

# Both optimized backends are measured against installed stringi, so ALTREP's
# target is the same public baseline the base backend has to beat.
base <- compare_pair("base", "stringi", "base")
altrep <- compare_pair("altrep1", "stringi", "altrep")
targets <- merge(base, altrep, by = "op", all = TRUE)
targets$complete <- targets$base_pass & targets$altrep_pass
targets <- targets[order(targets$complete, targets$op), , drop = FALSE]
write.csv(targets, targets_path, row.names = FALSE)

print(targets, row.names = FALSE, digits = 4)
cat("\nExact one-sided sign tests exclude ties; pass also requires median speedup > 1.\n")
cat("Both candidate backends are compared against the stringi condition.\n")
cat("targets: ", targets_path, "\n", sep = "")
