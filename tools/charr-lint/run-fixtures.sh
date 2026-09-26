#!/usr/bin/env bash
set -euo pipefail

lint=$1
fixture_dir=$(cd "$(dirname "$0")/fixtures" && pwd)
resource_effects="$fixture_dir/resource-effects.tsv"
resource_overrides="$fixture_dir/resource-effect-overrides.tsv"
project_effects="$fixture_dir/../effects.tsv"
project_overrides="$fixture_dir/../effect-overrides.tsv"
inferred_effects="$fixture_dir/inferred-effects.tsv"
reviewed_c_api_effects="$fixture_dir/reviewed-c-api-effects.tsv"
icu_owner_effects="$fixture_dir/icu-owner-effects.tsv"
icu_owner_overrides="$fixture_dir/icu-owner-effect-overrides.tsv"

# Entry-point fixtures use the real protection headers, whose R calls need the
# project manifest and overrides. Resource fixtures add their raw resource
# rows to those.
resource_project_effects=$(mktemp)
resource_project_overrides=$(mktemp)
trap 'rm -f "$resource_project_effects" "$resource_project_overrides"' EXIT
cat "$project_effects" >"$resource_project_effects"
tail -n +2 "$resource_effects" >>"$resource_project_effects"
cat "$project_overrides" >"$resource_project_overrides"
tail -n +2 "$resource_overrides" >>"$resource_project_overrides"

# charr-lint prints its own diagnostics as 'FILE:LINE:COL: error: MESSAGE',
# the same shape as a Clang error, but they do not pass through Clang's
# diagnostics engine. A Clang compile error is always followed by Clang's
# 'N error(s) generated.' summary and the tool's 'Error while processing'
# line, which charr-lint never prints. A fixture with either line lints a
# translation unit that does not compile, so it fails whatever else it
# reports.
reject_compile_errors() {
    if grep -E '^[0-9]+ errors? generated\.$|^Error while processing ' \
            "$1" >/dev/null; then
        printf 'fixture does not compile: %s\n' "$2" >&2
        cat "$1" >&2
        rm -f "$1"
        exit 1
    fi
}

# run_lint ARGS... runs a fixture that must lint cleanly.
run_lint() {
    local output
    output=$(mktemp)
    if ! "$lint" "$@" >"$output" 2>&1; then
        printf 'fixture failed: %s\n' "$*" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    rm -f "$output"
}

clang++ -std=c++17 -fsyntax-only -I/usr/share/R/include \
    "$fixture_dir/shared-foundation.cpp"

for entry_fixture in good.cpp good-reader.cpp good-reader-vector.cpp; do
    run_lint --effects "$project_effects" \
        --effect-overrides "$project_overrides" \
        "$fixture_dir/$entry_fixture" -- \
        -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
done
run_lint "$fixture_dir/good-dependent-template-call.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
run_lint --effects "$resource_project_effects" \
    --effect-overrides "$resource_project_overrides" \
    "$fixture_dir/good-resource-owner.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
run_lint --effects "$inferred_effects" \
    "$fixture_dir/good-inferred-effects.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
generated_effects=$(mktemp)
run_lint --audit --effects "$inferred_effects" \
    --write-effects-manifest "$generated_effects" \
    "$fixture_dir/good-inferred-effects.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
if ! cmp "$inferred_effects" "$generated_effects"; then
    printf '%s\n' 'generated external-effect manifest is not stable' >&2
    rm -f "$generated_effects"
    exit 1
fi
rm -f "$generated_effects"
# A reviewed C API keeps its neutral contract even when the header renames the
# entry point by token pasting, as ICU does.
run_lint --effects "$reviewed_c_api_effects" \
    "$fixture_dir/good-reviewed-c-api.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -isystem "$fixture_dir/sysapi"
generated_effects=$(mktemp)
run_lint --audit --effects "$reviewed_c_api_effects" \
    --write-effects-manifest "$generated_effects" \
    "$fixture_dir/good-reviewed-c-api.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -isystem "$fixture_dir/sysapi"
if ! cmp "$reviewed_c_api_effects" "$generated_effects"; then
    printf '%s\n' 'reviewed C API manifest is not stable' >&2
    rm -f "$generated_effects"
    exit 1
fi
rm -f "$generated_effects"
run_lint --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-protection.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
run_lint --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-reprotect-slot.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
run_lint --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-abi-shim.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
run_lint "$fixture_dir/good-contracts.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
# Variant fixtures must pass without a variant so that each failure below is
# caused by the variant alone.
for variant_base in bad-abi-shim-contract.cpp bad-raw-protection.cpp \
        bad-entry-protection-branch.cpp bad-helper-contract.cpp \
        bad-entry-unwind-region.cpp; do
    run_lint --effects "$project_effects" \
        --effect-overrides "$project_overrides" \
        "$fixture_dir/$variant_base" -- \
        -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
done

check_failure() {
    file=$1
    expected=$2
    output=$(mktemp)
    if "$lint" --effects "$project_effects" \
            --effect-overrides "$project_overrides" \
            "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include \
            >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$file" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture did not report expected diagnostic: %s\n' "$file" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_r_failure() {
    file=$1
    expected=$2
    output=$(mktemp)
    if "$lint" "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include \
            >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$file" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture did not report expected diagnostic: %s\n' "$file" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_resource_failure() {
    file=$1
    expected=$2
    output=$(mktemp)
    if "$lint" --effects "$resource_project_effects" \
            --effect-overrides "$resource_project_overrides" \
            "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include \
            >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$file" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture did not report expected diagnostic: %s\n' "$file" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_r_variant() {
    definition=$1
    expected=$2
    output=$(mktemp)
    if "$lint" --effects "$project_effects" \
            --effect-overrides "$project_overrides" \
            "$fixture_dir/bad-entry-protection-branch.cpp" -- \
            -std=c++17 -DCHARR_LINT=1 -D"$definition" \
            -I/usr/share/R/include >"$output" 2>&1; then
        printf 'fixture variant unexpectedly passed: %s\n' \
            "$definition" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture variant did not report expected diagnostic: %s\n' \
            "$definition" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

# check_variant MODE FILE DEFINITION EXPECTED
# MODE is plain (inferred effects only) or project (reviewed manifest and
# overrides). DEFINITION is a preprocessor symbol, or - for none.
check_variant() {
    mode=$1
    file=$2
    definition=$3
    expected=$4
    output=$(mktemp)
    args=()
    if [[ $mode == project ]]; then
        args+=(--effects "$project_effects")
        args+=(--effect-overrides "$project_overrides")
    fi
    defines=()
    if [[ $definition != - ]]; then
        defines+=(-D"$definition")
    fi
    if "$lint" "${args[@]}" "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include \
            "${defines[@]}" >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s %s\n' "$file" \
            "$definition" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture did not report expected diagnostic: %s %s\n' \
            "$file" "$definition" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_unclassified_audit() {
    output=$(mktemp)
    "$lint" --audit --dump-external-calls \
        "$fixture_dir/bad-unclassified-external.cpp" -- \
        -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1
    reject_compile_errors "$output" "$*"
    if ! grep -F \
            $'neutral\traw_open\tvoid *(void) noexcept\tneutral\tclang:no-cxx-propagation' \
            "$output" >/dev/null; then
        printf '%s\n' \
            'audit did not inventory an external call from an unclassified function' \
            >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_inference_failure() {
    manifest=$1
    overrides=$2
    expected=$3
    output=$(mktemp)
    args=(--effects "$fixture_dir/$manifest")
    if [[ $overrides != none ]]; then
        args+=(--effect-overrides "$fixture_dir/$overrides")
    fi
    if "$lint" "${args[@]}" \
            "$fixture_dir/good-inferred-effects.cpp" -- \
            -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1; then
        printf 'effect fixture unexpectedly passed: %s\n' "$manifest" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'effect fixture did not report expected diagnostic: %s\n' \
            "$manifest" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_integrity_failure() {
    expected=$1
    shift
    output=$(mktemp)
    generated_effects=$(mktemp)
    printf '%s\n' sentinel >"$generated_effects"
    if "$lint" --audit \
            --write-effects-manifest "$generated_effects" \
            "$@" -- -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1; then
        printf '%s\n' 'manifest-integrity fixture unexpectedly passed' >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf '%s\n' \
            'manifest-integrity fixture did not report expected diagnostic' \
            >&2
        cat "$output" >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    if [[ $(<"$generated_effects") != sentinel ]]; then
        printf '%s\n' \
            'manifest-integrity failure replaced the reviewed manifest' >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    rm -f "$output" "$generated_effects"
}

check_override_integrity_failure() {
    output=$(mktemp)
    generated_effects=$(mktemp)
    printf '%s\n' sentinel >"$generated_effects"
    if "$lint" --audit --effects "$inferred_effects" \
            --effect-overrides \
                "$fixture_dir/redundant-effect-overrides.tsv" \
            --write-effects-manifest "$generated_effects" \
            "$fixture_dir/good-inferred-effects.cpp" -- \
            -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1; then
        printf '%s\n' 'invalid override unexpectedly passed audit mode' >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "override redundantly adds an inferred effect" \
            "$output" >/dev/null; then
        printf '%s\n' 'invalid override did not report its integrity error' >&2
        cat "$output" >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    if [[ $(<"$generated_effects") != sentinel ]]; then
        printf '%s\n' 'invalid override replaced the reviewed manifest' >&2
        rm -f "$output" "$generated_effects"
        exit 1
    fi
    rm -f "$output" "$generated_effects"
}

check_unclassified_audit
check_inference_failure stale-inferred-effects.tsv none \
    "external effect manifest is stale for 'inferred_may_throw'"
check_inference_failure inferred-effects.tsv redundant-effect-overrides.tsv \
    "override redundantly adds an inferred effect"
check_inference_failure inferred-effects.tsv remove-owner-effect-overrides.tsv \
    "ownership inference cannot be removed"
# 'fatal' comes only from ICU headers: an override can neither remove it nor
# add it elsewhere.
check_inference_failure inferred-effects.tsv remove-fatal-effect-overrides.tsv \
    "'fatal' is not an override component"
check_inference_failure inferred-effects.tsv add-fatal-effect-overrides.tsv \
    "'fatal' is not an override component"
check_override_integrity_failure
check_integrity_failure \
    "external manifest key identifies distinct function template specializations" \
    "$fixture_dir/specialization-collision-true.cpp" \
    "$fixture_dir/specialization-collision-false.cpp"
check_integrity_failure \
    "external call has conflicting inferred effects across translation units" \
    "$fixture_dir/inference-conflict-r.cpp" \
    "$fixture_dir/inference-conflict-cxx.cpp"

# An override may remove the C++ effect from an ICU owner, from the vendored
# headers or a system unicode/ directory, and the owner keeps its ownership.
run_lint --effects "$icu_owner_effects" \
    --effect-overrides "$icu_owner_overrides" \
    "$fixture_dir/good-icu-owner-override.cpp" -- -std=c++17 -DCHARR_LINT=1
run_lint --effects "$icu_owner_effects" \
    --effect-overrides "$icu_owner_overrides" \
    "$fixture_dir/good-icu-owner-override.cpp" -- -std=c++17 -DCHARR_LINT=1 \
    -DICU_OWNER_SYSTEM_HEADER -isystem "$fixture_dir/sysicu"
generated_effects=$(mktemp)
run_lint --audit --effects "$icu_owner_effects" \
    --effect-overrides "$icu_owner_overrides" \
    --write-effects-manifest "$generated_effects" \
    "$fixture_dir/good-icu-owner-override.cpp" -- -std=c++17 -DCHARR_LINT=1
if ! cmp "$icu_owner_effects" "$generated_effects"; then
    printf '%s\n' 'ICU owner override manifest is not stable' >&2
    rm -f "$generated_effects"
    exit 1
fi
rm -f "$generated_effects"

# check_icu_owner_failure DEFINITION EXPECTED [EXTRA_ARGS...]
check_icu_owner_failure() {
    definition=$1
    expected=$2
    shift 2
    output=$(mktemp)
    if "$lint" --effects "$icu_owner_effects" \
            --effect-overrides "$icu_owner_overrides" \
            "$fixture_dir/good-icu-owner-override.cpp" -- \
            -std=c++17 -DCHARR_LINT=1 -D"$definition" "$@" \
            >"$output" 2>&1; then
        printf 'ICU owner fixture unexpectedly passed: %s\n' \
            "$definition" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'ICU owner fixture did not report expected diagnostic: %s\n' \
            "$definition" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_icu_owner_failure ICU_OWNER_NON_ICU_HEADER \
    "override cannot remove the C++ effect implied by ownership outside an ICU header"
# A unicode/ directory that is not a system include path is not trusted.
check_icu_owner_failure ICU_OWNER_SYSTEM_HEADER \
    "override cannot remove the C++ effect implied by ownership outside an ICU header" \
    -I "$fixture_dir/sysicu"
check_icu_owner_failure ICU_OWNER_IN_NEUTRAL_HELPER \
    "neutral helper calls an ownership-returning operation"
# The rejected override is an integrity error, so audit mode keeps the
# reviewed manifest.
output=$(mktemp)
generated_effects=$(mktemp)
printf '%s\n' sentinel >"$generated_effects"
if "$lint" --audit --effects "$icu_owner_effects" \
        --effect-overrides "$icu_owner_overrides" \
        --write-effects-manifest "$generated_effects" \
        "$fixture_dir/good-icu-owner-override.cpp" -- \
        -std=c++17 -DCHARR_LINT=1 -DICU_OWNER_NON_ICU_HEADER \
        >"$output" 2>&1; then
    printf '%s\n' 'non-ICU owner override unexpectedly passed audit mode' >&2
    rm -f "$output" "$generated_effects"
    exit 1
fi
reject_compile_errors "$output" 'non-ICU owner override audit'
if [[ $(<"$generated_effects") != sentinel ]]; then
    printf '%s\n' 'non-ICU owner override replaced the reviewed manifest' >&2
    rm -f "$output" "$generated_effects"
    exit 1
fi
rm -f "$output" "$generated_effects"

# A bundled ICU appends "_charr" to its versioned C entry points and
# namespace. The external effect key drops it, so both ICU modes, vendored or
# system headers, lint cleanly against one manifest and regenerate it byte for
# byte. The same names declared outside ICU headers keep the suffix.
# check_icu_renamed_manifest EFFECTS [COMPILER_ARGS...]
check_icu_renamed_manifest() {
    effects=$1
    shift
    run_lint --effects "$effects" "$fixture_dir/good-icu-renamed.cpp" -- \
        -std=c++17 -DCHARR_LINT=1 "$@"
    generated_effects=$(mktemp)
    run_lint --audit --write-effects-manifest "$generated_effects" \
        "$fixture_dir/good-icu-renamed.cpp" -- -std=c++17 -DCHARR_LINT=1 "$@"
    if ! cmp "$effects" "$generated_effects"; then
        printf 'ICU renamed manifest is not mode independent: %s\n' "$*" >&2
        rm -f "$generated_effects"
        exit 1
    fi
    rm -f "$generated_effects"
}

icu_renamed_effects="$fixture_dir/icu-renamed-effects.tsv"
check_icu_renamed_manifest "$icu_renamed_effects"
check_icu_renamed_manifest "$icu_renamed_effects" -DICU_RENAMED_BUNDLED
check_icu_renamed_manifest "$icu_renamed_effects" \
    -DICU_RENAMED_SYSTEM_HEADER -isystem "$fixture_dir/sysicu"
check_icu_renamed_manifest "$icu_renamed_effects" -DICU_RENAMED_BUNDLED \
    -DICU_RENAMED_SYSTEM_HEADER -isystem "$fixture_dir/sysicu"
check_icu_renamed_manifest "$fixture_dir/icu-renamed-non-icu-effects.tsv" \
    -DICU_RENAMED_BUNDLED -DICU_RENAMED_NON_ICU_HEADER
output=$(mktemp)
if "$lint" --effects "$icu_renamed_effects" \
        "$fixture_dir/good-icu-renamed.cpp" -- \
        -std=c++17 -DCHARR_LINT=1 -DICU_RENAMED_BUNDLED \
        -DICU_RENAMED_NON_ICU_HEADER >"$output" 2>&1; then
    printf '%s\n' 'non-ICU renamed name unexpectedly matched ICU rows' >&2
    rm -f "$output"
    exit 1
fi
reject_compile_errors "$output" 'non-ICU renamed name'
if ! grep -F \
        "calls unreviewed external function 'icu_renamed_open_78_charr'" \
        "$output" >/dev/null; then
    printf '%s\n' 'non-ICU renamed name did not keep its suffix' >&2
    cat "$output" >&2
    rm -f "$output"
    exit 1
fi
rm -f "$output"

# A compilation database names sources relative to its directory, so a
# header is reached as "sub/../shared/..."; it is still charr-owned and
# reported under its real path.
relative_db=$(mktemp -d)
cat >"$relative_db/compile_commands.json" <<JSON
[{"directory": "$fixture_dir/relative/src",
  "file": "$fixture_dir/relative/src/sub/bad-relative-header.cpp",
  "arguments": ["clang++", "-std=c++17", "-c",
                "sub/bad-relative-header.cpp"]}]
JSON
output=$(mktemp)
if "$lint" -p "$relative_db" \
        "$fixture_dir/relative/src/sub/bad-relative-header.cpp" \
        >"$output" 2>&1; then
    printf '%s\n' 'relative-path header fixture unexpectedly passed' >&2
    rm -rf "$output" "$relative_db"
    exit 1
fi
reject_compile_errors "$output" 'relative-path header'
if ! grep -F \
        "$fixture_dir/relative/src/shared/bad-relative-header.h:17:12: error: C++ helper calls fallible R operation 'relative_r_value'" \
        "$output" >/dev/null; then
    printf '%s\n' 'relative-path header was not linted under its real path' >&2
    cat "$output" >&2
    rm -rf "$output" "$relative_db"
    exit 1
fi
rm -rf "$output" "$relative_db"

check_failure bad-cxx-calls-r.cpp \
    "C++ helper calls fallible R operation"
check_failure bad-header-cxx-calls-r.cpp \
    "C++ helper calls fallible R operation"
check_failure bad-r-owner.cpp \
    "R helper stores cleanup-bearing local"
check_failure bad-entry-owner.cpp \
    "is owned by the unwind region"
check_failure bad-entry-owner-prelude.cpp \
    "is outside the entry point's owner region"
check_failure bad-entry-owner-after-unwind.cpp \
    "must be declared before the primary unwind boundary"
check_failure bad-entry-cxx-call-outside.cpp \
    "appears outside the entry point's owner try block"
check_failure bad-entry-throw-outside.cpp \
    "entry point contains a C++ throw outside its owner try block"
check_failure bad-entry-lifetime-owner.cpp \
    "cleanup-bearing temporary has an unwind-region lifetime"
check_failure bad-entry-r-outside.cpp \
    "appears outside the unwind region"
check_failure bad-entry-r-constructor.cpp \
    "fallible R construction creates an owner in the unwind region"
check_failure bad-entry-owner-in-r-call.cpp \
    "ownership-returning expression is nested in a fallible R call"
check_failure bad-r-new.cpp \
    "R helper contains a native allocation expression"
check_failure bad-r-owner-parameter.cpp \
    "R helper owns cleanup-bearing parameter"
check_failure bad-r-owner-return.cpp \
    "R helper 'bad_r_helper' returns a cleanup-bearing value"
check_failure bad-neutral-calls-cxx.cpp \
    "neutral helper calls C++ helper"
check_failure bad-reader-access-before-reset.cpp \
    "access is not dominated by its reset"
check_failure bad-reader-conditional-reset.cpp \
    "access is not dominated by its reset"
check_failure bad-reader-nondefault.cpp \
    "must be default constructed in the owner region"
check_failure bad-reader-vector-no-reset.cpp \
    "must have exactly one reset in the unwind region"
check_failure bad-unclassified.cpp \
    "has no lint role"
check_failure bad-unclassified-call.cpp \
    "calls unclassified charr function"
check_failure bad-unreviewed-external.cpp \
    "calls unreviewed external function 'raw_open'"
check_failure bad-r-extern-c-may-throw.cpp \
    "R helper calls potentially throwing operation 'external_cxx_operation'"
check_failure bad-trusted-unreviewed-external.cpp \
    "calls unreviewed external function 'unreviewed_unwind_operation'"
check_failure bad-indirect-call.cpp \
    "contains an indirect or unresolved call"
check_failure bad-dependent-adl-call.cpp \
    "contains an indirect or unresolved call"
check_failure bad-dependent-template-overload.cpp \
    "contains an indirect or unresolved call"
check_resource_failure bad-r-raw-acquire.cpp \
    "R helper directly calls raw resource acquisition 'raw_open'"
check_resource_failure bad-neutral-raw-release.cpp \
    "neutral helper directly calls raw resource release 'raw_close'"
check_resource_failure bad-entry-resource-in-unwind.cpp \
    "entry point directly calls raw resource acquisition 'raw_open'"
check_resource_failure bad-entry-resource-in-unwind.cpp \
    "entry point directly calls raw resource release 'raw_close'"
check_resource_failure bad-entry-resource-owner-try.cpp \
    "entry point directly calls raw resource acquisition 'raw_open'"
check_resource_failure bad-entry-resource-prelude.cpp \
    "entry point directly calls raw resource acquisition 'raw_open'"
check_resource_failure bad-constructor-init-resource.cpp \
    "R helper directly calls raw resource acquisition 'raw_open'"
check_r_failure bad-entry-unprotected-token.cpp \
    "continuation token must be protected by entry_protections.protect_one"
check_r_failure bad-entry-missing-result-slot.cpp \
    "must have exactly one entry_protections.protect_with_index()"
check_r_failure bad-entry-raw-callback-protect.cpp \
    "entry point uses raw R protection operation"
check_r_failure bad-entry-raw-reprotect-result.cpp \
    "entry point uses raw R protection operation"
check_r_failure bad-entry-conditional-callback-release.cpp \
    "normal unwind-callback return is not dominated by ProtHelper::release_all()"
check_r_failure bad-entry-helper-after-release.cpp \
    "ProtHelper operation appears after the callback's release_all()"
check_r_failure bad-entry-reprotect-slot-mismatch.cpp \
    "must assign the variable initialized for its PROTECT_INDEX"
check_r_failure bad-entry-reprotect-slot-missing.cpp \
    "must use a PROTECT_INDEX initialized by exactly one callback protect_with_index()"
check_r_failure bad-entry-wrong-protection-domain.cpp \
    "must use callback_protections"
check_r_failure bad-abi-shim-shape.cpp \
    "ABI shim body must contain one direct return call"
check_r_variant BAD_CALLBACK_CLEAR \
    "ProtHelper::clear() is forbidden in an entry point"
check_r_variant BAD_R_ERROR_RELEASE \
    "R-error continuation branch must not release either protection domain"
check_r_variant BAD_BEFORE_R_RELEASE \
    "pending R error must be continued before postlude R calls or protection cleanup"
check_r_variant BAD_BEFORE_R_CALLBACK_RELEASE \
    "pending R error must be continued before postlude R calls or protection cleanup"
check_r_variant BAD_BEFORE_R_CALL \
    "pending R error must be continued before postlude R calls or protection cleanup"
check_r_variant BAD_R_ERROR_OTHER_R_CALL \
    "R-error continuation branch must not make another fallible R call"
check_r_variant BAD_R_ERROR_TOKEN \
    "R-error branch must continue the trusted unwind token"
check_r_variant BAD_CPP_MISSING_RELEASE \
    "C++-error branch must release callback_protections exactly once"
check_r_variant BAD_CPP_DEPTH \
    "C++-error branch must use entry_protections instead of raw UNPROTECT"
check_r_variant BAD_SUCCESS_DEPTH \
    "successful return must be dominated by one entry_protections.release_all()"
check_r_variant BAD_HELPER_NAME \
    "SEXP entry point is missing callback_protections"
check_r_variant BAD_HELPER_NAME \
    "ProtHelper must have the semantic role name"
check_r_variant BAD_EXTRA_RETURN \
    "SEXP entry point must have one final successful return"
check_r_variant BAD_CPP_MISSING_ENTRY_RELEASE \
    "C++-error branch must release entry_protections exactly once"
check_r_variant BAD_POSTLUDE_AFTER_RELEASE \
    "successful-path postlude R calls must precede entry protection cleanup"
check_r_variant BAD_PRELUDE_UNPROTECT \
    "prelude protections must remain until the owner region has ended"
check_r_variant BAD_KEEP_RESULT_MISSING \
    "SEXP entry point must contain exactly one CHARR_UNWIND_KEEP_RESULT()"
check_r_variant BAD_KEEP_RESULT_POSTLUDE \
    "CHARR_UNWIND_KEEP_RESULT() must be the owner-try statement immediately after the result = unwind_protect(...) assignment"
check_r_variant BAD_KEEP_RESULT_VARIABLE \
    "CHARR_UNWIND_KEEP_RESULT() must re-protect the stable result variable with its PROTECT_INDEX"
check_r_failure bad-entry-prothelper-destructor.cpp \
    "ProtHelper must remain trivially destructible"

# Helper contracts.
check_variant project bad-helper-contract.cpp BAD_R_THROW \
    "R helper contains a C++ throw expression"
check_variant project bad-helper-contract.cpp BAD_NEUTRAL_THROW \
    "neutral helper contains a C++ throw expression"
check_variant project bad-helper-contract.cpp BAD_R_DELETE \
    "R helper contains a native deallocation expression"
check_variant project bad-helper-contract.cpp BAD_NEUTRAL_DELETE \
    "neutral helper contains a native deallocation expression"
check_variant project bad-helper-contract.cpp BAD_R_MAY_THROW \
    "R helper 'r_may_throw' must be noexcept"
check_variant project bad-helper-contract.cpp BAD_NEUTRAL_MAY_THROW \
    "neutral helper 'neutral_may_throw' must be noexcept"
check_variant project bad-helper-contract.cpp BAD_ENTRY_MAY_THROW \
    "entry point 'entry_may_throw' must be noexcept"
check_variant project bad-helper-contract.cpp BAD_CONFLICTING_ROLES \
    "function 'conflicting' has conflicting lint roles"
check_variant project bad-helper-contract.cpp BAD_CXX_CALLS_ENTRY \
    "C++ helper calls entry point 'entry_target'"
check_variant project bad-helper-contract.cpp BAD_NEUTRAL_CALLS_ENTRY \
    "neutral helper calls entry point 'entry_target'"
check_variant project bad-helper-contract.cpp BAD_UNWIND_FROM_HELPER \
    "trusted unwind intrinsic may be called only by an entry point"
check_variant project bad-helper-contract.cpp BAD_ENTRY_NO_UNWIND \
    "entry point 'entry_no_unwind' must contain exactly one trusted unwind call"
check_variant project bad-helper-contract.cpp BAD_ENTRY_TWO_UNWINDS \
    "entry point 'entry_two_unwinds' must contain exactly one trusted unwind call"
check_variant project bad-helper-contract.cpp BAD_ENTRY_NOT_SEXP \
    "entry point 'entry_not_sexp' must return SEXP"
check_variant project bad-helper-contract.cpp BAD_TRUSTED_OUTSIDE_UNWIND_H \
    "trusted unwind intrinsic 'local_unwind' must be defined in src/shared/unwind.h"
check_variant project bad-helper-contract.cpp BAD_NEUTRAL_LONGJMP \
    "neutral helper calls 'longjmp'; only a trusted unwind intrinsic may use setjmp or longjmp"
check_variant project bad-helper-contract.cpp BAD_CXX_SETJMP \
    "C++ helper calls '_setjmp'; only a trusted unwind intrinsic may use setjmp or longjmp"
check_variant project bad-helper-contract.cpp BAD_NONLOCAL_LAMBDA_CALL \
    "C++ helper calls unclassified charr function '(anonymous class)::operator()'"

# The ICU fatal handler: its reviewed definition passes; the role demands
# [[noreturn]], a declaration that may throw, and the reviewed files; and no
# charr function may call it or take its address.
run_lint --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/src/shared/icu_fatal.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
check_variant project src/shared/icu_fatal.cpp BAD_HANDLER_RETURNS \
    "ICU fatal handler 'returning_handler' must be [[noreturn]]"
check_variant project src/shared/icu_fatal.cpp BAD_HANDLER_NOEXCEPT \
    "ICU fatal handler 'noexcept_handler' must not be noexcept"
check_variant plain bad-icu-fatal-handler.cpp - \
    "C++ helper calls ICU fatal handler 'charr::shared::icu_invariant_failure'; only ICU reaches it"
check_variant plain bad-icu-fatal-handler.cpp BAD_HANDLER_ADDRESS \
    "neutral helper refers to ICU fatal handler 'charr::shared::icu_invariant_failure'"
check_variant plain bad-icu-fatal-handler.cpp BAD_HANDLER_OUTSIDE_FILE \
    "ICU fatal handler 'stray_handler' must be declared in src/shared/icu_fatal.h"

# ICU fatal sites are counted after preprocessing, keyed by the file that
# spells the outermost macro, and compared with a reviewed manifest.
fatal_site_units=(
    "$fixture_dir/src/icu78/fatal-site-a.cpp"
    "$fixture_dir/src/icu78/fatal-site-b.cpp"
)
run_lint --fatal-sites "$fixture_dir/fatal-sites.tsv" \
    "${fatal_site_units[@]}" -- -std=c++17 -DCHARR_LINT=1
generated_sites=$(mktemp)
cp "$fixture_dir/fatal-sites.tsv" "$generated_sites"
run_lint --fatal-sites "$generated_sites" --write-fatal-sites \
    "${fatal_site_units[@]}" -- -std=c++17 -DCHARR_LINT=1
if ! cmp "$fixture_dir/fatal-sites.tsv" "$generated_sites"; then
    printf '%s\n' 'ICU fatal-site manifest is not stable' >&2
    rm -f "$generated_sites"
    exit 1
fi
# Rewriting a stale manifest drops the missing row and clears the kind of a
# row whose count changed, so the next check fails until it is reviewed.
cp "$fixture_dir/stale-fatal-sites.tsv" "$generated_sites"
run_lint --fatal-sites "$generated_sites" --write-fatal-sites \
    "${fatal_site_units[@]}" -- -std=c++17 -DCHARR_LINT=1
if ! grep -F $'icu::two_sites(int)\t2\t\tFixture' "$generated_sites" \
        >/dev/null || grep -F 'removed.cpp' "$generated_sites" >/dev/null; then
    printf '%s\n' 'ICU fatal-site rewrite kept a stale review' >&2
    cat "$generated_sites" >&2
    rm -f "$generated_sites"
    exit 1
fi
rm -f "$generated_sites"

# check_fatal_sites_failure MANIFEST DEFINITION EXPECTED
check_fatal_sites_failure() {
    manifest=$1
    definition=$2
    expected=$3
    output=$(mktemp)
    defines=()
    if [[ $definition != - ]]; then
        defines+=(-D"$definition")
    fi
    if "$lint" --fatal-sites "$fixture_dir/$manifest" \
            "${fatal_site_units[@]}" -- -std=c++17 -DCHARR_LINT=1 \
            "${defines[@]}" >"$output" 2>&1; then
        printf 'ICU fatal-site fixture unexpectedly passed: %s %s\n' \
            "$manifest" "$definition" >&2
        rm -f "$output"
        exit 1
    fi
    reject_compile_errors "$output" "$*"
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'ICU fatal-site fixture did not report expected diagnostic: %s %s\n' \
            "$manifest" "$definition" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

check_fatal_sites_failure stale-fatal-sites.tsv - \
    "ICU fatal site count changed from 1 to 2: src/icu78/fatal-site-a.cpp"
check_fatal_sites_failure stale-fatal-sites.tsv - \
    "ICU fatal site row no longer observed: src/icu78/removed.cpp"
check_fatal_sites_failure unreviewed-fatal-sites.tsv - \
    "ICU fatal site row has no reviewed kind: src/icu78/fatal-site.h"
check_fatal_sites_failure fatal-sites.tsv BAD_DIRECT_CALL \
    "ICU fatal handler is referred to other than through a src/uconfig_local.h macro"
check_fatal_sites_failure fatal-sites.tsv BAD_NOEXCEPT_SITE \
    "ICU fatal site in 'noexcept_site()', which cannot throw"
check_fatal_sites_failure fatal-sites.tsv BAD_NOEXCEPT_LAMBDA_SITE \
    "ICU fatal site in 'noexcept_lambda_site(int)' (in a lambda), which cannot throw"

# Entry-point owner and unwind regions.
check_variant project bad-entry-unwind-region.cpp BAD_TEMPORARY_BEFORE_TRY \
    "cleanup-bearing temporary is outside the entry point's owner region"
check_variant project bad-entry-unwind-region.cpp BAD_TEMPORARY_AFTER_UNWIND \
    "cleanup-bearing temporary must precede the primary unwind boundary"
check_variant project bad-entry-unwind-region.cpp BAD_NEW_IN_UNWIND \
    "unwind region contains a native allocation expression"
check_variant project bad-entry-unwind-region.cpp BAD_DELETE_IN_UNWIND \
    "unwind region contains a native deallocation expression"
check_variant project bad-entry-unwind-region.cpp BAD_READER_INDIRECT \
    "Reader methods must use a direct owner-region variable or its indexed Reader vector"
check_variant project bad-entry-unwind-region.cpp BAD_READER_OUTSIDE_UNWIND \
    "Reader 'reader' is used outside the unwind region"

# ABI shim contract.
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_LINKAGE \
    'ABI shim must have extern "C" linkage'
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_NOEXCEPT \
    "ABI shim must be noexcept"
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_PARAMETER \
    "ABI shim parameters must all be SEXP"
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_RETURN \
    "ABI shim must return SEXP"
check_variant project bad-abi-shim-contract.cpp BAD_CXX_CALLS_SHIM \
    "C++ helper calls fallible R operation 'C_abi_target'"

# Reviewed effects: PRINTNAME and Rf_isInteger can signal, and
# Rf_unprotect is confined by the raw protection rule.
check_variant project bad-neutralized-r.cpp - \
    "C++ helper calls fallible R operation 'PRINTNAME'"
check_variant project bad-neutralized-r.cpp - \
    "C++ helper calls fallible R operation 'Rf_isInteger'"
check_variant project bad-neutralized-r.cpp - \
    "C++ helper uses raw R protection operation 'Rf_unprotect'"

# A noexcept specification is checked against the body.
check_variant plain bad-noexcept-cxx-throw.cpp - \
    "noexcept C++ helper 'grow' contains a C++ throw outside a catch (...) try block"
check_variant plain bad-noexcept-cxx-throw.cpp - \
    "noexcept C++ helper 'grow' calls potentially throwing operation 'std::vector<int>::push_back'"
check_variant plain bad-noexcept-cxx-throw.cpp BAD_THROWING_HELPER \
    "noexcept C++ helper 'calls_may_throw' calls potentially throwing operation 'may_throw'"
check_variant plain bad-noexcept-cxx-throw.cpp BAD_THROWING_NEW \
    "noexcept C++ helper 'allocate' contains a throwing allocation"

# Virtual overrides keep the contract of every method they override.
check_variant plain bad-override-role.cpp - \
    "R helper 'Derived::run' overrides C++ helper virtual 'Base::run' with an incompatible role"
check_variant plain bad-override-role.cpp BAD_INDIRECT_OVERRIDE \
    "R helper 'Leaf::run' overrides C++ helper virtual 'Base::run' with an incompatible role"
check_variant plain bad-override-role.cpp BAD_CXX_OVER_NEUTRAL \
    "C++ helper 'CxxOverride::value' overrides neutral helper virtual 'NeutralBase::value'"
check_variant plain bad-override-role.cpp BAD_EXTERNAL_BASE \
    "overrides external virtual 'std::exception::what' whose effect 'neutral' does not permit it"
check_variant plain bad-override-role.cpp BAD_EXTERNAL_BASE \
    "R helper 'Error::what' overrides external virtual 'std::exception::what'"

# Raw protection must balance inside the R helper that pushes it.
check_variant project bad-hidden-protect.cpp - \
    "R helper 'hide_protect' returns with unbalanced raw R protection"
check_variant project bad-raw-protection.cpp BAD_EXTRA_UNPROTECT \
    "R helper 'extra_unprotect_r' releases raw R protections it did not push"
check_variant project bad-raw-protection.cpp BAD_UNKNOWN_COUNT \
    "neither a constant nor a local counter with a known value"
check_variant project bad-raw-protection.cpp BAD_UNKNOWN_COUNT \
    "uses a raw UNPROTECT count that is neither a constant nor a local counter"
check_variant project bad-raw-protection.cpp BAD_BRANCH_LEAK \
    "R helper 'branch_leak_r' returns with unbalanced raw R protection"
check_variant project bad-raw-protection.cpp BAD_LOOP_GROWTH \
    "R helper 'loop_growth_r' grows raw R protection without a bound"
check_variant project bad-raw-protection.cpp BAD_LAMBDA_PROTECT \
    "raw R protection inside a lambda cannot be balanced"
check_variant project bad-raw-protection.cpp BAD_PROTHELPER_LOCAL \
    "R helper declares a ProtHelper; protection domains belong to entry points"
check_variant project bad-raw-protection.cpp BAD_PRESERVE \
    "R helper uses 'R_PreserveObject'"
check_variant project bad-raw-protection.cpp BAD_NEUTRAL_UNPROTECT \
    "neutral helper uses raw R protection operation 'Rf_unprotect'"

# Readers live only in direct entry-point locals or std::vector.
check_variant project bad-reader-array.cpp - \
    "charport::Reader holder 'readers' must be a direct entry-point local Reader"
check_variant project bad-reader-field.cpp - \
    "member 'reader' stores a charport::Reader"
check_variant project bad-reader-helper.cpp - \
    "parameter 'reader' passes a charport::Reader to a helper"
check_variant project bad-reader-helper.cpp - \
    "Reader method 'charport::Reader::size' is called outside an entry point's unwind region"

# Default arguments and dynamic initializers run code with no role.
check_variant plain bad-default-argument-r.cpp - \
    "default argument of parameter 'value' calls fallible R operation 'Rf_allocVector'"
check_variant plain bad-dynamic-initializer-r.cpp - \
    "dynamic initializer of 'leaked' calls fallible R operation 'Rf_allocVector'"
check_variant plain bad-dynamic-initializer-r.cpp BAD_THROWING_INITIALIZER \
    "dynamic initializer of 'loaded' calls potentially throwing operation"

# Callbacks run inside the function that receives them.
check_variant plain bad-external-callback.cpp - \
    "passes R helper 'touches_r' as a callback to external function 'run_callback'"
check_variant plain bad-external-callback.cpp BAD_ESCAPED_POINTER \
    "charr function 'neutral_callback' escapes as a function pointer"
check_variant plain bad-external-callback.cpp BAD_LAMBDA_CALLBACK \
    "lambda passed to external function 'run_functor' calls 'Rf_error'"
