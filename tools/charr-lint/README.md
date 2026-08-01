# charr-lint

`charr-lint` checks the native error and lifetime rules described in
`scratch/frame-unwind-architecture.md`. It uses Clang's AST and the package
compilation database.

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
