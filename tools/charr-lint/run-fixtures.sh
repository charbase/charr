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

clang++ -std=c++17 -fsyntax-only -I/usr/share/R/include \
    "$fixture_dir/shared-foundation.cpp"

"$lint" "$fixture_dir/good.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" "$fixture_dir/good-dependent-template-call.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
"$lint" "$fixture_dir/good-reader.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" "$fixture_dir/good-reader-vector.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
"$lint" --effects "$resource_effects" \
    --effect-overrides "$resource_overrides" \
    "$fixture_dir/good-resource-owner.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" --effects "$inferred_effects" \
    "$fixture_dir/good-inferred-effects.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
generated_effects=$(mktemp)
"$lint" --audit --effects "$inferred_effects" \
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
"$lint" --effects "$reviewed_c_api_effects" \
    "$fixture_dir/good-reviewed-c-api.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -isystem "$fixture_dir/sysapi"
generated_effects=$(mktemp)
"$lint" --audit --effects "$reviewed_c_api_effects" \
    --write-effects-manifest "$generated_effects" \
    "$fixture_dir/good-reviewed-c-api.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -isystem "$fixture_dir/sysapi"
if ! cmp "$reviewed_c_api_effects" "$generated_effects"; then
    printf '%s\n' 'reviewed C API manifest is not stable' >&2
    rm -f "$generated_effects"
    exit 1
fi
rm -f "$generated_effects"
"$lint" --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-protection.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
"$lint" --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-reprotect-slot.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
"$lint" --effects "$project_effects" \
    --effect-overrides "$project_overrides" \
    "$fixture_dir/good-abi-shim.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include

check_failure() {
    file=$1
    expected=$2
    output=$(mktemp)
    if "$lint" --effects "$project_effects" \
            --effect-overrides "$project_overrides" \
            "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$file" >&2
        rm -f "$output"
        exit 1
    fi
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
    if "$lint" --effects "$resource_effects" \
            --effect-overrides "$resource_overrides" \
            "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s\n' "$file" >&2
        rm -f "$output"
        exit 1
    fi
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
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture variant did not report expected diagnostic: %s\n' \
            "$definition" >&2
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
check_override_integrity_failure
check_integrity_failure \
    "external manifest key identifies distinct function template specializations" \
    "$fixture_dir/specialization-collision-true.cpp" \
    "$fixture_dir/specialization-collision-false.cpp"
check_integrity_failure \
    "external call has conflicting inferred effects across translation units" \
    "$fixture_dir/inference-conflict-r.cpp" \
    "$fixture_dir/inference-conflict-cxx.cpp"

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
    "successful return must be dominated by one entry_protections.release_all()"
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
