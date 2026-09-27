# charr-lint

`charr-lint` checks the native error and lifetime rules encoded by the
`CHARR_LINT_*` annotations. It uses Clang's AST and the package compilation
database.

A definition is charr-owned when it is written in the translation unit's
main file or in a file whose real path lies under a `src/` directory other
than `src/icu78/`. Paths are canonicalized (absolute, symlinks and `..`
resolved) before any path-based decision, because the compilation database
names headers relative to its directory, such as
`altrep_backend/../shared/unwind.h`. Diagnostics are reported, and
deduplicated across translation units, under the canonical path.

## Contract checks beyond direct calls

A role describes a function's body, so the linter also checks the places
where a direct-call model would otherwise take a declaration on trust.

- **noexcept is a claim about the body.** A `noexcept` C++ helper must not
  contain a `throw`, a throwing `new`, or a call to a potentially throwing
  callee unless a `try` with a `catch (...)` in the same body stops it. A
  callee declared `noexcept` counts as non-throwing, including an external
  one whose ownership inference carries the C++ effect. Lambda bodies are
  separate functions, so a `try` outside a lambda does not cover its body.
- **Virtual overrides keep the base contract.** A virtual call is
  classified by the static callee, so each override must have the role of
  every method it overrides, directly or through intermediate declarations.
  An override may narrow a C++ or R helper to a neutral helper. An override
  of an external virtual must fit that declaration's external effect, and a
  charr virtual without a role is rejected.
- **Raw protection balances where it is pushed.** `Rf_protect`,
  `R_ProtectWithIndex`, `Rf_unprotect`, and `Rf_unprotect_ptr` are allowed
  only in `ProtHelper` and in R helpers. In an R helper every normal return
  must leave R's protection stack where the helper found it. The checker
  walks the control-flow graph, follows a local `int` counter passed to
  `UNPROTECT`, and ignores paths that end in a noreturn call, because R
  restores the stack on an R error. An unknown `UNPROTECT` count, a release
  of protections the helper did not push, unbounded growth in a loop, and raw
  protection inside a lambda are errors. `R_PreserveObject` and
  `R_ReleaseObject` are rejected outside entry points too, and a
  `ProtHelper` local is reserved for entry points.
- **Readers stay in reach of the reset check.** A `charport::Reader` may be
  held only by a direct entry-point local of type `charport::Reader` or
  `std::vector<charport::Reader>`. Arrays, members, pointers, references,
  wrappers, globals, parameters, and return values that contain a Reader
  are rejected, and a Reader method called outside an entry point is an
  error. One narrow exception: a helper may take a non-const
  `charport::Reader&` whose only use is `reader = charport::Reader()`, which
  drops the borrow the same way the destructor does.
- **Default arguments and dynamic initializers have no role.** A default
  argument runs in each caller, and a namespace-scope dynamic initializer
  runs while the shared library loads. Every call in either must be
  neutral: no fallible R, no C++ effect or ownership, no raw resource
  operation, and no unclassified or indirect call.
- **Callbacks run inside their receiver.** A charr function named outside
  callee position is classified as a call at that point. When it is passed
  to an external function, its R and C++ effects must be permitted by that
  function's external effect. A function pointer that escapes any other way
  (stored, returned) is rejected. A lambda passed to an external function is
  held to the same rule for every call in its body.
- **ABI shims enter R.** Calling a `CHARR_ABI_SHIM` function is a fallible
  R call. Naming a shim or an R helper in an initializer, as the `.Call`
  registration table does, is not.
- **Implicit members and local lambdas.** A compiler-generated special
  member of a charr type takes the neutral role when it is `noexcept` and
  the C++ helper role otherwise; ownership still follows its type. Calls it
  makes on subobjects are not traced. A call to a lambda written in the
  caller's own body shares the caller's role (neutral in an entry point),
  because the lambda body is checked as part of that body. A lambda defined
  elsewhere is an unclassified call.
- **setjmp and longjmp.** Only a trusted unwind intrinsic may call
  `setjmp`, `longjmp`, or their variants.
- **The ICU fatal handler.** `CHARR_ICU_FATAL_HANDLER` marks
  `charr::shared::icu_invariant_failure`, where a bundled ICU goes instead of
  `abort()` when an invariant it intends to be unreachable fails. The
  handler may enter R and may throw, which the helper purity rule forbids
  everywhere else, so the role is narrow: it is accepted only on
  declarations in `src/shared/icu_fatal.h` and `src/shared/icu_fatal.cpp`,
  the function must be `[[noreturn]]`, and it must not be `noexcept`,
  because it throws inside a parallel body. No charr function may call it
  or name it, in any role or context, including trusted unwind intrinsics,
  default arguments, and dynamic and constant initializers; only ICU
  reaches it, through the macros in `src/uconfig_local.h`. Those sites are
  checked separately, see "ICU fatal sites" below.

## Entry-point shape

- Every `CHARR_ENTRYPOINT` returns `SEXP` and uses
  `charr::shared::unwind_protect` as its one primary boundary, so every
  entry point receives the full protection-shape check.
- `CHARR_TRUSTED_UNWIND` is accepted only on definitions in
  `src/shared/unwind.h`.
- Each entry point contains exactly one `CHARR_UNWIND_KEEP_RESULT()`. Its
  arguments are the stable `result` and `result_index`, and it is the
  owner-try statement immediately after `result = unwind_protect(...)`.
- A pending R error is continued before anything else runs: no call between
  the owner try block and the R-error branch, or in its condition, may make
  a fallible R call or release a protection domain.

## External effects

Calls into R, charport, ICU, and the standard libraries are outside charr's
definition graph. Two TSV files describe that boundary.

`effects.tsv` is the generated and reviewed manifest. Each row is keyed by the
qualified function name and canonical type, so separate overloads require
separate approval. The linter also compares Clang USRs across translation
units. If distinct template specializations collapse to the same readable
key, linting stops instead of applying one contract to both.

| Column | Meaning |
|---|---|
| `effect` | Effective effect used by the linter after overrides. |
| `qualified_name` | Clang's qualified function name. |
| `canonical_type` | Clang's canonical function type. |
| `inferred_effect` | Effect derived from the current declaration. |
| `inference_basis` | Stable rule or Clang property supporting the inference. |
| `override_reason` | Reason copied from the manual override, when one exists. |

Keys do not depend on the ICU mode. A bundled ICU is built with
`U_LIB_SUFFIX_C_NAME=_charr`, so its C entry points are `ucol_open_78_charr`
rather than `ucol_open_78` and its namespace is `icu_78_charr` rather than
`icu_78`. The key drops that `_charr`: from the function's own name when every
file declaration of the function is in an ICU header, and from ICU's namespace
wherever the name or canonical type spells it, provided the namespace is
declared only in ICU headers. The version stays, so an ICU update is reviewed
again. A charr or R name, or any name declared outside ICU headers, is never
rewritten. One manifest therefore serves both modes, and
`--write-effects-manifest` produces the same file from either compilation
database.

The inference is conservative:

- declarations from R headers, and recognized R API names, receive `r`;
- a cleanup-bearing return or construction receives `owner`, which also
  carries the C++ error effect;
- a declaration without `noexcept` receives `cxx` unless it comes from a
  reviewed C API header;
- a `noexcept` declaration has no C++ error effect; and
- C linkage alone does not remove the C++ error effect.

The reviewed C API rule is limited to R headers, system C headers, charport's
public C headers, and the vendored ICU C headers. Extending that list is a
change to the linter's trust boundary.

`effect-overrides.tsv` contains the facts that declarations cannot express.
Examples include a charport operation that can raise an R error, an R accessor
that cannot signal, and a C function that returns an owned handle. Each
override adds or removes specific effects and requires a reason. An override
cannot remove mechanical ownership inference. Redundant and stale overrides
are errors.

ICU is built without C++ exceptions: its objects allocate through
`uprv_malloc` and report failure through `UErrorCode` or a bogus state. An
override may therefore remove `cxx` from an owner-inferred ICU function, such
as a `UnicodeString` constructor or `UnicodeString::fromUTF8`, but only when
every file declaration of that function is in an ICU header: the vendored
`src/icu78/unicode/` directory or a `unicode/` directory on a system include
path. The same override on any other declaration is an integrity error. The
function keeps its ownership, which the manifest records as `owner-nothrow`,
so the owner rules still apply; only the C++ error effect is gone.
`owner-nothrow` is a result, not an override component.

### The `fatal` component

`fatal` means: the function may not return, and only when an internal
invariant of the callee fails at a site the callee's authors intend to be
unreachable. It is not an error effect. It is not a failure mode of the
function's contract, and no caller handles it.

It is inferred, with basis `rule:icu-header-fatal`, for every function
whose file declarations are all in ICU headers, by the same test the ICU
owner overrides use: the vendored `src/icu78/unicode/` directory or a
`unicode/` directory on a system include path. It combines with the other
components, as in `neutral+fatal`, `cxx+fatal`, and `owner-nothrow+fatal`.
The exit it records differs by build. With a system ICU it is upstream's
`abort()`. In a bundled build `src/uconfig_local.h` routes ICU's
`UPRV_UNREACHABLE_EXIT` and double-conversion's
`DOUBLE_CONVERSION_UNIMPLEMENTED()` and `DOUBLE_CONVERSION_UNREACHABLE()` to
charr's handler, which throws inside a `ParallelBody::run` and raises an R
error elsewhere.

`fatal` is recorded so the manifest shows where such exits exist. It never
changes which calls are accepted: every role and context may call a `fatal`
function, including R helpers, neutral helpers, `noexcept` C++ helpers,
destructors, entry points, and `ParallelBody::run`. The statement above
that ICU is built without C++ exceptions stays true, because `fatal` is not
`cxx`. An override can neither remove `fatal` nor add it to any function;
either is an integrity error, and the manifest is left untouched.

The files have no comments or blank separator rows, so they can be read
directly from R:

```r
effects <- data.table::fread("tools/charr-lint/effects.tsv")
overrides <- data.table::fread("tools/charr-lint/effect-overrides.tsv")
```

## ICU fatal sites

In a bundled build every expansion of the fatal macros in
`src/uconfig_local.h` is a call to the ICU fatal handler.
`tools/charr-lint/fatal-sites.tsv` lists them, and `--fatal-sites PATH`
compares a compilation database with it instead of linting. The sites come
from the AST after preprocessing, so a macro in a comment or a disabled
`#if` block is not counted, and a site is reported if the handler is named
outside `src/icu78/`, other than through a `src/uconfig_local.h` macro, or
in a function that cannot throw (the handler's exception would then call
`std::terminate`).

| Column | Meaning |
|---|---|
| `file` | Repository path of the file where the outermost macro is written. For a function defined in a header this is the header, and the sites count once however many translation units include it. |
| `function` | Enclosing function with its parameter types. ICU's versioned namespace is written `icu`, so the rows survive a version bump. A site in a lambda belongs to the enclosing function; one at namespace scope is `(namespace scope)`. |
| `sites` | Number of distinct sites in that function. |
| `kind` | Why the site cannot be reached from charr's calls. |
| `reason` | One line naming the invariant. |

The kinds are:

- `loop-exit`: dead code after a loop or switch that leaves only by
  `return` or `continue`;
- `closed-switch`: the default of a switch, or the last branch of an if
  chain, over an enum or tag whose every value is handled, so only a value
  outside the type or never produced by ICU reaches it;
- `data-invariant`: a bound or structure that ICU keeps consistent, such as
  a hash table that always has a free slot or an opcode that fits its field;
- `uncalled-override`: a virtual override that nothing calls;
- `disabled-path`: code reached only under an option or configuration that
  ICU never enables.

A new row, a missing row, a changed count, and a row without a kind are
errors. `--write-fatal-sites` rewrites the file from the observed sites. It
keeps the kind and reason of a row whose count is unchanged, keeps only the
reason of a row whose count changed, and leaves new rows without a kind, so
the next check fails until each is reviewed.

After an ICU update, or any change to `src/uconfig_local.h`:

```sh
MAKEFLAGS= make lint-db-icu    # bundled-ICU database, full ICU compile
make lint-fatal-sites          # check
make lint-fatal-sites-update   # on failure: rewrite, then review
make lint-converted-icu        # strict lint of the bundled build
```

`lint-converted-icu` runs the strict lint of `lint-converted` against the
bundled database, with the same manifest and overrides.

Review means reading each new or changed site, filling in `kind` and
`reason`, and running `make lint-fatal-sites` again. `lint-db-icu` writes
`local/charr-lint/icu-db/compile_commands.json` and leaves
`compile_commands.json` alone. The check runs every translation unit in
that database, charr's included, so a charr file that named the handler
would also be caught there.

## Review workflow

A new external call fails normal linting because its exact signature is absent
from the manifest. Run:

```sh
make lint-effects-update
```

The target infers the new row and updates the manifest. Review that diff. If
the declaration does not contain the whole contract, add a narrow entry to
`effect-overrides.tsv`, run the update target again, and review both files.
Finish with `make lint`.

The update merges observations into the existing manifest. It preserves rows
that are not present in the current compilation database because another ICU
or platform configuration may use them. Migrated rows that have not yet been
observed carry `legacy-unobserved` as their inference basis. The first build
configuration that reaches one must infer and review it before strict linting
passes. A call made only in a bundled build, such as `udata_setCommonData`, is
observed only through the bundled database, so `lint-converted-icu` is where
it fails. The writer still applies current override effects and reasons to
retained legacy rows so the visible contract does not go stale. Conflicting
observations and invalid overrides are integrity errors: they remain fatal in
audit mode and the writer leaves the existing manifest untouched.
