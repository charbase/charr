args <- commandArgs(trailingOnly = TRUE)
label <- if (length(args) >= 1L) args[[1L]] else stop("label is required")

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
bench_dir <- dirname(normalizePath(sub("^--file=", "", file_arg)))
results_dir <- file.path(bench_dir, "results")
source(file.path(bench_dir, "benchmark-ops.R"))

safe_label <- gsub("[^A-Za-z0-9_.-]", "_", label)
times_path <- file.path(results_dir, paste0(safe_label, "-times.csv"))
summary_path <- file.path(results_dir, paste0(safe_label, "-summary.csv"))
png_path <- file.path(results_dir, paste0(safe_label, "-backends.png"))
pdf_path <- file.path(results_dir, paste0(safe_label, "-backends.pdf"))

times <- read.csv(times_path, check.names = FALSE, stringsAsFactors = FALSE)
required <- c(
  "op", "family", "condition", "condition_label", "branch_commit", "rep",
  "reps", "milliseconds", "input_n", "scale_class", "input_class",
  "icu_owner", "icu_version", "icu_mode", "ops_md5", "worker_md5",
  "run_md5", "prepare_md5"
)
stopifnot(all(required %in% names(times)))
plot_reps <- sort(unique(times$reps))
stopifnot(length(plot_reps) >= 1L, all(!is.na(plot_reps)), all(plot_reps >= 1L))

condition_order <- c("stringi", "base", "altrep")
condition_labels <- c(
  stringi = "stringi",
  base = "Main base",
  altrep = "Main ALTREP"
)
stopifnot(
  identical(sort(unique(times$condition)), sort(condition_order)),
  all(times$op %in% names(bench_ops))
)
timing_groups <- split(
  times,
  interaction(times$op, times$condition, drop = TRUE, lex.order = TRUE)
)
stopifnot(all(vapply(timing_groups, function(group) {
  group_reps <- unique(group$reps)
  length(group_reps) == 1L && nrow(group) == group_reps
}, logical(1L))))
op_reps <- split(times$reps, times$op)
stopifnot(all(vapply(op_reps, function(values) {
  length(unique(values)) == 1L
}, logical(1L))))

groups <- split(
  times,
  interaction(times$op, times$condition, drop = TRUE, lex.order = TRUE)
)
summary <- do.call(rbind, lapply(groups, function(group) {
  first <- group[1L, c(
    "label", "op", "family", "condition", "condition_label", "branch",
    "branch_commit", "backend", "input_n", "scale_class", "input_class",
    "seed", "corpus_md5", "icu_owner", "icu_version", "icu_mode",
    "r_version", "package_path", "ops_md5", "worker_md5", "run_md5",
    "prepare_md5"
  )]
  first$reps <- nrow(group)
  first$median_ms <- median(group$milliseconds)
  first$min_ms <- min(group$milliseconds)
  first$max_ms <- max(group$milliseconds)
  first
}))
rownames(summary) <- NULL

op_order <- names(bench_ops)[names(bench_ops) %in% unique(times$op)]
family_order <- unique(vapply(bench_ops[op_order], `[[`, character(1L), "family"))
family_counts <- table(factor(
  vapply(bench_ops[op_order], `[[`, character(1L), "family"),
  levels = family_order
))
family_labels <- setNames(
  sprintf("%s (n=%d)", family_order, as.integer(family_counts)),
  family_order
)
scale_short <- c(fast = "1m", medium = "100k", slow = "10k")
op_labels <- setNames(
  sprintf(
    "%s\n(%s inputs)",
    op_order,
    unname(scale_short[bench_scale_class[op_order]])
  ),
  op_order
)
summary <- summary[order(
  match(summary$op, op_order),
  match(summary$condition, condition_order)
), , drop = FALSE]
write.csv(summary, summary_path, row.names = FALSE)

summary$op <- factor(summary$op, levels = op_order)
summary$family <- factor(summary$family, levels = family_order)
summary$condition <- factor(summary$condition, levels = condition_order)
times$op <- factor(times$op, levels = op_order)
times$family <- factor(times$family, levels = family_order)
times$condition <- factor(times$condition, levels = condition_order)

suppressPackageStartupMessages(library(ggplot2))
dodge <- position_dodge(width = 0.82)
plot <- ggplot(
  summary,
  aes(x = op, y = median_ms, fill = condition)
) +
  geom_col(
    position = dodge, width = 0.74,
    colour = "#333333", linewidth = 0.2
  ) +
  geom_errorbar(
    aes(ymin = min_ms, ymax = max_ms),
    position = dodge, width = 0.16, linewidth = 0.45
  ) +
  geom_point(
    data = times,
    aes(y = milliseconds),
    position = position_jitterdodge(
      jitter.width = 0.045, jitter.height = 0,
      dodge.width = 0.82, seed = 20260721
    ),
    shape = 21, size = 1.45, stroke = 0.3,
    colour = "#222222", show.legend = FALSE
  ) +
  facet_wrap(
    vars(family), ncol = 4, scales = "free",
    labeller = as_labeller(family_labels)
  ) +
  scale_fill_manual(
    values = c(
      stringi = "#777777",
      base = "#56B4E9",
      altrep = "#0072B2"
    ),
    breaks = condition_order,
    labels = unname(condition_labels[condition_order]),
    name = NULL
  ) +
  scale_x_discrete(labels = op_labels) +
  scale_y_continuous(
    breaks = scales::breaks_pretty(n = 6),
    expand = expansion(mult = c(0, 0.055))
  ) +
  labs(
    title = "String operation runtime across three backends",
    subtitle = paste0(
      "Raw linear scale; fast operations use 1m inputs, medium 100k, slow 10k. ",
      "Bars are medians; points show ",
      paste(plot_reps, collapse = " or "),
      " repetitions by operation; error bars span min-max."
    ),
    x = "Operation",
    y = "Elapsed time (milliseconds)",
    caption = paste0(
      "Seed 20260721. Timed conditions only: stringi/plain, ",
      "Main base/plain, Main ALTREP/charvec."
    )
  ) +
  theme_minimal(base_size = 15) +
  theme(
    panel.grid.major.x = element_blank(),
    panel.grid.minor = element_blank(),
    panel.spacing = grid::unit(1.0, "lines"),
    strip.text.x = element_text(face = "bold", size = 13),
    strip.background = element_rect(fill = "#F1F1F1", colour = "#CCCCCC"),
    axis.text.x = element_text(angle = 55, hjust = 1, vjust = 1, size = 10),
    axis.text.y = element_text(size = 11),
    axis.title = element_text(size = 14),
    legend.position = "top",
    legend.text = element_text(size = 13),
    plot.title = element_text(face = "bold", size = 21),
    plot.subtitle = element_text(size = 14),
    plot.caption = element_text(size = 11, hjust = 0)
  )

ggsave(png_path, plot, width = 28, height = 18, units = "in", dpi = 180)
ggsave(pdf_path, plot, width = 28, height = 18, units = "in")

cat("summary: ", summary_path, "\n", sep = "")
cat("PNG:     ", png_path, "\n", sep = "")
cat("PDF:     ", pdf_path, "\n", sep = "")
