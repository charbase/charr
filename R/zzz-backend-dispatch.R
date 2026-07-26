.charr_backend_roots <- c(
  "str_sub<-",
  "str_c",
  "str_conv",
  "str_count",
  "str_detect",
  "str_dup",
  "str_ends",
  "str_equal",
  "str_escape",
  "str_extract",
  "str_extract_all",
  "str_flatten",
  "str_flatten_comma",
  "str_ilike",
  "str_interp",
  "str_length",
  "str_like",
  "str_locate",
  "str_locate_all",
  "str_match",
  "str_match_all",
  "str_order",
  "str_pad",
  "str_rank",
  "str_read_lines",
  "str_remove",
  "str_remove_all",
  "str_replace",
  "str_replace_all",
  "str_replace_na",
  "str_reverse",
  "str_sort",
  "str_split",
  "str_split_1",
  "str_split_fixed",
  "str_split_i",
  "str_squish",
  "str_starts",
  "str_sub",
  "str_sub_all",
  "str_subset",
  "str_to_camel",
  "str_to_kebab",
  "str_to_lower",
  "str_to_sentence",
  "str_to_snake",
  "str_to_title",
  "str_to_upper",
  "str_trim",
  "str_trunc",
  "str_unique",
  "str_view",
  "str_view_all",
  "str_which",
  "str_width",
  "str_wrap",
  "word"
)

.charr_backend_helpers <- c(
  "fix_replacement",
  "fix_replacement_one",
  "like_to_regex",
  "str_transform",
  "str_transform_all",
  "str_view_filter",
  "str_view_highlighter",
  "str_view_print",
  "str_view_special",
  "str_view_widget",
  "to_separated_case",
  "to_words"
)

.charr_make_backend_environment <- function(leaves, templates, namespace) {
  backend <- new.env(parent = namespace)
  list2env(leaves, envir = backend)

  for (function_name in names(templates)) {
    implementation <- .charr_clone_function(
      templates[[function_name]],
      backend
    )
    assign(function_name, implementation, envir = backend)
  }

  backend
}

.charr_forward_call <- function(implementation, arguments) {
  forwarded <- lapply(names(arguments), as.name)
  names(forwarded) <- names(arguments)
  names(forwarded)[names(forwarded) == "..."] <- ""
  as.call(c(list(as.name(implementation)), forwarded))
}

.charr_make_public_dispatch <- function(name, template, environments, namespace) {
  dispatch_environment <- new.env(parent = namespace)
  assign(
    ".stringi_implementation",
    environments$stringi[[name]],
    envir = dispatch_environment
  )
  assign(
    ".base_implementation",
    environments$base[[name]],
    envir = dispatch_environment
  )
  assign(
    ".altrep_implementation",
    environments$altrep[[name]],
    envir = dispatch_environment
  )

  arguments <- formals(template)
  implementation <- call(
    "switch",
    call(".charr_backend_value"),
    stringi = as.name(".stringi_implementation"),
    base = as.name(".base_implementation"),
    altrep = as.name(".altrep_implementation")
  )
  bind_implementation <- call("<-", as.name(name), implementation)
  implementation_call <- .charr_forward_call(name, arguments)

  dispatch <- eval(call("function", arguments, quote(NULL)))
  body(dispatch) <- as.call(list(
    as.name("{"),
    bind_implementation,
    implementation_call
  ))
  environment(dispatch) <- dispatch_environment
  dispatch
}

.charr_assert_backend_environment <- function(backend, names) {
  missing <- names[!vapply(
    names,
    exists,
    logical(1),
    envir = backend,
    inherits = FALSE
  )]
  if (length(missing) > 0L) {
    stop(
      "backend environment is incomplete: ",
      paste(sprintf("`%s`", missing), collapse = ", "),
      call. = FALSE
    )
  }
}

.charr_wrap_joined_stringi <- function(
  string, width, indent, exdent, whitespace_only
) {
  out <- stri_wrap(
    string,
    width = width,
    indent = indent,
    exdent = exdent,
    whitespace_only = whitespace_only,
    simplify = FALSE
  )
  vapply(out, str_c, collapse = "\n", character(1))
}

.charr_wrap_joined_native <- function(
  string, width, indent, exdent, whitespace_only
) {
  wrap <- stri_wrap
  wrap(
    string,
    width = width,
    cost_exponent = 2,
    simplify = TRUE,
    normalize = TRUE,
    indent = indent,
    exdent = exdent,
    prefix = "",
    initial = "",
    whitespace_only = whitespace_only,
    use_length = FALSE,
    locale = NULL,
    .output_mode = 2L
  )
}

.charr_namespace <- environment(.charr_make_public_dispatch)
.charr_backend_templates <- mget(
  c(.charr_backend_roots, .charr_backend_helpers),
  envir = .charr_namespace,
  inherits = FALSE
)

.charr_stringi_leaves <- lapply(names(.charr_leaf_map), function(name) {
  getExportedValue("stringi", name)
})
names(.charr_stringi_leaves) <- names(.charr_leaf_map)

.charr_altrep_leaves <- mget(
  unname(.charr_leaf_map),
  envir = .charr_namespace,
  inherits = FALSE
)
names(.charr_altrep_leaves) <- names(.charr_leaf_map)

.charr_base_leaves <- mget(
  names(.charr_leaf_map),
  envir = .charr_base_leaf_bindings,
  inherits = FALSE
)

.charr_backend_environments <- list(
  stringi = .charr_make_backend_environment(
    .charr_stringi_leaves,
    .charr_backend_templates,
    .charr_namespace
  ),
  base = .charr_make_backend_environment(
    .charr_base_leaves,
    .charr_backend_templates,
    .charr_namespace
  ),
  altrep = .charr_make_backend_environment(
    .charr_altrep_leaves,
    .charr_backend_templates,
    .charr_namespace
  )
)

for (.charr_backend_name in names(.charr_backend_environments)) {
  .charr_backend_environment <- .charr_backend_environments[[
    .charr_backend_name
  ]]
  .charr_wrap_joined_template <- if (
    identical(.charr_backend_name, "stringi")
  ) {
    .charr_wrap_joined_stringi
  } else {
    .charr_wrap_joined_native
  }
  assign(
    ".charr_wrap_joined",
    .charr_clone_function(
      .charr_wrap_joined_template,
      .charr_backend_environment
    ),
    envir = .charr_backend_environment
  )
}
rm(
  .charr_backend_name,
  .charr_backend_environment,
  .charr_wrap_joined_template
)

for (.charr_backend_environment in .charr_backend_environments) {
  .charr_assert_backend_environment(
    .charr_backend_environment,
    c(
      names(.charr_leaf_map),
      names(.charr_backend_templates),
      ".charr_wrap_joined"
    )
  )
}
rm(.charr_backend_environment)

for (.charr_root in .charr_backend_roots) {
  assign(
    .charr_root,
    .charr_make_public_dispatch(
      .charr_root,
      .charr_backend_templates[[.charr_root]],
      .charr_backend_environments,
      .charr_namespace
    ),
    envir = .charr_namespace
  )
}
rm(.charr_root)
