#!/usr/bin/env Rscript

# Vignette figures for charr, drawn in the same style as charport's benchmark
# figure: base graphics on a transparent field, white rounded cards, and the
# purple ramp from the package logo. Transparency lets one PNG sit on both the
# light and dark documentation themes.
#
# This script only reads a recorded benchmark label. It never runs the
# benchmark. Regenerate measurements with run.R first, then:
#
#   Rscript tools/benchmark/make-vignette-figures.R <label>
#
# `make figures` passes BENCH_LABEL. All four plotted conditions come from that
# one run, so no two rows in a figure can come from different measurements.
#
# It writes exactly three files: the two tracked figures under man/figures, and
# one summary TSV beside the measurements it read.

args <- commandArgs(trailingOnly = TRUE)
if (length(args) != 1L) {
  stop(
    "usage: make-vignette-figures.R <label>\n",
    "  the label names a local run in results/, without its CSV suffix",
    call. = FALSE
  )
}
label <- args[[1L]]

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
bench_dir <- dirname(normalizePath(sub("^--file=", "", file_arg)))
pkg_root <- normalizePath(file.path(bench_dir, "..", ".."))
results_dir <- file.path(bench_dir, "results")
figures_dir <- file.path(pkg_root, "man", "figures")
stopifnot(dir.exists(figures_dir))

summary_path <- file.path(results_dir, paste0(label, "-summary.csv"))
if (!file.exists(summary_path)) {
  stop("no summary for label ", label, ": ", summary_path)
}

# Measurements ---------------------------------------------------------

records <- read.csv(summary_path, stringsAsFactors = FALSE)
stopifnot(
  all(c("op", "family", "condition", "median_ms", "input_n", "reps")
      %in% names(records)),
  setequal(
    unique(records$condition), c("stringi", "base", "altrep1", "altrep4")
  ),
  !anyDuplicated(records[c("op", "condition")])
)

widen <- function(data) {
  keys <- unique(data[c("op", "family", "scale_class", "input_n")])
  pick <- function(condition) {
    rows <- data[data$condition == condition, c("op", "median_ms", "input_n")]
    stopifnot(identical(rows$input_n[match(keys$op, rows$op)], keys$input_n))
    rows$median_ms[match(keys$op, rows$op)]
  }
  keys$stringi <- pick("stringi")
  keys$base <- pick("base")
  keys$altrep_1 <- pick("altrep1")
  keys$altrep_4 <- pick("altrep4")
  # An operation with no data-parallel shape runs on one thread whatever the
  # count is, so its four-thread row is the one-thread row measured again.
  # Drawing that as a bar would claim threading was tried and gained nothing.
  keys$altrep_4[keys$op %in% serial_ops] <- NA_real_
  keys$base_x <- keys$stringi / keys$base
  keys$altrep_1_x <- keys$stringi / keys$altrep_1
  keys$altrep_4_x <- keys$stringi / keys$altrep_4
  keys
}

# Operations the ALTREP backend deliberately does not split. Each fails the
# data-parallel test in `CLAUDE.md`: a reduction, a whole-vector algorithm, or
# an output whose cardinality is not known before the work runs.
#
#   ci_flatten     reduces the whole vector to one string
#   ci_order       whole-vector sort
#   ci_rank        whole-vector sort
#   ci_duplicated  whole-vector uniqueness
#   ci_read_lines  reads a file; the record count is a result, not an input
#
# Verified against the native sources by walking the call graph from each
# entry point: these five are the only ones from which no `parallel_plan` is
# reachable. Recheck when an operation gains or loses a `ParallelBody`.
serial_ops <- c(
  "ci_flatten", "ci_order", "ci_rank", "ci_duplicated", "ci_read_lines"
)

timings <- widen(records)
reps <- unique(records$reps)
stopifnot(all(serial_ops %in% timings$op), length(reps) == 1L)

# Operation labels -----------------------------------------------------

# Each benchmark row times the backend leaf that a stringr call reaches, so
# rows are named for the stringr function and the pattern engine it used.
op_labels <- c(
  ci_detect_fixed = "str_detect (fixed)",
  ci_startswith_fixed = "str_starts (fixed)",
  ci_endswith_fixed = "str_ends (fixed)",
  ci_count_fixed = "str_count (fixed)",
  ci_locate_first_fixed = "str_locate (fixed)",
  ci_locate_all_fixed = "str_locate_all (fixed)",
  ci_extract_first_fixed = "str_extract (fixed)",
  ci_extract_all_fixed = "str_extract_all (fixed)",
  ci_replace_first_fixed = "str_replace (fixed)",
  ci_replace_all_fixed = "str_replace_all (fixed)",
  ci_split_fixed = "str_split (fixed)",
  ci_detect_regex = "str_detect (regex)",
  ci_count_regex = "str_count (regex)",
  ci_locate_first_regex = "str_locate (regex)",
  ci_locate_all_regex = "str_locate_all (regex)",
  ci_extract_first_regex = "str_extract (regex)",
  ci_extract_all_regex = "str_extract_all (regex)",
  ci_replace_first_regex = "str_replace (regex)",
  ci_replace_all_regex = "str_replace_all (regex)",
  ci_split_regex = "str_split (regex)",
  ci_match_first_regex = "str_match (regex)",
  ci_match_all_regex = "str_match_all (regex)",
  ci_detect_coll = "str_detect (coll)",
  ci_startswith_coll = "str_starts (coll)",
  ci_endswith_coll = "str_ends (coll)",
  ci_count_coll = "str_count (coll)",
  ci_locate_first_coll = "str_locate (coll)",
  ci_locate_all_coll = "str_locate_all (coll)",
  ci_extract_first_coll = "str_extract (coll)",
  ci_extract_all_coll = "str_extract_all (coll)",
  ci_replace_first_coll = "str_replace (coll)",
  ci_replace_all_coll = "str_replace_all (coll)",
  ci_split_coll = "str_split (coll)",
  ci_count_boundaries = "str_count (boundary)",
  ci_locate_first_boundaries = "str_locate (boundary)",
  ci_locate_all_boundaries = "str_locate_all (boundary)",
  ci_extract_first_boundaries = "str_extract (boundary)",
  ci_extract_all_boundaries = "str_extract_all (boundary)",
  ci_split_boundaries = "str_split (boundary)",
  ci_sub = "str_sub",
  `ci_sub<-` = "str_sub<-",
  ci_sub_all = "str_sub_all",
  `ci_sub_all<-` = "str_sub_all<-",
  ci_c = "str_c",
  ci_flatten = "str_flatten",
  ci_dup = "str_dup",
  ci_reverse = "str_reverse",
  ci_replace_na = "str_replace_na",
  ci_length = "str_length",
  ci_trim_left = "str_trim (left)",
  ci_trim_right = "str_trim (right)",
  ci_trim_both = "str_trim (both)",
  ci_trans_tolower = "str_to_lower",
  ci_trans_toupper = "str_to_upper",
  ci_trans_totitle = "str_to_title",
  ci_order = "str_order",
  ci_rank = "str_rank",
  ci_cmp_equiv = "str_equal",
  ci_duplicated = "str_unique",
  ci_wrap = "str_wrap",
  ci_pad_left = "str_pad (left)",
  ci_pad_right = "str_pad (right)",
  ci_pad_both = "str_pad (both)",
  ci_width = "str_width",
  ci_escape_unicode = "str_view (escape)",
  ci_conv = "str_conv",
  ci_read_lines = "str_read_lines"
)
missing_labels <- setdiff(timings$op, names(op_labels))
if (length(missing_labels)) {
  stop("unlabelled operations: ", paste(missing_labels, collapse = ", "))
}
timings$label <- unname(op_labels[timings$op])

family_titles <- c(
  `encoding-files` = "Encoding and files",
  core = "Combine and duplicate",
  substring = "Substring",
  other = "Trim and measure",
  layout = "Padding and layout",
  `case-map` = "Case mapping",
  fixed = "Fixed pattern",
  regex = "Regular expression",
  boundary = "Text boundary",
  collation = "Collation",
  ordering = "Order and compare"
)
stopifnot(setequal(unique(timings$family), names(family_titles)))

# Palette --------------------------------------------------------------

col_card <- "#ffffff"
col_border <- "#b9a9df"
# The three plotted series are one ordered scale -- base strings, then ALTREP,
# then ALTREP with threads -- so they get one ramp that deepens along it rather
# than a contrasting hue for the newest series. A separate hue would read as a
# different kind of measurement, and the four-thread bar is the longest bar in
# most rows already.
col_base <- "#c3b3e3"
col_altrep <- "#8265bd"
col_altrep_4 <- "#432b7d"
col_ink <- "#2c1f57"
col_axis <- "#8670bf"
col_rule <- "#8f7cc4"

roundrect <- function(x0, y0, x1, y1, rx, ry, ...) {
  a <- seq(0, pi / 2, length.out = 14)
  xs <- c(x1 - rx + rx * cos(a),      x0 + rx + rx * cos(a + pi / 2),
          x0 + rx + rx * cos(a + pi), x1 - rx + rx * cos(a + 3 * pi / 2))
  ys <- c(y1 - ry + ry * sin(a),      y1 - ry + ry * sin(a + pi / 2),
          y0 + ry + ry * sin(a + pi), y0 + ry + ry * sin(a + 3 * pi / 2))
  polygon(xs, ys, ...)
}

# Every card is drawn in one pass over the whole device, because a second
# plot.new() would start a fresh page and erase the cards already placed. The
# panels are then overlaid with par(new = TRUE).
card_layer <- function(rects, ry = 0.014) {
  par(fig = c(0, 1, 0, 1), mar = c(0, 0, 0, 0), new = FALSE)
  plot.new()
  plot.window(c(0, 1), c(0, 1), xaxs = "i", yaxs = "i")
  for (r in rects) {
    roundrect(r[[1L]], r[[2L]], r[[3L]], r[[4L]], 0.011, ry,
              col = col_card, border = col_border, lwd = 2.4)
  }
}

format_ms <- function(ms) {
  ifelse(ms >= 1000, sprintf("%.2f s", ms / 1000), sprintf("%d ms", round(ms)))
}

# Speedup bars ---------------------------------------------------------

# Bar length is how many times faster charr is than the stringi reference on
# the same input; the reference time rides in the row label so the figure keeps
# its absolute scale.
serial_note <- "operations with no data-parallel shape have no four-thread bar"

# The reference time rides in the row label so the figure keeps its absolute
# scale. "stacked" puts it on its own line, which the summary can afford;
# "inline" keeps the row one line high, which the full figure's panels need.
row_labels <- function(rows, baseline) {
  switch(baseline,
    stacked = sprintf("%s\n(%s)", rows$label, format_ms(rows$stringi)),
    inline = sprintf("%s (%s)", rows$label, format_ms(rows$stringi)),
    none = rows$label
  )
}

draw_speedup <- function(rows, title, cex_label = 1.0,
                         headroom = 0.10, show_values = TRUE,
                         baseline = "stacked", axis_title = TRUE,
                         note = FALSE, space = c(0, 0.85)) {
  rows <- rows[rev(seq_len(nrow(rows))), , drop = FALSE]
  # A horizontal barplot draws the first row of the matrix at the foot of each
  # group, so the rows run backwards here to put base at the top of a group and
  # the four-thread bar last.
  values <- rbind(
    altrep_4 = rows$altrep_4_x,
    altrep_1 = rows$altrep_1_x,
    base = rows$base_x
  )
  cols <- c(col_altrep_4, col_altrep, col_base)
  # A serial operation contributes no four-thread bar, so its slot is NA and
  # stays empty at the right width rather than closing up the group.
  top <- max(values, na.rm = TRUE) * (1 + headroom)
  bp <- barplot(values, beside = TRUE, horiz = TRUE, col = cols, border = NA,
                names.arg = rep("", nrow(rows)), xlim = c(0, top),
                xlab = "", main = "", axes = FALSE, space = space)
  axis(1, col = col_axis, col.axis = col_axis, cex.axis = cex_label * 0.98,
       lwd = 1.4)
  if (axis_title) {
    mtext("times faster than reference", side = 1, line = 2.2,
          col = col_ink, cex = cex_label * 0.98)
    mtext(sprintf("dashed line is the reference; median of %d runs", reps),
          side = 1, line = 3.2, col = col_axis, cex = cex_label * 0.82)
    if (note) {
      mtext(serial_note, side = 1, line = 4.2, col = col_axis,
            cex = cex_label * 0.82)
    }
  }
  if (nzchar(title)) {
    mtext(title, side = 3, line = 0.5, col = col_ink, cex = cex_label * 1.2,
          font = 2)
  }
  # The reference line crosses every bar, and the deepest bar is close enough
  # to the rule colour to swallow it. A white casing under the same dashes
  # keeps it readable there and disappears against the card everywhere else.
  abline(v = 1, col = col_card, lty = 2, lwd = 3.0)
  abline(v = 1, col = col_rule, lty = 2, lwd = 1.7)

  text(x = -0.012 * top, y = colMeans(bp),
       labels = row_labels(rows, baseline), adj = 1,
       xpd = NA, col = col_ink, cex = cex_label)
  if (show_values) {
    text(x = as.vector(values), y = as.vector(bp),
         labels = sprintf("%.1f×", as.vector(values)), pos = 4,
         offset = 0.28, xpd = NA, col = col_ink, cex = cex_label * 0.92,
         font = 2)
  }
  invisible(bp)
}

# Entries run in the order the bars stack within a group, top to bottom, so
# the key can be read straight off any group in the panel.
legend_bar <- function(cex = 1.0, where = "bottomright") {
  legend(where,
         legend = c(
           "charr base strings",
           "charr ALTREP, 1 thread",
           "charr ALTREP, 4 threads"
         ),
         fill = c(col_base, col_altrep, col_altrep_4), border = NA, bty = "n",
         text.col = col_ink, cex = cex, y.intersp = 1.2, inset = c(0.01, 0.03))
}

# Summary figure -------------------------------------------------------

# A cross-section rather than a highlight reel: one operation per family, and
# a second for the two largest. Every family appears, including `ordering`,
# where charr gains least -- a figure captioned "representative" has to carry
# the weak family too.
#
# str_c and str_dup are deliberately absent. Both are 20x and above, but only
# because ALTREP answers them with an unmaterialized charvec, so all three
# core rows were reporting one result three times while their bars flattened
# everything else. The full figure still carries them, where an outlier
# distorts one panel instead of the whole chart.
#
# str_read_lines leads: reading a file straight into an ALTREP vector is the
# clearest case for keeping strings out of R's global cache. The rest follow
# by four-thread speedup, so the chart descends whatever a rerun measures.
summary_ops <- c(
  "ci_read_lines",
  "ci_pad_left",
  "ci_trim_left",
  "ci_sub",
  "ci_width",
  "ci_count_fixed",
  "ci_trans_tolower",
  "ci_extract_first_regex",
  "ci_reverse",
  "ci_split_fixed",
  "ci_split_boundaries",
  "ci_detect_coll",
  "ci_cmp_equiv"
)
summary_rows <- timings[match(summary_ops, timings$op), , drop = FALSE]
stopifnot(!anyNA(summary_rows$op))
lead <- summary_rows$op == "ci_read_lines"
summary_rows <- rbind(
  summary_rows[lead, , drop = FALSE],
  summary_rows[!lead, , drop = FALSE][
    order(-summary_rows$altrep_4_x[!lead]), , drop = FALSE
  ]
)

# Three bars a row and a printed multiple beside each one, so the row pitch is
# set by the height of the value text rather than by the bar: at 1200 px the
# labels of two near-equal bars collided.
summary_png <- function(path) {
  png(path, width = 1700, height = 1480, res = 180, bg = "transparent")
  on.exit(dev.off(), add = TRUE)
  card_layer(list(c(0.012, 0.012, 0.988, 0.988)), ry = 0.011)
  par(las = 1, mgp = c(2.4, 0.7, 0))
  par(fig = c(0.028, 0.982, 0.02, 0.98), mar = c(5.4, 12.6, 1.0, 2.6),
      new = TRUE)
  draw_speedup(summary_rows, "", cex_label = 0.92, headroom = 0.13,
               baseline = "stacked",
               note = anyNA(summary_rows$altrep_4_x), space = c(0.1, 0.9))
  legend_bar(cex = 0.95)
}

summary_path_out <- file.path(figures_dir, "bench-summary.png")
summary_png(summary_path_out)
cat(sprintf("summary figure written to %s\n", summary_path_out))

# Full figure ----------------------------------------------------------

# One card per operation family, laid out in two columns with heights
# proportional to the number of rows so every bar keeps the same thickness.
full_png <- function(path) {
  families <- names(family_titles)
  groups <- lapply(families, function(fam) {
    rows <- timings[timings$family == fam, , drop = FALSE]
    # Rank on the best bar an operation actually has, so a serial one sorts on
    # its ALTREP result instead of falling to the bottom of its family.
    rank_x <- ifelse(is.na(rows$altrep_4_x), rows$altrep_1_x, rows$altrep_4_x)
    rows[order(-rank_x), , drop = FALSE]
  })
  names(groups) <- families
  counts <- vapply(groups, nrow, integer(1))

  # Card height is measured in row units. A card also pays for its title and
  # axis whatever its length, so that overhead joins the budget: without it a
  # three-row family would squeeze its bars into a sliver of a tall card.
  overhead <- 3.6
  overhead_axis_title <- 6.4

  left <- character()
  right <- character()
  left_n <- 0
  right_n <- 0
  for (fam in families) {
    if (left_n <= right_n) {
      left <- c(left, fam)
      left_n <- left_n + counts[[fam]] + overhead
    } else {
      right <- c(right, fam)
      right_n <- right_n + counts[[fam]] + overhead
    }
  }
  columns <- list(left, right)

  block_units <- function(fam, last) {
    counts[[fam]] + if (last) overhead_axis_title else overhead
  }
  column_units <- vapply(columns, function(column) {
    sum(vapply(seq_along(column), function(i) {
      block_units(column[[i]], i == length(column))
    }, numeric(1)))
  }, numeric(1))
  units <- max(column_units)

  row_height <- 34
  header_units <- 1.5
  height <- round((units + header_units) * row_height + 90)
  png(path, width = 2000, height = height, res = 180, bg = "transparent")
  on.exit(dev.off(), add = TRUE)

  pad_x <- 0.008
  gap_x <- 0.007
  width_col <- (1 - 2 * pad_x - gap_x) / 2
  pad_y <- 26 / height
  header_height <- header_units * row_height / height
  usable <- 1 - 2 * pad_y - header_height
  inner_pad <- 0.0018
  header <- c(pad_x, 1 - pad_y - header_height + inner_pad * 2,
              1 - pad_x, 1 - pad_y)

  # Geometry first, so every card can be stroked before any panel is drawn.
  #
  # Card heights come from the taller column's unit total, so a bar is the
  # same thickness in both. The shorter column then has height left over: it
  # is spread across the gaps between that column's cards rather than added
  # to them, so both columns still finish on the same line.
  layout_cards <- list()
  panels <- list()
  for (i in seq_along(columns)) {
    x0 <- pad_x + (i - 1) * (width_col + gap_x)
    x1 <- x0 + width_col
    top <- 1 - pad_y - header_height
    column <- columns[[i]]
    slack <- (units - column_units[[i]]) / units * usable
    gap_y <- if (length(column) > 1L) slack / (length(column) - 1L) else 0
    for (j in seq_along(column)) {
      fam <- column[[j]]
      last <- j == length(column)
      block <- block_units(fam, last) / units * usable
      bottom <- top - block
      layout_cards[[length(layout_cards) + 1L]] <-
        c(x0, bottom + inner_pad, x1, top - inner_pad)
      panels[[length(panels) + 1L]] <- list(
        fam = fam,
        fig = c(x0 + 0.005, x1 - 0.004, bottom + 0.006, top - 0.006),
        last = last,
        first = i == 1L && j == 1L
      )
      top <- bottom - if (last) 0 else gap_y
    }
  }

  card_layer(c(list(header), layout_cards), ry = 0.011 * 2000 / height)

  # One key for the whole record, in its own strip so it cannot land on a bar.
  par(fig = c(header[[1L]], header[[3L]], header[[2L]], header[[4L]]),
      mar = c(0, 0, 0, 0), new = TRUE)
  plot.new()
  plot.window(c(0, 1), c(0, 1))
  legend("center", horiz = TRUE, bty = "n", text.col = col_ink, cex = 0.92,
         legend = c(
           "charr base strings", "charr ALTREP, 1 thread",
           "charr ALTREP, 4 threads", "reference (1×)"
         ),
         fill = c(col_base, col_altrep, col_altrep_4, NA), border = NA,
         lty = c(0, 0, 0, 2), lwd = c(0, 0, 0, 1.6),
         col = c(NA, NA, NA, col_rule), seg.len = 1.6)

  # The label gutter is measured per family rather than shared, so a panel of
  # short names does not reserve room for the longest name in the figure.
  label_margin <- function(labels, cex) {
    max(strwidth(labels, units = "inches", cex = cex)) / par("csi") + 1.1
  }

  for (panel in panels) {
    rows <- groups[[panel$fam]]
    par(las = 1, mgp = c(2.1, 0.5, 0))
    par(fig = panel$fig,
        mar = c(if (panel$last) 4.2 else 1.2,
                label_margin(row_labels(rows, "inline"), 0.70), 1.4, 1.2),
        new = TRUE)
    # No per-bar numbers here: the summary TSV carries exact medians, and
    # per-bar numbers would crowd the record out of its own figure.
    draw_speedup(rows, family_titles[[panel$fam]], cex_label = 0.70,
                 headroom = 0.06, show_values = FALSE, baseline = "inline",
                 axis_title = panel$last, space = c(0.12, 0.75))
  }
  height
}

full_path_out <- file.path(figures_dir, "bench-full.png")
h <- full_png(full_path_out)
cat(sprintf("full figure written to %s (2000x%d)\n", full_path_out, h))

# Summary table -------------------------------------------------------

# The numbers behind the two figures, in the order the full figure draws them:
# family by family, and within a family by four-thread speedup. Tab-separated
# because several operation labels carry a comma.
summary_table <- timings[c(
  "family", "label", "input_n", "stringi", "base",
  "altrep_1", "altrep_4", "base_x", "altrep_1_x", "altrep_4_x"
)]
summary_table$family <- unname(family_titles[summary_table$family])
rank_x <- ifelse(
  is.na(summary_table$altrep_4_x),
  summary_table$altrep_1_x,
  summary_table$altrep_4_x
)
summary_table <- summary_table[
  order(match(summary_table$family, family_titles), -rank_x), , drop = FALSE
]
numeric_columns <- vapply(summary_table, is.numeric, logical(1))
numeric_columns[["input_n"]] <- FALSE
summary_table[numeric_columns] <- lapply(
  summary_table[numeric_columns], round, digits = 3
)

table_path <- file.path(
  results_dir, paste0(label, "-figure-summary.tsv")
)
write.table(
  summary_table, table_path, sep = "\t", quote = FALSE, row.names = FALSE
)
cat(sprintf("summary table written to %s\n", table_path))
