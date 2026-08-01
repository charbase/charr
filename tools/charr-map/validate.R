args <- commandArgs(trailingOnly = TRUE)
if (length(args) != 3L) {
    stop(
        "usage: validate.R OUTPUT_DIR EXPECTED_UNITS EXPECTED_BACKEND_METHODS",
        call. = FALSE
    )
}

output_dir <- args[[1L]]
expected_units <- as.integer(args[[2L]])
expected_methods <- as.integer(args[[3L]])

read_table <- function(name) {
    path <- file.path(output_dir, name)
    if (!file.exists(path)) {
        stop("missing code-map table: ", path, call. = FALSE)
    }
    read.delim(
        path,
        quote = "",
        comment.char = "",
        colClasses = "character",
        check.names = FALSE,
        na.strings = NULL
    )
}

entities <- read_table("entities.tsv")
relationships <- read_table("relationships.tsv")
unit_dependencies <- read_table("unit_dependencies.tsv")
failures <- character()

expect <- function(condition, message) {
    if (!isTRUE(condition)) {
        failures <<- c(failures, message)
    }
}

expect_count <- function(actual, expected, label) {
    expect(
        identical(as.integer(actual), as.integer(expected)),
        sprintf("%s: expected %d, found %d", label, expected, actual)
    )
}

expect(!anyDuplicated(entities$id), "entity IDs are not unique")

known_ids <- unique(entities$id)
expect(
    all(relationships$source_id %in% known_ids),
    "relationships contain unknown source IDs"
)
expect(
    all(relationships$target_id %in% known_ids),
    "relationships contain unknown target IDs"
)

units <- entities[entities$kind == "compilation_unit", , drop = FALSE]
expect_count(nrow(units), expected_units, "compilation units")
expect(
    all(unit_dependencies$source_unit %in% units$id),
    "unit dependencies contain unknown source units"
)
expect(
    all(unit_dependencies$target_unit %in% units$id),
    "unit dependencies contain unknown target units"
)
expect(
    !any(unit_dependencies$source_unit == unit_dependencies$target_unit),
    "unit dependencies contain self edges"
)

entrypoints <- entities[entities$lint_tag == "entrypoint", , drop = FALSE]
abi_shims <- entities[entities$lint_tag == "abi_shim", , drop = FALSE]
expect_count(nrow(entrypoints), 2L * expected_methods, "semantic entrypoints")
expect_count(nrow(abi_shims), 2L * expected_methods, "ABI shims")

for (backend in c("base", "altrep")) {
    expect_count(
        sum(entrypoints$module == backend),
        expected_methods,
        paste(backend, "semantic entrypoints")
    )
    expect_count(
        sum(abi_shims$module == backend),
        expected_methods,
        paste(backend, "ABI shims")
    )
}

expect(
    setequal(
        entrypoints$name[entrypoints$module == "base"],
        entrypoints$name[entrypoints$module == "altrep"]
    ),
    "base and ALTREP entrypoint names are not paired"
)
expect_count(
    length(unique(entrypoints$name[entrypoints$module == "base"])),
    expected_methods,
    "paired backend operation names"
)

forwards <- relationships[
    relationships$relationship == "forwards_to",
    ,
    drop = FALSE
]
expect_count(nrow(forwards), 2L * expected_methods, "shim forwarding edges")
expect(
    all(forwards$source_id %in% abi_shims$id),
    "a forwarding edge starts outside an ABI shim"
)
expect(
    all(forwards$target_id %in% entrypoints$id),
    "a forwarding edge ends outside a semantic entrypoint"
)

registration_id <- "unit:src/runtime/registration.cpp"
registration_refs <- relationships[
    relationships$source_id == registration_id &
        relationships$relationship == "references_function",
    ,
    drop = FALSE
]
expect_count(
    nrow(registration_refs),
    2L * expected_methods + 4L,
    "registration function references"
)
expect_count(
    length(unique(registration_refs$target_id)),
    2L * expected_methods + 4L,
    "distinct registered functions"
)

expect(
    !any(entities$namespace_path_match == "false"),
    "entities contain namespace and path mismatches"
)

entity_module <- setNames(entities$module, entities$id)
semantic_relations <- relationships$relationship %in% c(
    "calls", "constructs", "forwards_to", "references_function"
)
source_module <- unname(entity_module[relationships$source_id])
target_module <- unname(entity_module[relationships$target_id])
cross_backend <- semantic_relations &
    source_module %in% c("base", "altrep") &
    target_module %in% c("base", "altrep") &
    source_module != target_module
expect(!any(cross_backend), "semantic edges cross between backend modules")

recursive_edges <- sum(relationships$source_id == relationships$target_id)

if (length(failures)) {
    stop(paste(c("code-map validation failed:", failures), collapse = "\n  "), call. = FALSE)
}

cat(sprintf(
    paste0(
        "code-map validation passed: %d units, %d entrypoints, ",
        "%d ABI shims, %d registration references, %d recursive edges\n"
    ),
    nrow(units),
    nrow(entrypoints),
    nrow(abi_shims),
    nrow(registration_refs),
    recursive_edges
))
