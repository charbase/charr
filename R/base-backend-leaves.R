.charr_base_wrapper_helpers <- c(
  "ci_opts_fixed",
  "ci_opts_regex",
  "ci_opts_collator",
  "ci_opts_brkiter",
  "ci_split_lines",
  "ci_trim",
  "ci_replace_all_charclass",
  "ci_trans_nfc",
  "ci_enc_get",
  "ci_read_raw",
  "ci_encode",
  "ci_split_lines1",
  "ci_enc_info"
)

.charr_base_native_aliases <- c(
  "C_ci_detect_fixed",
  "C_ci_startswith_fixed",
  "C_ci_endswith_fixed",
  "C_ci_count_fixed",
  "C_ci_locate_first_fixed",
  "C_ci_locate_all_fixed",
  "C_ci_extract_first_fixed",
  "C_ci_extract_all_fixed",
  "C_ci_replace_first_fixed",
  "C_ci_replace_all_fixed",
  "C_ci_split_fixed",
  "C_ci_sub",
  "C_ci_sub_replacement",
  "C_ci_sub_all",
  "C_ci_sub_replacement_all",
  "C_ci_length",
  "C_ci_join",
  "C_ci_flatten",
  "C_ci_dup",
  "C_ci_reverse",
  "C_ci_trim_left",
  "C_ci_trim_right",
  "C_ci_trim_both",
  "C_ci_replace_na",
  "C_ci_detect_regex",
  "C_ci_count_regex",
  "C_ci_locate_first_regex",
  "C_ci_locate_all_regex",
  "C_ci_extract_first_regex",
  "C_ci_extract_all_regex",
  "C_ci_replace_first_regex",
  "C_ci_replace_all_regex",
  "C_ci_split_regex",
  "C_ci_match_first_regex",
  "C_ci_match_all_regex",
  "C_ci_detect_coll",
  "C_ci_startswith_coll",
  "C_ci_endswith_coll",
  "C_ci_count_coll",
  "C_ci_locate_first_coll",
  "C_ci_locate_all_coll",
  "C_ci_extract_first_coll",
  "C_ci_extract_all_coll",
  "C_ci_replace_first_coll",
  "C_ci_replace_all_coll",
  "C_ci_split_coll",
  "C_ci_order",
  "C_ci_rank",
  "C_ci_cmp_equiv",
  "C_ci_duplicated",
  "C_ci_trans_tolower",
  "C_ci_trans_toupper",
  "C_ci_trans_totitle",
  "C_ci_count_boundaries",
  "C_ci_locate_first_boundaries",
  "C_ci_locate_all_boundaries",
  "C_ci_extract_first_boundaries",
  "C_ci_extract_all_boundaries",
  "C_ci_split_boundaries",
  "C_ci_wrap",
  "C_ci_pad",
  "C_ci_width",
  "C_ci_escape_unicode",
  "C_ci_encode",
  "C_ci_read_lines",
  "C_ci_split_lines",
  "C_ci_replace_all_charclass",
  "C_ci_trans_nfc",
  "C_ci_split_lines1",
  "C_ci_enc_info"
)
.charr_base_native_aliases <- setNames(
  sub(
    "^C_ci_",
    "C_charr_base_ci_",
    .charr_base_native_aliases
  ),
  .charr_base_native_aliases
)

.charr_base_wrapper_roots <- unique(unname(.charr_leaf_map))
.charr_base_wrapper_graph <- c(
  .charr_base_wrapper_roots,
  .charr_base_wrapper_helpers
)
.charr_base_namespace <- environment(.charr_register_base_leaf_bindings)

.charr_missing_base_wrappers <- .charr_base_wrapper_graph[!vapply(
  .charr_base_wrapper_graph,
  exists,
  logical(1),
  envir = .charr_base_namespace,
  inherits = FALSE
)]
if (length(.charr_missing_base_wrappers) > 0L) {
  stop(
    "base wrapper manifest is missing: ",
    paste(sprintf("`%s`", .charr_missing_base_wrappers), collapse = ", "),
    call. = FALSE
  )
}

.charr_base_references <- unique(unlist(lapply(
  .charr_base_wrapper_graph,
  function(name) {
    all.names(
      body(get(name, envir = .charr_base_namespace, inherits = FALSE)),
      functions = TRUE,
      unique = TRUE
    )
  }
)))
.charr_unbound_base_wrappers <- setdiff(
  grep("^ci_", .charr_base_references, value = TRUE),
  .charr_base_wrapper_graph
)
.charr_unbound_base_natives <- setdiff(
  grep("^C_ci_", .charr_base_references, value = TRUE),
  names(.charr_base_native_aliases)
)
.charr_unused_base_native_aliases <- setdiff(
  names(.charr_base_native_aliases),
  grep("^C_ci_", .charr_base_references, value = TRUE)
)
if (
  length(.charr_unbound_base_wrappers) > 0L ||
    length(.charr_unbound_base_natives) > 0L ||
    length(.charr_unused_base_native_aliases) > 0L
) {
  stop("base wrapper graph has an unbound backend reference", call. = FALSE)
}

.charr_base_leaf_environment <- new.env(parent = baseenv())
for (.charr_native_alias in names(.charr_base_native_aliases)) {
  assign(.charr_native_alias, NULL, envir = .charr_base_leaf_environment)
}
for (.charr_wrapper in .charr_base_wrapper_graph) {
  assign(
    .charr_wrapper,
    .charr_clone_function(
      get(.charr_wrapper, envir = .charr_base_namespace, inherits = FALSE),
      .charr_base_leaf_environment
    ),
    envir = .charr_base_leaf_environment
  )
}

.charr_initialize_base_native_aliases <- function() {
  for (old_name in names(.charr_base_native_aliases)) {
    new_name <- .charr_base_native_aliases[[old_name]]
    if (!exists(new_name, envir = .charr_base_namespace, inherits = FALSE)) {
      stop(
        "base backend native binding is missing: `",
        new_name,
        "`",
        call. = FALSE
      )
    }
    native <- get(new_name, envir = .charr_base_namespace, inherits = FALSE)
    if (!inherits(native, "NativeSymbolInfo")) {
      stop("base backend binding is not a registered native symbol", call. = FALSE)
    }
    assign(old_name, native, envir = .charr_base_leaf_environment)
  }
  invisible(NULL)
}

.charr_register_base_leaf_bindings(setNames(
  mget(
    unname(.charr_leaf_map),
    envir = .charr_base_leaf_environment,
    inherits = FALSE
  ),
  names(.charr_leaf_map)
))

rm(
  .charr_missing_base_wrappers,
  .charr_base_references,
  .charr_unbound_base_wrappers,
  .charr_unbound_base_natives,
  .charr_unused_base_native_aliases,
  .charr_native_alias,
  .charr_wrapper
)
