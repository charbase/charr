# charr code map

The code map is generated from Clang's semantic model of the package source.
It records declarations once by Clang USR, even when a header is parsed in
many translation units.

Run `make code-map` from the package root to refresh `compile_commands.json`
and write the map to `scratch/code-map`. Run `make code-map-current` when the
existing compilation database still matches the source. Open the generated
`index.html` directly in a browser. The viewer uses only local HTML,
JavaScript, and SVG.

The output has three tables:

- `entities.tsv` lists compilation units, source files, records, functions,
  entrypoints, ABI shims, and external targets.
- `relationships.tsv` lists containment, calls, construction, function
  references, forwarding, inheritance, and type use between entities.
- `unit_dependencies.tsv` collapses calls, construction, function references,
  and forwarding into dependencies between compilation units.

A unit dependency means that code owned by one translation unit uses an
entity defined in another translation unit. Header inclusion is not treated
as a unit dependency. `uses_type` remains visible at the entity level because
type use describes architectural coupling but does not always require a
linked definition.
