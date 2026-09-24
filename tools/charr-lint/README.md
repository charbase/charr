# charr-lint

`charr-lint` checks the native error and lifetime rules encoded by the
`CHARR_LINT_*` annotations. It uses Clang's AST and the package compilation
database.

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

The files have no comments or blank separator rows, so they can be read
directly from R:

```r
effects <- data.table::fread("tools/charr-lint/effects.tsv")
overrides <- data.table::fread("tools/charr-lint/effect-overrides.tsv")
```

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
passes. The writer still applies current override effects and reasons to
retained legacy rows so the visible contract does not go stale. Conflicting
observations and invalid overrides are integrity errors: they remain fatal in
audit mode and the writer leaves the existing manifest untouched.
