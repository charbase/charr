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
"$lint" "$fixture_dir/good-contracts.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
# Variant fixtures must pass without a variant so that each failure below is
# caused by the variant alone.
"$lint" "$fixture_dir/bad-helper-contract.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" "$fixture_dir/bad-entry-unwind-region.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
for variant_base in bad-abi-shim-contract.cpp bad-raw-protection.cpp \
        bad-entry-protection-branch.cpp; do
    "$lint" --effects "$project_effects" \
        --effect-overrides "$project_overrides" \
        "$fixture_dir/$variant_base" -- \
        -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
done

# Variant fixtures pass without a -D definition, so each failure below is
# caused by the definition it names.
"$lint" "$fixture_dir/good-contracts.cpp" -- \
    -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
"$lint" "$fixture_dir/bad-helper-contract.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" "$fixture_dir/bad-entry-unwind-region.cpp" -- \
    -std=c++17 -DCHARR_LINT=1
for variant_base in bad-entry-protection-branch.cpp \
        bad-abi-shim-contract.cpp bad-raw-protection.cpp; do
    "$lint" --effects "$project_effects" \
        --effect-overrides "$project_overrides" \
        "$fixture_dir/$variant_base" -- \
        -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include
done

# check_variant MODE FILE DEFINITION EXPECTED
# MODE is 'plain' (inferred effects only) or 'project' (the reviewed project
# manifest and overrides). DEFINITION 'none' compiles the file unchanged.
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
    if [[ $definition != none ]]; then
        defines+=(-D"$definition")
    fi
    if "$lint" "${args[@]}" "$fixture_dir/$file" -- \
            -std=c++17 -DCHARR_LINT=1 -I/usr/share/R/include \
            "${defines[@]}" >"$output" 2>&1; then
        printf 'fixture unexpectedly passed: %s %s\n' \
            "$file" "$definition" >&2
        rm -f "$output"
        exit 1
    fi
    if ! grep -F "$expected" "$output" >/dev/null; then
        printf 'fixture did not report expected diagnostic: %s %s\n' \
            "$file" "$definition" >&2
        cat "$output" >&2
        rm -f "$output"
        exit 1
    fi
    rm -f "$output"
}

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

# An override may remove the C++ effect from an ICU owner, from the vendored
# headers or a system unicode/ directory, and the owner keeps its ownership.
"$lint" --effects "$icu_owner_effects" \
    --effect-overrides "$icu_owner_overrides" \
    "$fixture_dir/good-icu-owner-override.cpp" -- -std=c++17 -DCHARR_LINT=1
"$lint" --effects "$icu_owner_effects" \
    --effect-overrides "$icu_owner_overrides" \
    "$fixture_dir/good-icu-owner-override.cpp" -- -std=c++17 -DCHARR_LINT=1 \
    -DICU_OWNER_SYSTEM_HEADER -isystem "$fixture_dir/sysicu"
generated_effects=$(mktemp)
"$lint" --audit --effects "$icu_owner_effects" \
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
if [[ $(<"$generated_effects") != sentinel ]]; then
    printf '%s\n' 'non-ICU owner override replaced the reviewed manifest' >&2
    rm -f "$output" "$generated_effects"
    exit 1
fi
rm -f "$output" "$generated_effects"

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
check_r_failure bad-entry-prothelper-destructor.cpp \
    "ProtHelper must remain trivially destructible"

# Helper contracts.
check_variant plain bad-helper-contract.cpp BAD_R_THROW \
    "R helper contains a C++ throw expression"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_THROW \
    "neutral helper contains a C++ throw expression"
check_variant plain bad-helper-contract.cpp BAD_R_DELETE \
    "R helper contains a native deallocation expression"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_DELETE \
    "neutral helper contains a native deallocation expression"
check_variant plain bad-helper-contract.cpp BAD_R_MAY_THROW \
    "R helper 'r_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_MAY_THROW \
    "neutral helper 'neutral_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_MAY_THROW \
    "entry point 'entry_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_CONFLICTING_ROLES \
    "function 'conflicting' has conflicting lint roles"
check_variant plain bad-helper-contract.cpp BAD_CXX_CALLS_ENTRY \
    "C++ helper calls entry point 'entry_target'"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_CALLS_ENTRY \
    "neutral helper calls entry point 'entry_target'"
check_variant plain bad-helper-contract.cpp BAD_UNWIND_FROM_HELPER \
    "trusted unwind intrinsic may be called only by an entry point"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_NO_UNWIND \
    "entry point 'entry_no_unwind' must contain exactly one trusted unwind call"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_TWO_UNWINDS \
    "entry point 'entry_two_unwinds' must contain exactly one trusted unwind call"

# Entry-point owner and unwind regions.
check_variant plain bad-entry-unwind-region.cpp BAD_TEMPORARY_BEFORE_TRY \
    "cleanup-bearing temporary is outside the entry point's owner region"
check_variant plain bad-entry-unwind-region.cpp BAD_TEMPORARY_AFTER_UNWIND \
    "cleanup-bearing temporary must precede the primary unwind boundary"
check_variant plain bad-entry-unwind-region.cpp BAD_NEW_IN_UNWIND \
    "unwind region contains a native allocation expression"
check_variant plain bad-entry-unwind-region.cpp BAD_DELETE_IN_UNWIND \
    "unwind region contains a native deallocation expression"
check_variant plain bad-entry-unwind-region.cpp BAD_READER_INDIRECT \
    "Reader methods must use a direct owner-region variable or its indexed Reader vector"
check_variant plain bad-entry-unwind-region.cpp BAD_READER_OUTSIDE_UNWIND \
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
    "C++ helper 'CxxOverride::value' overrides neutral helper virtual"
check_variant plain bad-override-role.cpp BAD_EXTERNAL_BASE \
    "overrides external virtual 'std::exception::what' whose effect 'neutral' does not permit it"

# Raw protection must balance inside the R helper that pushes it.
check_variant project bad-hidden-protect.cpp - \
    "R helper 'hide_protect' returns with unbalanced raw R protection"
check_variant project bad-raw-protection.cpp BAD_EXTRA_UNPROTECT \
    "releases raw R protections it did not push"
check_variant project bad-raw-protection.cpp BAD_UNKNOWN_COUNT \
    "neither a constant nor a local counter with a known value"
check_variant project bad-raw-protection.cpp BAD_BRANCH_LEAK \
    "R helper 'branch_leak_r' returns with unbalanced raw R protection"
check_variant project bad-raw-protection.cpp BAD_LOOP_GROWTH \
    "grows raw R protection without a bound"
check_variant project bad-raw-protection.cpp BAD_LAMBDA_PROTECT \
    "raw R protection inside a lambda cannot be balanced"
check_variant project bad-raw-protection.cpp BAD_PROTHELPER_LOCAL \
    "R helper declares a ProtHelper"
check_variant project bad-raw-protection.cpp BAD_PRESERVE \
    "R helper uses 'R_PreserveObject'"
check_variant project bad-raw-protection.cpp BAD_NEUTRAL_UNPROTECT \
    "neutral helper uses raw R protection operation 'Rf_unprotect'"

# Readers live only in direct entry-point locals or std::vector.
check_variant plain bad-reader-array.cpp - \
    "charport::Reader holder 'readers' must be a direct entry-point local"
check_variant plain bad-reader-field.cpp - \
    "member 'reader' stores a charport::Reader"
check_variant plain bad-reader-helper.cpp - \
    "parameter 'reader' passes a charport::Reader to a helper"
check_variant plain bad-reader-helper.cpp - \
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

# Helper and entry-point contracts that had no negative fixture.
check_variant plain bad-helper-contract.cpp BAD_R_THROW \
    "R helper contains a C++ throw expression"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_THROW \
    "neutral helper contains a C++ throw expression"
check_variant plain bad-helper-contract.cpp BAD_R_DELETE \
    "R helper contains a native deallocation expression"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_DELETE \
    "neutral helper contains a native deallocation expression"
check_variant plain bad-helper-contract.cpp BAD_R_MAY_THROW \
    "R helper 'r_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_MAY_THROW \
    "neutral helper 'neutral_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_MAY_THROW \
    "entry point 'entry_may_throw' must be noexcept"
check_variant plain bad-helper-contract.cpp BAD_CONFLICTING_ROLES \
    "function 'conflicting' has conflicting lint roles"
check_variant plain bad-helper-contract.cpp BAD_CXX_CALLS_ENTRY \
    "C++ helper calls entry point 'entry_target'"
check_variant plain bad-helper-contract.cpp BAD_NEUTRAL_CALLS_ENTRY \
    "neutral helper calls entry point 'entry_target'"
check_variant plain bad-helper-contract.cpp BAD_UNWIND_FROM_HELPER \
    "trusted unwind intrinsic may be called only by an entry point"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_NO_UNWIND \
    "entry point 'entry_no_unwind' must contain exactly one trusted unwind call"
check_variant plain bad-helper-contract.cpp BAD_ENTRY_TWO_UNWINDS \
    "entry point 'entry_two_unwinds' must contain exactly one trusted unwind call"
check_variant plain bad-entry-unwind-region.cpp BAD_TEMPORARY_BEFORE_TRY \
    "cleanup-bearing temporary is outside the entry point's owner region"
check_variant plain bad-entry-unwind-region.cpp BAD_TEMPORARY_AFTER_UNWIND \
    "cleanup-bearing temporary must precede the primary unwind boundary"
check_variant plain bad-entry-unwind-region.cpp BAD_NEW_IN_UNWIND \
    "unwind region contains a native allocation expression"
check_variant plain bad-entry-unwind-region.cpp BAD_DELETE_IN_UNWIND \
    "unwind region contains a native deallocation expression"
check_variant plain bad-entry-unwind-region.cpp BAD_READER_INDIRECT \
    "Reader methods must use a direct owner-region variable or its indexed Reader vector"
check_variant plain bad-entry-unwind-region.cpp BAD_READER_OUTSIDE_UNWIND \
    "Reader 'reader' is used outside the unwind region"
check_variant plain bad-entry-prothelper-destructor.cpp none \
    "ProtHelper must remain trivially destructible"
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_LINKAGE \
    'ABI shim must have extern "C" linkage'
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_NOEXCEPT \
    "ABI shim must be noexcept"
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_PARAMETER \
    "ABI shim parameters must all be SEXP"
check_variant project bad-abi-shim-contract.cpp BAD_SHIM_RETURN \
    "ABI shim must return SEXP"

# Effect table: these R entry points can signal, so a C++ helper may not
# call them, and a C++ helper may not write the protection stack.
check_variant project bad-neutralized-r.cpp none \
    "C++ helper calls fallible R operation 'PRINTNAME'"
check_variant project bad-neutralized-r.cpp none \
    "C++ helper calls fallible R operation 'Rf_isInteger'"
check_variant project bad-neutralized-r.cpp none \
    "C++ helper uses raw R protection operation 'Rf_unprotect'"

# noexcept is checked against the body.
check_variant plain bad-noexcept-cxx-throw.cpp none \
    "noexcept C++ helper 'grow' contains a C++ throw outside a catch (...) try block"
check_variant plain bad-noexcept-cxx-throw.cpp none \
    "noexcept C++ helper 'grow' calls potentially throwing operation 'std::vector<int>::push_back'"
check_variant plain bad-noexcept-cxx-throw.cpp BAD_THROWING_HELPER \
    "noexcept C++ helper 'calls_may_throw' calls potentially throwing operation 'may_throw'"
check_variant plain bad-noexcept-cxx-throw.cpp BAD_THROWING_NEW \
    "noexcept C++ helper 'allocate' contains a throwing allocation"

# Overrides must honor the contract of every virtual they override.
check_variant plain bad-override-role.cpp none \
    "R helper 'Derived::run' overrides C++ helper virtual 'Base::run' with an incompatible role"
check_variant plain bad-override-role.cpp BAD_INDIRECT_OVERRIDE \
    "R helper 'Leaf::run' overrides C++ helper virtual 'Base::run' with an incompatible role"
check_variant plain bad-override-role.cpp BAD_CXX_OVER_NEUTRAL \
    "C++ helper 'CxxOverride::value' overrides neutral helper virtual 'NeutralBase::value'"
check_variant plain bad-override-role.cpp BAD_EXTERNAL_BASE \
    "R helper 'Error::what' overrides external virtual 'std::exception::what'"

# Raw protection must balance inside the R helper that pushes it.
check_variant project bad-hidden-protect.cpp none \
    "R helper 'hide_protect' returns with unbalanced raw R protection"
check_variant project bad-raw-protection.cpp BAD_EXTRA_UNPROTECT \
    "R helper 'extra_unprotect_r' releases raw R protections it did not push"
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
check_variant plain bad-reader-array.cpp none \
    "charport::Reader holder 'readers' must be a direct entry-point local Reader"
check_variant plain bad-reader-field.cpp none \
    "member 'reader' stores a charport::Reader"
check_variant plain bad-reader-helper.cpp none \
    "parameter 'reader' passes a charport::Reader to a helper"
check_variant plain bad-reader-helper.cpp none \
    "Reader method 'charport::Reader::size' is called outside an entry point's unwind region"

# Default arguments and dynamic initializers run without a role.
check_variant plain bad-default-argument-r.cpp none \
    "default argument of parameter 'value' calls fallible R operation 'Rf_allocVector'"
check_variant plain bad-dynamic-initializer-r.cpp none \
    "dynamic initializer of 'leaked' calls fallible R operation 'Rf_allocVector'"
check_variant plain bad-dynamic-initializer-r.cpp BAD_THROWING_INITIALIZER \
    "dynamic initializer of 'loaded' calls potentially throwing operation"

# Callbacks are calls at the registration site.
check_variant plain bad-external-callback.cpp none \
    "passes R helper 'touches_r' as a callback to external function 'run_callback'"
check_variant plain bad-external-callback.cpp BAD_ESCAPED_POINTER \
    "charr function 'neutral_callback' escapes as a function pointer"
check_variant plain bad-external-callback.cpp BAD_LAMBDA_CALLBACK \
    "lambda passed to external function 'run_functor' calls 'Rf_error'"
