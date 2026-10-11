# AGENTS.md — mir

## Project Overview

`mir` (Motif Introspection / Reflection) produces a **machine-readable description of an X Intrinsics (Xt) widget set, primarily Motif (OpenMotif / LessTif)**.

The description is the single source of truth for generating **multiple sets of language bindings** (e.g., Python, Rust, Ada — exact targets TBD). Do not hand-write bindings or duplicate widget metadata; fix the generator and the description instead.

### What is being described

- Xt Intrinsics core: `Widget`, `Composite`, `Constraint`, `Shell`, `Object` etc. (`<X11/Intrinsic.h>`, `<X11/StringDefs.h>`, `<X11/Shell.h>`)
- Motif widget set: everything under `<Xm/*.h>` — `XmPushButton`, `XmTextField`, `XmForm`, `XmRowColumn`, etc.
- For each widget class: inheritance chain, resources (XtResourceList / XtResource), constraint resources, class record, callbacks (`XtCallbackList`), enumerated types, convenience functions (`XmCreate*`), translation tables and actions where relevant.

### Output

A single versioned machine-readable artifact (default: `mir.json`, JSON Schema defined in `schema/mir.schema.json`). Consumers must not parse headers directly — they consume this artifact.

Field stability matters. Adding a widget/resource is a minor change; removing or renaming is major. Keep `schema/` and `schema_version` in sync.

## Repository Layout

```
mir/
├── AGENTS.md
├── schema/mir.schema.json    # JSON Schema for the emitted artifact
├── src/                      # Tooling source code
├── tools/                    # binding generators (examples / smoke tests)
└── docs/
```

If a directory does not yet exist, create it following this layout.

## Testing

Run `make` (or `make mir.json`) to rebuild the artifact on demand, and
`make test` to run the test suite. Override `PYTHON`, `MOTIF_SOURCE`, or
`MKMIR_ARGS` on the make command line as needed.
Keep the Makefile's phony `test` target up to date whenever adding tests.
Python tests named `test_*.py` under `tests/` are discovered automatically;
add any tests requiring other runners to the target.

Use `~/src/motif/lib/Xm` as the source tree when testing the Python extractor.
From the repository root, run:

```sh
python3 src/mkmir.py --help
python3 src/mkmir.py ~/src/motif/lib/Xm -o /tmp/mir-test.json
```

The script discovers standard C headers from the installed `clang` compiler.
To override that discovery and supply the Motif source include directory:

```sh
python3 src/mkmir.py ~/src/motif/lib/Xm -o /tmp/mir-test.json \
  --clang-arg=-isystem --clang-arg="$(clang -print-resource-dir)/include" \
  --clang-arg="-I$HOME/src/motif/lib"
```

Python bindings for libclang (`clang.cindex`) and a loadable libclang shared
library are required. Use `--libclang /path/to/libclang.so` if library discovery
needs an override. Pass compiler flags with repeated `--clang-arg` options
(for example, `--clang-arg=-I/home/dg/src/motif/lib`).

The current smoke test should exit successfully after recursively discovering
all `.c` files and loading each as an independent translation unit. It writes
JSON matching `schema/mir.schema.json` to `--output` (default: `mir.json`), with
`prefix` (default: `xm`, configurable with `--prefix`) and a `widgets` dictionary
keyed by externally visible `WidgetClass` symbols. Each widget entry contains
`name` with the literal core.class_name string, without prefix stripping, and
`sourcefile` with the absolute path containing the initialized ClassRec definition
pointed to by the WidgetClass (excluding declarations),
`header` derived from that source path by keeping the directory matching the
toolkit prefix (case-insensitive) and its descendants and replacing `.c` with
`.h` (for example, `Xm/PushB.h`; omitted if the prefix directory is absent), and
`superclass` with the WidgetClass symbol pointing to the ClassRec referenced by
core.superclass. All translation units are loaded before analysis; definitions
and the ClassRec-to-WidgetClass index are shared across them using Clang USRs.
If multiple WidgetClass symbols point to one record, the lexicographically first
symbol is used. Null or unresolved superclass values leave the field absent.
For each included widget, the tool follows core.resources to its resource array
and fills the widget's `resources` dictionary with names from each entry's first
field. Each resource's `ctype` is the declared C type of the field referenced by
its fifth-field `XtOffsetOf(OBJ, FIELD)`, preserving typedef names. If this differs
from the fourth-field `sizeof(TYPE)` type, the tool warns on stderr with the
widget, resource, and both types. If the offset field cannot be resolved, it
falls back to the sizeof type. Its `rtype` is the literal
string from the third field (the declared Xt resource type), and `class` is the
literal string from the second field (the resource class). Unresolved fields
are omitted. No resource names are printed.
Every discovered resource currently has `set` and `get` set to `true`.
Widgets whose class record has a `constraint_part`, `constraint_class`, or
`constraint` member also emit an optional `constraints` dictionary, populated
from that part's `constraints` or `resources` array. Entries use the same
metadata and type-resolution rules as ordinary resources; null arrays produce
an empty dictionary. Constraint ctypes also contribute to top-level `types`.
Parsing defines `XMSTRINGDEFINES` and `XTSTRINGDEFINES` so Xt/Motif resource name
macros expand to string literals.
Each widget's `methods` dictionary contains externally visible function symbols
whose names start with its `name` (case-sensitive) and whose first parameter is
`Widget` (including typedef aliases). Functions are indexed across all loaded
translation units, including header declarations; each method contains `name`
with the widget's class-name prefix stripped from the full function symbol.
Each method also has a `parameters` dictionary keyed by parameter name, with
`name` and C `type` fields, omitting the leading Widget parameter. Definitions
are preferred over prototypes for parameter names. Unnamed parameters use
`arg1`, `arg2`, etc. as keys (adding underscores to avoid collisions) and an empty
`name`; methods with no remaining parameters have an empty dictionary.
Each method's `return_type` contains its signature's C return type, preserving
typedef names.
Each widget's `constructors` dictionary contains externally visible `XmCreate*`
functions declared directly in the public header identified by `header`.
Entries are keyed by full function symbol and contain only `name`, stripped
of `XmCreate`. Only non-variadic functions returning `Widget` and taking
exactly `(Widget, char *, ArgList, Cardinal)` are included, resolving typedef
aliases when checking the signature. Neither `parameters` nor `return_type`
is emitted.
Included headers and source-only functions do
not contribute constructors to that widget. Missing headers yield an empty
dictionary. Constructor signature types also contribute to `types`.
The top-level `types` dictionary contains an enum description keyed by the C
type spelling for each enum referenced by a resource's `ctype`, a method
parameter's `type`, or a method's `return_type`. Typedef aliases to enums are
resolved across all loaded translation units; unreferenced named enums are
excluded. Each enum entry has `type` set to `"enum"` and a `fields`
dictionary mapping enum constant names to their evaluated integer values.
The synthetic `XtEnum` entry collects `Xm`- and `Xt`-prefixed constants from
anonymous enums across all loaded translation units, excluding enums with a
tag or typedef name. Distinct constants may share the same integer value.
Referenced typedefs resolving to integral C types also have entries with
`type` set to `"integer"` and `ctype` containing the underlying C type spelling
after resolving typedef chains. Bare integral types and typedefs to arrays,
floating-point types, or records do not get integer entries.
Referenced pointer typedefs have `type` set to `"pointer"` and `ctype` containing
the pointed-to C type spelling. Aliases to the pointer are resolved, preserving
typedef names and qualifiers in its pointee. Bare pointer types are excluded.
Function pointer typedefs (including aliases and unprototyped functions) are
excluded from `types`; they will be handled separately.
Only symbols with a resolved core `class_name` are included;
widget class symbol stems in `WIDGET_CLASS_BLACKLIST` in `src/mkmir.py` are
excluded before indexing or emission (initially `xmColorObj`). Conventional
`Class`, `WidgetClass`, `GadgetClass`, and `ObjectClass` suffixes are removed
before matching the blacklist.
unresolved classes are silently skipped. Header-only declarations and variables
with internal or no linkage are excluded. Missing generated Motif headers can
prevent extraction from some files. Successful loading does not guarantee
that translation units are free of compiler diagnostics; diagnostics are not
currently reported or checked.

## Non-Goals

- Full C++ binding for Xt/Motif (this tool only *describes*; binding generators consume the description).
- Supporting non-Xt widget sets in v1 (GTK/Qt are out of scope).
- GUI designer or interactive builder — strictly a headless CLI.

## References

- Xt Intrinsics: `X Toolkit Intrinsics — C Language Interface` (X11R7 docs)
- Motif 2.1 → 2.3: `Motif Programmer's Guide` / `Xm` man pages (`man XmPushButton`)
- Clang Tooling: https://clang.llvm.org/docs/Tooling.html , https://clang.llvm.org/docs/LibClang.html
- JSON Schema: https://json-schema.org/
