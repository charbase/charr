# Backend-neutral option records used by stringr pattern objects.
#
# These functions only construct named R lists. Keeping them outside the
# backend environments lets a pattern be created under one backend and used
# under another without carrying an implementation-specific closure or object.

.charr_opts_fixed <- function(case_insensitive = FALSE, overlap = FALSE) {
  opts <- list()
  if (!missing(case_insensitive)) {
    opts["case_insensitive"] <- case_insensitive
  }
  if (!missing(overlap)) {
    opts["overlap"] <- overlap
  }
  opts
}

.charr_opts_collator <- function(
  locale = NULL,
  strength = 3L,
  alternate_shifted = FALSE,
  french = FALSE,
  uppercase_first = NA,
  case_level = FALSE,
  normalization = FALSE,
  normalisation = normalization,
  numeric = FALSE
) {
  opts <- list()
  if (!missing(locale)) {
    opts["locale"] <- locale
  }
  if (!missing(strength)) {
    opts["strength"] <- strength
  }
  if (!missing(alternate_shifted)) {
    opts["alternate_shifted"] <- alternate_shifted
  }
  if (!missing(french)) {
    opts["french"] <- french
  }
  if (!missing(uppercase_first)) {
    opts["uppercase_first"] <- uppercase_first
  }
  if (!missing(case_level)) {
    opts["case_level"] <- case_level
  }
  if (!missing(numeric)) {
    opts["numeric"] <- numeric
  }

  if (!missing(normalization)) {
    opts["normalization"] <- normalization
  } else if (!missing(normalisation)) {
    opts["normalization"] <- normalisation
  }

  opts
}

.charr_opts_regex <- function(
  case_insensitive,
  comments,
  dotall,
  dot_all = dotall,
  literal,
  multiline,
  multi_line = multiline,
  unix_lines,
  uword,
  error_on_unknown_escapes,
  time_limit = 0L,
  stack_limit = 0L
) {
  opts <- list()
  if (!missing(case_insensitive)) {
    opts["case_insensitive"] <- case_insensitive
  }
  if (!missing(comments)) {
    opts["comments"] <- comments
  }
  if (!missing(literal)) {
    opts["literal"] <- literal
  }
  if (!missing(unix_lines)) {
    opts["unix_lines"] <- unix_lines
  }
  if (!missing(uword)) {
    opts["uword"] <- uword
  }
  if (!missing(error_on_unknown_escapes)) {
    opts["error_on_unknown_escapes"] <- error_on_unknown_escapes
  }
  if (!missing(stack_limit)) {
    opts["stack_limit"] <- stack_limit
  }
  if (!missing(time_limit)) {
    opts["time_limit"] <- time_limit
  }

  if (!missing(dotall)) {
    opts["dotall"] <- dotall
  } else if (!missing(dot_all)) {
    opts["dotall"] <- dot_all
  }
  if (!missing(multiline)) {
    opts["multiline"] <- multiline
  } else if (!missing(multi_line)) {
    opts["multiline"] <- multi_line
  }

  opts
}

.charr_opts_brkiter <- function(
  type,
  locale,
  skip_word_none,
  skip_word_number,
  skip_word_letter,
  skip_word_kana,
  skip_word_ideo,
  skip_line_soft,
  skip_line_hard,
  skip_sentence_term,
  skip_sentence_sep
) {
  opts <- list()
  if (!missing(type)) {
    opts["type"] <- type
  }
  if (!missing(locale)) {
    opts["locale"] <- locale
  }
  if (!missing(skip_word_none)) {
    opts["skip_word_none"] <- skip_word_none
  }
  if (!missing(skip_word_number)) {
    opts["skip_word_number"] <- skip_word_number
  }
  if (!missing(skip_word_letter)) {
    opts["skip_word_letter"] <- skip_word_letter
  }
  if (!missing(skip_word_kana)) {
    opts["skip_word_kana"] <- skip_word_kana
  }
  if (!missing(skip_word_ideo)) {
    opts["skip_word_ideo"] <- skip_word_ideo
  }
  if (!missing(skip_line_soft)) {
    opts["skip_line_soft"] <- skip_line_soft
  }
  if (!missing(skip_line_hard)) {
    opts["skip_line_hard"] <- skip_line_hard
  }
  if (!missing(skip_sentence_term)) {
    opts["skip_sentence_term"] <- skip_sentence_term
  }
  if (!missing(skip_sentence_sep)) {
    opts["skip_sentence_sep"] <- skip_sentence_sep
  }
  opts
}
