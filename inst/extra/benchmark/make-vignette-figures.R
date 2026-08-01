#!/usr/bin/env Rscript

# Vignette figures for charr, drawn in the same style as charport's benchmark
# figure: base graphics on a transparent field, white rounded cards, and the
# purple ramp from the package logo. Transparency lets one PNG sit on both the
# light and dark documentation themes.
#
# This script only reads a recorded benchmark label. It never runs the
# benchmark. Regenerate measurements with run.R first, then:
#
#   Rscript inst/extra/benchmark/make-vignette-figures.R <label> [--variants]
#
# The label is required. It used to default to whichever record was current
# when this line was written, which silently drew the vignette from a
# superseded run once a newer one existed. `make figures` passes BENCH_LABEL.
#
# --variants also writes the candidate summary layouts to local/ for review.

args <- commandArgs(trailingOnly = TRUE)
variants <- "--variants" %in% args
args <- args[args != "--variants"]
if (!length(args)) {
  stop(
    "usage: make-vignette-figures.R <label> [--variants]\n",
    "  label names a recorded run in results/, without the -times.csv suffix",
    call. = FALSE
  )
}
label <- args[[1L]]

file_arg <- grep("^--file=", commandArgs(FALSE), value = TRUE)[[1L]]
bench_dir <- dirname(normalizePath(sub("^--file=", "", file_arg)))
pkg_root <- normalizePath(file.path(bench_dir, "..", "..", ".."))
results_dir <- file.path(bench_dir, "results")
figures_dir <- file.path(pkg_root, "man", "figures")
stopifnot(dir.exists(figures_dir))

summary_path <- file.path(results_dir, paste0(label, "-summary.csv"))
if (!file.exists(summary_path)) {
  stop("no summary for label ", label, ": ", summary_path)
}

# Measurements ---------------------------------------------------------

records <- read.csv(summary_path, stringsAsFactors = FALSE)
stopifnot(all(c("op", "family", "condition", "median_ms", "input_n") %in% names(records)))
stopifnot(setequal(unique(records$condition), c("stringi", "base", "altrep")))

widen <- function(data) {
  keys <- unique(data[c("op", "family", "scale_class", "input_n")])
  pick <- function(condition) {
    rows <- data[data$condition == condition, c("op", "median_ms")]
    rows$median_ms[match(keys$op, rows$op)]
  }
  keys$stringi <- pick("stringi")
  keys$base <- pick("base")
  keys$altrep <- pick("altrep")
  keys$base_x <- keys$stringi / keys$base
  keys$altrep_x <- keys$stringi / keys$altrep
  keys
}

timings <- widen(records)
reps <- unique(records$reps)

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
col_stringi <- "#d3c9ea"
col_base <- "#a291d1"
col_altrep <- "#6a4fa6"
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
draw_speedup <- function(rows, title, with_stringi = FALSE, cex_label = 1.0,
                         headroom = 0.10, show_values = TRUE,
                         show_baseline = TRUE, axis_title = TRUE,
                         space = c(0, 0.85)) {
  rows <- rows[rev(seq_len(nrow(rows))), , drop = FALSE]
  values <- if (with_stringi) {
    rbind(altrep = rows$altrep_x, base = rows$base_x, stringi = 1)
  } else {
    rbind(altrep = rows$altrep_x, base = rows$base_x)
  }
  cols <- if (with_stringi) {
    c(col_altrep, col_base, col_stringi)
  } else {
    c(col_altrep, col_base)
  }
  top <- max(values) * (1 + headroom)
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
  }
  if (nzchar(title)) {
    mtext(title, side = 3, line = 0.5, col = col_ink, cex = cex_label * 1.2,
          font = 2)
  }
  abline(v = 1, col = col_rule, lty = 2, lwd = 1.6)

  labels <- if (show_baseline) {
    sprintf("%s\n(%s)", rows$label, format_ms(rows$stringi))
  } else {
    rows$label
  }
  text(x = -0.012 * top, y = colMeans(bp), labels = labels, adj = 1,
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
legend_bar <- function(with_stringi = FALSE, cex = 1.0,
                       where = "bottomright") {
  entries <- c("charr base strings", "charr ALTREP strings")
  fills <- c(col_base, col_altrep)
  if (with_stringi) {
    entries <- c("stringi reference", entries)
    fills <- c(col_stringi, fills)
  }
  legend(where, legend = entries, fill = fills, border = NA, bty = "n",
         text.col = col_ink, cex = cex, y.intersp = 1.2, inset = c(0.01, 0.03))
}

# Summary figure -------------------------------------------------------

# str_read_lines leads: reading a file straight into an ALTREP vector is the
# clearest case for keeping strings out of R's global cache.
summary_ops <- c(
  "ci_read_lines",
  "ci_dup",
  "ci_c",
  "ci_sub",
  "ci_trim_left",
  "ci_reverse",
  "ci_trans_tolower",
  "ci_pad_left",
  "ci_count_fixed",
  "ci_width",
  "ci_extract_first_regex",
  "ci_split_fixed",
  "ci_detect_coll"
)
summary_rows <- timings[match(summary_ops, timings$op), , drop = FALSE]
stopifnot(!anyNA(summary_rows$op))

summary_png <- function(path, with_stringi = FALSE) {
  png(path, width = 1700, height = 1200, res = 180, bg = "transparent")
  on.exit(dev.off(), add = TRUE)
  card_layer(list(c(0.012, 0.012, 0.988, 0.988)))
  par(las = 1, mgp = c(2.4, 0.7, 0))
  par(fig = c(0.028, 0.982, 0.02, 0.98), mar = c(5.0, 12.6, 1.2, 2.6),
      new = TRUE)
  draw_speedup(summary_rows, "", with_stringi = with_stringi, cex_label = 0.92,
               headroom = 0.13)
  legend_bar(with_stringi, cex = 0.95)
}

raw_png <- function(path) {
  rows <- summary_rows[rev(seq_len(nrow(summary_rows))), , drop = FALSE]
  values <- rbind(altrep = rows$altrep, base = rows$base,
                  stringi = rows$stringi)
  png(path, width = 1700, height = 1200, res = 180, bg = "transparent")
  on.exit(dev.off(), add = TRUE)
  card_layer(list(c(0.012, 0.012, 0.988, 0.988)))
  par(las = 1, mgp = c(2.4, 0.7, 0))
  par(fig = c(0.028, 0.982, 0.02, 0.98), mar = c(4.2, 12.6, 1.2, 2.2),
      new = TRUE)
  ticks <- c(1, 3, 10, 30, 100, 300, 1000)
  bp <- barplot(log10(values), beside = TRUE, horiz = TRUE,
                col = c(col_altrep, col_base, col_stringi), border = NA,
                names.arg = rep("", nrow(rows)), axes = FALSE,
                xlim = c(0, log10(1600)), space = c(0, 0.85))
  axis(1, at = log10(ticks), labels = ticks, col = col_axis,
       col.axis = col_axis, cex.axis = 0.95, lwd = 1.4)
  mtext("elapsed milliseconds, log scale (lower is better)", side = 1,
        line = 2.3, col = col_ink, cex = 0.95)
  text(x = -0.02, y = colMeans(bp), labels = rows$label, adj = 1, xpd = NA,
       col = col_ink, cex = 0.92)
  legend_bar(TRUE, cex = 0.95, where = "topright")
}

if (variants) {
  dir.create(file.path(pkg_root, "local"), showWarnings = FALSE)
  summary_png(file.path(pkg_root, "local", "bench-summary-v1.png"), FALSE)
  summary_png(file.path(pkg_root, "local", "bench-summary-v2.png"), TRUE)
  raw_png(file.path(pkg_root, "local", "bench-summary-v3.png"))
  cat("variants written to local/bench-summary-v{1,2,3}.png\n")
}

summary_path_out <- file.path(figures_dir, "bench-summary.png")
summary_png(summary_path_out, FALSE)
cat(sprintf("summary figure written to %s\n", summary_path_out))

# Full figure ----------------------------------------------------------

# One card per operation family, laid out in two columns with heights
# proportional to the number of rows so every bar keeps the same thickness.
full_png <- function(path) {
  families <- names(family_titles)
  groups <- lapply(families, function(fam) {
    rows <- timings[timings$family == fam, , drop = FALSE]
    rows[order(-rows$altrep_x), , drop = FALSE]
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
  gap_x <- 0.014
  width_col <- (1 - 2 * pad_x - gap_x) / 2
  pad_y <- 26 / height
  header_height <- header_units * row_height / height
  usable <- 1 - 2 * pad_y - header_height
  inner_pad <- 0.0018
  header <- c(pad_x, 1 - pad_y - header_height + inner_pad * 2,
              1 - pad_x, 1 - pad_y)

  # Geometry first, so every card can be stroked before any panel is drawn.
  layout_cards <- list()
  panels <- list()
  for (i in seq_along(columns)) {
    x0 <- pad_x + (i - 1) * (width_col + gap_x)
    x1 <- x0 + width_col
    top <- 1 - pad_y - header_height
    column <- columns[[i]]
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
      top <- bottom
    }
  }

  card_layer(c(list(header), layout_cards), ry = 0.011 * 2000 / height)

  # One key for the whole record, in its own strip so it cannot land on a bar.
  par(fig = c(header[[1L]], header[[3L]], header[[2L]], header[[4L]]),
      mar = c(0, 0, 0, 0), new = TRUE)
  plot.new()
  plot.window(c(0, 1), c(0, 1))
  legend("center", horiz = TRUE, bty = "n", text.col = col_ink, cex = 0.92,
         legend = c("charr base strings", "charr ALTREP strings",
                    "reference (1×)"),
         fill = c(col_base, col_altrep, NA), border = NA,
         lty = c(0, 0, 2), lwd = c(0, 0, 1.6),
         col = c(NA, NA, col_rule), seg.len = 1.6)

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
                label_margin(rows$label, 0.72), 1.4, 1.6),
        new = TRUE)
    # No per-bar numbers here: the vignette table carries exact medians, and
    # 134 labels would crowd the record out of its own figure.
    draw_speedup(rows, family_titles[[panel$fam]], cex_label = 0.72,
                 headroom = 0.06, show_values = FALSE, show_baseline = FALSE,
                 axis_title = panel$last, space = c(0.12, 0.75))
  }
  height
}

full_path_out <- file.path(figures_dir, "bench-full.png")
h <- full_png(full_path_out)
cat(sprintf("full figure written to %s (2000x%d)\n", full_path_out, h))

# Table for the implementation vignette --------------------------------

table_out <- timings[c("family", "label", "input_n", "stringi", "base",
                       "altrep", "base_x", "altrep_x")]
table_out$family <- unname(family_titles[table_out$family])
table_out <- table_out[order(match(table_out$family, family_titles),
                             -table_out$altrep_x), , drop = FALSE]
table_path <- file.path(results_dir, paste0(label, "-vignette-table.csv"))
write.csv(table_out, table_path, row.names = FALSE)
cat(sprintf("vignette table written to %s\n", table_path))

# The implementation vignette carries this table as static markdown, so the
# rendered numbers cannot drift from the figure beside them. Paste the block
# below in whole when the record is refreshed.
md_rows <- sprintf(
  "| %s | %s | %s | %s | %s | %.2f | %.2f |",
  table_out$label,
  formatC(table_out$input_n, big.mark = ",", format = "d"),
  format_ms(table_out$stringi), format_ms(table_out$base),
  format_ms(table_out$altrep), table_out$base_x, table_out$altrep_x
)
md <- c(
  "| Operation | Records | reference | base | ALTREP | base × | ALTREP × |",
  "|---|---:|---:|---:|---:|---:|---:|"
)
for (fam in unique(table_out$family)) {
  md <- c(md, sprintf("| **%s** | | | | | | |", fam),
          md_rows[table_out$family == fam])
}
md_path <- file.path(results_dir, paste0(label, "-vignette-table.md"))
writeLines(md, md_path)
cat(sprintf("vignette markdown written to %s\n", md_path))
